#!/usr/bin/env python3
"""Install this verified local app, retaining an earlier installation as a backup."""
import datetime
import os
import pathlib
import subprocess
import shutil
import uuid

KEEP_BACKUPS = 3

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
    # 备份放在 .noindex 目录，避免 Spotlight / 启动台把旧版本当成可启动应用；只保留最近几份。
    backups = pathlib.Path.home() / 'Library/Application Support/BurnLight/AppBackups.noindex'
    backups.mkdir(parents=True, exist_ok=True, mode=0o700)
    backup = backups / ('BurnLight-previous-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + str(uuid.uuid4())[:8] + '.app')
    destination.rename(backup)
    print('Previous app retained:', backup)
    for old in sorted(backups.glob('BurnLight-previous-*.app'))[:-KEEP_BACKUPS]:
        if old.is_symlink() or not old.is_dir():
            continue
        shutil.rmtree(old)
        print('Removed old backup:', old)
pending.rename(destination)
print('Installed:', destination)
# 已安装副本通过签名校验后删除构建产物，避免 Spotlight / 启动台再收录一份 BurnLight.app。
if os.environ.get('BURNLIGHT_KEEP_BUILD') != '1' and app.is_dir() and not app.is_symlink():
    subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(destination)], check=True)
    lsregister = '/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister'
    subprocess.run([lsregister, '-u', str(app)], stderr=subprocess.DEVNULL)
    shutil.rmtree(app)
    print('Removed build output:', app, '(set BURNLIGHT_KEEP_BUILD=1 to keep it)')
