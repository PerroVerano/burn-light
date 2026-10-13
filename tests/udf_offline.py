#!/usr/bin/env python3
"""UDF continuation tests; no optical or Windows acceptance is implied."""
import hashlib
import pathlib
import shutil
import subprocess
import sys
import struct
import binascii
import os

cli=pathlib.Path(sys.argv[1]).resolve()
out=pathlib.Path(os.environ.get('APPENDCD_TEST_ARTIFACT_ROOT', str(cli.parent.parent/'artifacts')))/'udf-tests'
if out.exists(): shutil.rmtree(out)
out.mkdir(parents=True)
logs=[]
checks=[]
def run(*args,ok=True):
    r=subprocess.run([str(x) for x in args],text=True,capture_output=True)
    text=r.stdout+r.stderr;logs.append('$ '+' '.join(map(str,args))+'\n'+text)
    assert (r.returncode==0)==ok,logs[-1]
    return text
def check(name,condition):
    assert condition,name
    checks.append(name)
def file(folder,name,content):
    p=folder/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(content)
def build(source,last,next_lba,files,image):
    return run(cli,'build',files,image,'--source',source,'--last',last,'--next',next_lba)
def retag(data,offset):
    length=struct.unpack_from('<H',data,offset+10)[0]
    struct.pack_into('<H',data,offset+8,binascii.crc_hqx(data[offset+16:offset+16+length],0))
    data[offset+4]=sum(data[offset+i] for i in range(16) if i!=4)&255
def corrupt(image,name,mutate):
    data=bytearray(image.read_bytes());mutate(data)
    bad=out/(name+'.iso');bad.write_bytes(data)
    manifest=pathlib.Path(str(image)+'.manifest').read_text()
    manifest=manifest.replace(hashlib.sha256(image.read_bytes()).hexdigest(),hashlib.sha256(data).hexdigest())
    pathlib.Path(str(bad)+'.manifest').write_text(manifest)
    return run(cli,'verify',bad,ok=False)
try:
    old=out/'old';new=out/'new';third=out/'third'
    for p in [old,new,third]:p.mkdir()
    # cdrtools UDF generator corrupts this host's three-Chinese-character directory name;
    # use a verified source name. The new writer still tests a three-character directory.
    file(old,'目录/旧文件.txt',b'old-udf-data\n')
    file(old,'old-large.bin',bytes(range(256))*4096)
    file(old,'empty-old.txt',b'')
    file(old,'.DS_Store',b'legacy Finder cache on disc\n')
    file(new,'目录/中文New.txt','新 UDF 内容\n'.encode())
    file(new,'hello-from-macos.txt',b'new macOS data\n')
    file(new,'新目录/Empty.txt',b'')
    file(new,'.DS_Store',b'new Finder cache must not replace legacy cache\n')
    file(new,'新目录/.DS_Store',b'nested Finder cache must not enter the image\n')
    source=out/'source.iso'
    run('/opt/homebrew/bin/mkisofs','-quiet','-udf','-J','-input-charset','utf-8','-o',source,old)
    listing=run(cli,'list','--source',source,'--last',0)
    check('UDF names and absolute payload LBAs imported','目录/旧文件.txt' in listing and 'LBA=' in listing)
    legacy_cache=next(line for line in listing.splitlines() if line.startswith('FILE ".DS_Store"'))
    image=out/'next.iso';result=build(source,0,12128,new,image)
    check('UDF generator reads no old source blocks','Old-source reads during generation: 0' in result)
    check('Old 1 MiB payload is not copied',image.stat().st_size<1048576*1.5)
    check('UDF image aligns end Anchors to DVD-R 16-block Track padding',image.stat().st_size//2048%16==0)
    check('CRC/anchors/VDS/FSD/FID/FE/extents and new hashes pass','Verified UDF:' in run(cli,'verify',image))
    combined=out/'composite.iso'
    with combined.open('wb') as f:f.write(source.read_bytes());f.seek(12128*2048);f.write(image.read_bytes())
    listing=run(cli,'list','--source',combined,'--last',12128)
    check('Last UDF session contains exact old and new names','目录/旧文件.txt' in listing and '目录/中文New.txt' in listing)
    check('Finder caches excluded recursively; legacy cache name/size/extents unchanged',
          legacy_cache in listing.splitlines() and '新目录/.DS_Store' not in listing and listing.count('FILE ".DS_Store"')==1)
    file(third,'third.txt',b'next UDF generation\n')
    third_image=out/'third.iso';build(combined,12128,24576,third,third_image)
    check('Second UDF continuation preserves both old generations','Verified UDF:' in run(cli,'verify',third_image))
    check('UDF output overwrite refused','output exists' in run(cli,'build',new,image,'--source',source,'--last',0,'--next',12128,ok=False))
    conflict=out/'conflict';conflict.mkdir();file(conflict,'OLD-LARGE.BIN',b'x')
    check('UDF Windows case collisions rejected','collision' in run(cli,'build',conflict,out/'collision.iso','--source',source,'--last',0,'--next',12128,ok=False))
    check('Primary Anchor CRC damage rejected','CRC' in corrupt(image,'anchor-crc',lambda b:b.__setitem__(256*2048+30,b[256*2048+30]^1)))
    check('Reserve VDS damage rejected','CRC' in corrupt(image,'reserve-crc',lambda b:b.__setitem__(96*2048+30,b[96*2048+30]^1)))
    def bad_parent(b):
        root=290*2048;start=struct.unpack_from('<I',b,root+180)[0]+struct.unpack_from('<I',b,35*2048+188)[0]-12128
        off=start*2048;struct.pack_into('<I',b,off+24,struct.unpack_from('<I',b,off+24)[0]+1);retag(b,off)
    check('Parent FID mismatch rejected after CRC rebinding','parent FID' in corrupt(image,'bad-parent',bad_parent))
    def bad_pd(b):
        off=35*2048;struct.pack_into('<I',b,off+192,struct.unpack_from('<I',b,off+192)[0]+1);retag(b,off)
    check('Partition geometry corruption rejected','partition span' in corrupt(image,'bad-partition',bad_pd))
    def bad_payload(b):
        # Find generated FE for the unique nonempty new data size.
        for off in range(290*2048,len(b),2048):
            if struct.unpack_from('<H',b,off)[0]==261 and struct.unpack_from('<Q',b,off+56)[0]==len(b'new macOS data\n'):
                part=struct.unpack_from('<I',b,35*2048+188)[0];lba=struct.unpack_from('<I',b,off+180)[0]+part-12128;b[lba*2048]^=1;return
        raise AssertionError('new FE not found')
    check('New payload hash corruption rejected','hash mismatch' in corrupt(image,'bad-payload',bad_payload))
    print('PASS: '+str(len(checks))+' UDF offline assertions')
    (out/'result.txt').write_text('UDF_SYNTHETIC_PASS\nWINDOWS_NOT_TESTED\n'+'\n'.join(checks)+'\n')
finally:(out/'commands.log').write_text('\n\n'.join(logs))
