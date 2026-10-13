#!/usr/bin/env python3
"""Anonymous independent VAT fixture -> bundled CLI/adapter -> known source bytes."""
import hashlib
import json
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tests'))
from vat_fixture import fixture, windows_fid_compatibility, windows_carried_fid
from vat_oracle import read as oracle
app=pathlib.Path(os.environ['BURNLIGHT_TEST_APP'])
cli=app/'Contents/MacOS/appendcd';adapter=cli.with_name('burnlight-export')
out=pathlib.Path(tempfile.mkdtemp(prefix='burnlight-vat-export-',dir='/private/tmp'))
evidence=pathlib.Path(os.environ['BURNLIGHT_EVIDENCE_DIR']);evidence.mkdir(parents=True,exist_ok=True)
checks=[];logs=[]
def run(*args,ok=True):
    p=subprocess.run(list(map(str,args)),text=True,capture_output=True)
    logs.append(shlex.join(list(map(str,args)))+'\n'+p.stdout+p.stderr)
    assert (p.returncode==0)==ok,logs[-1]
    return p.stdout
try:
    image=out/'eight.iso';fixture(image)
    listing=run(cli,'list','--source',image,'--last',14000)
    identity=next(line.split(': ')[1] for line in listing.splitlines() if line.startswith('Filesystem view:'))
    expected=oracle(image,14000)['files']
    def export(label,paths,view=identity,ok=True,compatibility='',last=14000):
        work=out/label;work.mkdir(mode=0o700);selection=work/'selection.txt'
        lines=['BURNLIGHT_EXPORT_SELECTION 1','ALL' if len(paths)==len(expected) else 'SELECTED']
        dirs=set()
        for name in paths:dirs.update(str(p) for p in pathlib.PurePosixPath(name).parents if str(p)!='.')
        for name in sorted(dirs):lines.append(json.dumps(name,ensure_ascii=False)+' D 0')
        for name in paths:lines.append(json.dumps(name,ensure_ascii=False)+' F '+str(len(expected[name]['bytes'])))
        selection.write_text('\n'.join(lines)+'\n')
        result=run(adapter,'offline-export','--image',image,'--last',last,'--selection',selection,'--work-dir',work,'--expected-view',view,ok=ok)
        if not ok:
            assert not list(work.glob('run-*/result.txt'));checks.append(label);return
        exported=pathlib.Path(next(line[20:] for line in result.splitlines() if line.startswith('Evidence directory: ')))
        receipt=json.loads((exported/'export.json').read_text());assert receipt['filesystem']=='udf2.01+vat'
        assert receipt['udfCompatibility']==compatibility
        for name in paths:assert (exported/'files'/name).read_bytes()==expected[name]['bytes']
        checks.append(label)
    export('current-eight-session-all-inline-physical-Chinese-empty',list(expected))
    export('inline-old-original-ICB',['inline-old.txt'])
    export('Chinese-empty-nested',['中文目录/空文件.txt'])
    export('stale-VAT-transaction-rejected',['latest.txt'],'0'*64,False)
    image=out/'windows-compat.iso';windows_fid_compatibility(image)
    expected=oracle(image,14000,windows_fid_compatibility=True)['files']
    listing=run(cli,'list','--source',image,'--last',14000)
    compatibleView=next(line.split(': ')[1] for line in listing.splitlines() if line.startswith('Filesystem view:'))
    export('Windows-profile-export-receipt-explicit',list(expected),compatibleView,compatibility='windows-fid-location-v1')
    image=out/'windows-carried.iso';layout=windows_carried_fid(image)
    expected=oracle(image,layout['session'],windows_fid_compatibility=True)['files']
    listing=run(cli,'list','--source',image,'--last',layout['session'])
    carriedView=next(line.split(': ')[1] for line in listing.splitlines() if line.startswith('Filesystem view:'))
    export('Windows-carried-origin-and-deleted-FID-export',list(expected),carriedView,compatibility='windows-fid-location-v1',last=layout['session'])
    report={'result':'PASS','checks':checks,'count':len(checks),'cliSHA256':hashlib.sha256(cli.read_bytes()).hexdigest(),'adapterSHA256':hashlib.sha256(adapter.read_bytes()).hexdigest(),'fixture':'tests/vat_fixture.py','oracle':'tests/vat_oracle.py','scratch':str(out),'physicalWrites':0}
    (evidence/'vat-export.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print('PASS:',len(checks),'VAT export scenarios;',out)
finally:(evidence/'vat-export.log').write_text('\n\n'.join(logs))
