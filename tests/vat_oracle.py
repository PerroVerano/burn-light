"""Small independent reader for generated composite images, without core/manifest use.

The acceptance oracle intentionally covers only the fixture's supported structures.
It checks ECMA tags, virtual ICB versus physical allocation addresses, file contents,
previous VAT and final-sector/end Anchor using Python's independent CRC primitive.
"""
import binascii
import hashlib
import pathlib
import struct

B = 2048
def u16(b, p): return struct.unpack_from('<H', b, p)[0]
def u32(b, p): return struct.unpack_from('<I', b, p)[0]
def u64(b, p): return struct.unpack_from('<Q', b, p)[0]
def decode(b):
    assert b and b[0] in (8, 16)
    return b[1:].decode('latin1' if b[0] == 8 else 'utf-16-be')

def read(path, session, generated=False, windows_fid_compatibility=False):
    path = pathlib.Path(path)
    ranges = [tuple(map(int, line.split()[:2])) for line in pathlib.Path(str(path)+'.tracks').read_text().splitlines()]
    index=pathlib.Path(str(path)+'.blocks')
    valid=set(map(int,index.read_text().splitlines())) if index.exists() else None
    with path.open('rb') as stream:
        def block(lba):
            assert any(begin <= lba < end for begin, end in ranges), ('unrecorded', lba)
            assert valid is None or lba in valid, ('cache hole', lba)
            stream.seek(lba*B); data = stream.read(B); assert len(data) == B
            return data
        def tagged(b, kind, location):
            assert u16(b, 0) == kind and u16(b, 2) == 3 and u32(b, 12) == location
            assert sum(b[i] for i in range(16) if i != 4) & 255 == b[4]
            size = u16(b, 10); assert size and 16+size <= len(b)
            assert binascii.crc_hqx(b[16:16+size], 0) == u16(b, 8)
            return b
        anchor = tagged(block(session+256), 2, session+256)
        vds = u32(anchor, 20); descriptors = {}
        for i in range(u32(anchor, 16)//B):
            b = block(vds+i); kind = u16(b, 0); tagged(b, kind, vds+i); descriptors[kind] = b
            if kind == 8: break
        lvd, pd = descriptors[6], descriptors[5]
        assert u16(lvd, 240) == 0x201 and u16(pd, 22) == 8192 and u32(pd, 184) == 2
        partition = u32(pd, 188)
        assert lvd[440:442] == bytes([1, 6]) and lvd[446:448] == bytes([2, 64])
        assert u16(lvd, 444) == u16(lvd, 484) == 8192 and u16(lvd, 256) == 1
        maps = []
        def physical(number, ref, table=None):
            assert ref in (0, 1)
            n = number if ref == 0 else (maps if table is None else table)[number]
            assert n < u32(pd, 192)
            return partition+n
        def contents(number, ref, table=None, metadata_only=False):
            absolute = physical(number, ref, table); fe = block(absolute); kind = u16(fe, 0)
            assert kind in (261, 266); tagged(fe, kind, number)
            header = 176 if kind == 261 else 216
            position = header+u32(fe, header-8); size = u32(fe, header-4)
            assert position+size <= B
            flags = u16(fe, 34) & 7; locations = []
            if flags == 3:
                payload = fe[position:position+u64(fe, 56)]; locations = [number]
                sections = [(absolute, len(payload), position)] if payload else []
            else:
                assert flags in (0, 1); step = 8 if flags == 0 else 16; payload = b''; sections = []
                for at in range(position, position+size, step):
                    length, address = u32(fe, at), u32(fe, at+4)
                    reference = ref if flags == 0 else u16(fe, at+8)
                    assert length < 1 << 30 and reference == 0
                    start = physical(address, reference); chunks = []
                    for offset in range((length+B-1)//B):
                        if not metadata_only:chunks.append(block(start+offset))
                        locations.append(address+offset)
                    payload += b''.join(chunks)[:length]; sections.append((start, length, 0))
            assert metadata_only or len(payload) == u64(fe, 56)
            return fe, payload, sections, locations
        end = ranges[-1][1]; vat_lba = end-1
        while block(vat_lba) == bytes(B): vat_lba -= 1
        vat_fe, table, _, _ = contents(vat_lba-partition, 0)
        assert vat_fe[27] == 248 and u16(table, 144) == u16(table, 146) == u16(table, 148) == 0x201
        maps[:] = [u32(table, pos) for pos in range(u16(table, 0), len(table), 4)]
        def microsoft(fe):return fe[(129 if u16(fe,0)==261 else 169):][:23].rstrip(b'\0 ') == b'*Microsoft Windows'
        histories=[];issues=[]
        if windows_fid_compatibility:
            previous=u32(table,132);seen={vat_lba};budget=len(table)
            while previous!=0xffffffff:
                old=partition+previous;assert old<vat_lba and old not in seen and len(histories)<1024;seen.add(old)
                oldfe,oldtable,_,_=contents(previous,0);assert oldfe[27]==248
                header=u16(oldtable,0);assert header>=152 and header<=len(oldtable) and (len(oldtable)-header)%4==0
                budget+=len(oldtable);assert budget<=64*1024*1024
                histories.append([old,[u32(oldtable,i) for i in range(header,len(oldtable),4)],oldfe,u64(oldfe,160 if u16(oldfe,0)==261 else 200)])
                previous=u32(oldtable,132)
        fsd = tagged(block(physical(u32(lvd, 252), 1)), 256, u32(lvd, 252))
        root = u32(fsd, 404); files = {}; directories = set(); seen = set()
        def first_allocation(child, vat, next_uid, older, childfe,table=None):
            absolute=physical(child,1,table)
            if absolute>vat or older<len(histories) and absolute<=histories[older][0]:return False
            if u64(childfe,200)>=next_uid:return False
            if not any(begin<=vat<end and begin<=absolute<end for begin,end in ranges):return False
            for oldvat,oldmap,_,oldnext in histories[older:]:
                if u64(childfe,200)<oldnext:return False
                if child<len(oldmap) and oldmap[child]!=0xffffffff:
                    prior=physical(child,1,oldmap)
                    if prior>oldvat:return False
                    b=block(prior)
                    if b==bytes(B):continue
                    tagged(b,266,child)
                    if b[27]!=5 or not microsoft(b) or not 16<=u64(b,200)<oldnext or u64(b,200)>=u64(childfe,200):return False
            return True
        def updated_child(child,index,childfe):
            origin=histories[index];previous=physical(child,1,origin[1])
            states=[origin]+histories[:index][::-1]+[[vat_lba,maps,vat_fe,u64(vat_fe,160 if u16(vat_fe,0)==261 else 200)]]
            boundary=None
            for vat,table,writer,next_uid in states:
                if child>=len(table) or table[child]==0xffffffff:return False
                absolute=physical(child,1,table)
                if boundary is None or absolute!=previous:
                    b=tagged(block(absolute),266,child)
                    if b[27]!=childfe[27] or not microsoft(b) or u64(b,200)!=u64(childfe,200) or u64(b,200)>=next_uid:return False
                    if boundary is not None and (not microsoft(writer) or not boundary<absolute<=vat or
                            not any(begin<=absolute<end and begin<=vat<end for begin,end in ranges)):return False
                previous=absolute;boundary=vat
            return True
        def relocation_record(number,fe,b,position,first):
            size=len(b);recorded=u32(b,12)
            for oldvat,oldmap,_,_ in histories[first:]:
                if number>=len(oldmap) or oldmap[number]==0xffffffff or physical(number,1,oldmap)>oldvat:continue
                oldfe,_,_,oldlocations=contents(number,1,oldmap,metadata_only=True)
                if oldfe[27]!=4 or position+size>u64(oldfe,56):continue
                if u64(oldfe,160 if u16(oldfe,0)==261 else 200)!=u64(fe,160 if u16(fe,0)==261 else 200):continue
                if oldlocations[position//B]!=recorded:continue
                _,oldbytes,_,_=contents(number,1,oldmap)
                if oldbytes[position:position+size]!=b:continue
                tagged(b,257,recorded)
                child=u32(b,24);oldchild=block(physical(child,u16(b,28),oldmap));tagged(oldchild,u16(oldchild,0),child)
                assert u32(b,32)==u64(oldchild,160 if u16(oldchild,0)==261 else 200)&0xffffffff
                return True
            return False
        def carried_origin(number,fe,b,position,child,childfe):
            for index,(vat,table,vatfe,next_uid) in enumerate(histories):
                if not microsoft(vatfe) or number>=len(table) or table[number]==0xffffffff or child>=len(table) or table[child]==0xffffffff:continue
                if not first_allocation(child,vat,next_uid,index+1,childfe,table):continue
                oldfe,_,_,locations=contents(number,1,table,metadata_only=True)
                if not microsoft(oldfe) or oldfe[27]!=4 or physical(number,1,table)>vat or position+len(b)>u64(oldfe,56):continue
                if u64(oldfe,160 if u16(oldfe,0)==261 else 200)!=u64(fe,160 if u16(fe,0)==261 else 200):continue
                _,payload,_,_=contents(number,1,table)
                if payload[position:position+len(b)]!=b:continue
                offset=0;proven=False
                while offset<position:
                    q=payload[offset:];n=(38+u16(q,36)+q[19]+3)&~3
                    assert n<=B and offset+n<=position;tagged(q[:n],257,u32(q,12))
                    if u32(q,12) and u32(q,12)!=locations[offset//B]:
                        proven |= relocation_record(number,oldfe,q[:n],offset,index+1)
                    offset+=n
                if proven and updated_child(child,index,childfe):return vat
            return None
        def walk(number, path, parent):
            assert number not in seen; seen.add(number)
            fe, payload, sections, locations = contents(number, 1)
            if fe[27] == 5:
                files[path] = {'bytes': payload, 'sha256': hashlib.sha256(payload).hexdigest(), 'sections': sections, 'uniqueId':u64(fe,160 if u16(fe,0)==261 else 200)}
                return
            assert fe[27] == 4; directories.add(path); position = 0
            relocation=False
            while position < len(payload):
                b = payload[position:]; impl, name_length = u16(b, 36), b[19]
                size = (38+impl+name_length+3) & ~3
                assert size<=B and position+size<=len(payload) and u16(b,10)==size-16 and u16(b,16)==1
                deleted=bool(b[18]&4)
                if deleted:
                    assert b[18]&~7==0 and impl==0 and name_length>1 and b[20:36]==bytes(16)
                    assert b[38] in (254,255)
                    decode(bytes([8 if b[38]==254 else 16])+b[39:38+name_length])
                location=locations[position//B];recorded=u32(b,12)
                if recorded!=location and windows_fid_compatibility:
                    tagged(b[:size],257,recorded)
                    assert microsoft(vat_fe) and microsoft(fe)
                    if recorded:
                        proven=relocation_record(number,fe,b[:size],position,0)
                        assert proven;relocation=True
                    else:
                        assert relocation and impl==0
                        if not deleted:
                            child=u32(b,24);assert b[18]&~3==0 and u16(b,28)==1
                            childfe,_,_,_=contents(child,1,metadata_only=True)
                            assert u16(childfe,0)==266 and childfe[27]==(4 if b[18]&2 else 5) and microsoft(childfe)
                            assert 16<=u64(childfe,200)<u64(vat_fe,160 if u16(vat_fe,0)==261 else 200)
                            assert u32(b,32)==u64(childfe,200)&0xffffffff
                            assert first_allocation(child,vat_lba,u64(vat_fe,160 if u16(vat_fe,0)==261 else 200),0,childfe) or carried_origin(number,fe,b[:size],position,child,childfe) is not None
                    issues.append({'directory':path,'byteOffset':position,'expected':location,'recorded':recorded})
                else:tagged(b[:size], 257, location)
                if deleted:position+=size;continue
                assert u16(b, 28) == 1
                child = u32(b, 24)
                if b[18] & 8: assert position == 0 and child == parent
                else:
                    name = decode(b[38+impl:38+impl+name_length])
                    child_fe=block(physical(child,1));unique=u64(child_fe,160 if u16(child_fe,0)==261 else 200)
                    assert u32(b,32)==unique & 0xffffffff
                    walk(child, name if not path else path+'/'+name, number)
                position += size
        walk(root, '', root)
        assert len(files) == u32(table, 136) and len(directories) == u32(table, 140)
        if generated:
            assert vat_lba == end-1
            tail_anchor = tagged(block(end-257), 2, end-257)
            assert tail_anchor[16:32] == anchor[16:32]
            assert all(block(u32(anchor, 20)+i)[16:] == block(u32(anchor, 28)+i)[16:] for i in range(6))
            integrity=tagged(block(u32(lvd,436)),9,u32(lvd,436))
            assert u32(integrity,88)==u32(pd,192) and u32(integrity,80)!=0xffffffff
            assert u32(integrity,84)==u32(integrity,92)==0xffffffff
            assert u64(integrity,40)==u64(vat_fe,160 if u16(vat_fe,0)==261 else 200)
        return {'files': files, 'directories': directories, 'previous': u32(table, 132)+partition, 'vat': vat_lba, 'vds': vds,
                'compatibility':issues, 'strictConformance':not issues}
