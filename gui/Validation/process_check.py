#!/usr/bin/env python3
"""Read-only task-specific worker and Disk Arbitration observation; no signals."""
import json,os,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[2]
p=subprocess.run(['/bin/ps','-axo','stat=,comm='],capture_output=True,text=True,check=True)
workers={'appendcd','appendcd-cdrecord','appendcd-initial-cdrecord','readcd','burnlight-export','osascript'}
rows=[]
for line in p.stdout.splitlines():
 a=line.strip().split(None,1)
 if len(a)==2 and pathlib.Path(a[1]).name in workers|{'BurnLight','diskarbitrationd'}:rows.append(dict(state=a[0],process=a[1]))
active=pathlib.Path(f'/private/tmp/burnlight-physical-{os.getuid()}/active.json').exists()
assert not active and not any(pathlib.Path(x['process']).name in workers for x in rows)
assert any(pathlib.Path(x['process']).name=='diskarbitrationd' and 'T' not in x['state'] and 'Z' not in x['state'] for x in rows)
evidence=pathlib.Path(os.environ.get('BURNLIGHT_EVIDENCE_DIR',str(root/'gui/evidence/write-acceptance-20261010')))
(evidence/'process-state.json').write_text(json.dumps(dict(result='PASS',activeMarker=active,processes=rows),indent=2)+'\n')
print('PASS: no optical worker or active marker; Disk Arbitration running')
