#!/usr/bin/env python3
"""Explicit collision policies, payload reads and retained extents in both writers. No optical I/O."""
import hashlib,json,os,pathlib,shlex,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[1]
cli=pathlib.Path(os.environ.get('BURNLIGHT_TEST_APP',str(root/'output/BurnLight.app')))/'Contents/MacOS/appendcd'
adapter=cli.with_name('burnlight-export')
scratch=pathlib.Path(tempfile.mkdtemp(prefix='burnlight-conflict-',dir='/private/tmp'))
evidence=pathlib.Path(os.environ.get('BURNLIGHT_EVIDENCE_DIR',str(root/'evidence')));evidence.mkdir(parents=True,exist_ok=True)
checks=[];logs=[]
def run(args,ok=True):
 p=subprocess.run([str(x) for x in args],capture_output=True,text=True);logs.append(shlex.join(map(str,args))+'\n'+p.stdout+p.stderr)
 assert (p.returncode==0)==ok,logs[-1]
 return p.stdout+p.stderr
def check(name,value):
 assert value,name
 checks.append(name)
def write(base,name,data):
 p=base/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
def manifest(image):
 rows=[shlex.split(l) for l in pathlib.Path(str(image)+'.manifest').read_text().splitlines()[3:]]
 return {r[2]:dict(kind=r[0],origin=r[1],size=int(r[3]),sha=r[4],sections=r[6:]) for r in rows}
try:
 for kind in ('joliet','udf'):
  work=scratch/kind;work.mkdir();old=work/'old';new=work/'new';old.mkdir();new.mkdir()
  old_data={'same.txt':b'old same\n','目录/zero.txt':b'old nonempty\n','retained.bin':bytes(range(256))*4096,'目录/keep.txt':b'unchanged nested\n'}
  new_data={'same.txt':b'new same\n','目录/zero.txt':b'','目录/added.txt':b'new nested\n'}
  for n,d in old_data.items():write(old,n,d)
  for n,d in new_data.items():write(new,n,d)
  source=work/'source.iso';run(['/opt/homebrew/bin/mkisofs','-quiet',*(['-udf'] if kind=='udf' else []),'-J','-input-charset','utf-8','-o',source,old])
  old_listing=run([cli,'list','--source',source,'--last',0])
  def build(folder,image,policy=None,ok=True):
   return run([cli,'build',folder,image,'--source',source,'--last',0,'--next',12128,*(['--on-conflict',policy] if policy else [])],ok)
  check(kind+' default rejects exact file conflict','conflict' in build(new,work/'reject.iso',ok=False))
  for policy in ('skip','replace'):
   image=work/(policy+'.iso');output=build(new,image,policy);m=manifest(image)
   check(kind+' '+policy+' writer uses no old payload while generating','Old-source reads during generation: 0' in output)
   run([cli,'verify',image]);check(kind+' '+policy+' independent verify passes',True)
   check(kind+' '+policy+' retained entries remain old extents',all(m[p]['origin']=='O' for p in ('retained.bin','目录/keep.txt')))
   expected={**old_data,**({k:v for k,v in new_data.items() if k not in old_data} if policy=='skip' else new_data)}
   check(kind+' '+policy+' collision origin matches choice',all(m[p]['origin']==('O' if policy=='skip' else 'N') for p in ('same.txt','目录/zero.txt')))
   if policy=='replace':check(kind+' replacement hashes include empty and nested files',all(m[p]['sha']==hashlib.sha256(new_data[p]).hexdigest() for p in ('same.txt','目录/zero.txt')))
   composite=work/(policy+'-composite.iso')
   with composite.open('wb') as f:f.write(source.read_bytes());f.seek(12128*2048);f.write(image.read_bytes())
   listing=run([cli,'list','--source',composite,'--last',12128])
   for p in ('retained.bin','目录/keep.txt'):
    line=next(l for l in old_listing.splitlines() if l.startswith('FILE '+json.dumps(p,ensure_ascii=False)))
    check(kind+' '+policy+' actual listing reuses '+p,line in listing.splitlines())
   exported=work/(policy+'-export');exported.mkdir(mode=0o700);selection=exported/'selection.txt'
   rows=['BURNLIGHT_EXPORT_SELECTION 1','ALL']
   for line in listing.splitlines():
    if line.startswith(('FILE ','DIR  ')):
     r=shlex.split(line);rows.append(json.dumps(r[1],ensure_ascii=False)+(' D 0' if r[0]=='DIR' else ' F '+r[2][6:]))
   selection.write_text('\n'.join(rows)+'\n')
   text=run([adapter,'offline-export','--image',composite,'--last',12128,'--selection',selection,'--work-dir',exported])
   path=pathlib.Path(next(l[20:] for l in text.splitlines() if l.startswith('Evidence directory: ')))
   check(kind+' '+policy+' all payload bytes match choice',all((path/'files'/p).read_bytes()==d for p,d in expected.items()))
  only=work/'only-conflicts';only.mkdir();write(only,'same.txt',b'replacement')
  check(kind+' all skipped rejects empty append','Nothing new to append' in build(only,work/'empty.iso','skip',False))
  case=work/'case';case.mkdir();write(case,'SAME.TXT',b'x')
  for policy in ('skip','replace'):
   check(kind+' '+policy+' case collision remains rejected','collision' in build(case,work/('case-'+policy+'.iso'),policy,False))
   typed=work/('typed-'+policy);typed.mkdir();write(typed,'目录',b'file instead of directory')
   failure=build(typed,work/('typed-'+policy+'.iso'),policy,False)
   check(kind+' '+policy+' type collision remains rejected','collision' in failure or 'conflict' in failure)
 check('invalid policy rejected','must be skip or replace' in run([cli,'build','missing','missing.iso','--on-conflict','unknown'],False))
 for command in ('init','init-build','readback','recover','list','verify','help'):
  check('policy cannot leak into '+command,'for build/burn only' in run([cli,command,'--on-conflict','replace'],False))
 (evidence/'conflict-offline.json').write_text(json.dumps(dict(result='PASS',checks=checks,count=len(checks),scratch=str(scratch),physicalWrites=0,cliSHA256=hashlib.sha256(cli.read_bytes()).hexdigest()),indent=2,ensure_ascii=False)+'\n')
 print('PASS:',len(checks),'conflict assertions; zero physical writes')
finally:(evidence/'conflict-offline.log').write_text('\n\n'.join(logs))
