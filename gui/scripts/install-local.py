#!/usr/bin/env python3
"""Install this verified local app, retaining an earlier installation as a backup."""
import datetime
import os
import pathlib
import subprocess
import uuid

root = pathlib.Path(__file__).resolve().parents[1]
app = root / 'output/BurnLight.app'
if pathlib.Path(f'/private/tmp/burnlight-physical-{os.getuid()}/active.json').exists():
    raise SystemExit('Physical task active/unknown: installation refused')
if subprocess.run(['/usr/bin/pgrep', '-x', 'BurnLight'], stdout=subprocess.DEVNULL).returncode == 0:
    raise SystemExit('Quit the idle BurnLight app normally before installing; no process will be terminated')
subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(app)], check=True)
folder = pathlib.Path.home() / 'Applications'
folder.mkdir(exist_ok=True)
destination = folder / 'BurnLight.app'
pending = folder / ('.BurnLight-' + str(uuid.uuid4()) + '.app')
subprocess.run(['/usr/bin/ditto', str(app), str(pending)], check=True)
subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(pending)], check=True)
if destination.exists() or destination.is_symlink():
    if destination.is_symlink():
        raise SystemExit('Existing installation is a symbolic link; left unchanged')
    identifier = subprocess.check_output(['/usr/libexec/PlistBuddy', '-c', 'Print :CFBundleIdentifier', str(destination / 'Contents/Info.plist')], text=True).strip()
    if identifier != 'local.burnlight.gui':
        raise SystemExit('Existing app has another identity; left unchanged')
    backup = folder / ('BurnLight-previous-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + str(uuid.uuid4())[:8] + '.app')
    destination.rename(backup)
    print('Previous app retained:', backup)
pending.rename(destination)
print('Installed:', destination)
