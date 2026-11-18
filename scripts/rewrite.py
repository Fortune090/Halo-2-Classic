"""Rewrite the game objects for the in-place build: every data symbol that stands for retail data becomes a reference
(to the retail address, defined in retail_syms.S), and static functions that retail data points to become visible."""
import sys,os,json,struct,collections
sys.path.insert(0,'/home/claude/scripts')
from coff import Coff
NAT='/home/claude/native'
plan=json.load(open(NAT+'/plan.json'))['plan']
fix=json.load(open(NAT+'/fixups2.json'))['fix']
static_needed=collections.defaultdict(set)      # object name -> static function names retail data points to
for d in fix:
    if d.get('static') and d.get('symbol'): static_needed[d['object'].split(':')[-1]].add(d['symbol'])
# file-scope and function-local statics that matched code shows to be retail data (statics2.py): object|name -> retail
static_data={}
if os.path.exists(NAT+'/statics_plan.json'):
    for k,v in json.load(open(NAT+'/statics_plan.json')).items(): static_data[tuple(k.split('|',1))]=v['retail']
# vtables the plan does not place, found in retail's data by the functions they hold (vtables.py): object|symbol -> retail
vtables={}
if os.path.exists(NAT+'/vtables_plan.json'):
    for k,v in json.load(open(NAT+'/vtables_plan.json')).items(): vtables[tuple(k.split('|',1))]=v
# references that matched code proves to stand for another retail address than the symbol's own (oracle2.py): the source
# uses one name where retail has two things (c_marker_object's vtable in function_244980 is not the one its create()
# writes), or the wrong field. Inside that one function the reference is aimed at retail's address; everywhere else the
# symbol keeps its place. Only where every field of that function for the same symbol and offset says the same address.
repoint=collections.defaultdict(list)            # object -> [(function symbol, data symbol, offset, retail address, fields in the matched build)]
repoint_report=[]
if os.path.exists(NAT+'/oracle2.json') and os.path.exists(NAT+'/funcmap.json'):
    o2=json.load(open(NAT+'/oracle2.json')); fm=json.load(open(NAT+'/funcmap.json'))
    groups=collections.defaultdict(collections.Counter)
    for fva,name,off,theirs,at in o2.get('data_fields',[]): groups[(fva,name,off)][theirs]+=1
    for (fva,name,off),seen in sorted(groups.items()):
        if name not in plan: continue
        if len(seen)>1:
            if any(t!=plan[name]['retail']+off for t in seen): repoint_report.append(('mixed, left alone',fva,name,off,[hex(t) for t in seen]))
            continue
        theirs=next(iter(seen))
        if theirs==plan[name]['retail']+off: continue
        f=fm.get('%08x'%int(fva,16))
        if not f: repoint_report.append(('no symbol for the function',fva,name,off,hex(theirs))); continue
        repoint[f['object'].split(':')[-1]].append((f['name'],name,off,theirs,seen[theirs],fva))
objs=[l.strip() for l in open(NAT+'/objects2.rsp') if l.strip()]
out_dir=NAT+'/obj_r'; os.makedirs(out_dir,exist_ok=True)
stats=collections.Counter(); renamed={}
new_list=[]
for path in objs:
    c=Coff(path); d=bytearray(c.d); base=os.path.basename(path)
    machine,nsec,ts,symptr,nsym,optsz,chars=struct.unpack_from('<HHIIIHH',d,0)
    strtab=symptr+nsym*18
    strsize=struct.unpack_from('<I',d,strtab)[0]
    assert strtab+strsize==len(d),(base,strtab,strsize,len(d))
    extra=bytearray()
    def set_name(symoff,name):
        b=name.encode('latin1')
        if len(b)<=8: d[symoff:symoff+8]=b.ljust(8,b'\0')
        else:
            off=strsize+len(extra); extra.extend(b+b'\0')
            struct.pack_into('<II',d,symoff,0,off)
    key=base.replace('_keep.obj','.obj')
    want_static=static_needed.get(key,set())
    # data of 16 bytes or more starts on a 16-byte boundary: the source declares SSE constants as plain arrays, retail
    # has them aligned, and an aligned load (movaps) of one that is not faults (stage 7: absolute_bits in function_303540)
    for sec in c.sections:
        if not (sec.flags&0x20) and (sec.flags&0xc0) and sec.rawsize>=16 and not (sec.flags&0x02000000):
            align=(sec.flags>>20)&15
            if align<5:
                hdr=20+optsz+(sec.index-1)*40
                sec.flags=(sec.flags&~0x00f00000)|0x00500000
                struct.pack_into('<I',d,hdr+36,sec.flags); stats['data sections aligned to 16']+=1
    for y in c.symbols:
        symoff=symptr+y.index*18
        if y.storage==2 and y.section>0 and y.name in plan:
            s=c.sections[y.section-1]
            if s.flags&0x20: continue
            struct.pack_into('<Ih',d,symoff+8,0,0)             # value 0, section 0: undefined
            if s.flags&0x1000:                                   # the section loses its COMDAT leader: make it an ordinary section
                hdr=20+optsz+(s.index-1)*40
                struct.pack_into('<I',d,hdr+36,s.flags&~0x1000)
                s.flags&=~0x1000
                stats['COMDAT sections made ordinary']+=1
            stats['data symbols redirected']+=1
        elif y.storage==2 and y.section>0 and (key,y.name) in vtables:
            # this file's copy of the vtable becomes a private absolute symbol at retail's table: the objects built
            # here get retail's slot order, which is what every caller in matched code uses
            s=c.sections[y.section-1]
            struct.pack_into('<Ih',d,symoff+8,vtables[(key,y.name)],-1); d[symoff+16]=3
            if s.flags&0x1000:
                hdr=20+optsz+(s.index-1)*40
                struct.pack_into('<I',d,hdr+36,s.flags&~0x1000); s.flags&=~0x1000
            stats['vtables moved to retail tables']+=1
        elif y.storage==3 and y.section>0 and (key,y.name) in static_data and not (c.sections[y.section-1].flags&0x20):
            # the static stays private to its file but becomes an absolute symbol at retail's address: other files
            # reach the same data by its own name there
            struct.pack_into('<Ih',d,symoff+8,static_data[(key,y.name)],-1)
            stats['static data moved to retail addresses']+=1
        elif y.storage==3 and y.section>0 and y.name in want_static and (c.sections[y.section-1].flags&0x20):
            new=y.name+'$h2n$'+key[:-4]
            set_name(symoff,new); d[symoff+16]=2
            renamed[(key,y.name)]=new; stats['static functions made visible']+=1
    new_symbols=bytearray()
    for fname,dname,off,theirs,expected,fva in repoint.get(key,[]):
        fsym=[y for y in c.symbols if y.name==fname and y.section>0 and (c.sections[y.section-1].flags&0x20)]
        if not fsym: repoint_report.append(('function not in its object',fva,dname,off,hex(theirs),key)); continue
        fy=fsym[0]; sec=c.sections[fy.section-1]
        later=[z.value for z in sec.symbols if z.value>fy.value and (z.type==0x20 or z.storage==2)]
        end=min(later) if later else sec.rawsize
        relbase=sec.relptr+(10 if (sec.flags&0x01000000) and sec.nrel==0xffff else 0)
        found=0; index=None
        for k,(va,symi,typ) in enumerate(sec.relocs):
            if typ!=6 or not (fy.value<=va<end) or c.by_index[symi].name!=dname: continue
            if struct.unpack_from('<I',d,sec.rawptr+va)[0]!=off: continue
            if index is None:
                index=nsym+len(new_symbols)//18
                nm=('$h2n$at$%x'%theirs).encode()
                so=strsize+len(extra); extra.extend(nm+b'\0')
                new_symbols.extend(struct.pack('<IIIhHBB',0,so,theirs,-1,0,3,0))
            struct.pack_into('<I',d,relbase+k*10+4,index)
            struct.pack_into('<I',d,sec.rawptr+va,0)
            found+=1
        repoint_report.append(('re-aimed' if found else 'no such reference in the function',fva,dname,off,hex(theirs),'%d here, %d in the matched build'%(found,expected)))
        if found: stats['references re-aimed at retail addresses']+=found
    if new_symbols:
        d[strtab:strtab]=new_symbols
        struct.pack_into('<I',d,12,nsym+len(new_symbols)//18)
        strtab+=len(new_symbols)
    if extra:
        d.extend(extra); struct.pack_into('<I',d,strtab,strsize+len(extra))
    o=os.path.join(out_dir,base); open(o,'wb').write(d); new_list.append(o)
open(NAT+'/objects_r.rsp','w').write('\n'.join(new_list)+'\n')
json.dump({k[0]+'|'+k[1]:v for k,v in renamed.items()},open(NAT+'/renamed_statics.json','w'))
print(dict(stats),'objects',len(new_list))
for r in repoint_report: print('  field:',*r)
missing=[(o,n) for o,ns in static_needed.items() for n in ns if (o,n) not in renamed]
print('static functions wanted but not found:',missing[:10])
