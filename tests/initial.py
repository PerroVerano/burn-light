#!/usr/bin/env python3
"""First-session ISO/Joliet and existing continuation regression; no optical writes."""
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import os

CLI = pathlib.Path(sys.argv[1]).resolve()
OUT = pathlib.Path(os.environ.get('APPENDCD_TEST_ARTIFACT_ROOT', str(CLI.parent.parent/'artifacts'))) / 'initial-offline'
if OUT.exists():
    shutil.rmtree(OUT)
OUT.mkdir(parents=True)
logs, checks = [], []

def run(*args, ok=True):
    p = subprocess.run([str(x) for x in args], capture_output=True, text=True)
    text = p.stdout + p.stderr
    logs.append('$ ' + ' '.join(str(x) for x in args) + '\n' + text)
    assert (p.returncode == 0) == ok, logs[-1]
    return text

def check(name, condition):
    assert condition, name
    checks.append(name)

def file(folder, name, content):
    p = folder / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(content)

try:
    first, second, third = [OUT / name for name in ('first-files', 'second-files', 'third-files')]
    for p in (first, second, third):
        p.mkdir()
    payloads = {
        'hello-from-macos.txt': b'Mac creates Session 1\n',
        '中文测试.txt': 'Mac 首刻中文\n'.encode(),
        '中文目录/MixedCase.txt': b'case-preserved\n',
        '中文目录/二级/空文件.txt': b'',
        'LongName_' + 'abcdef' * 12 + '.txt': b'long Joliet name\n',
        'large.bin': bytes(range(256)) * 4096,
    }
    for name, data in payloads.items():
        file(first, name, data)
    file(first, '.DS_Store', b'Finder cache\n')
    file(first, '中文目录/.DS_Store', b'nested Finder cache\n')
    image = OUT / 'first.iso'
    check('standalone initial generator uses verified LBA 0', 'Initial Session LBA: 0' in run(CLI, 'init-build', first, image))
    check('manifest start 0 roundtrips with strict independent verification', '0 old files' in run(CLI, 'verify', image))
    listing = run(CLI, 'list', '--source', image, '--last', 0)
    check('Chinese, case, long, nested and empty names survive import', all(name in listing for name in payloads))
    check('Finder cache excluded recursively on first recording', '.DS_Store' not in listing)
    iso = image.read_bytes()
    check('PVD at LBA 16 and Joliet SVD at LBA 17', iso[16*2048:16*2048+7] == b'\x01CD001\x01' and iso[17*2048:17*2048+7] == b'\x02CD001\x01')
    check('first Volume Space Size equals actual image blocks', int.from_bytes(iso[16*2048+80:16*2048+84], 'little') == len(iso)//2048)
    check('isoinfo reads standalone Joliet tree independently', 'hello-from-macos.txt' in run('/opt/homebrew/bin/isoinfo', '-J', '-f', '-i', image))
    for name, data in payloads.items():
        if name.isascii() and '/' not in name:
            p = subprocess.run(['/opt/homebrew/bin/isoinfo', '-J', '-i', str(image), '-x', '/' + name + ';1'], capture_output=True)
            check('independent first payload ' + name, p.returncode == 0 and p.stdout == data)
    file(second, 'hello-from-windows.txt', b'synthetic second-session payload\n')
    file(second, '中文目录/追加.txt', b'new nested file\n')
    continuation = OUT / 'second.iso'
    generated = run(CLI, 'build', second, continuation, '--source', image, '--last', 0, '--next', 12128)
    check('existing append generator does not reread first-session payload', 'Old-source reads during generation: 0' in generated)
    check('existing append verifier preserves initial extents and all files', '6 old files' in run(CLI, 'verify', continuation))
    check('1 MiB old file not copied into continuation', continuation.stat().st_size < 200000)
    composite = OUT / 'composite.iso'
    with composite.open('wb') as out:
        out.write(iso)
        out.seek(12128*2048)
        out.write(continuation.read_bytes())
    check('latest-session import contains old plus new files', 'hello-from-windows.txt' in run(CLI, 'list', '--source', composite, '--last', 12128))
    file(third, '第三轮.txt', b'third\n')
    run(CLI, 'build', third, OUT / 'third.iso', '--source', composite, '--last', 12128, '--next', 24576)
    check('two continuations after Mac initial image verify', '8 old files' in run(CLI, 'verify', OUT / 'third.iso'))
    check('existing output never overwritten', 'already exists' in run(CLI, 'init-build', first, image, ok=False))
    empty = OUT / 'empty'; empty.mkdir(); file(empty, '.DS_Store', b'ignored')
    check('empty/cache-only input rejected before writing image', 'Nothing to record' in run(CLI, 'init-build', empty, OUT / 'empty.iso', ok=False) and not (OUT/'empty.iso').exists())
    collision = OUT / 'collision'; collision.mkdir()
    file(collision, 'Case.txt', b'one'); file(collision, 'case.txt', b'two')
    if len(list(collision.iterdir())) == 2:
        check('Windows case collision rejected', 'collision' in run(CLI, 'init-build', collision, OUT / 'collision.iso', ok=False))
    else:
        logs.append('SKIP: host filesystem cannot represent Case.txt and case.txt as distinct inputs; existing import-vs-new case collision regression still runs.')
    symlink = OUT / 'symlink'; symlink.mkdir(); (symlink/'link').symlink_to(first/'large.bin')
    check('symlink rejected', 'symlink' in run(CLI, 'init-build', symlink, OUT/'symlink.iso', ok=False))
    check('initial offline image cannot select device', 'offline' in run(CLI,'init-build',first,OUT/'bad.iso','--device','1,0,0',ok=False))
    invalid = OUT / 'invalid.iso'; shutil.copyfile(image, invalid)
    manifest = pathlib.Path(str(image)+'.manifest').read_text()
    pathlib.Path(str(invalid)+'.manifest').write_text(manifest.replace('F N ', 'F O ', 1))
    check('initial manifest cannot claim old references', 'old references' in run(CLI, 'verify', invalid, ok=False))
    print('PASS:', len(checks), 'initial-session assertions')
    (OUT/'result.json').write_text(json.dumps({'result':'OFFLINE_PASS','windowsAcceptance':'PENDING','checks':checks},ensure_ascii=False,indent=2)+'\n')
finally:
    (OUT/'commands.log').write_text('\n\n'.join(logs))
