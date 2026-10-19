#!/usr/bin/env python3
"""VAT regression from an independent encoder, including address/corruption boundaries."""
import hashlib
import json
import pathlib
import shlex
import struct
import subprocess
import sys
import tempfile
from vat_fixture import fixture, split_directory, windows_fid_compatibility, windows_carried_fid, windows_updated_fid, windows_reused_fid, windows_directory_fid, fid, tag, B
from vat_oracle import read as oracle

cli=pathlib.Path(sys.argv[1]).resolve()
out=pathlib.Path(tempfile.mkdtemp(prefix='burnlight-vat-'))
checks=[];logs=[]
def run(*args,ok=True,contains=None):
    p=subprocess.run([str(cli),*map(str,args)],capture_output=True,text=True)
    output=p.stdout+p.stderr;logs.append(shlex.join([str(cli),*map(str,args)])+'\n'+output)
    assert (p.returncode==0)==ok,logs[-1]
    if contains is not None:assert contains in output,output
    return output
def check(name,value=True):assert value,name;checks.append(name)
def patch(source,name,lba,mutate,retag=True):
    target=out/(name+'.iso');target.write_bytes(source.read_bytes())
    pathlib.Path(str(target)+'.tracks').write_text(pathlib.Path(str(source)+'.tracks').read_text())
    with target.open('r+b') as f:
        f.seek(lba*B);b=bytearray(f.read(B));mutate(b)
        if retag:tag(b,struct.unpack_from('<H',b)[0],struct.unpack_from('<I',b,12)[0],struct.unpack_from('<H',b,10)[0])
        f.seek(lba*B);f.write(b)
    return target
def rejected(name,source,contains=None,last=14000):
    run('list','--source',source,'--last',last,ok=False,contains=contains);check(name)
def pack(o,offset,value):struct.pack_into('<I',o,offset,value)
def composite(old,new,start,target):
    with target.open('wb') as f:f.write(old.read_bytes());f.seek(start*B);f.write(new.read_bytes())
    tracks=pathlib.Path(str(old)+'.tracks').read_text();count=len(tracks.splitlines())+1
    pathlib.Path(str(target)+'.tracks').write_text(tracks+f'{start} {start+new.stat().st_size//B} {count} {count}\n')
def actual_tree(source,last):
    output=run('list','--source',source,'--last',last,'--hash-files')
    return {line.split(' bytes=')[0]:line for line in output.splitlines() if line.startswith('FILE ')}
try:
    eight=out/'eight.iso';layout=fixture(eight);listing=run('list','--source',eight,'--last',14000,'--hash-files')
    check('eight-session Anchor uses sixth-session absolute VDS', 'latest.txt' in listing and 'Filesystem: udf2.01+vat' in listing)
    check('latest+32 contains no VDS',eight.read_bytes()[(14000+32)*B:(14000+33)*B]==bytes(B))
    check('old inline content read from original ICB',hashlib.sha256(b'old embedded bytes\n').hexdigest() in listing)
    check('old physical payload hash',hashlib.sha256(b'old physical bytes\n').hexdigest() in listing)
    check('Chinese nested directory and empty file','中文目录/空文件.txt' in listing)
    check('multiple VAT transactions in one physical Session',layout['partition']+10 in layout['blocks'])
    oldlisting=run('list','--source',eight,'--last',10000,ok=False)
    check('wrong entry rejected against recorded latest Session','latest completed Session' in oldlisting)
    history=out/'history.iso';history.write_bytes(eight.read_bytes()[:10600*B]);pathlib.Path(str(history)+'.tracks').write_text(''.join('%d %d %d %d\n'%r for r in layout['tracks'][:6]))
    historical=run('list','--source',history,'--last',10000)
    check('independent historical view has explicit difference','latest.txt' not in historical and 'inline-old.txt' in historical)
    single=out/'single.iso';fixture(single,True);run('list','--source',single,'--last',0,'--hash-files');check('independent single-session UDF 2.01')
    split=out/'split-directory.iso';split_layout=split_directory(split)
    split_listing=run('list','--source',split,'--last',14000,'--hash-files')
    check('cross-Session directory allocations and packed final FIDs','中文目录/空文件.txt' in split_listing and 'latest.txt' in split_listing)
    split_view=oracle(split,14000)
    check('independent split-directory contents equal original fixture',split_view['files']==oracle(eight,14000)['files'])
    relocated=patch(split,'relocated-FID-stale-location',split_layout['directoryTail'],lambda b:pack(b,12,split_layout['directoryEarlier']+2-split_layout['partition']))
    rejected('CRC-valid relocated FID with stale Tag Location is rejected',relocated,'expected id=257 version=3 location=')
    zero_fid=patch(split,'new-FID-zero-location',split_layout['directoryPacked'],lambda b:pack(b,12,0))
    rejected('CRC-valid new FID with zero Tag Location is rejected',zero_fid,'recorded id=257 version=3 location=0')
    for bad in (relocated,zero_fid):
        try:oracle(bad,14000)
        except AssertionError:pass
        else:raise AssertionError('Independent oracle accepted invalid FID location')
    check('independent oracle also rejects both Windows-return FID defects')
    windows=out/'windows-compat.iso';windows_layout=windows_fid_compatibility(windows)
    windows_listing=run('list','--source',windows,'--last',14000,'--hash-files',contains='UDF compatibility: windows-fid-location-v1')
    check('Windows compatibility requires exact previous-VAT directory provenance','latest.txt' in windows_listing)
    windows_view=oracle(windows,14000,windows_fid_compatibility=True)
    check('independent compatibility oracle reports both deviations explicitly',len(windows_view['compatibility'])==2 and not windows_view['strictConformance'])
    try:oracle(windows,14000)
    except AssertionError:pass
    else:raise AssertionError('Strict oracle accepted Windows deviations')
    check('strict conformance oracle remains distinct from authorized compatibility')
    no_proof=patch(windows,'Windows-proof-altered',windows_layout['historyCopiedFID'],lambda b:b.__setitem__(100,b[100]^1))
    rejected('Windows copied FID must exactly match historical record',no_proof,'exact historical')
    def correct_old_location(b):pack(b,12,windows_layout['directoryTail']-windows_layout['partition'])
    no_pair=patch(windows,'Windows-zero-no-relocation',windows_layout['directoryTail'],correct_old_location)
    rejected('Windows zero FID without proven relocation profile',no_pair,'proven directory relocation')
    wrong_writer=patch(windows,'Windows-wrong-VAT-writer',windows_layout['vat'],lambda b:b.__setitem__(169,ord('X')))
    rejected('Windows profile cannot be selected by arbitrary VAT writer',wrong_writer,'location mismatch')
    corrupt_windows=patch(windows,'Windows-compat-bad-CRC',windows_layout['directoryTail'],lambda b:b.__setitem__(100,b[100]^1),False)
    rejected('Windows compatibility still rejects CRC corruption',corrupt_windows,'checksum/CRC')
    corrupt_child=patch(windows,'Windows-new-child-wrong-writer',windows_layout['newFileICB'],lambda b:b.__setitem__(169,ord('X')))
    rejected('Windows zero FID requires current Microsoft EFE',corrupt_child,'child identity/type')
    wrong_uid=patch(windows,'Windows-copied-FID-wrong-UID',windows_layout['directoryEarlier']+1,lambda b:pack(b,32,999))
    rejected('Windows compatibility cannot bypass FID UniqueID binding',wrong_uid,'UniqueID')
    carried=out/'Windows-carried-zero.iso';carried_layout=windows_carried_fid(carried)
    carry_last=carried_layout['session']
    run('list','--source',carried,'--last',carry_last,'--hash-files',contains='latest.txt')
    check('Windows following Session retains original zero-FID file ICB')
    carried_view=oracle(carried,carry_last,windows_fid_compatibility=True)
    check('independent origin proof preserves every carried payload and excludes deleted tombstone',carried_view['files']==windows_view['files'])
    same_transaction=out/'Windows-same-session-next-VAT.iso';same_layout=windows_carried_fid(same_transaction,True)
    run('list','--source',same_transaction,'--last',same_layout['session'],'--hash-files')
    check('linked Windows VAT transactions inside same physical Session preserve zero FID')
    check('independent same-Session transaction namespace',oracle(same_transaction,same_layout['session'],windows_fid_compatibility=True)['files']==windows_view['files'])
    for candidate,last in [(carried,carry_last),(same_transaction,same_layout['session'])]:
        try:oracle(candidate,last)
        except AssertionError:pass
        else:raise AssertionError('Strict oracle accepted carried Windows deviations')
    check('carried Windows compatibility still not strict conformance')
    updated=out/'Windows-updated-zero.iso';updated_layout=windows_updated_fid(updated);updated_last=updated_layout['session']
    updated_listing=run('list','--source',updated,'--last',updated_last,'--hash-files')
    updated_view=oracle(updated,updated_last,windows_fid_compatibility=True)
    latest_payload=b'Windows updated version two\n'
    check('Windows repeated updates read latest payload rather than origin',updated_view['files']['latest.txt']['bytes']==latest_payload and hashlib.sha256(latest_payload).hexdigest() in updated_listing)
    check('Windows repeated updates retain original file identity',updated_view['files']['latest.txt']['uniqueId']==windows_view['files']['latest.txt']['uniqueId'])
    updated_same=out/'Windows-updated-same-session.iso';updated_same_layout=windows_updated_fid(updated_same,True)
    run('list','--source',updated_same,'--last',updated_same_layout['session'],'--hash-files')
    same_files=oracle(updated_same,updated_same_layout['session'],windows_fid_compatibility=True)['files']
    check('independent same-Session repeated updates',{p:(e['bytes'],e['uniqueId']) for p,e in same_files.items()}=={p:(e['bytes'],e['uniqueId']) for p,e in updated_view['files'].items()})
    try:oracle(updated,updated_last)
    except AssertionError:pass
    else:raise AssertionError('Strict oracle accepted updated zero-location FID')
    check('updated zero FID remains explicitly non-strict compatibility')
    update_cases=[
        ('origin-identity',updated_layout['newFileICB'],lambda b:struct.pack_into('<Q',b,200,99)),
        ('intermediate-identity',updated_layout['update1FileICB'],lambda b:struct.pack_into('<Q',b,200,99)),
        ('origin-writer',updated_layout['newFileICB'],lambda b:b.__setitem__(169,ord('X'))),
        ('intermediate-writer',updated_layout['update1FileICB'],lambda b:b.__setitem__(169,ord('X'))),
        ('intermediate-VAT-writer',updated_layout['update1VAT'],lambda b:b.__setitem__(169,ord('X'))),
        ('deleted-and-reallocated-ICB',updated_layout['update1VAT'],lambda b:pack(b,216+152+4*4,0xffffffff)),
        ('backwards-remap',updated_layout['vat'],lambda b:pack(b,216+152+4*4,updated_layout['newFileICB']-updated_layout['partition'])),
    ]
    for name,lba,mutate in update_cases:
        bad=patch(updated,'updated-'+name,lba,mutate)
        rejected('updated zero FID rejects '+name,bad,'first-allocation VAT origin',updated_last)
        try:oracle(bad,updated_last,windows_fid_compatibility=True)
        except AssertionError:pass
        else:raise AssertionError('Independent oracle accepted '+name)
    check('independent oracle rejects every invalid update chain')
    corrupt_update=patch(updated,'updated-intermediate-bad-CRC',updated_layout['update1FileICB'],lambda b:b.__setitem__(80,b[80]^1),False)
    rejected('updated zero FID validates intermediate descriptor CRC',corrupt_update,'checksum/CRC',updated_last)
    missing_update=patch(updated,'updated-missing-intermediate-cache',updated_layout['update1FileICB'],lambda b:None,False)
    pathlib.Path(str(missing_update)+'.blocks').write_text(''.join(str(lba)+'\n' for begin,end,*_ in updated_layout['tracks'] for lba in range(begin,end) if lba!=updated_layout['update1FileICB']))
    rejected('updated zero FID requires actual intermediate metadata',missing_update,'hole in optical cache',updated_last)
    reused=out/'Windows-reused-virtual-slot.iso';reused_layout=windows_reused_fid(reused)
    run('list','--source',reused,'--last',reused_layout['session'],'--hash-files')
    reused_view=oracle(reused,reused_layout['session'],windows_fid_compatibility=True)
    check('reused virtual slot requires distinct earlier identity and new UID allocation interval',reused_view['files']==updated_view['files'])
    for name,lba,mutate in [
        ('same-UID',reused_layout['priorGenerationICB'],lambda b:struct.pack_into('<Q',b,200,20)),
        ('unknown-prior-writer',reused_layout['priorGenerationICB'],lambda b:b.__setitem__(169,ord('X'))),
        ('UID-before-allocation-interval',reused_layout['priorGenerationVAT'],lambda b:struct.pack_into('<Q',b,160,21)),
    ]:
        bad=patch(reused,'reused-'+name,lba,mutate)
        rejected('reused slot rejects '+name,bad,'first-allocation VAT origin',reused_layout['session'])
        try:oracle(bad,reused_layout['session'],windows_fid_compatibility=True)
        except AssertionError:pass
        else:raise AssertionError('Independent oracle accepted reused '+name)
    reserved=out/'Windows-zero-data-slot.iso';reserved_layout=windows_reused_fid(reserved,True)
    run('list','--source',reserved,'--last',reserved_layout['session'],'--hash-files')
    check('recorded zero-data slot can later become a file ICB with a freshly allocated UID',oracle(reserved,reserved_layout['session'],windows_fid_compatibility=True)['files']==updated_view['files'])
    dirty_reserved=patch(reserved,'zero-slot-nonzero-invalid-metadata',reserved_layout['priorGenerationICB'],lambda b:b.__setitem__(100,1),False)
    rejected('zero-slot exception cannot hide nonzero invalid metadata',dirty_reserved,'Unsupported UDF ICB',reserved_layout['session'])
    missing_reserved=patch(reserved,'zero-slot-cache-hole',reserved_layout['priorGenerationICB'],lambda b:None,False)
    pathlib.Path(str(missing_reserved)+'.blocks').write_text(''.join(str(lba)+'\n' for begin,end,*_ in reserved_layout['tracks'] for lba in range(begin,end) if lba!=reserved_layout['priorGenerationICB']))
    rejected('zero-slot proof cannot substitute a sparse cache hole',missing_reserved,'hole in optical cache',reserved_layout['session'])
    hidden_dir=out/'Windows-hidden-directory-zero-FID.iso';hidden_layout=windows_directory_fid(hidden_dir)
    run('list','--source',hidden_dir,'--last',hidden_layout['session'],'--hash-files')
    check('hidden directory zero FID retains directory identity and nested files across updates',oracle(hidden_dir,hidden_layout['session'],windows_fid_compatibility=True)['files']==updated_view['files'])
    bad=patch(hidden_dir,'hidden-directory-disguised-as-file',hidden_layout['currentZeroFidBlock'],lambda b:b.__setitem__(18,1))
    rejected('zero FID directory flag must match the child ICB type',bad,'child identity/type',hidden_layout['session'])
    origin=carried_layout['originZeroFidBlock'];at=carried_layout['zeroFidOffset']
    def change_origin_fid(b):
        size=(38+struct.unpack_from('<H',b,at+36)[0]+b[at+19]+3)&~3
        record=bytearray(b[at:at+size]);record[39]^=1;tag(record,257,0,size-16);b[at:at+size]=record
    bad=patch(carried,'carried-origin-fid-altered',origin,change_origin_fid,False)
    rejected('carried zero FID must exactly equal origin bytes',bad,'first-allocation VAT origin',carry_last)
    bad=patch(carried,'carried-origin-writer-altered',carried_layout['previousWindowsVAT'],lambda b:b.__setitem__(169,ord('X')))
    rejected('carried origin must be a Microsoft VAT',bad,'first-allocation VAT origin',carry_last)
    bad=patch(carried,'carried-origin-directory-altered',carried_layout['originDirectory'],lambda b:b.__setitem__(169,ord('X')))
    rejected('carried origin directory must be Microsoft metadata',bad,'first-allocation VAT origin',carry_last)
    bad=patch(carried,'carried-child-UID-altered',carried_layout['newFileICB'],lambda b:struct.pack_into('<Q',b,200,99))
    rejected('carried child UniqueID cannot change',bad,'identity/type',carry_last)
    def premature_mapping(b):
        size=struct.unpack_from('<Q',b,56)[0]+4;struct.pack_into('<Q',b,56,size);pack(b,172,size)
        pack(b,176+152+4*4,carried_layout['newFileICB']-carried_layout['partition'])
        tag(b,261,struct.unpack_from('<I',b,12)[0],160+size)
    bad=patch(carried,'carried-origin-previous-already-mapped',12599,premature_mapping,False)
    rejected('carried origin cannot claim a virtual ICB allocated in an older VAT',bad,'first-allocation VAT origin',carry_last)
    bad=patch(carried,'carried-missing-origin-cache',origin,lambda b:None,False)
    pathlib.Path(str(bad)+'.blocks').write_text(''.join(str(lba)+'\n' for begin,end,*_ in carried_layout['tracks'] for lba in range(begin,end) if lba!=origin))
    rejected('carried provenance cannot use a missing historical cache block',bad,'hole in optical cache',carry_last)
    # Support deallocated deleted FIDs according to UDF 2.01 2.3.4.2, including erased-name IDs.
    def tombstones(b):
        old=struct.unpack_from('<Q',b,56)[0];addition=bytearray()
        for comp in (254,255):
            d=fid('gone.txt',0,location=2)
            if comp==254:
                name=b'\xfe' + b'gone.txt';d=bytearray((38+len(name)+3)&~3);struct.pack_into('<H',d,16,1);d[19]=len(name);d[38:38+len(name)]=name
            else:d[38]=255
            d[18]=5;d[20:36]=bytes(16);tag(d,257,2,len(d)-16);addition+=d
        b[216+old:216+old+len(addition)]=addition;struct.pack_into('<Q',b,56,old+len(addition));struct.pack_into('<Q',b,64,old+len(addition));pack(b,212,old+len(addition));tag(b,266,2,200+old+len(addition))
    deleted_strict=patch(eight,'strict-deleted-FIDs',14300,tombstones,False)
    run('list','--source',deleted_strict,'--last',14000,'--hash-files')
    check('strict deleted zero ICB and compression IDs 254/255 not live names','gone.txt' not in actual_tree(deleted_strict,14000))
    check('independent strict deleted FID namespace',oracle(deleted_strict,14000)['files']==oracle(eight,14000)['files'])
    dead_at=carried_layout['deletedFidOffset'];dead_lba=carried_layout['currentZeroFidBlock']
    def dead_mutation(b,offset,value,crc=True):
        size=(38+struct.unpack_from('<H',b,dead_at+36)[0]+b[dead_at+19]+3)&~3
        d=bytearray(b[dead_at:dead_at+size]);d[offset]=value
        if crc:tag(d,257,0,size-16)
        b[dead_at:dead_at+size]=d
    for name,offset,value,message in [('ICB',20,1,'deleted UDF FID'),('name',38,16,'compression ID'),('flags',18,13,'deleted UDF FID')]:
        bad=patch(carried,'deleted-invalid-'+name,dead_lba,lambda b,offset=offset,value=value:dead_mutation(b,offset,value),False)
        rejected('deleted FID invalid '+name+' rejected',bad,message,carry_last)
    bad=patch(carried,'deleted-invalid-CRC',dead_lba,lambda b:dead_mutation(b,45,1,False),False)
    rejected('deleted FID still needs valid CRC',bad,'checksum/CRC',carry_last)
    for name,target in [('rebased-vds',layout['vds']+14000),('track-gap',11000),('future-blank-track',16000),('overflow',0xfffffff0)]:
        bad=patch(eight,name,14256,lambda b,target=target:(pack(b,20,target),pack(b,28,target)))
        rejected(name,bad)
    # Both sequences damaged must fail. A valid reserve serves the SAME latest Anchor.
    bad=patch(eight,'main-crc',10032,lambda b:b.__setitem__(30,b[30]^1),False)
    run('list','--source',bad,'--last',14000,contains='latest.txt');check('reserve recovery retains current VAT')
    both=patch(bad,'both-vds-invalid',10064,lambda b:b.__setitem__(30,b[30]^1),False);rejected('both VDS fail',both,'Main/reserve')
    for name,lba,offset in [('anchor-crc',14256,30),('anchor-checksum',14256,4),('vat-crc',14797,180),('vat-checksum',14797,4)]:
        bad=patch(eight,name,lba,lambda b,offset=offset:b.__setitem__(offset,b[offset]^1),False);rejected(name,bad)
    bad=patch(eight,'tag-location',14797,lambda b:pack(b,12,14797));rejected('VAT location uses physical partition offset',bad,'location')
    bad=patch(eight,'truncated-vat',14797,lambda b:pack(b,212,8));rejected('truncated VAT',bad)
    bad=patch(eight,'invalid-vat-map',14797,lambda b:pack(b,216+152,999999));rejected('invalid VAT mapping',bad)
    bad=patch(eight,'cycle-vat',14797,lambda b:pack(b,216+132,14797-layout['partition']));rejected('VAT history cycle',bad)
    def cycle_directory(b):
        pos=216+40;pack(b,pos+24,2);pack(b,pos+32,0);length=struct.unpack_from('<H',b,pos+10)[0]+16
        record=tag(bytearray(b[pos:pos+length]),257,2,length-16);b[pos:pos+length]=record
    bad=patch(eight,'directory-cycle',14300,cycle_directory);rejected('directory/parent cycle',bad,'cycle')
    for name,target in [('payload-gap',11000),('payload-future',16000)]:
        bad=patch(eight,name,14797,lambda b,target=target:pack(b,216+152+4*4,target-layout['partition']))
        rejected('VAT-mapped '+name,bad)
    bad=patch(eight,'nonzero-runout',14799,lambda b:b.__setitem__(100,1),False);rejected('unknown nonzero tail cannot hide latest VAT',bad)
    for name,offset,value in [('map-number',484,1),('map-identifier',451,ord('X'))]:
        bad=patch(eight,name,10035,lambda b,offset=offset,value=value:b.__setitem__(offset,value))
        bad=patch(bad,name+'-reserve',10067,lambda b,offset=offset,value=value:b.__setitem__(offset,value))
        rejected('unsupported '+name,bad)
    bad=patch(eight,'external-EA-ICB',layout['partition']+33,lambda b:pack(b,112,B));rejected('unsupported external EA ICB',bad,'extended attribute')
    # Independent allocation extent encoding: EFE long_ad -> physical AED -> old payload.
    from vat_fixture import p16,p32,ad
    chain=patch(eight,'allocation-chain',layout['partition']+34,lambda b:(pack(b,216,0xc0000800),pack(b,220,43)))
    aed=bytearray(B);p32(aed,20,16);ad(aed,24,42,0,19);tag(aed,258,43,24)
    with chain.open('r+b') as f:f.seek((layout['partition']+43)*B);f.write(aed)
    run('list','--source',chain,'--last',14000,'--hash-files',contains=hashlib.sha256(b'old physical bytes\n').hexdigest());check('independent physical allocation continuation')
    loop=patch(chain,'allocation-loop',layout['partition']+43,lambda b:(pack(b,24,0xc0000800),pack(b,28,43)));rejected('allocation continuation cycle',loop,'cycle')
    # Zeroing the newest VAT cannot make a historical directory pass.
    bad=patch(eight,'missing-latest-vat',14797,lambda b:b.__setitem__(slice(None),bytes(B)),False);rejected('latest VAT removed rejects history',bad)
    bad=patch(eight,'missing-cache',14797,lambda b:None)
    valid=[lba for begin,end,*_ in layout['tracks'] for lba in range(begin,end) if lba!=10032]
    pathlib.Path(str(bad)+'.blocks').write_text('\n'.join(map(str,valid))+'\n')
    # Reserve is also absent, so sparse holes cannot be accepted as zeros.
    pathlib.Path(str(bad)+'.blocks').write_text('\n'.join(str(n) for n in valid if n!=10064)+'\n');rejected('missing sparse cache blocks',bad,'hole')
    malformed=patch(eight,'partial-track-map',14797,lambda b:None)
    with pathlib.Path(str(malformed)+'.tracks').open('a') as f:f.write('16000 16800 9')
    rejected('truncated recorded-range map',malformed,'Malformed')
    malformed=patch(eight,'overflow-cache-map',14797,lambda b:None)
    pathlib.Path(str(malformed)+'.blocks').write_text('4294967296\n')
    rejected('overflow sparse validity entry',malformed,'Malformed')
    original=actual_tree(eight,14000)
    new=out/'new';new.mkdir();(new/'mac.txt').write_bytes(b'Mac first VAT append\n');(new/'中文目录').mkdir();(new/'中文目录/新.txt').write_text('新内容\n')
    (new/'.DS_Store').write_bytes(b'excluded')
    image=out/'append1.iso';run('build',new,image,'--source',eight,'--last',14000,'--next',16000,contains='Old-source reads during generation: 0')
    win_append=out/'windows-source-append.iso';run('build',new,win_append,'--source',windows,'--last',14000,'--next',16000)
    run('verify',win_append);win_composite=out/'windows-source-composite.iso';composite(windows,win_append,16000,win_composite)
    win_after=oracle(win_composite,16000,True)
    check('append from compatible Windows view generates strictly valid new directory',win_after['strictConformance'] and not win_after['compatibility'])
    check('Windows compatible source retains every old payload and inline ICB',all(win_after['files'][k]==v for k,v in windows_view['files'].items()))
    carry_append=out/'carried-source-append.iso';run('build',new,carry_append,'--source',carried,'--last',carry_last,'--next',18000);run('verify',carry_append)
    carry_composite=out/'carried-source-composite.iso';composite(carried,carry_append,18000,carry_composite);carry_after=oracle(carry_composite,18000,True)
    check('append from carried zero FID and tombstone view creates strict new directory',carry_after['strictConformance'] and all(carry_after['files'][k]==v for k,v in carried_view['files'].items()))
    update_append=out/'updated-source-append.iso';run('build',new,update_append,'--source',updated,'--last',updated_last,'--next',20000);run('verify',update_append)
    update_composite=out/'updated-source-composite.iso';composite(updated,update_append,20000,update_composite)
    update_after=oracle(update_composite,20000,True)
    check('offline continuation from updated Windows view retains latest payload and extents',update_after['strictConformance'] and all(update_after['files'][k]==v for k,v in updated_view['files'].items()))
    run('verify',image);check('VAT append independently reparsed/verified')
    c1=out/'composite1.iso';composite(eight,image,16000,c1);current=actual_tree(c1,16000)
    independent=oracle(c1,16000,True);original_oracle=oracle(eight,14000)
    check('independent Python parser validates tags/end layout/VAT history',independent['previous']==layout['vat'])
    check('independent payload and original ICB byte addresses',all(independent['files'][p]==f for p,f in original_oracle['files'].items()) and independent['files']['mac.txt']['bytes']==b'Mac first VAT append\n')
    check('all unchanged extents and inline addresses retained',all(current[k]==v for k,v in original.items()))
    check('old payload not written to continuation',b'old embedded bytes\n' not in image.read_bytes() and b'old physical bytes\n' not in image.read_bytes())
    check('DS_Store excluded','FILE ".DS_Store"' not in current)
    check('latest differs from earlier VAT',any('mac.txt' in k for k in current) and not any('mac.txt' in k for k in original))
    newer=out/'newer';newer.mkdir();(newer/'mac2.txt').write_text('second append\n')
    second=out/'append2.iso';run('build',newer,second,'--source',c1,'--last',16000,'--next',18000);run('verify',second)
    c2=out/'composite2.iso';composite(c1,second,18000,c2);check('two consecutive appends retain all generations','mac2.txt' in run('list','--source',c2,'--last',18000))
    independent2=oracle(c2,18000,True);check('independent second append preserves previous transaction',independent2['previous']==independent['vat'] and independent2['files']['mac2.txt']['bytes']==b'second append\n')
    many=out/'many';many.mkdir()
    for i in range(470):(many/f'empty-{i:03}.txt').write_bytes(b'')
    large=out/'large.iso';run('build',many,large,'--source',eight,'--last',14000,'--next',16000);run('verify',large)
    combined=out/'large-disc.iso';composite(eight,large,16000,combined)
    check('external VAT generated and independently read',len(oracle(combined,16000,True)['files'])==474)
    run('list','--source',combined,'--last',16000,'--hash-files');check('external VAT core reader and empty files')
    wrapped=patch(eight,'next-UID-wrap',14797,lambda b:struct.pack_into('<Q',b,200,0xfffffffe))
    wrap_image=out/'wrap.iso';run('build',new,wrap_image,'--source',wrapped,'--last',14000,'--next',16000);run('verify',wrap_image)
    wrap_disc=out/'wrap-disc.iso';composite(wrapped,wrap_image,16000,wrap_disc);wrapped_view=oracle(wrap_disc,16000,True)
    check('UniqueID lower-32 rollover skips reserved range',wrapped_view['files']['mac.txt']['uniqueId']>=0x100000010)
    single_image=out/'single-append.iso';run('build',new,single_image,'--source',single,'--last',0,'--next',2000);run('verify',single_image)
    combined=out/'single-disc.iso';composite(single,single_image,2000,combined);check('single-session append independent composite',oracle(combined,2000,True)['files']['mac.txt']['bytes']==b'Mac first VAT append\n')
    conflict=out/'conflict';conflict.mkdir();(conflict/'inline-old.txt').write_text('replacement\n');(conflict/'added.txt').write_text('added\n')
    run('build',conflict,out/'reject.iso','--source',eight,'--last',14000,'--next',16000,ok=False,contains='conflict');check('default conflicts rejected')
    for policy in ('skip','replace'):
        target=out/(policy+'.iso');run('build',conflict,target,'--source',eight,'--last',14000,'--next',16000,'--on-conflict',policy);run('verify',target)
        combined=out/(policy+'-disc.iso');composite(eight,target,16000,combined);view=actual_tree(combined,16000)
        check(policy+' inline original address/content semantics',(view['FILE "inline-old.txt"']==original['FILE "inline-old.txt"'])==(policy=='skip'))
        check(policy+' unrelated old extent preserved',view['FILE "physical-old.txt"']==original['FILE "physical-old.txt"'])
    run('build',new,out/'stale-view.iso','--source',eight,'--last',14000,'--next',16000,'--expected-view','0'*64,ok=False,contains='view changed');check('preview VAT identity rejects stale transaction')
    report={'result':'PASS','count':len(checks),'checks':checks,'cliSHA256':hashlib.sha256(cli.read_bytes()).hexdigest(),'scratch':str(out),'physicalWrites':0,'fixture':'tests/vat_fixture.py independent encoder'}
    (cli.parent/'vat-result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(f'PASS: {len(checks)} VAT offline assertions; {out}')
finally:(out/'commands.log').write_text('\n\n'.join(logs))
