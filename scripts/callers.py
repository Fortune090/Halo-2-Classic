import csv,sys
rows={int(r['va'],16):r for r in csv.DictReader(open(sys.argv[1]))}
callers={}
for va,r in rows.items():
    for c in r['calls'].split():
        callers.setdefault(int(c,16),[]).append(va)
def show(a,depth=0,seen=None):
    seen=seen or set()
    r=rows.get(a)
    print('  '*depth+f'{a:#x} {r["owner"] if r else "?"} {r["status"] if r else ""} {r["size"] if r else ""} {r["source"] if r else ""}')
    if depth>=5 or a in seen: return
    seen.add(a)
    for c in callers.get(a,[])[:4]: show(c,depth+1,seen)
show(int(sys.argv[2],16))
# functions nobody calls (roots) in game owner, big ones
