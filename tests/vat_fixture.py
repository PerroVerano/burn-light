"""Independent ECMA-167 / OSTA 2.01 encoder; never imports the BurnLight writer.

Small anonymous files only. Address geometry deliberately separates the last entry,
earlier VDS, physical partition, virtual blocks and Partition Number 8192.
"""
import binascii
import pathlib
import struct

B = 2048
def p16(b, o, n): struct.pack_into('<H', b, o, n)
def p32(b, o, n): struct.pack_into('<I', b, o, n)
def p64(b, o, n): struct.pack_into('<Q', b, o, n)
def tag(b, kind, location, length):
    p16(b, 0, kind); p16(b, 2, 3); p16(b, 6, 1)
    p16(b, 8, binascii.crc_hqx(b[16:16+length], 0)); p16(b, 10, length); p32(b, 12, location)
    b[4] = sum(b[i] for i in range(16) if i != 4) & 255
    return b
def entity(b, o, name, revision=None):
    b[o+1:o+1+len(name)] = name.encode('ascii')
    if revision is not None: p16(b,o+24,revision)
def cs0(s): return b'\x10'+s.encode('utf-16-be')
def ds(b, o, length, s):
    value=cs0(s);b[o:o+len(value)]=value;b[o+length-1]=len(value)
def charset(b,o):
    b[o+1:o+24]=b'OSTA Compressed Unicode'
def timestamp(b,o):
    p16(b,o,0x1000);p16(b,o+2,2020);b[o+4:o+6]=bytes([1,1])
def ad(b,o,block,ref=1,bytes=B):
    p32(b,o,bytes);p32(b,o+4,block);p16(b,o+8,ref)
def fid(name,block,directory=False,parent=False,location=2):
    value=b'' if parent else cs0(name);length=(38+len(value)+3)//4*4
    b=bytearray(length);p16(b,16,1);b[18]=(2 if directory else 0)|(8 if parent else 0);b[19]=len(value)
    ad(b,20,block);p32(b,32,0 if block==2 else 16+block);b[38:38+len(value)]=value
    return tag(b,257,location,length-16)
def fe(location,kind,content=None,extent=None,efe=False,unique=None):
    b=bytearray(B);header=216 if efe else 176
    p16(b,20,4);p16(b,24,1);b[27]=kind;p16(b,48,1)
    p32(b,36,0xffffffff);p32(b,40,0xffffffff);p32(b,44,0x14a5)
    for offset in ((80,92,104,116) if efe else (72,84,96)):timestamp(b,offset)
    entity(b,168 if efe else 128,'*VAT Fixture')
    p64(b,160 if not efe else 200,unique if unique is not None else (20 if kind==248 else (0 if kind==4 and location==2 else 16+location)))
    if extent is None:
        if kind==4:
            pos=0;links=1
            while pos<len(content):
                links+=bool(content[pos+18]&2 and not content[pos+18]&8)
                pos+=(38+struct.unpack_from('<H',content,pos+36)[0]+content[pos+19]+3)&~3
            p16(b,48,links)
        p16(b,34,3);p64(b,56,len(content));p32(b,header-4,len(content));b[header:header+len(content)]=content
        if efe:p64(b,64,len(content))
        length=header-16+len(content)
    else:
        block,size,ref=extent;p16(b,34,1);p64(b,56,size);p64(b,64 if not efe else 72,(size+B-1)//B)
        if efe:p64(b,64,size)
        p32(b,header-4,16);ad(b,header,block,ref,size);length=header
    return tag(b,266 if efe else 261,location,length)
def vat(entries,previous,files,dirs):
    b=bytearray(152+4*len(entries));p16(b,0,152);ds(b,4,128,'VAT-FIXTURE')
    p32(b,132,previous);p32(b,136,files);p32(b,140,dirs)
    for offset in (144,146,148):p16(b,offset,0x0201)
    for i,value in enumerate(entries):p32(b,152+4*i,value)
    return b
def fixture(path,single=False):
    starts=[0] if single else [0,2000,4000,6000,8000,10000,12000,14000]
    session=starts[-1];vds=32 if single else starts[5]+32;partition=vds+256
    end=session+800;blocks={};tracks=[(s,s+(800 if i==len(starts)-1 else 600),i+1,i+1) for i,s in enumerate(starts)]
    for s in starts:
        for offset,name in [(16,'BEA01'),(17,'NSR03'),(18,'TEA01')]:
            b=bytearray(B);b[1:6]=name.encode();b[6]=1;blocks[s+offset]=b
    for s in starts:
        b=bytearray(B);p32(b,16,32768);p32(b,20,vds);p32(b,24,32768);p32(b,28,vds+32);blocks[s+256]=tag(b,2,s+256,496)
    pvd=bytearray(B);p32(pvd,16,1);ds(pvd,24,32,'VAT-FIXTURE');ds(pvd,72,128,'0000000100000001 VAT Fixture')
    for offset,value in [(56,1),(58,1),(60,3),(62,3)]:p16(pvd,offset,value)
    p32(pvd,64,1);p32(pvd,68,1);charset(pvd,200);charset(pvd,264);timestamp(pvd,376);entity(pvd,344,'*VAT Fixture');entity(pvd,388,'*VAT Fixture')
    iuvd=bytearray(B);p32(iuvd,16,2);entity(iuvd,20,'*UDF LV Info',0x0201);charset(iuvd,52);ds(iuvd,116,128,'VAT-FIXTURE');entity(iuvd,352,'*VAT Fixture')
    pd=bytearray(B);p32(pd,16,3);p16(pd,20,1);p16(pd,22,8192);entity(pd,24,'+NSR03');p32(pd,184,2);p32(pd,188,partition);p32(pd,192,100000)
    lvd=bytearray(B);p32(lvd,16,4);charset(lvd,20);ds(lvd,84,128,'VAT-FIXTURE');p32(lvd,212,B);entity(lvd,216,'*OSTA UDF Compliant',0x0201)
    entity(pd,196,'*VAT Fixture');entity(lvd,272,'*VAT Fixture')
    ad(lvd,248,0);p32(lvd,264,70);p32(lvd,268,2);p32(lvd,432,4096);p32(lvd,436,vds+64)
    lvd[440:442]=bytes([1,6]);p16(lvd,442,1);p16(lvd,444,8192)
    lvd[446:448]=bytes([2,64]);entity(lvd,450,'*UDF Virtual Partition',0x0201);p16(lvd,482,1);p16(lvd,484,8192)
    usd=bytearray(B);p32(usd,16,5)
    for base in (vds,vds+32):
        for i,(kind,b,length) in enumerate([(1,pvd,496),(4,iuvd,496),(5,pd,496),(6,lvd,494),(7,usd,8),(8,bytearray(B),496)]):blocks[base+i]=tag(bytearray(b),kind,base+i,length)
    integrity=bytearray(B);timestamp(integrity,16);p64(integrity,40,20);p32(integrity,72,2);p32(integrity,76,46);entity(integrity,96,'*VAT Fixture');p32(integrity,128,2);p32(integrity,132,1)
    for offset in (136,138,140):p16(integrity,offset,0x0201)
    for offset,value in [(80,100000-(end-partition)),(84,0xffffffff),(88,100000),(92,0xffffffff)]:p32(integrity,offset,value)
    blocks[vds+64]=tag(integrity,9,vds+64,126)
    fsd=bytearray(B);timestamp(fsd,16);p16(fsd,28,3);p16(fsd,30,3);p32(fsd,32,1);p32(fsd,36,1)
    charset(fsd,48);ds(fsd,112,128,'VAT-FIXTURE');charset(fsd,240);ds(fsd,304,32,'VAT-FIXTURE')
    ad(fsd,400,2);entity(fsd,416,'*OSTA UDF Compliant',0x0201);blocks[partition+32]=tag(fsd,256,0,496)
    blocks[partition+33]=fe(1,5,b'old embedded bytes\n')
    blocks[partition+34]=fe(3,5,extent=(42,19,0),efe=True);blocks[partition+42]=bytearray(b'old physical bytes\n'.ljust(B,b'\0'))
    oldroot=fid('',2,True,True)+fid('inline-old.txt',1)+fid('physical-old.txt',3)
    blocks[partition+35]=fe(2,4,oldroot)
    oldentries=[32,33,35,34];previous=0xffffffff
    if not single:
        # Two VAT transactions inside Session 6; a transaction is not a Session.
        blocks[partition+10]=fe(10,248,vat(oldentries,previous,2,1));previous=10
        for index in (5,6):
            loc=starts[index]+599;blocks[loc]=fe(loc-partition,248,vat(oldentries,previous,2,1));previous=loc-partition
    rootphysical=session+300;newphysical=session+301;dirphysical=session+302;emptyphysical=session+303
    entries=[32,33,rootphysical-partition,34,newphysical-partition,dirphysical-partition,emptyphysical-partition]
    root=fid('',2,True,True)+fid('inline-old.txt',1)+fid('physical-old.txt',3)+fid('中文目录',5,True)+fid('latest.txt',4)
    blocks[rootphysical]=fe(2,4,root,efe=True);blocks[newphysical]=fe(4,5,b'latest eight-session view\n')
    blocks[dirphysical]=fe(5,4,fid('',2,True,True,location=5)+fid('空文件.txt',6,location=5))
    blocks[emptyphysical]=fe(6,5,b'');vat_lba=end-3;blocks[vat_lba]=fe(vat_lba-partition,248,vat(entries,previous,4,2),efe=True,unique=23)
    with pathlib.Path(path).open('wb') as f:
        f.truncate(end*B)
        for lba,data in blocks.items():f.seek(lba*B);f.write(data)
    pathlib.Path(str(path)+'.tracks').write_text(''.join('%d %d %d %d\n'%r for r in tracks))
    return {'session':session,'end':end,'partition':partition,'vds':vds,'vat':vat_lba,'tracks':tracks,'blocks':blocks}

def split_directory(path):
    """Earlier directory extent plus relocated tail and packed new FIDs.

    Anonymous independent geometry reproduces the returned Windows directory's
    allocation shape. Correct locations are mandatory even when FIDs are copied.
    """
    layout=fixture(path);partition=layout['partition'];session=layout['session']
    earlier=partition+43;tail=session+310
    def padded(name,icb,lba,parent=False):
        value=b'' if parent else cs0(name);b=bytearray(B)
        p16(b,16,1);b[18]=10 if parent else 0;b[19]=len(value)
        ad(b,20,icb);p32(b,32,0 if parent else 16+icb)
        padding=(-(38+len(value)))%4;impl=B-38-len(value)-padding
        p16(b,36,impl);entity(b,38,'*VAT Fixture');b[38+impl:38+impl+len(value)]=value
        return tag(b,257,lba-partition,B-16)
    blocks={earlier:padded('',2,earlier,True),earlier+1:padded('inline-old.txt',1,earlier+1),
            tail:padded('physical-old.txt',3,tail)}
    packed=fid('中文目录',5,True,location=tail+1-partition)+fid('latest.txt',4,location=tail+1-partition)
    blocks[tail+1]=packed.ljust(B,b'\0')
    size=3*B+len(packed)
    root=fe(2,4,extent=(earlier-partition,2*B,0),efe=True)
    p64(root,56,size);p64(root,64,size);p64(root,72,4);p16(root,48,2)
    p32(root,212,32);ad(root,232,tail-partition,0,B+len(packed));tag(root,266,2,232)
    blocks[session+300]=root
    with pathlib.Path(path).open('r+b') as f:
        for lba,data in blocks.items():f.seek(lba*B);f.write(data)
    layout.update(directoryTail=tail,directoryPacked=tail+1,directoryEarlier=earlier)
    return layout

def windows_fid_compatibility(path):
    """Independent anonymous reproduction of the authorized Windows deviations."""
    layout=split_directory(path);p=layout['partition'];s=layout['session'];tail=layout['directoryTail']
    with pathlib.Path(path).open('r+b') as f:
        def read(lba):f.seek(lba*B);return bytearray(f.read(B))
        def write(lba,b):f.seek(lba*B);f.write(b)
        def retag(b):return tag(b,struct.unpack_from('<H',b,0)[0],struct.unpack_from('<I',b,12)[0],struct.unpack_from('<H',b,10)[0])
        copied=read(tail);tag(copied,257,45,B-16);write(p+45,copied);write(tail,copied)
        old=fe(2,4,extent=(43,3*B,0),efe=True);write(p+54,old)
        prior=read(s-1401);p32(prior,176+152+2*4,54);write(s-1401,retag(prior))
        current=read(s+300);entity(current,168,'*Microsoft Windows');write(s+300,retag(current))
        latest=fe(4,5,b'latest eight-session view\n',efe=True);entity(latest,168,'*Microsoft Windows');write(s+301,retag(latest))
        vat_fe=read(layout['vat']);entity(vat_fe,168,'*Microsoft Windows');write(layout['vat'],retag(vat_fe))
        packed=read(tail+1);at=(38+struct.unpack_from('<H',packed,36)[0]+packed[19]+3)&~3
        size=(38+struct.unpack_from('<H',packed,at+36)[0]+packed[at+19]+3)&~3
        packed[at:at+size]=tag(bytearray(packed[at:at+size]),257,0,size-16);write(tail+1,packed)
    layout.update(historyDirectoryICB=p+54,historyCopiedFID=p+45,zeroFidOffset=at,newFileICB=s+301)
    return layout

def windows_carried_fid(path,same_session=False,deleted=True):
    """Another independently encoded Windows transaction reuses its zero-tag file FID.

    The file EFE and zero-location FID originate in the preceding VAT, which itself
    has the exact older directory-relocation proof. No BurnLight writer is called.
    """
    layout=windows_fid_compatibility(path);p=layout['partition'];previous=layout['vat']
    start=layout['end'] if same_session else 16000;end=start+800;tail=start+310;root=start+300
    with pathlib.Path(path).open('r+b') as f:
        def read(lba):f.seek(lba*B);return bytearray(f.read(B))
        def write(lba,b):f.seek(lba*B);f.write(b)
        source_root=read(layout['session']+300);copied=read(layout['directoryTail']);packed=read(layout['directoryPacked'])
        length=struct.unpack_from('<Q',source_root,56)[0]-3*B;packed=packed[:length]
        if deleted:
            tombstone=fid('~$deleted.docx',0,location=0);tombstone[18]=5;tombstone[20:36]=bytes(16);tombstone[38]=255
            tag(tombstone,257,0,len(tombstone)-16);packed+=tombstone
        size=3*B+len(packed);p64(source_root,56,size);p64(source_root,64,size)
        p32(source_root,232,B+len(packed));p32(source_root,236,tail-p);tag(source_root,266,2,232)
        oldvat=read(previous);h=216;table=bytearray(oldvat[h:h+struct.unpack_from('<Q',oldvat,56)[0]])
        p32(table,132,previous-p);p32(table,152+2*4,root-p)
        latest=fe(end-3-p,248,table,efe=True,unique=23);entity(latest,168,'*Microsoft Windows');tag(latest,266,end-3-p,200+len(table))
        f.truncate(end*B);write(root,source_root);write(tail,copied);write(tail+1,packed.ljust(B,b'\0'));write(end-3,latest)
        if not same_session:
            for offset,name in [(16,'BEA01'),(17,'NSR03'),(18,'TEA01')]:
                b=bytearray(B);b[1:6]=name.encode();b[6]=1;write(start+offset,b)
            anchor=read(layout['session']+256);write(start+256,tag(anchor,2,start+256,496))
    tracks=list(layout['tracks'])
    if same_session:tracks[-1]=(tracks[-1][0],end,8,8)
    else:tracks.append((start,end,9,9))
    pathlib.Path(str(path)+'.tracks').write_text(''.join('%d %d %d %d\n'%r for r in tracks))
    layout.update(session=layout['session'] if same_session else start,end=end,vat=end-3,tracks=tracks,
                  previousWindowsVAT=previous,originDirectory=layout['session']+300,
                  originZeroFidBlock=layout['directoryPacked'],currentZeroFidBlock=tail+1,currentRoot=root,
                  deletedFidOffset=length)
    return layout
