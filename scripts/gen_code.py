"""Generate layer/retail_code.S: what the layer needs to run retail's own code in place for the functions the
decompilation does not have: the XBE's code sections (to load at their own addresses, CRC-checked), the kernel
functions that code imports (each slot of the import table gets a trap, or the layer's implementation), and the stubs
of the decompilation that are to jump to retail's function instead (retail_routes.json from retailconv.py, limited to
the allow-list here)."""
import sys,json,zlib,struct
sys.path.insert(0,'/home/claude/scripts')
from xbeimg import XbeImage
NAT='/home/claude/native'
x=XbeImage('/home/claude/xbe/default.xbe')
names=json.load(open(NAT+'/kernel_names.json'))
fm=json.load(open(NAT+'/funcmap.json'))
# What runs from retail's code. Chosen by hand for now, each checked against retail's code for how it takes its
# arguments (retailconv.py's automatic answer is a guide: it still errs towards "needs an adapter"):
#   'jmp'   retail's function takes its arguments exactly as the source declares it: our symbol jumps there
#   'fast1' / 'fast2'  the source's draft is declared with the file's default register convention, but retail's
#           function takes its one / two arguments on the stack: our symbol pushes them and jumps, and the pointers
#           to it in the game's data go back to retail's address (they are called retail's way)
# Game logic only: nothing in the renderer (it reaches the console's Direct3D), no Bink, no library internals.
ROUTE={0x329d20:'jmp',0x32a850:'jmp',   # stage 8: the page heap and allocator library constructors (the allocator the network code is given)
       0x6d4c0:'jmp',0xb7430:'jmp',0xb7b40:'jmp',0xb8540:'jmp',0x1d3920:'jmp',0x1f6090:'jmp',0x1fbac0:'jmp',0x1fc7e0:'jmp',
       0x279410:'jmp',0x2902b0:'jmp',0x2ce7b0:'jmp',0x2ce9e0:'jmp',0x2e2f70:'jmp',
       0x1608e0:'jmp',        # stage 9: the scoreboard panel (GAME OVER): the draft gives the heading row an opaque white where retail's is a dim translucent grey
       0xd4e60:'fast1'}       # a draft whose convention is wrong: the scenario's lifecycle table calls it with its argument on the stack
# Stage 9: besides those, every unproven draft that retail calls through a pointer and that passes the checks of
# unproven_routes.py (its docstring has the rule) runs as retail's code.
# stage 9: an actor's movement update (it picks the path point, runs the mover, notices standing still, chooses the speed).
# The source's body is an unworked draft that differs from retail from +0x157 on, and with it no AI ever stopped moving.
# Our callers pass the actor in ecx (the declaration); retail's function takes it in eax, and keeps ebx, esi, edi, ebp.
ROUTE[0x1f38c0]='eax1'
# stage 9 (s9e), an experiment behind H2N_TICK=retail (off by default): the game's tick and the director's frame update as
# retail's code, whole. Both take no register arguments (the tick none at all; the director the frame's seconds on the
# stack, the same for the source's declaration and for retail), so our callers reach them as they are, and everything
# they call directly is then retail's code too - 3,642 functions under the tick, 225 of them unproven drafts that no
# route reaches one by one (their arguments travel in registers the compiler chose). Measured on Metropolis: the game
# runs, the AI behaves as with the table entries alone, and the first-person weapon and body are no longer drawn -
# something our drawing code reads is kept somewhere else by retail's tick. Not understood yet, hence not the default.
ROUTE[0x137fe0]='jmp'; ROUTE[0x1554b0]='jmp'
# s9e: the view of a light that casts onto the scene (0x497a0; nothing but arithmetic, it calls no one). The source's
# draft stopped Cairo Station with a null pointer: for a source of a kind other than 0 and 1 it reads the second shape's
# sizes through a pointer it set from another field than retail tests. Our callers pass the source in ecx and the
# result in edx, then eight arguments on the stack; retail takes the source in ecx, the result in eax and the same
# eight on the stack (ret 0x20), and keeps ebx, esi, edi, ebp.
ROUTE[0x497a0]='edx_eax'
# s9f: the actor for a member of a squad (0x1e0850, a draft of 1272 bytes; stdcall with four stack arguments in retail
# as declared - its "live-in" esi and edi are writes). With the draft every enemy came out as the highest rank: on
# Metropolis (Normal) the three Elites at the start were all elite_ultra; with retail's function they are elite,
# elite_major, elite (H2N_WATCH names each actor's character tag). Retail's code reaches nothing it must not from it.
ROUTE[0x1e0850]='jmp'
# s9q: a weapon's barrel firing its projectiles (0x104150, a draft of 5212 bytes; stdcall with two stack arguments in
# retail as declared - its "live-in" ecx is the stack probe's). With the draft a player's shot left the barrel straight
# along the aim instead of turning toward the point under the reticle: the Banshee's bolts went low, turrets low and to
# the right (the user's reports, 17 Nov). In the Scorpion here the draft's shell flew exactly along the aim, retail's
# 0.93 degrees above it. H2N_UNROUTE=104150 runs the draft again (for comparing).
ROUTE[0x104150]='jmp'
for e in json.load(open(NAT+'/routes_unproven.json')): ROUTE.setdefault(e['va'],'jmp')
# s9s: the game's Bink movie player. The Bink library (RAD's, linked into the XBE: the BINK* sections and BINKDATA) runs as its
# own code; the game's source has only stand-ins for the 11 entry points it calls (stubs/bink.cpp, stubs/simulation.cpp:
# BinkSetMemory 0x3e2820, BinkOpen 0x3e0af0, BinkWait 0x3e2330, BinkDoFrame 0x3e2870, BinkNextFrame 0x3e2e50,
# BinkCopyToBuffer 0x3e2830, BinkSoundUseDirectSound 0x3e0450 and its open function 0x3e3c90, the two mix-bin calls
# 0x3e2630 / 0x3e2680, BinkClose 0x3e2ff0), each returning 0 - so no movie ever started. Each is __stdcall on both sides
# (checked: every one pops exactly the arguments the stub declares). Kind flag 0x200: H2N_MOVIES=off leaves them out.
BINK_ROUTES=(0x3e2820,0x3e0af0,0x3e2330,0x3e2870,0x3e2e50,0x3e2830,0x3e0450,0x3e3c90,0x3e2630,0x3e2680,0x3e2ff0)
for va in BINK_ROUTES: ROUTE[va]='jmp'
# s9u: the system-link join (found with two copies joined over H2N_XNET=retail): the connect-request handler 0x785d0 is an
# unproven draft (status todo) that stopped the joining copy (its first argument is not the observer it takes it for, and it
# calls 0x785f7, a label inside itself, as a function). Retail takes the same three arguments on the stack (ret 0xc).
NET_ROUTES=(0x785d0,)
for va in NET_ROUTES: ROUTE[va]='jmp'
# ... and the message dispatcher that calls it, 0x938e0 (s_network_message_handler, status todo): the source declares it
# a member taking (address, type, message) on the stack (ret 0xc); retail takes this in ecx, the type in eax, the address in
# edx and the message on the stack (ret 4: retail's read_packet 0x7afd0 calls it so at 0x7b10a). Kind this3_eax_edx moves
# the arguments over (retail.c) and jumps to retail's.
ROUTE[0x938e0]='this3_eax_edx'
# s9x: network_observer_update_connection 0x76ff0 (status todo; the same join path, called right after the connect
# handling). The source declares it fastcall (observer in ecx, channel index in edx); retail takes the index in ebx and the
# observer on the stack (ret 4; [esp+0x14] read at 0x76ffc after four pushes). Kind fast2_ebx = 6 (retail.c keeps ebx).
ROUTE[0x76ff0]='fast2_ebx'
# s10n: the network's memory setup 0x81780 (status todo; found joining a game xemu hosts, over H2N_XNET=retail). The draft
# builds the unreliable streams and the two dispatchers with one-slot vtables of its own (c_unreliable_stream_storage and
# the c_dispatcher_*_storage classes of unknown_081780.cpp) where retail stores 0x450db8, 0x450d60 and 0x450cb8 (at 0x81a25,
# 0x81c48 and 0x81c6b), and it hands network_observer_recreate_connections the contents of g_5291a0 where retail passes its
# address (0x81f66). Sending on a stream (function_095580) then called slot 1 of the one-slot table - the next class's v0,
# which pops nothing - so the stack slipped 4 bytes there, its epilogue gave back the wrong ebx, esi and edi, and the
# connect handler 0x785d0 went on with the observer register zeroed (network_observer_channel_check_connect(NULL, 0)).
# Retail takes the stage on the stack and pops it (ret 4, through its tail jump to 0x78900), as the source declares.
ROUTE[0x81780]='jmp'
import os
if os.path.exists(NAT+'/routes_called.json'):      # stage 9: unproven drafts with a proven (matched) caller, see called_routes.py
    for e in json.load(open(NAT+'/routes_called.json')): ROUTE.setdefault(e['va'],'jmp')
# s10m: a hand route whose function the decompilation has since matched runs as the source's function (its code is
# proven, and the source's callers and the function itself then agree on the declared convention). The game's tick and
# the director's frame update stay listed: retail.c leaves them to the source unless H2N_TICK=retail asks for retail's.
import csv
_status={int(r['va'],16):r['status'] for r in csv.DictReader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv'))}
_matched=sorted(va for va in ROUTE if _status.get(va)=='matched' and va not in (0x137fe0,0x1554b0))
for va in _matched: del ROUTE[va]
print('hand routes left out, their functions now matched:',' '.join('%#x'%va for va in _matched))
o=['/* generated by gen_code.py from the retail XBE (v1.0) */','.section .rdata,"dr"','.balign 4','.globl _h2n_code_sections','_h2n_code_sections:']
strs=[]
def s(text):
    strs.append(text); return 'LC%d'%(len(strs)-1)
for sec in x.sections:
    if sec['va']>=0x411000 or not sec['rsize']: continue
    raw=x.x[sec['raw']:sec['raw']+sec['rsize']]
    o.append('  .long %s, 0x%x, 0x%x, 0x%x'%(s(sec['name']),sec['va'],sec['rsize'],zlib.crc32(raw)&0xffffffff))
o.append('  .long 0,0,0,0')
o+=['.globl _h2n_kernel_table','_h2n_kernel_table: .long 0x%x'%x.kernel_thunk,'.globl _h2n_kernel_imports','_h2n_kernel_imports:']
t=x.kernel_thunk; n=0; ords=[]
while True:
    v=x.u32(t+4*n)
    if not v: break
    ords.append(v&0x7fffffff); n+=1
for k in ords: o.append('  .long %d, %s, KT%d'%(k,s(names.get(str(k),'?')),k))
o.append('  .long 0,0,0')
o+=['.globl _h2n_routes','_h2n_routes:']
KIND={'jmp':0,'fast1':1,'fast2':2,'eax1':3,'edx_eax':4,'this3_eax_edx':5,'fast2_ebx':6}
# s9f: routes through an adapter of their own (written out below, in .text): our symbol jumps to the adapter, which
# calls retail's function retail's way and returns ours.
#   0x26c590, the pathfinding trace from a point through the sectors (a draft, 1605 bytes): on Outskirts a matched
#   behaviour function (0x2551f0) returned into a float after calling it - the draft had written past the result it was
#   handed on the caller's stack. Ours: fastcall (pathfinding ecx, origin edx; sector, target, direction, distance,
#   location, trace on the stack, 24 bytes popped). Retail's: the sector in eax, the trace in esi, and on the stack
#   pathfinding, origin, target, direction, distance, location (ret 0x18; read off its code: it compares eax with
#   [first stack argument] - the sector count - and with the third, and writes the result through esi); keeps ebx, ebp, edi.
CUSTOM={0x26c590:"""  pushl %esi
  movl 28(%esp), %esi
  movl 8(%esp), %eax
  pushl 24(%esp)
  pushl 24(%esp)
  pushl 24(%esp)
  pushl 24(%esp)
  pushl %edx
  pushl %ecx
  call *RA26c590
  popl %esi
  ret $24"""}
# s10p: 0x298d90 (a draft, 1732 bytes, in routes_called.json since s9e and routed as 'jmp' until now): an actor's
#   avoidance of an obstacle in its way. Retail takes arg_7 in ecx and arg_8 in edx, arg_0 ... arg_6 on the stack, and
#   pops 0x1c (read off its one retail caller, 0x29a5e0-0x29a617, and its first instructions, which add [ecx] and [edx]
#   as two point2f). Ours: fastcall (arg_0 ecx, arg_1 edx; arg_2 ... arg_8 on the stack, 28 bytes). The plain jump
#   handed retail the two datum indices as the two points: the user's two stops at 0x298d97 on Metropolis, when an
#   actor had to get out of the way of the Ghost the player was boosting (build 37, with DLSS and again with SMAA).
CUSTOM[0x298d90]="""  pushl 20(%esp)
  pushl 20(%esp)
  pushl 20(%esp)
  pushl 20(%esp)
  pushl 20(%esp)
  pushl %edx
  pushl %ecx
  movl 52(%esp), %ecx
  movl 56(%esp), %edx
  call *RA298d90
  ret $28"""
ROUTE.pop(0x298d90, None)
# s10p: 0x11dd80 (a draft, 429 bytes, in routes_called.json since s9e): the inverse of a node transform, for an
#   animation graph's inheritance (its one caller, function_27a100, is ours). The source declares (in, out) fastcall:
#   in in ecx, out in edx. Retail's function reads in from edx and writes out through ecx (its first instructions copy
#   [edx] to [ecx]; its one retail caller, 0x27a1cc, puts the result buffer in ecx). The plain jump made retail read the
#   result buffer and write over the input. Swapped here; nothing on the stack, retail pops nothing.
CUSTOM[0x11dd80]="""  xchgl %ecx, %edx
  jmp *RA11dd80"""
ROUTE.pop(0x11dd80, None)
# s9f: the routes this build adds to s9e's carry 0x100 in their kind, so that H2N_NEWROUTES=off can leave them out (an A/B
# test on the user's PC): the 29 + 6 the corrected classifier admits, the squad member's actor (0x1e0850) and the
# pathfinding trace through its adapter (0x26c590)
OLD=set()
for f in ('routes_called_s9e.json','routes_unproven_s9e.json'):
    if os.path.exists(NAT+'/'+f): OLD|={e['va'] for e in json.load(open(NAT+'/'+f))}
HAND_OLD={0x329d20,0x32a850,0x6d4c0,0xb7430,0xb7b40,0xb8540,0x1d3920,0x1f6090,0x1fbac0,0x1fc7e0,0x279410,0x2902b0,0x2ce7b0,0x2ce9e0,0x2e2f70,0x1608e0,0xd4e60,0x1f38c0,0x497a0}
nnew=0
for va,kind in sorted(ROUTE.items()):
    name=fm['%08x'%va]['name']
    new = va not in OLD and va not in HAND_OLD
    nnew+=new
    if va in BINK_ROUTES: new=False
    o.append('  .long "%s", 0x%x, %s, %d'%(name,va,s(name),KIND[kind]|(0x100 if new else 0)|(0x200 if va in BINK_ROUTES else 0)))
for va in sorted(CUSTOM):
    name=fm['%08x'%va]['name']
    o.append('  .long "%s", AD%x, %s, 0x100'%(name,va,s(name))); nnew+=1
print('routes new in s9f (H2N_NEWROUTES=off leaves them out):',nnew)
o.append('  .long 0,0,0,0')
for va in sorted(CUSTOM): o.append('RA%x: .long 0x%x'%(va,va))
# retail's copies of library functions that the native build has too (the layer's, a stand-in, or Windows' own): each
# gets a jump to ours at its retail address, so that retail code which calls one ends up where our code does. Not the
# console's Direct3D (retail calls it with conventions of the compiler's own choosing, and nothing routed reaches it)
# and not the compiler's register-convention helpers (retail's own copies are right for retail code).
lib=json.load(open(NAT+'/oracle2.json')).get('libnames', {})     # (the published oracle2.json has no libnames: oracle2.py remakes them from your XDK)
# stage 9: and the library functions no matched function calls, named from the SDK's libraries (libsig.py)
import os
if os.path.exists(NAT+'/libsig.json'):
    for k,v in json.load(open(NAT+'/libsig.json')).items(): lib.setdefault(k,v)
used=json.load(open(NAT+'/unresolved3.json')); used=set(used if isinstance(used,list) else used.keys())
KEEP={'__chkstk','__ftol2','__CIacos','__CIasin','__CIfmod','__CIpow','__allmul','__alldiv','__allrem','__aulldiv','__allshl',
      '__except_list','___CxxFrameHandler','__purecall','__fltused'}
o+=['.globl _h2n_library_patches','_h2n_library_patches:']; npatch=0
for va,(name,where) in sorted(lib.items()):
    if name not in used or name in KEEP or 'D3D' in name or 'Direct3D' in name: continue
    o.append('  .long 0x%s, "%s", %s'%(va,name,s(name))); npatch+=1
# s9s: the library functions retail's Bink calls that no game object does (so `used` above leaves them out), each to the
# function of ours or Windows' own that takes the same arguments: 0x2d2008 is ResumeThread (it calls NtResumeThread; the
# repository's list names it SuspendThread), 0x2d616a CreateEventA, 0x2d61cb SetEvent (NtSetEvent), 0x2d640c
# WaitForMultipleObjects - the handles Bink waits on all come from our CreateThread or Windows' own calls - and the three
# buffer calls of its DirectSound output the game never makes (xdsound.cpp). And the C library's malloc (0x322e16; no game
# object calls it, so it had no name here): Bink's own allocator (0x36d3b0) takes memory from the callback the game gives
# BinkSetMemory and falls back to malloc when that returns nothing; its free (0x36d420) hands such blocks to free, which is
# already ours, so both must be the same heap (retail's own heap is never set up here). Everything else leads to ours.
BINK_LIB={0x322e16:'_malloc',0x2d2008:'_ResumeThread@4',0x2d616a:'_CreateEventA@16',0x2d61cb:'_SetEvent@4',0x2d640c:'_WaitForMultipleObjects@16',
          0x37b703:'_IDirectSoundBuffer_Stop@4',0x37b777:'_IDirectSoundBuffer_GetCurrentPosition@12',0x37c5c8:'_IDirectSoundBuffer_SetFrequency@8'}
done={int(l.split(',')[0].split()[-1],16) for l in o if l.startswith('  .long 0x') and l.count(',')==2}
for va,name in sorted(BINK_LIB.items()):
    if va in done: continue
    o.append('  .long 0x%08x, "%s", %s'%(va,name,s(name))); npatch+=1
o.append('  .long 0,0,0')
print('library functions patched to ours:',npatch)
# retail's copies of game functions that call the console's Direct3D themselves and that retail code can reach from a
# routed function (found by following retail's direct calls from the routes; stage 8: a thrown grenade's lighting
# sample went object creation -> 0xd30d0 -> 0x1d2f0 -> retail's Direct3D, which was never started). Each gets a jump
# to our build's version: None where retail takes its arguments as the source declares, 'eax' where retail takes its
# one argument in eax and ours takes it in ecx.
BACK={0x1d2f0:None,0x439d0:'eax',
      0x15ca10:None}    # s9e: under the tick, the user interface's update (void, no arguments): it reaches a function with Direct3D written into it (0x1995a0)
o+=['.globl _h2n_game_patches','_h2n_game_patches:']
adapters=[]
for va,how in sorted(BACK.items()):
    name=fm['%08x'%va]['name']
    if how is None: o.append('  .long 0x%x, "%s", %s'%(va,name,s(name)))
    else:
        o.append('  .long 0x%x, GB%x, %s'%(va,va,s(name)))
        adapters+=['GB%x: movl %%eax, %%ecx'%va,'  jmp "%s"'%name]
o.append('  .long 0,0,0')
# stage 9: unproven functions that retail's tables point to: the table entries stay retail's address (pointer_routes.py)
o+=['.globl _h2n_pointer_retail','_h2n_pointer_retail:']; nptr=0
if os.path.exists(NAT+'/routes_pointer.json'):
    for e in json.load(open(NAT+'/routes_pointer.json')):
        if e['va'] in ROUTE and ROUTE[e['va']]=='jmp': continue      # already retail's code whoever calls it
        o.append('  .long 0x%x'%e['va']); nptr+=1
o.append('  .long 0')
print('unproven functions whose table entries stay retail\'s:',nptr)
# s9g: the numbers in retail's code that name a game function, re-aimed at ours like the pointers in the data
# (code_immediates.py has the why: the zoom). Not for functions whose pointers stay retail's (kinds fast1 / fast2).
o+=['.globl _h2n_code_immediates','_h2n_code_immediates:']; nimm=0
if os.path.exists(NAT+'/code_immediates.json'):
    for e in json.load(open(NAT+'/code_immediates.json')):
        if ROUTE.get(e['target']) in ('fast1','fast2'): continue
        o.append('  .long 0x%x, 0x%x, "%s"'%(e['field'],e['target'],e['name'])); nimm+=1
o.append('  .long 0,0,0')
print('function addresses inside retail code re-aimed:',nimm)
# stage 9: every game function of the source with its retail address, for the switches that try a function as retail's
# code or hand one back to the source at start-up (H2N_ROUTE, H2N_BACK, H2N_UNROUTE). The names are those of the last link.
linked=set()
if os.path.exists(NAT+'/out/halo2_native.map'):
    for l in open(NAT+'/out/halo2_native.map',errors='replace'):
        q=l.split()
        if len(q)>=4 and q[0].startswith('0001:'): linked.add(q[1])
o+=['.globl _h2n_all_functions','_h2n_all_functions:']; nall=0
for k,v in sorted(fm.items()):
    if v.get('static') or v['name'] not in linked: continue
    o.append('  .long 0x%s, "%s"'%(k,v['name'])); nall+=1
o.append('  .long 0,0')
print('functions in the address table:',nall)
o+=['LC%d: .asciz "%s"'%(i,t) for i,t in enumerate(strs)]
o+=['.text']+adapters
for va,body in sorted(CUSTOM.items()): o+=['AD%x:'%va,body]
for k in ords: o+=['KT%d: pushl $%d'%(k,k),'  jmp _h2n_kernel_trap_entry']
o+=['_h2n_kernel_trap_entry:','  call _h2n_kernel_trap','  int3']
open(NAT+'/layer/retail_code.S','w').write('\n'.join(o)+'\n')
print('kernel imports:',len(ords),'routes:',len(ROUTE))
