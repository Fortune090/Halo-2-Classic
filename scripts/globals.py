import re,os,sys,collections
root=sys.argv[1]
ident=re.compile(r'\bg_(?:unknown_|global_)?([0-9a-fA-F]{5,6})\b')
names=collections.Counter()
defs={}   # name -> (has_init, file)
externs=set()
# crude: track brace depth per file; a definition is a depth-0 statement that isn't extern/typedef and has declarator g_xxx followed by [ ; = 
decl=re.compile(r'^(?!\s)(?!extern\b|typedef\b|#|//|/\*|\*|return\b|using\b|struct\b.*\{|class\b.*\{)(?:static\s+|const\s+|volatile\s+)*[A-Za-z_][\w:<>\s\*&,]*?[\s\*&](g_\w+)\s*((?:\[[^\]]*\]\s*)*)(=|;)')
allg=re.compile(r'\bg_\w+\b')
total_defs=0
for base in ('src','include'):
    for d,_,ns in os.walk(os.path.join(root,base)):
        for n in ns:
            if not n.endswith(('.cpp','.h')): continue
            p=os.path.join(d,n)
            depth=0; disabled=0
            for line in open(p,encoding='utf-8',errors='replace'):
                for m in allg.finditer(line): names[m.group(0)]+=1
                s=line.rstrip('\n')
                if depth==0:
                    if s.startswith('extern'):
                        for m in allg.finditer(s): externs.add(m.group(0))
                    m=decl.match(s)
                    if m:
                        nm=m.group(1); init=m.group(3)=='='
                        if nm not in defs or (init and not defs[nm][0]): defs[nm]=(init,p)
                depth+=s.count('{')-s.count('}')
                if depth<0: depth=0
print('distinct g_ identifiers:',len(names))
print('with a file-scope definition found:',len(defs),' of which initialised:',sum(1 for v in defs.values() if v[0]))
nodef=[n for n in names if n not in defs]
print('no definition found (extern only / macro alias / member):',len(nodef), 'of which declared extern:',len([n for n in nodef if n in externs]))
# address-named ones: range
addrs=sorted(int(m.group(1),16) for n in names for m in [ident.fullmatch(n)] if m)
print('address-named:',len(addrs), hex(addrs[0]), hex(addrs[-1]))
import bisect
for lo,hi,lab in ((0x12000,0x412000,'inside code range (rdata tables / consts in .text)'),(0x412000,0x470000,'0x412000-0x470000'),(0x470000,0x586000,'0x470000-0x586000')):
    print(' ',lab, sum(1 for a in addrs if lo<=a<hi))
