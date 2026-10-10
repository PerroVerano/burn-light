#!/usr/bin/env python3
"""Import a complete 31-block Joliet volume without weakening generated-image checks."""
import hashlib
import json
import os
import pathlib
import shlex
import struct
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
app = pathlib.Path(os.environ.get('BURNLIGHT_TEST_APP', str(root / 'output/BurnLight.app')))
cli = app / 'Contents/MacOS/appendcd'
adapter = cli.with_name('burnlight-export')
evidence = pathlib.Path(os.environ.get('BURNLIGHT_EVIDENCE_DIR', str(root / 'evidence')))
evidence.mkdir(parents=True, exist_ok=True)
scratch = pathlib.Path(tempfile.mkdtemp(prefix='burnlight-compact-', dir='/private/tmp'))
checks, logs = [], []

def run(args, ok=True):
    p = subprocess.run([str(x) for x in args], text=True, capture_output=True)
    logs.append(shlex.join(map(str, args)) + '\n' + p.stdout + p.stderr)
    assert (p.returncode == 0) == ok, logs[-1]
    return p.stdout + p.stderr

def check(name, condition):
    assert condition, name
    checks.append(name)

try:
    files = scratch / 'files'
    files.mkdir()
    payload = b'compact Windows-like session\n'
    (files / 'compact.txt').write_bytes(payload)
    image = scratch / 'compact.iso'
    run(['/opt/homebrew/bin/mkisofs', '-quiet', '-no-pad', '-J', '-input-charset', 'utf-8', '-o', image, files])
    raw = image.read_bytes()
    check('fixture is a complete 31-block volume', len(raw) == 31 * 2048 and struct.unpack_from('<I', raw, 16 * 2048 + 80)[0] == 31)
    check('compact Joliet list imports expected file', 'FILE "compact.txt" bytes=29' in run([cli, 'list', '--source', image, '--last', 0]))
    check('independent descriptors and path tables accept compact volume', 'VolumeSpaceSize=31' in run([cli, 'inspect', '--source', image, '--last', 0]))
    work = scratch / 'export'
    work.mkdir(mode=0o700)
    selection = work / 'selection.txt'
    selection.write_text('BURNLIGHT_EXPORT_SELECTION 1\nALL\n"compact.txt" F 29\n')
    output = run([adapter, 'offline-export', '--image', image, '--last', 0, '--selection', selection, '--work-dir', work])
    exported = pathlib.Path(next(x[20:] for x in output.splitlines() if x.startswith('Evidence directory: ')))
    check('compact source exported payload matches independent oracle', (exported / 'files/compact.txt').read_bytes() == payload)
    new = scratch / 'new'
    new.mkdir()
    (new / 'added.txt').write_text('new continuation\n')
    continuation = scratch / 'next.iso'
    check('compact source continuation generated without reading old payload', 'Old-source reads during generation: 0' in run([cli, 'build', new, continuation, '--source', image, '--last', 0, '--next', 12128]))
    check('generated continuation still passes strict verify', 'IMAGE VERIFIED' in run([cli, 'verify', continuation]))
    bad = bytearray(raw)
    for sector in (16, 17):
        struct.pack_into('<I', bad, sector * 2048 + 80, 16)
        struct.pack_into('>I', bad, sector * 2048 + 84, 16)
    damaged = scratch / 'descriptor-outside-volume.iso'
    damaged.write_bytes(bad)
    check('descriptor outside reported volume remains rejected', 'Invalid Volume Space Size' in run([cli, 'inspect', '--source', damaged, '--last', 0], ok=False))
    bad = bytearray(continuation.read_bytes())
    for sector in (16, 17):
        struct.pack_into('<I', bad, sector * 2048 + 80, 31)
        struct.pack_into('>I', bad, sector * 2048 + 84, 31)
    damaged = scratch / 'generated-small-volume.iso'
    damaged.write_bytes(bad)
    original_hash = hashlib.sha256(continuation.read_bytes()).hexdigest()
    pathlib.Path(str(damaged) + '.manifest').write_text(pathlib.Path(str(continuation) + '.manifest').read_text().replace(original_hash, hashlib.sha256(bad).hexdigest()))
    check('rebinding hash cannot weaken generated 32-block minimum', 'Invalid generated Volume Space Size' in run([cli, 'verify', damaged], ok=False))
    (evidence / 'compact-session.json').write_text(json.dumps(dict(result='PASS', count=len(checks), checks=checks, scratch=str(scratch), physicalWrites=0, cliSHA256=hashlib.sha256(cli.read_bytes()).hexdigest()), indent=2) + '\n')
    print('PASS:', len(checks), 'compact-session assertions; zero physical writes')
finally:
    (evidence / 'compact-session.log').write_text('\n\n'.join(logs))
