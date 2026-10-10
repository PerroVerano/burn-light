#!/usr/bin/env python3
"""Audit GUI receipts and copied payloads only; never opens an optical device."""
import hashlib,json,os,pathlib,shlex
root=pathlib.Path(__file__).resolve().parents[2]
evidence=root/'gui/evidence/write-acceptance-20261010'
records={r['id']:r for r in (json.loads(p.read_text()) for p in (pathlib.Path.home()/'Library/Application Support/BurnLight/Records').glob('*.json'))}
first=records['EE38FFEE-330A-4CCB-A17C-97791DB8145B'];second=records['3651DFC7-29CC-4495-BB56-3D57B4FDF778'];skip=records['2B6BDEB9-2903-4ED8-844A-8B685A2C01C0']
def manifest(record):
 rows=[shlex.split(l) for l in pathlib.Path(record['imagePath']+'.manifest').read_text().splitlines()[3:]]
 return {x[2]:dict(kind=x[0],origin=x[1],bytes=int(x[3]),sha256=x[4],sections=x[6:]) for x in rows}
def listing(record):
 commands=sorted(pathlib.Path(record['workspacePath']).glob('command-*'),key=lambda p:p.stat().st_mtime)
 command=next(c for c in commands if json.loads((c/'request.json').read_text())['operation']=='list')
 assert json.loads((command/'transport.json').read_text())['exitCode']==0
 tree={}
 for l in (command/'stdout.log').read_text().splitlines():
  if l.startswith('FILE '):
   row=shlex.split(l);tree[row[1]]=dict(bytes=int(row[2][6:]),sections=row[3:])
 return tree
a,b,c=map(manifest,(first,second,skip));la,lb=map(listing,(first,second))
assert c['DirectionRangeSimplifier.java']['origin']=='O' and ['LBA='+c['DirectionRangeSimplifier.java']['sections'][0]+'+'+c['DirectionRangeSimplifier.java']['sections'][1]]==la['DirectionRangeSimplifier.java']['sections']
assert b['DirectionRangeSimplifier.java']['origin']=='N' and lb['DirectionRangeSimplifier.java']['sections']!=la['DirectionRangeSimplifier.java']['sections']
fixture=root/'gui/Validation/PhysicalFixtures/conflict-20261010/DirectionRangeSimplifier.java'
assert b['DirectionRangeSimplifier.java']['sha256']==hashlib.sha256(fixture.read_bytes()).hexdigest()
retained=[p for p in a if a[p]['kind']=='F' and p!='DirectionRangeSimplifier.java']
assert all(lb[p]==la[p] and b[p]['sha256']==a[p]['sha256'] for p in retained)
for record in (first,second):
 assert record['phase']=='completed'
 assert pathlib.Path(record['evidencePath'],'result.txt').read_text().startswith('PHYSICAL_READBACK_PASS\n')
 command=next(c for c in pathlib.Path(record['workspacePath']).glob('command-*') if json.loads((c/'request.json').read_text())['operation']=='burn')
 assert json.loads((command/'transport.json').read_text())['exitCode']==0
 assert 'OPTICAL READBACK VERIFIED' in (command/'stdout.log').read_text()
expected=json.loads((evidence/'windows/windows-expected.json').read_text())
assert expected==json.loads(pathlib.Path(second['evidencePath'],'windows-expected.json').read_text())
export=max((r for r in records.values() if r.get('exportPath','').startswith(str(evidence/'local-copy'))),key=lambda x:x['created'])
assert export['phase']=='completed'
target=pathlib.Path(export['exportPath']);copies=[]
for entry in expected['entries']:
 p=target/entry['relativePath']
 if entry['directory']:assert p.is_dir();continue
 digest=hashlib.sha256(p.read_bytes()).hexdigest()
 assert p.stat().st_size==entry['size'] and digest==entry['sha256'] and p.stat().st_uid==os.getuid()
 copies.append(dict(path=entry['relativePath'],size=entry['size'],sha256=digest,currentUserOwned=True))
assert len(copies)==7
historical=json.loads((root/'artifacts/cd-initial-roundtrip/readback/windows-session2-expected.json').read_text())
for entry in historical['entries']:
 if not entry['directory']:assert hashlib.sha256((target/entry['relativePath']).read_bytes()).hexdigest()==entry['sha256']
original=json.loads((evidence/'authorized-input.json').read_text())
for entry in original['entries']:
 p=pathlib.Path(original['source'])/entry['path'];assert p.stat().st_size==entry['size'] and hashlib.sha256(p.read_bytes()).hexdigest()==entry['sha256']
assert (target/'方向范围约束航迹抽稀技术说明.docx').read_bytes()==pathlib.Path(original['source'],'方向范围约束航迹抽稀技术说明.docx').read_bytes()
assert (target/'DirectionRangeSimplifier.java').read_bytes()==fixture.read_bytes()
assert not pathlib.Path(f'/private/tmp/burnlight-physical-{os.getuid()}/active.json').exists()
report=dict(result='PASS',firstGUIWrite=first['id'],replacementGUIWrite=second['id'],skipGUIReadOnlyPreview=skip['id'],skipPreservesJavaExtent=la['DirectionRangeSimplifier.java'],replacementJava=dict(**b['DirectionRangeSimplifier.java'],actualSections=lb['DirectionRangeSimplifier.java']['sections']),retainedFiles=retained,allRetainedExtentsAndHashesUnchanged=True,physicalWriteRequests=2,exportGUIJob=export['id'],localExport=export['exportPath'],copiedFiles=copies,desktopOriginalsUnchanged=True,windowsAcceptance='PENDING')
(evidence/'physical-conflict-check.json').write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n')
print('PASS: two GUI writes/readbacks, skip preview preserves extent, replacement content differs, five retained extents/hashes, seven user-owned optical exports, Desktop originals unchanged')
