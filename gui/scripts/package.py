#!/usr/bin/env python3
"""Assemble and verify a relocatable local bundle; source/runtime originals are read only."""
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import os

root = pathlib.Path(sys.argv[1]).resolve()
app = pathlib.Path(sys.argv[2]).resolve()
contents = app / 'Contents'
macos, frameworks, resources = (contents / n for n in ('MacOS', 'Frameworks', 'Resources'))
for path in (macos, frameworks, resources):
    path.mkdir(parents=True, exist_ok=True)
baseline = json.loads((root / 'artifacts/baseline-before-gui/latest.json').read_text())
expected = json.loads((root / baseline['snapshot'] / 'sha256.json').read_text())['files']
provenance = {}

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def copy_binary(source, target):
    # Homebrew executables may be mode 0555: stage and replace so rebuilding never
    # opens an existing signed/read-only executable for writing.
    pending = target.with_name('.' + target.name + '-stage')
    if pending.exists():
        pending.unlink()
    shutil.copy2(source, pending)
    pending.replace(target)

for name in ('appendcd-cdrecord', 'appendcd-initial-cdrecord'):
    source = pathlib.Path.home() / '.local/libexec/appendcd' / name
    if digest(source) != expected['runtime/' + name]:
        raise SystemExit('Original runtime baseline mismatch: ' + str(source))
    copy_binary(source, macos / name)
    provenance[name] = dict(source=str(source), sourceSHA256=digest(source), baseline=True)
if digest(root / 'build/appendcd') != expected['build/appendcd']:
    raise SystemExit('Original CLI baseline mismatch')
vat = pathlib.Path(os.environ.get('BURNLIGHT_VAT_BACKEND', str(root / 'build-vat-backend-v9/appendcd-vat-cdrecord')))
if vat.exists():
    version = subprocess.run([vat, '-version'], check=True, capture_output=True, text=True)
    if '3.02a09-appendcd-vat9' not in version.stdout + version.stderr:
        raise SystemExit('VAT packaging requires vat9 Session fixation; vat8 is rejected')
    vat_provenance = json.loads((vat.parent / 'provenance.json').read_text())
    if digest(vat) != vat_provenance['binarySha256']:
        raise SystemExit('VAT backend provenance mismatch')
    copy_binary(vat, macos / vat.name)
    provenance[vat.name] = dict(source=str(vat), sourceSHA256=digest(vat), baseline=False, provenance=vat_provenance)
for name in ('appendcd', 'burnlight-export'):
    copy_binary(pathlib.Path(os.environ.get('BURNLIGHT_ADAPTER_BUILD', str(root / 'gui/.adapter-build'))) / name, macos / name)
copy_binary('/opt/homebrew/bin/readcd', macos / 'readcd')
provenance['readcd'] = dict(source='/opt/homebrew/bin/readcd', sourceSHA256=digest(pathlib.Path('/opt/homebrew/bin/readcd')), baseline=False)
libs = {pathlib.Path('/opt/homebrew/opt/libisofs/lib/libisofs.6.dylib'), pathlib.Path('/opt/homebrew/opt/libudfread/lib/libudfread.3.dylib')}
for source in libs:
    target = frameworks / source.name
    copy_binary(source.resolve(), target)
    subprocess.run(['/usr/bin/install_name_tool', '-id', '@rpath/' + source.name, str(target)], check=True)
    provenance[source.name] = dict(source=str(source), sourceSHA256=digest(source.resolve()), baseline=False)
for name in ('appendcd', 'burnlight-export'):
    target = macos / name
    for source in libs:
        subprocess.run(['/usr/bin/install_name_tool', '-change', str(source), '@executable_path/../Frameworks/' + source.name, str(target)], check=True)
for target in sorted(list(macos.iterdir()) + list(frameworks.iterdir())):
    if target.is_file() and target.name != 'BurnLight':
        subprocess.run(['/usr/bin/codesign', '--force', '--sign', '-', str(target)], check=True)
trusted = {str(path.relative_to(contents)): digest(path) for path in sorted(list(macos.iterdir()) + list(frameworks.iterdir())) if path.is_file() and path.name != 'BurnLight'}
write_flag = os.environ.get('BURNLIGHT_ENABLE_WRITES', '0')
if write_flag not in ('0', '1'):
    raise SystemExit('BURNLIGHT_ENABLE_WRITES must be 0 or 1')
config = dict(schema=2, cli='MacOS/appendcd', exportCLI='MacOS/burnlight-export', trustedFiles=trusted,
              recordDirectory='ApplicationSupport/Records', allowPhysicalWrites=write_flag == '1')
(resources / 'LocalConfiguration.json').write_text(json.dumps(config, indent=2) + '\n')
(resources / 'Provenance.json').write_text(json.dumps(dict(baseline=baseline['snapshot'], originalCLIsha256=expected['build/appendcd'], sources=provenance, architecture='arm64', minimumOS='26.0', bundledCLI='shared main/core with UDF 2.01 VAT view binding, cross-Session recorded ranges, explicit conflicts and bounded compact-volume import; bundle-relative isolated runtimes'), indent=2) + '\n')
shutil.copy2(root / 'gui/Resources/Usage.md', resources / 'Usage.md')
notices = resources / 'ThirdPartyNotices'
notices.mkdir(exist_ok=True)
for source, name in [('/opt/homebrew/opt/libisofs/COPYING', 'libisofs-COPYING.txt'),
                     ('/opt/homebrew/opt/libisofs/COPYRIGHT', 'libisofs-COPYRIGHT.txt'),
                     ('/opt/homebrew/opt/libudfread/COPYING', 'libudfread-COPYING.txt'),
                     ('/opt/homebrew/opt/cdrtools/COPYING', 'cdrtools-COPYING.txt'),
                     (str(root / 'backend/CDDL.Schily.txt'), 'CDDL.Schily.txt')]:
    shutil.copy2(source, notices / name)
for name in ('appendcd', 'burnlight-export'):
    result = subprocess.run(['/usr/bin/otool', '-L', str(macos / name)], text=True, capture_output=True, check=True)
    if '/opt/homebrew' in result.stdout or str(root) in '\n'.join(result.stdout.splitlines()[1:]):
        raise SystemExit('Non-relocatable dependency: ' + name)
print('Packaged all CLI/backend/readcd/dylib dependencies; originals unchanged.')
