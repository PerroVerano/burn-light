#!/usr/bin/env python3
"""Collect completed local GUI jobs without editing or removing their originals."""
import hashlib
import json
import os
import pathlib
import shutil
import tarfile

root = pathlib.Path(__file__).resolve().parents[2]
evidence = pathlib.Path(os.environ.get('BURNLIGHT_EVIDENCE_DIR', str(root / 'gui/evidence')))
destination = evidence / os.environ.get('BURNLIGHT_JOB_BUNDLE', 'gui-readonly-20261010')
destination.mkdir(exist_ok=True)
app = pathlib.Path(os.environ.get('BURNLIGHT_APP_PATH', str(root / 'gui/output/BurnLight.app')))
config = json.loads((app / 'Contents/Resources/LocalConfiguration.json').read_text())
default_records = pathlib.Path.home() / 'Library/Application Support/BurnLight/Records' if config['recordDirectory'] == 'ApplicationSupport/Records' else pathlib.Path(config['recordDirectory'])
record_directory = pathlib.Path(os.environ.get('BURNLIGHT_RECORD_DIRECTORY', str(default_records)))
records = sorted([json.loads(p.read_text()) for p in record_directory.glob('*.json')], key=lambda record: record['created'])
job_ids = set(os.environ.get('BURNLIGHT_JOB_IDS', '').split(',')) - {''}
if job_ids:
    records = [record for record in records if record['id'] in job_ids]
jobs, commands = [], []
for record in records:
    if record['phase'] in ('physical', 'unknown'):
        raise SystemExit('Refuse to archive an active/unknown job')
    source = pathlib.Path('/private/tmp/burnlight-job-' + record['id'])
    if not source.is_dir():
        continue
    job_dest = destination / record['id']
    job_dest.mkdir(exist_ok=True)
    (job_dest / 'job.json').write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n')
    for command in sorted(source.glob('command-*'), key=lambda path: path.stat().st_mtime):
        if not (command / 'transport.json').exists():
            continue
        transport = json.loads((command / 'transport.json').read_text())
        if transport['operation'] in ('init', 'burn') and os.environ.get('BURNLIGHT_COLLECT_WRITES') != '1':
            raise SystemExit('This evidence bundle is restricted to read-only jobs')
        target = job_dest / command.name
        target.mkdir(exist_ok=True)
        for name in ('stdout.log', 'stderr.log', 'bridge.stdout.log', 'bridge.stderr.log', 'transport.json', 'request.json', 'exit.code'):
            if (command / name).is_file():
                shutil.copy2(command / name, target / name)
        commands.append(dict(jobID=record['id'], operation=transport['operation'], exitCode=transport['exitCode'], directory=str(target.relative_to(root))))
    for result in source.glob('optical/run-*/result.txt'):
        shutil.copy2(result, job_dest / (result.parent.name + '-result.txt'))
    for expected in source.glob('optical/run-*/windows-expected.json'):
        shutil.copy2(expected, job_dest / (expected.parent.name + '-windows-expected.json'))
    for export in source.glob('optical/run-*/export.json'):
        shutil.copy2(export, job_dest / (export.parent.name + '-export.json'))
    for name in ('local-export.json', 'export-selection.txt'):
        if (source / name).is_file():
            shutil.copy2(source / name, job_dest / name)
    archive = job_dest / 'original-job.tar.gz'
    omitted = []
    def readable(member):
        actual = source.parent / member.name
        if member.isfile() and not os.access(actual, os.R_OK):
            omitted.append(member.name)
            return None
        return member
    with tarfile.open(archive, 'w:gz') as tar:
        tar.add(source, arcname=source.name, filter=readable)
        if record.get('snapshotPath'):
            snapshot_root = pathlib.Path(record['snapshotPath']).parent
            tar.add(snapshot_root, arcname=snapshot_root.name, filter=readable)
    jobs.append(dict(id=record['id'], original=str(source), archive=str(archive.relative_to(root)), archiveSHA256=hashlib.sha256(archive.read_bytes()).hexdigest(), unreadableExtraFilesOmitted=omitted))
write_count = sum(row['operation'] in ('init', 'burn') for row in commands)
summary = dict(date='2026-10-10', physicalWrites=write_count, commands=commands, jobs=jobs,
               allowPhysicalWrites=config['allowPhysicalWrites'], dependencies=config['trustedFiles'],
               application=str(app), recordDirectory=str(record_directory),
               GUIsha256=hashlib.sha256((app / 'Contents/MacOS/BurnLight').read_bytes()).hexdigest(),
               archiveBoundary='Original absolute manifest/cache references retained; archives are diagnostic evidence, not relocatable standalone verify packages.')
(destination / 'index.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n')
baseline_index = json.loads((root / 'artifacts/baseline-before-gui/latest.json').read_text())
manifest = json.loads((root / baseline_index['snapshot'] / 'sha256.json').read_text())
core = []
for name, expected in manifest['files'].items():
    if name.startswith(('src/', 'backend/', 'scripts/', 'tests/', 'fixtures/', 'build/', 'runtime/')) or name == 'CMakeLists.txt':
        path = pathlib.Path.home() / '.local/libexec/appendcd' / pathlib.Path(name).name if name.startswith('runtime/') else root / name
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        core.append(dict(path=name, expected=expected, actual=actual, unchanged=actual == expected))
allowed = {'src/IsoCore.hpp', 'src/IsoCore.cpp', 'src/UdfSession.hpp', 'src/UdfSession.cpp', 'src/main.cpp', 'src/DiscScanner.hpp', 'src/DiscScanner.cpp', 'src/IsoVerifier.cpp', 'src/UdfReader.hpp', 'src/UdfReader.cpp', 'tests/drive_selection.cpp'} if os.environ.get('BURNLIGHT_COLLECT_WRITES') == '1' else set()
final = dict(result='PASS' if all(row['unchanged'] or row['path'] in allowed for row in core) else 'FAIL', intentionalSourceChanges=sorted(allowed), comparedTo=baseline_index['snapshot'], files=core, intentionalDocumentationChanges=['README.md', 'docs/gui-development-guide.md'])
(evidence / 'baseline-final.json').write_text(json.dumps(final, indent=2) + '\n')
print(json.dumps(dict(commands=len(commands), jobs=len(jobs), core=final['result'], physicalWrites=write_count, GUIsha256=summary['GUIsha256']), indent=2))
if final['result'] != 'PASS':
    raise SystemExit(1)
