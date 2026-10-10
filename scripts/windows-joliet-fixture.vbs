' Native Windows IMAPI fixture/reference writer. NOT executed on macOS.
' Official API: https://learn.microsoft.com/windows/win32/imapi/creating-a-multisession-disc
' Usage: cscript //nologo windows-joliet-fixture.vbs D: C:\fixture first
'        cscript //nologo windows-joliet-fixture.vbs D: C:\newfiles append
' Only the explicitly selected Lenovo Ultraslim DVD + single-layer DVD-R is accepted.
Option Explicit
If WScript.Arguments.Count <> 3 Then
    WScript.Echo "Usage: cscript //nologo windows-joliet-fixture.vbs DRIVE: FILES first|append"
    WScript.Quit 1
End If
Dim drive, folder, mode, fso
drive = UCase(WScript.Arguments(0))
folder = WScript.Arguments(1)
mode = LCase(WScript.Arguments(2))
Set fso = CreateObject("Scripting.FileSystemObject")
If Len(drive) <> 2 Or Right(drive, 1) <> ":" Then WScript.Quit 2
If Left(drive, 1) < "A" Or Left(drive, 1) > "Z" Then WScript.Quit 2
If mode <> "first" And mode <> "append" Then WScript.Quit 2
If Not fso.FolderExists(folder) Then WScript.Quit 2
If fso.GetFolder(folder).Size > 1048576 Then
    WScript.Echo "Fixture is limited to 1 MiB; select only small experiment files"
    WScript.Quit 2
End If
If fso.GetDrive(drive).DriveType <> 4 Then
    WScript.Echo "Selected drive is not an optical drive"
    WScript.Quit 3
End If
Dim master, candidate, recorder, index, path, count
Set master = CreateObject("IMAPI2.MsftDiscMaster2")
count = 0
For index = 0 To master.Count - 1
    Set candidate = CreateObject("IMAPI2.MsftDiscRecorder2")
    candidate.InitializeDiscRecorder master.Item(index)
    For Each path In candidate.VolumePathNames
        If UCase(Left(path, 2)) = drive Then
            Set recorder = candidate
            count = count + 1
        End If
    Next
Next
If count <> 1 Then
    WScript.Echo "Cannot uniquely select optical recorder"
    WScript.Quit 3
End If
WScript.Echo "Target: " & recorder.VendorId & " " & recorder.ProductId & " " & recorder.ProductRevision
If Trim(recorder.VendorId) <> "Lenovo" Or Trim(recorder.ProductId) <> "Ultraslim DVD" Or Trim(recorder.ProductRevision) <> "XL6M" Then
    WScript.Echo "Target does not match this experiment"
    WScript.Quit 3
End If
Dim writer, image, result, stream
Set writer = CreateObject("IMAPI2.MsftDiscFormat2Data")
writer.Recorder = recorder
writer.ClientName = "appendcd Windows Joliet fixture"
If writer.CurrentPhysicalMediaType <> 9 Then
    WScript.Echo "Need single-layer DVD-R (IMAPI_MEDIA_TYPE_DVDDASHR=9)"
    WScript.Quit 4
End If
If mode = "first" And Not writer.MediaHeuristicallyBlank Then
    WScript.Echo "first mode requires a blank experiment DVD-R; existing discs are refused"
    WScript.Quit 4
End If
If mode = "append" And writer.MediaHeuristicallyBlank Then
    WScript.Echo "append mode requires an existing ISO/Joliet session"
    WScript.Quit 4
End If
Set image = CreateObject("IMAPI2FS.MsftFileSystemImage")
image.ChooseImageDefaults recorder
' Override Windows/DVD defaults explicitly: ISO9660=1, Joliet=2, UDF=4.
image.FileSystemsToCreate = 3
image.VolumeName = "APPENDCD_WIN"
If mode = "append" Then
    image.MultisessionInterfaces = writer.MultisessionInterfaces
    image.ImportSpecificFileSystem 2
End If
image.Root.AddTree folder, False
Set result = image.CreateResultImage()
' IStream is assigned as a Variant, following the official IMAPI VBScript example.
stream = result.ImageStream
writer.ForceMediaToBeClosed = False
writer.DisableConsumerDvdCompatibilityMode = True
WScript.Echo "Writing ISO9660 + Joliet, keeping DVD-R open. Blocks: " & result.TotalBlocks
writer.Write stream
WScript.Echo "Write completed. Reinsert and verify filenames; this script does not certify acceptance."
