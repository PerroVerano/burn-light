#!/usr/bin/env python3
"""Restore missing baseline inputs for a fresh checkout; never replace existing files."""
import argparse
import hashlib
import json
import os
import pathlib
import stat
import tarfile
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--runtime-dir', type=pathlib.Path, default=pathlib.Path.home() / '.local/libexec/appendcd')
parser.add_argument('--check', action='store_true', help='Validate existing inputs without restoring files')
args = parser.parse_args()
if pathlib.Path(f'/private/tmp/burnlight-physical-{os.getuid()}/active.json').exists():
    raise SystemExit('Physical task active/unknown; reference restoration refused')
index = json.loads((root / 'artifacts/baseline-before-gui/latest.json').read_text())
snapshot = root / index['snapshot']
if snapshot.resolve().parent != (root / 'artifacts/baseline-before-gui').resolve():
    raise SystemExit('Baseline index escapes expected directory')
manifest = json.loads((snapshot / 'sha256.json').read_text())
archive = snapshot / 'baseline.tar.gz'
if hashlib.sha256(archive.read_bytes()).hexdigest() != index['archiveSha256'] or manifest['archiveSha256'] != index['archiveSha256']:
    raise SystemExit('Baseline archive hash mismatch')
runtime = args.runtime_dir.expanduser().absolute()
targets = {
    'build/appendcd': root / 'build/appendcd',
    'runtime/appendcd-cdrecord': runtime / 'appendcd-cdrecord',
    'runtime/appendcd-initial-cdrecord': runtime / 'appendcd-initial-cdrecord',
    'build/backend/provenance.json': runtime / 'provenance.json',
    'build/initial-backend/provenance.json': runtime / 'initial-provenance.json',
}
pending = []
with tarfile.open(archive) as tar:
    for name, target in targets.items():
        expected = manifest['files'][name]
        member = tar.getmember(name)
        if not member.isfile():
            raise SystemExit('Baseline input is not a regular file: ' + name)
        data = tar.extractfile(member).read()
        if hashlib.sha256(data).hexdigest() != expected:
            raise SystemExit('Baseline member hash mismatch: ' + name)
        if target.exists() or target.is_symlink():
            if not stat.S_ISREG(target.lstat().st_mode) or hashlib.sha256(target.read_bytes()).hexdigest() != expected:
                raise SystemExit('Existing reference differs; left unchanged: ' + str(target))
        elif args.check:
            raise SystemExit('Missing reference: ' + str(target))
        else:
            pending.append((target, data, 0o600 if name.endswith('.json') else 0o755))
for target, data, mode in pending:
    target.parent.mkdir(parents=True, exist_ok=True)
    # Stage privately, then link exclusively: an intervening file is never overwritten.
    fd, temporary = tempfile.mkstemp(prefix='.burnlight-reference-', dir=target.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fchmod(stream.fileno(), mode)
            os.fsync(stream.fileno())
        os.link(temporary, target)
        print('Restored:', target)
    finally:
        pathlib.Path(temporary).unlink()
print('PASS: reference archive and all five input hashes; existing files preserved')
