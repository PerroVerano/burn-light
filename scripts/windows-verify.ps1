# Native Windows read-only acceptance collector. Requires an administrator PowerShell.
# NOT executed on macOS. Output must be saved to a local drive, never the optical disc.
param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z]:$')][string]$Drive,
    [string]$ExpectedJson,
    [string]$Output = '.\windows-result.json'
)
$ErrorActionPreference = 'Stop'
$Drive = $Drive.ToUpperInvariant()
if ([IO.Path]::GetPathRoot([IO.Path]::GetFullPath($Output)).TrimEnd('\') -eq $Drive) {
    throw 'Output cannot be written to the optical drive'
}
$optical = @(Get-CimInstance Win32_CDROMDrive | Where-Object { $_.Drive -eq $Drive })
if ($optical.Count -ne 1) { throw 'The selected letter is not a unique optical drive' }
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class AppendCdReadOnly {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool DeviceIoControl(SafeFileHandle h, uint code, byte[] input, int inSize, byte[] output, int outSize, out int returned, IntPtr overlapped);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool SetFilePointerEx(SafeFileHandle h, long distance, out long position, uint method);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool ReadFile(SafeFileHandle h, byte[] buffer, int bytes, out int returned, IntPtr overlapped);
    static SafeFileHandle Open(string drive) {
        var h = CreateFile(@"\\.\" + drive, 0x80000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (h.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
        return h;
    }
    public static byte[] Ioctl(string drive, uint code, byte[] input) {
        using (var h = Open(drive)) {
            var bytes = new byte[65536]; int n;
            if (!DeviceIoControl(h, code, input, input == null ? 0 : input.Length, bytes, bytes.Length, out n, IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error());
            Array.Resize(ref bytes, n); return bytes;
        }
    }
    public static byte[] Sector(string drive, long lba) {
        using (var h = Open(drive)) {
            long position; int n; var bytes = new byte[2048];
            if (!SetFilePointerEx(h, lba * 2048, out position, 0) || !ReadFile(h, bytes, bytes.Length, out n, IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error());
            if (n != 2048) throw new Exception("Short optical read"); return bytes;
        }
    }
}
'@
$report = [ordered]@{
    timestamp = [DateTime]::UtcNow.ToString('o')
    os = (Get-CimInstance Win32_OperatingSystem).Caption
    osVersion = (Get-CimInstance Win32_OperatingSystem).Version
    osBuild = (Get-CimInstance Win32_OperatingSystem).BuildNumber
    drive = $Drive
    opticalModel = $optical[0].Name
    result = 'OBSERVED_ONLY'
    filesMatched = $false
    ioctl = [ordered]@{}
    descriptors = @()
    files = @()
    errors = @()
}
# CTL_CODE(FILE_DEVICE_CD_ROM=2, function, METHOD_BUFFERED=0, FILE_READ_ACCESS=1).
$requests = @(
    @{name='GET_LAST_SESSION'; code=[uint32]0x24038; input=$null},
    @{name='READ_TOC'; code=[uint32]0x24000; input=$null},
    @{name='READ_TOC_EX_SESSION'; code=[uint32]0x24054; input=[byte[]](1,0,0,0)},
    @{name='READ_TOC_EX_FULL'; code=[uint32]0x24054; input=[byte[]](2,0,0,0)}
)
foreach ($request in $requests) {
    try {
        [byte[]]$raw = [AppendCdReadOnly]::Ioctl($Drive, $request.code, $request.input)
        $item = [ordered]@{hex=([BitConverter]::ToString($raw)); bytes=$raw.Length}
        if ($request.name -eq 'GET_LAST_SESSION' -and $raw.Length -ge 12) {
            $item.firstCompleteSession = [int]$raw[2]
            $item.lastCompleteSession = [int]$raw[3]
            $item.lastStartLba = [uint32]([uint64]$raw[8]*16777216 + [uint64]$raw[9]*65536 + [uint64]$raw[10]*256 + $raw[11])
        }
        $report.ioctl[$request.name] = $item
    } catch { $report.ioctl[$request.name] = @{error=$_.Exception.Message} }
}
$root = "$Drive\"
try {
    $report.files = @(Get-ChildItem -LiteralPath $root -Recurse -Force | ForEach-Object {
        [ordered]@{relativePath=$_.FullName.Substring($root.Length).Replace('\','/'); directory=$_.PSIsContainer; size=$(if ($_.PSIsContainer) {0} else {$_.Length}); sha256=$(if ($_.PSIsContainer) {''} else {(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()})}
    })
    if ($ExpectedJson) {
        $expected = Get-Content -LiteralPath $ExpectedJson -Raw -Encoding UTF8 | ConvertFrom-Json
        $actual = New-Object 'System.Collections.Generic.Dictionary[string,object]' ([StringComparer]::Ordinal)
        foreach ($entry in $report.files) { $actual.Add($entry.relativePath, $entry) }
        foreach ($entry in $expected.entries) {
            if (-not $actual.ContainsKey($entry.relativePath)) { throw "Missing/exact-case name mismatch: $($entry.relativePath)" }
            $found = $actual[$entry.relativePath]
            if ($found.directory -ne $entry.directory) { throw "Entry type mismatch: $($entry.relativePath)" }
            if (-not $entry.directory) {
                if (-not $entry.sha256 -or $found.sha256 -ne $entry.sha256 -or $found.size -ne $entry.size) { throw "Content mismatch: $($entry.relativePath)" }
            }
        }
        if (-not @($expected.entries | Where-Object { -not $_.directory -and $_.origin -eq 'old' }).Count -or
            -not @($expected.entries | Where-Object { -not $_.directory -and $_.origin -eq 'new' }).Count) {
            throw 'Acceptance requires both old and new files in the expectation'
        }
        foreach ($offset in 16..18) {
            try {
                [byte[]]$raw = [AppendCdReadOnly]::Sector($Drive, [long]$expected.sessionStartLba + $offset)
                $report.descriptors += @{lba=([long]$expected.sessionStartLba+$offset); hex=[BitConverter]::ToString($raw)}
            } catch { $report.descriptors += @{offset=$offset; error=$_.Exception.Message} }
        }
        $report.filesystem = $expected.filesystem
        if ($expected.filesystem -in @('udf1.02', 'udf2.01+vat')) {
            try {
                [byte[]]$anchor = [AppendCdReadOnly]::Sector($Drive, [long]$expected.sessionStartLba + 256)
                $report.descriptors += @{kind='UDF_AVDP'; lba=([long]$expected.sessionStartLba+256); hex=[BitConverter]::ToString($anchor)}
                $main = [BitConverter]::ToUInt32($anchor,20)
                $reserve = [BitConverter]::ToUInt32($anchor,28)
                # Capture the six generated VDS descriptors at both locations.
                foreach ($base in @($main,$reserve)) {
                    foreach ($index in 0..5) {
                        [byte[]]$raw = [AppendCdReadOnly]::Sector($Drive,[long]$base+$index)
                        $report.descriptors += @{kind='UDF_VDS'; lba=([long]$base+$index); hex=[BitConverter]::ToString($raw)}
                    }
                }
            } catch { $report.descriptors += @{kind='UDF'; error=$_.Exception.Message} }
        }
        $report.filesMatched = $true
        $report.result = 'WINDOWS_FILES_PASS'
        if ($report.ioctl.GET_LAST_SESSION.Contains('lastStartLba')) {
            if ($report.ioctl.GET_LAST_SESSION.lastStartLba -ne $expected.sessionStartLba) {
                throw "Windows last-session LBA differs: $($report.ioctl.GET_LAST_SESSION.lastStartLba), expected $($expected.sessionStartLba)"
            }
            $report.result = 'WINDOWS_ACCEPTANCE_PASS'
        } else {
            $report.errors += 'Files matched, but GET_LAST_SESSION was unavailable; acceptance remains pending'
        }
    }
} catch {
    $report.errors += $_.Exception.Message
    $report.result = $(if ($report.filesMatched) { 'WINDOWS_ACCEPTANCE_FAIL' } else { 'WINDOWS_FILES_FAIL' })
}
$report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Output -Encoding UTF8
Write-Host "Result: $($report.result). Evidence: $Output"
if ($report.result.EndsWith('_FAIL')) { exit 1 }
if ($ExpectedJson -and $report.result -ne 'WINDOWS_ACCEPTANCE_PASS') { exit 2 }
