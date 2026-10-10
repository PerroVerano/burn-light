#!/usr/bin/env python3
"""Exercise the independent read-only adapter with existing multisession fixtures."""
import hashlib
import json
import pathlib
import os
import shlex
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
adapter = root / 'output/BurnLight.app/Contents/MacOS/burnlight-export'
project = root.parent
scratch = pathlib.Path(tempfile.mkdtemp(prefix='burnlight-export-offline-', dir='/private/tmp'))
checks, logs = [], []

def run(args, ok=True):
    p = subprocess.run([str(a) for a in args], capture_output=True, text=True)
    logs.append(shlex.join([str(a) for a in args]) + '\n' + p.stdout + p.stderr)
    assert (p.returncode == 0) == ok, logs[-1]
    return p.stdout

def export(image, last, label, roots=(), broken=None):
    listing = run([root / 'output/BurnLight.app/Contents/MacOS/appendcd', 'list', '--source', image, '--last', last])
    entries = []
    for line in listing.splitlines():
        if line.startswith(('FILE ', 'DIR  ')):
            parts = shlex.split(line)
            entries.append(dict(path=parts[1], directory=parts[0] == 'DIR', bytes=0 if parts[0] == 'DIR' else int(parts[2][6:])))
    wanted = [e for e in entries if not roots or any(e['path'] == p or e['path'].startswith(p + '/') for p in roots)]
    for entry in list(wanted):
        for parent in pathlib.PurePosixPath(entry['path']).parents:
            if str(parent) != '.' and not any(e['path'] == str(parent) for e in wanted):
                wanted.append(next(e for e in entries if e['path'] == str(parent)))
    work = scratch / label
    work.mkdir(mode=0o700)
    selection = work / 'selection.txt'
    rows = ['BURNLIGHT_EXPORT_SELECTION 1', 'SELECTED' if roots else 'ALL']
    rows += [json.dumps(e['path'], ensure_ascii=False) + (' D ' if e['directory'] else ' F ') + str(e['bytes']) for e in wanted]
    if broken:
        rows[-1] = broken
    selection.write_text('\n'.join(rows) + '\n')
    output = run([adapter, 'offline-export', '--image', image, '--last', last, '--selection', selection, '--work-dir', work], ok=not broken)
    if broken:
        checks.append(label)
        assert not any(p.exists() for p in work.glob('run-*/result.txt'))
        return
    evidence = pathlib.Path(next(line[20:] for line in output.splitlines() if line.startswith('Evidence directory: ')))
    receipt = json.loads((evidence / 'export.json').read_text())
    assert receipt['schema'] == 1 and receipt['sourceKind'] == 'offline'
    assert {e['path'] for e in receipt['entries']} == {e['path'] for e in wanted}
    for e in receipt['entries']:
        p = evidence / 'files' / e['path']
        if e['directory']:
            assert p.is_dir()
        else:
            assert p.stat().st_size == e['bytes'] and hashlib.sha256(p.read_bytes()).hexdigest() == e['sha256']
            # 独立输入或历史真实读回 hash 作 oracle，不能只核对适配器自己的清单。
            originals = []
            if image == project / 'artifacts/offline/session1.iso':
                originals = [project / 'artifacts/offline/session1-files' / e['path']]
            elif image == project / 'artifacts/udf-tests/composite.iso':
                originals = [project / 'artifacts/udf-tests' / d / e['path'] for d in ('old', 'new')]
            if originals:
                original = next(p for p in originals if p.is_file())
                assert hashlib.sha256(original.read_bytes()).hexdigest() == e['sha256']
            else:
                baseline = json.loads((project / 'artifacts/cd-initial-roundtrip/readback/windows-session2-expected.json').read_text())
                original = next(x for x in baseline['entries'] if x['relativePath'] == e['path'])
                assert original['sha256'] == e['sha256']
    checks.append(label)
    return receipt

try:
    iso = project / 'artifacts/offline/session1.iso'
    full = export(iso, 0, 'Joliet-all-old-payload-nested-Chinese-empty-Finder-cache')
    assert any(e['path'] == '.DS_Store' for e in full['entries'])
    export(iso, 0, 'Joliet-selected-folder-includes-descendants', ['目录'])
    export(iso, 0, 'Joliet-selected-nested-file-includes-ancestors', ['目录/二级/旧文件.txt'])
    export(project / 'artifacts/udf-tests/composite.iso', 12128, 'UDF-multisession-retained-extents-and-new-files')
    export(project / 'artifacts/cd-initial-roundtrip/readback/read-two/disc-cache.iso', 11702, 'saved-real-CD-Windows-session-and-Mac-old-extents')
    export(iso, 0, 'missing-selection-path-rejected', broken='"does-not-exist" F 0')
    export(iso, 0, 'path-traversal-rejected', broken='"../outside" F 0')
    export(iso, 0, 'wrong-file-size-rejected', broken='"hello-from-windows.txt" F 999')
    run([adapter, 'export', '--last', '0'], ok=False)
    checks.append('physical-LBA-option-rejected-before-any-device-access')
    run([adapter, 'burn'], ok=False)
    checks.append('write-command-rejected-without-CLI-fallback')
    out = pathlib.Path(os.environ.get('BURNLIGHT_EVIDENCE_DIR', str(root / 'evidence/export-20261010')))
    out.mkdir(exist_ok=True)
    (out / 'adapter-offline.json').write_text(json.dumps(dict(result='PASS', count=len(checks), checks=checks, scratch=str(scratch), physicalWrites=0, actualOpticalRead=False), ensure_ascii=False, indent=2) + '\n')
    print(f'PASS: {len(checks)} adapter offline scenarios; no physical commands')
finally:
    out = pathlib.Path(os.environ.get('BURNLIGHT_EVIDENCE_DIR', str(root / 'evidence/export-20261010')))
    out.mkdir(exist_ok=True)
    (out / 'adapter-offline.log').write_text('\n\n'.join(logs))
