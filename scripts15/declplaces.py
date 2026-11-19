"""declplaces.py: where our compile takes each argument (by 4-byte slot, in declaration order), from the declaration as
llvm-undname spells it: __fastcall puts only the first two integer or pointer arguments of 4 bytes or less in ecx and
edx - a float, a double, an __int64 or a structure goes on the stack (0x3db00's priority, a float, did; taking it for
edx put the adapter's stack 4 bytes out); __thiscall takes this in ecx; the rest are on the stack in order."""
import re, subprocess, functools
@functools.lru_cache(maxsize=None)
def undname(name):
    return subprocess.run(['llvm-undname', name], capture_output=True, text=True, stdin=subprocess.DEVNULL).stdout.strip().split('\n')[-1]
def params(und):
    m = re.search(r'(__stdcall|__fastcall|__cdecl|__thiscall)\s+[^(]*\((.*)\)\s*(const)?$', und)
    if not m: return None, None
    args = m.group(2).strip()
    return m.group(1), ([] if args in ('void', '') else [a.strip() for a in re.split(r',(?![^<(]*[>)])', args)])
def places(name):
    """-> (cc, [place of slot 0, 1, ...]) with places 'ecx', 'edx' or ('stk', k)"""
    und = undname(name)
    cc, al = params(und)
    if cc is None: return None, None
    out = []; regs = (['ecx', 'edx'] if cc == '__fastcall' else [])
    if cc == '__thiscall': out.append('ecx')
    k = 0
    for a in al:
        wide = a in ('double', '__int64', 'unsigned __int64')
        integral = not wide and a not in ('float',) and not (a.startswith(('struct ', 'class ', 'union ')) and not a.endswith(('*', '&')))
        if regs and integral: out.append(regs.pop(0)); continue
        for _ in range(2 if wide else 1): out.append(('stk', k)); k += 1
    return cc, out
if __name__ == '__main__':
    import sys
    for n in sys.argv[1:]: print(n, places(n))
