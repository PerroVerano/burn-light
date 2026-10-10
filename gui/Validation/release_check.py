#!/usr/bin/env python3
"""Check the installed/released bundle and offline relocation without touching media."""
import hashlib
import json
import os
import pathlib
import plistlib
import shutil
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
evidence = pathlib.Path(os.environ.get('BURNLIGHT_EVIDENCE_DIR', str(root / 'gui/evidence/local-release-20261010')))
app = root / 'gui/output/BurnLight.app'
installed = pathlib.Path.home() / 'Applications/BurnLight.app'
version = plistlib.loads((app / 'Contents/Info.plist').read_bytes())['CFBundleShortVersionString']
archive = root / ('gui/output/BurnLight-' + version + '-local-arm64.zip')
scratch = pathlib.Path(tempfile.mkdtemp(prefix="BurnLight Local ' $ ", dir='/private/tmp'))
subprocess.run(['/usr/bin/ditto', '-x', '-k', str(archive), str(scratch)], check=True)
relocated = scratch / 'BurnLight.app'
for target in (app, installed, relocated):
    subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(target)], check=True)
contents = app / 'Contents'
config = json.loads((contents / 'Resources/LocalConfiguration.json').read_text())
assert config['recordDirectory'] == 'ApplicationSupport/Records' and config['allowPhysicalWrites'] == (os.environ.get('BURNLIGHT_ENABLE_WRITES', '0') == '1')
rows = []
for relative, expected in config['trustedFiles'].items():
    for target in (app, installed, relocated):
        p = target / 'Contents' / relative
        assert not p.is_symlink() and hashlib.sha256(p.read_bytes()).hexdigest() == expected
    rows.append(dict(path=relative, sha256=expected, installedAndRelocatedMatch=True))
assert len(rows) == 7
app_hash = hashlib.sha256((app / 'Contents/MacOS/BurnLight').read_bytes()).hexdigest()
assert all(hashlib.sha256((x / 'Contents/MacOS/BurnLight').read_bytes()).hexdigest() == app_hash for x in (installed, relocated))
imports = {}
for folder in ('MacOS', 'Frameworks'):
    for binary in (contents / folder).iterdir():
        if not binary.is_file():
            continue
        raw = subprocess.check_output(['/usr/bin/otool', '-L', str(binary)], text=True)
        dependencies = [line.strip().split(' (')[0] for line in raw.splitlines()[1:]]
        assert all(not x.startswith(('/opt/', '/Users/', '/Library/Developer/')) for x in dependencies), raw
        imports[str(binary.relative_to(contents))] = dependencies
source = scratch / "新增 ' $ 中文"
source.mkdir(); (source / "文件 ' $ 中文.txt").write_text('relocated offline input\n')
cli = relocated / 'Contents/MacOS/appendcd'
image = scratch / "预览 ' $.iso"
logs = []
for arguments in ([str(cli), 'init-build', str(source), str(image)], [str(cli), 'verify', str(image)]):
    p = subprocess.run(arguments, text=True, capture_output=True)
    logs.append(p.stdout + p.stderr)
    assert p.returncode == 0, logs[-1]
assert 'INITIAL IMAGE VERIFIED' in logs[0] and 'IMAGE VERIFIED' in logs[1]
(evidence / 'relocation.log').write_text('\n'.join(logs))
report = dict(result='PASS', GUIsha256=app_hash, application=str(installed), original=str(app), relocated=str(relocated), dependencies=rows, imports=imports, releaseZIPsha256=hashlib.sha256(archive.read_bytes()).hexdigest(), offlineBuildAndVerify=True, physicalWrites=0, operatingSystem='macOS 26', architecture='arm64', signature='local ad hoc')
(evidence / 'release-check.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
print('PASS: signature, 7 dependency pins, installed/build/ZIP equality, no external imports, relocation with quotes/dollars/Chinese paths')
