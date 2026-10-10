#!/usr/bin/env python3
"""Audit saved actual optical blocks and GUI copies; never accesses an optical device."""
import hashlib
import json
import os
import pathlib
import shlex
import stat

root = pathlib.Path(__file__).resolve().parents[2]
evidence = root / 'gui/evidence/write-acceptance-20261010'
output = evidence / 'windows-return'
records = {d['id']: d for d in (json.loads(p.read_text()) for p in (pathlib.Path.home() / 'Library/Application Support/BurnLight/Records').glob('*.json'))}
inspection = records['44A8C222-4F9D-4F9F-B051-F4EE920C3233']
export = records['46B5B7F1-66D9-424E-885D-74110C6BA19A']
assert inspection['phase'] == 'idle' and export['phase'] == 'completed'

def commands(record):
    return sorted(pathlib.Path(record['workspacePath']).glob('command-*'), key=lambda p: p.stat().st_mtime)

def command(record, operation):
    c = next(c for c in commands(record) if json.loads((c / 'request.json').read_text())['operation'] == operation)
    assert json.loads((c / 'transport.json').read_text())['exitCode'] == 0
    return c

listing = (command(inspection, 'list') / 'stdout.log').read_text()
tree = {}
for line in listing.splitlines():
    if line.startswith('FILE '):
        row = shlex.split(line)
        tree[row[1]] = dict(bytes=int(row[2][6:]), sections=[tuple(map(int, s[4:].split('+'))) for s in row[3:]])
assert len(tree) == 8 and tree['AlgoLogFileWatcher.java'] == dict(bytes=2564, sections=[(33493, 2564)])
for op in ('inspect', 'list'):
    text = (command(inspection, op) / 'stdout.log').read_text()
    assert all(x in text.splitlines() for x in ('Sessions: 5', 'Tracks: 5', 'Last session LBA: 33464', 'Next writable LBA: 40666', 'Free space: 319177 blocks / 653674496 bytes', 'Appendable: yes', 'Unfinished Session: no'))

target = pathlib.Path(export['exportPath'])
work = pathlib.Path(export['evidencePath'])
receipt = json.loads((work / 'export.json').read_text())
assert (work / 'result.txt').read_text().startswith('OPTICAL_EXPORT_VERIFIED\n')
command(export, 'export')
assert {e['path'] for e in receipt['entries'] if not e['directory']} == set(tree)
expected = json.loads((evidence / 'windows/windows-expected.json').read_text())
old = {e['relativePath']: e for e in expected['entries'] if not e['directory']}
assert len(old) == 7 and set(tree) == set(old) | {'AlgoLogFileWatcher.java'}
before_tree = {}
before_listing = (command(records['3651DFC7-29CC-4495-BB56-3D57B4FDF778'], 'list') / 'stdout.log').read_text()
for line in before_listing.splitlines():
    if line.startswith('FILE '):
        row = shlex.split(line)
        before_tree[row[1]] = dict(bytes=int(row[2][6:]), sections=[tuple(map(int, s[4:].split('+'))) for s in row[3:]])
assert all(tree[path] == before_tree[path] for path in old)

def cached_hash(cache_dir, sections):
    cache = cache_dir / 'disc-cache.iso'
    valid = set(map(int, pathlib.Path(str(cache) + '.blocks').read_text().split()))
    digest = hashlib.sha256()
    with cache.open('rb') as f:
        for start, length in sections:
            for offset in range(0, length, 2048):
                lba = start + offset // 2048
                assert lba in valid, 'Optical cache hole cannot prove a read'
                f.seek(lba * 2048)
                b = f.read(2048)
                assert len(b) == 2048
                digest.update(b[:min(2048, length - offset)])
    return digest.hexdigest()

files = []
for e in receipt['entries']:
    p = target / e['path']
    mode = p.lstat().st_mode
    if e['directory']:
        assert stat.S_ISDIR(mode)
        continue
    assert stat.S_ISREG(mode) and p.stat().st_uid == os.getuid()
    digest = hashlib.sha256(p.read_bytes()).hexdigest()
    assert p.stat().st_size == e['bytes'] == tree[e['path']]['bytes'] and digest == e['sha256']
    hashes = [cached_hash(work / folder, tree[e['path']]['sections']) for folder in ('source-read', 'verification-read')]
    assert hashes == [digest, digest]
    if e['path'] in old:
        assert old[e['path']]['size'] == e['bytes'] and old[e['path']]['sha256'] == digest
    files.append(dict(path=e['path'], size=e['bytes'], sha256=digest, actualOpticalReads=2, localCopyMatches=True, currentUserOwned=True, matchesBeforeWindows= e['path'] in old))
assert len(files) == 8
assert {p.relative_to(target).as_posix() for p in target.rglob('*')} == {e['path'] for e in receipt['entries']}
original = json.loads((evidence / 'authorized-input.json').read_text())
for e in original['entries']:
    p = pathlib.Path(original['source']) / e['path']
    assert p.stat().st_size == e['size'] and hashlib.sha256(p.read_bytes()).hexdigest() == e['sha256']
assert not pathlib.Path(f'/private/tmp/burnlight-physical-{os.getuid()}/active.json').exists()
all_commands = commands(inspection) + commands(export)
assert not any(json.loads((p / 'request.json').read_text())['operation'] in ('init', 'burn') for p in all_commands)
report = dict(result='PASS', GUIInspectionJob=inspection['id'], GUIExportJob=export['id'], localExport=str(target), media='CD-R', sessions=5, tracks=5, latestSession=33464, nextWritable=40666, freeBytes=653674496, appendable=True, files=files, sevenHistoricalHashesUnchanged=True, sevenHistoricalExtentsUnchanged=True, desktopOriginalsUnchanged=True, physicalWritesDuringReturn=0, windows='MANUAL_PASS', newWindowsFileOriginalHashCollected=False, boundary='New Windows file is proven by two independent actual optical reads and a matching local copy; its original Windows source hash was not supplied.')
(output / 'return-readback-check.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
print('PASS: eight files, two actual optical caches each, local ownership/hash/tree, seven pre-Windows hashes, Desktop originals unchanged; zero writes')
