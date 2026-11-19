"""gen_code15.py -> native15/layer/retail_code.S: native/layer/retail_code.S (gen_code.py's, for 1.0) carried over to the
1.5 title update's XBE, table by table:
  code sections and kernel imports: 1.5's own;
  routes: each 1.0 route at its function's 1.5 place (its 1.5 twin has the same body; a route whose function 1.5
    changed is listed and goes to 1.5's function the same way, its callers being byte-identical) - and the changed
    functions our same code calls (boundary15.json / adapt15.json): 'plain' ones jump straight to 1.5's function,
    'adapter' ones go through an adapter written out below (it moves the declared arguments to where 1.5's function
    takes them, keeps ebx, esi, edi and ebp for our callers, and pops what the declaration pops), and the ones whose
    convention is not settled yet jump to a trap that names them (H2Classic stops and says which);
  library patches, game patches, pointer routes, the function addresses inside retail code: at their 1.5 places, each
    checked against 1.5's bytes; the table of every function for H2N_ROUTE / H2N_BACK: 1.5 addresses."""
import re, json, struct, sys, zlib, csv, collections
sys.path.insert(0, '/home/claude/v15'); import declplaces
sys.path.insert(0, '/home/claude/scripts'); sys.path.insert(0, '/home/claude/v15')
from xbeimg import XbeImage
from xmap15_lib import r10, r15
from fmap_lib import mask
x = XbeImage('/home/claude/v15/default_v15.xbe')
src = open('/home/claude/native/layer/retail_code.S').read().split('\n')
M = json.load(open('/home/claude/v15/map15.json'))['funcs']
R = json.load(open('/home/claude/v15/reach15.json'))
A = json.load(open('/home/claude/v15/adapt15.json'))
B = json.load(open('/home/claude/v15/boundary15.json'))
fm = {int(k, 16): v for k, v in json.load(open('/home/claude/native/funcmap.json')).items()}
size = {int(r[0], 16): int(r[1]) for r in csv.reader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv'))
        if r and r[0][:1] in '0123456789abcdef' and r[1].isdigit()}
starts = sorted(size)
import bisect
report = []
def is_same(a10):
    g = M.get('%x' % a10)
    return bool(g and g[0] and (g[1] == 'same' or R.get('%x' % a10) == 'same'))
def f15(a10):
    g = M.get('%x' % a10)
    return g[0] if g and g[0] else None
def inside15(a10):
    """a place inside a function: the same offset in its 1.5 twin, if the function is the same"""
    i = bisect.bisect_right(starts, a10) - 1
    if i < 0: return None
    f = starts[i]
    if a10 - f >= size[f] or not is_same(f): return None
    return f15(f) + (a10 - f)
XON = json.load(open('/home/claude/v15/xonline15.json'))
# (s10p) the call sites settle the conventions (callsite15.py); where they cannot, only a plain one (the declared
# convention itself) or one settled by reading (adapt15.MANUAL) is used - anything else stops with its name
import os
CS = json.load(open('/home/claude/v15/callsite15.json')) if os.path.exists('/home/claude/v15/callsite15.json') else {}
# what each body does with each argument (taint15.py / taintall15.py): taken where one pairing clearly wins - on the 33
# conventions also settled by reading or by call-site constants it agreed every time
TT = json.load(open('/home/claude/v15/taint15.json')) if os.path.exists('/home/claude/v15/taint15.json') else {}
MANUAL_KEYS = {'58ee0', '18f58c'}
# settled by reading 1.5's code and retail's call sites (show15.py), where neither the call sites' constants nor counting can
MANUAL = {
    # (s10p, the split-screen game start) read off the 1.0 call sites, which 1.5's prologues agree with:
    '69470': dict(regs={'edi': 0, 'esi': 1}, stack=[2]),   # world_next_view(world, iterator, out): 1.0 0x689d7 mov edi, [g_4cf77c];
                                                           # lea esi, iterator; push &view. 1.5's push ebx is a save
    '69640': dict(regs={'eax': 0, 'edx': 1}, stack=[2]),   # simulation_world_player_valid(index, world, out): 1.0 0x85d4f
    '69a00': dict(regs={'eax': 2}, stack=[0, 1]),          # (world, mask, input): 1.0 0x6851e push mask, push world, eax = block + 0xc
    '69b50': dict(regs={'eax': 2}, stack=[0, 1]),          # (world, mask, input): 1.0 0x68531 push mask, push world, eax = block + 0x610
    '6a690': dict(regs={'eax': 0, 'edx': 1}, stack=[2]),   # (index, world, value): 1.0 0xa7b73 / 0xa9543 eax index, edx world, push
    '6aa20': dict(regs={'edi': 0}, stack=[1]),             # world_buffer_complete(world, index): 1.0 0x85f1a mov edi, world; push
    '14f040': dict(regs={'eax': 0, 'ebx': 1}, stack=[2]),  # player_get_representation(player, field_b4, third_person): 1.0
                                                           # 0x14f133 push 0 (third_person NULL), lea ebx, local; mov eax, player
    '1662c1': dict(regs={}, stack=[0, 1, 2, 3]),           # first_person_weapon_get_markers_internal: all four pushed (1.0 0x166474)
    '825e0': dict(regs={'edi': 0, 'esi': 1}, stack=[]),    # (activity, action): 1.5 copies [edi] into [esi+4..]; 1.0 0x84a01
    '79a10': dict(regs={'eax': 1}, stack=[0]),             # (observer, index): 1.0 0x79c27 push observer, mov eax, index
    '8a090': dict(regs={}, stack=[0]),                     # entry_table_flush_updates(table): 1.0 0x8e052 push table
    # (s10q) the boundary functions that were still traps, each from the instructions before its retail 1.0 calls (the
    # same in 1.5) and the 1.5 entry (v15tools/trapinfo.py):
    '23654b': dict(regs={'edx': 0}, stack=[1, 2]),         # function_23654b(players, &address, &count): 1.0 0x2364f8 push &count,
                                                           # push &address, lea edx players; ret 8 (New Campaign's start)
    '31590': dict(regs={'edi': 0}, stack=[1]),             # (index, shape): 1.0 0x31999 push shape; mov edi, index
    '5e7f0': dict(regs={'ebx': 0, 'ecx': 1}, stack=[2, 3, 4, 5]),  # (session, address, message, &reason, &has_identity,
                                                           # &identity): 1.0 0x9455d; 1.5 passes ecx on to the address lookup
    '61e00': dict(regs={'eax': 0}, stack=[]),              # (session): 1.0 0x5a0e6 mov eax, esi
    'ce6b0': dict(regs={'ecx': 0, 'eax': 1}, stack=[2, 3]),  # (unit, name, object, scale): 1.0 0xce4f8 push 1.0f, mov eax name,
                                                           # push object, mov ecx unit
    '861c0': dict(regs={'eax': 0}, stack=[]),              # c_simulation_view::update_established: 1.0 0x69e84 mov eax, view
    '8e1e0': dict(regs={}, stack=[]),                      # pending_message_find_free(void) (lea ebx, [ebx] is padding)
    '19b72a': dict(regs={'ebx': 0, 'eax': 1}, stack=[2]),  # c_dialog_screen::set_dialog(id, bool): 1.0 0x19b56a ebx screen,
                                                           # mov eax id, push 0
    '1eb8a0': dict(regs={'esi': 0, 'eax': 1, 'ecx': 2, 'edx': 3}, stack=[4, 5, 6, 7, 8]),  # (state, update, &velocity, &target,
                                                           # ...): 1.0 0x1eb84f al update, edx &target, esi state, ecx &velocity
    '24b971': dict(regs={'esi': 0}, stack=[]),             # s_player_slot_profile::task_succeeded: this stays in esi (1.0 0x24b928)
    'b58c0': dict(regs={}, stack=[0, 1]),                  # (index, flags): 1.0 0x9dc50 push 0x800000, push eax; ret 8
    '8a400': dict(regs={'ecx': 0, 'eax': 1}, stack=[]),    # c_entry_table::function_08a400(K): 1.0 0xa7901 mov eax, edi; mov ecx, table
    'b35d0': dict(regs={'eax': 0}, stack=[]),              # (bool): test al, al at entry, both versions
    '61ac0': dict(regs={}, stack=[0]),                     # (session): 1.0 0x5a0dd push esi
    'd2f90': dict(regs={'ebx': 5}, stack=[0, 1, 2, 3, 4]),  # (send-back) function_d2f90(flags, bitmap, palette, u, v, out): 1.0 0xd1768
                                                           # push v, u, palette, bitmap, 0; lea ebx, &color. (taint15 had ebx = u, a float)
    'd30d0': dict(regs={'eax': 0, 'edi': 4}, stack=[1, 2, 3]),  # (send-back) function_d30d0(tag, flags, u, v, out): 1.0 0xd2065 push v, u,
                                                           # flags; mov eax, tag; lea edi, &out. (taint15 had edi = u, a float)
    'b75a0': dict(regs={'eax': 1, 'ecx': 2, 'edx': 3}, stack=[0, 4, 5]),  # (send-back) function_b75a0(object, point, forward,
             # up, location, bool): 1.0 0xbc262 push 0, push &location, push object; eax = &point, ecx forward, edx up; ret 0xc
    '1060a0': dict(regs={'eax': 0, 'ecx': 1}, stack=[]),   # (weapon, unit): the body looks the weapon up first (1.5 with
                                                           # eax) and keeps the unit (ecx -> edi); 1.0 0x10cb9a, in
                                                           # function_10ca80(object, owner): mov ecx, [owner]; mov eax, esi
    '1c4a80': dict(regs={'eax': 0}, stack=[1, 2]),         # (object, bool, bool): 1.0 0xe2c31 push 0, push 0, mov eax, esi
    'd0870': dict(regs={'edi': 0}, stack=[1, 2]),          # (weapon, unit, secondary): 1.5 reads the unit's definition
                                                           # (+0x158/+0x15c/+0x160) through the first stack slot and the
                                                           # weapon's (+0x284) through edi
    'fff40': dict(regs={'edi': 0}, stack=[1, 2]),          # (weapon, silent, immediate): 1.5 indexes the object table with
                                                           # edi; 1.0 0xe88bc pushes the two flags
    '14be90': dict(regs={'eax': 1}, stack=[0]),            # (player, configuration): 1.5 indexes the players with [ebp+8];
                                                           # 1.0 0x8552b / 0x14c279 set eax to a pointer, push the player
    '22f9bf': dict(regs={'ebx': 0}, stack=[1, 2]),         # (screen, index, definition): 1.5 tests [esp+8]'s flags 0x10
                                                           # (the definition's), reads [ebx+8] (the screen's user flags)
    '22fa7d': dict(regs={}, stack=[0, 1, 2]),              # (screen, index, definition): all three on the stack ([ebp+8],
                                                           # [ebp+0xc], [ebp+0x10]); 1.0 0x22fce0 pushes all three
    '3db00': dict(regs={'eax': 0}, stack=[1]),             # (object, priority): 1.5 indexes the object table with eax;
                                                           # 1.0 0x4ba21 pushes the priority, mov eax, [esi]
    '158090': dict(regs={}, stack=[0, 1]),                 # (player, old team): both on the stack (1.0 0x14beff pushes
                                                           # the team, then the player); 1.5's "push ecx" makes room
    # static functions: our own compile already calls them its own way (the compiler gives a file-local function a
    # convention of its choosing, as link-time code generation does for retail) - read off our callers and our body
    '311940': dict(jmp=True),                              # ours passes first in edi, second in ecx, kind / value / matrix /
                                                           # vector on the stack - as 1.5 takes them: a straight jump
    '658c0': dict(regs={'eax': 0}, stack=[1], cc='__stdcall', decl_pop=8),   # ours takes both on the stack (ret 8); 1.5
                                                           # the session in eax (mov ebx, eax), the member on the stack
    # (s10r) the strict map (map15.py: twins must agree instruction for instruction) made 88 more functions run as 1.5's
    # own code; each convention below was read off a retail 1.0 call and 1.5's entry (v15tools/trapinfo.py, rev.sh),
    # because conv15's live registers err three ways: a "push ecx" that only makes room reads as ecx live; a register
    # __chkstk's callers leave alone reads as live; and taint15 pairs registers with the wrong arguments at times
    '18fb34': dict(regs={}, stack=[0, 1, 2]),              # plain __stdcall: 1.5's push ecx makes room for a local
    '19a78e': dict(regs={'eax': 0}, stack=[1, 2]),         # (controller, a, b): imul eax, 0xc74 at the entry
    '19a7e9': dict(regs={'eax': 0}, stack=[1]),
    '225880': dict(regs={'eax': 0}, stack=[]),
    '225910': dict(regs={'eax': 0}, stack=[]),
    '541a0': dict(regs={}, stack=[0]),                     # plain __stdcall (push ecx: room)
    '5a090': dict(regs={'eax': 0}, stack=[]),
    '5f970': dict(regs={'edi': 0, 'edx': 1}, stack=[]),
    '75970': dict(regs={'esi': 0, 'eax': 1, 'edx': 3, 'ecx': 4}, stack=[2]),   # network_observer_initialize
    '768b0': dict(regs={'ecx': 0, 'eax': 2}, stack=[3]),   # network_observer_channel_ready(observer, owner, channel, x):
    '76930': dict(regs={'ecx': 0, 'eax': 2}, stack=[3]),   # retail drops the owner (never read) - so does the adapter
    '769a0': dict(regs={'eax': 2}, stack=[0]),             # network_observer_channel_timed_out: the same
    '85880': dict(regs={'esi': 0}, stack=[]),
    '85ed0': dict(regs={'esi': 0, 'ecx': 1, 'eax': 3}, stack=[2]),             # c_simulation_view::join_data_receive
    '8d5a0': dict(regs={'ebx': 1}, stack=[0]),             # content_find_first(item, handle out): out in ebx
    '536b0': dict(regs={'eax': 0}, stack=[]),              # voice_port_can_talk: cmp [eax*4 + table] (was ecx, first block)
    '2b1ff4': dict(regs={'edi': 0}, stack=[]),             # c_playlist_saved_game_file_list::fill: mov esi, [edi + 0x70]
    '2b2181': dict(regs={'edi': 2}, stack=[0, 1]),
    '698e0': dict(regs={'esi': 0, 'edx': 1}, stack=[]),    # simulation_world_queue_block(world, block): mov eax, [edx]
    '250cda': dict(regs={'esi': 0, 'eax': 1}, stack=[2]),  # c_session_lobby_screen: mov [esi + edi*4 + 0x816], ax
    '8ae80': dict(regs={'edx': 0, 'ecx': 1}, stack=[2, 3, 4, 5, 6]),           # c_entry_table: lea eax, [edx + (ecx & 0x3ff)*36 + 0x14]
    '8aed0': dict(regs={'ecx': 0, 'eax': 1}, stack=[]),    # c_entry_table: mov dl, [ecx + 5]; and eax, 0x3ff
    # (s10s) a float result in xmm0: 1.5's 0xb49e8 comiss xmm0, xmm1 / movaps xmm0, xmm1 ... ret 0x14, with no x87 load
    # (v15tools: the only border function that returns a float). All five arguments on the stack (ret 0x14).
    'aa4d0': dict(regs={}, stack=[0, 1, 2, 3, 4], fret='xmm0'),
    # send-backs (1.5's code calling ours): the same reading, of the function's own 1.0 body (it is the same in 1.5)
    '1240b0': dict(regs={'esi': 0}, stack=[]),             # mov edi, esi at the entry (ecx is written first)
    '24b6ea': dict(regs={'eax': 0, 'ecx': 1}, stack=[2]),  # s_player_slot_profile::show_dialog(callback, x): this in eax
                                                           # (mov [eax + 0x25c], ecx), the callback in ecx
    'aa260': dict(regs={'eax': 0, 'edx': 1, 'ecx': 2}, stack=[3, 4, 5, 6, 7]), # (selection, object, linear, mode, ...):
                                                           # dec eax / je; mov eax, edi(=edx); test esi(=ecx), esi
    '151ea0': dict(regs={}, stack=[0, 1]),                 # both on the stack (push ecx: room)
    '176780': dict(regs={'edx': 2, 'ebx': 1, 'xmm0': 3}, stack=[0, 4, 5, 6]),  # (tag, owner, object, scale a, scale b,
                                                           # origin, direction): movss [parameters.scale_a], xmm0 - a float
                                                           # argument in a register (link-time code generation with SSE)
    '199a57': dict(regs={}, stack=[]),                     # void (void), leaves by a jump
    '1e04a0': dict(regs={'eax': 0}, stack=[1, 2, 3, 4]),   # (primary, character, unit, secondary, grenade)
    '2238f4': dict(regs={'esi': 0}, stack=[1, 2, 3]),      # (page, context, a, b): mov eax, [ebp + esi*4 - 0x18]
    '28dab0': dict(regs={}, stack=[0, 1]),                 # plain __stdcall (push ecx: room)
}
# (s10r) file-local copies of a boundary function (syms15.py's STATIC_COPY gives each its own name): what our copy's
# callers pass, read off the copy's compiled body. The adapter keeps every register but the result - the compiler knew
# the copy's body when it compiled its callers.
#   unknown_29f5b0.obj's player_slot_get: cmp eax, -1 / imul eax, eax, 0xc70 / add eax, offset g_54e8e0 - the index in eax
STATIC_COPIES = {'?player_slot_geT@@YIPAUs_player_slot_view@@J@Z': dict(of='?player_slot_get@@YIPAUs_player_slot_view@@J@Z', ours={0: 'eax'})}
copies = []
imm_first = [0]
def scan15_immediates():
    """places in 1.5's code that name, as a plain number (cmp / mov / push), a function that is ours in 1.5 - for the
    functions the 1.0 list names this way (scripts/code_immediates.py chose them)"""
    import capstone
    md2 = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); md2.detail = True
    src10 = open('/home/claude/native/layer/retail_code.S').read()
    sec = src10[src10.index('_h2n_code_immediates:'):]
    sec = sec[:sec.index('.long 0,0,0')]
    targets = {}
    for m in re.finditer(r'^  \.long 0x[0-9a-f]+, 0x([0-9a-f]+), "(.*)"$', sec, re.M):
        t10 = int(m.group(1), 16)
        if is_same(t10) and f15(t10): targets[f15(t10)] = m.group(2)
    t = [x for x in _S15 if x['name'] == '.text'][0]
    code = bytes(_D15[t['raw']:t['raw'] + t['rs']]); va0 = t['va']
    starts15 = sorted({v[0] for v in M.values() if v and v[0]})
    res = []
    for t15, nm in sorted(targets.items()):
        pat = struct.pack('<I', t15); q = 0
        while True:
            q = code.find(pat, q)
            if q < 0: break
            va = va0 + q; q += 1
            k = bisect.bisect_right(starts15, va) - 1
            if k < 0: continue
            f = starts15[k]
            if va - f > 0x4000: continue
            for i in md2.disasm(code[f - va0:va - va0 + 16], f):
                if i.address + i.size <= va: continue
                if i.address > va: break
                if i.mnemonic in ('cmp', 'mov', 'push') and i.encoding.imm_size == 4 and i.address + i.encoding.imm_offset == va:
                    res.append((va, t15, nm))
                break
    return res
from capstone import x86 as _x86
_RN = {_x86.X86_REG_EAX: 'eax', _x86.X86_REG_ECX: 'ecx', _x86.X86_REG_EDX: 'edx', _x86.X86_REG_EBX: 'ebx', _x86.X86_REG_ESI: 'esi',
       _x86.X86_REG_EDI: 'edi', _x86.X86_REG_EBP: 'ebp', _x86.X86_REG_AL: 'eax', _x86.X86_REG_CL: 'ecx', _x86.X86_REG_DL: 'edx',
       _x86.X86_REG_BL: 'ebx', _x86.X86_REG_AX: 'eax', _x86.X86_REG_CX: 'ecx', _x86.X86_REG_DX: 'edx'}
def early_regs(a15):
    """the registers 1.5's function reads in its first straight-line block, before writing them (saves aside): what it
    certainly takes on entry - a register the liveness analysis finds only deeper (through a callee's own over-reach)
    is not among them"""
    from xmap15_lib import md as _md
    _md.detail = True
    seen_w = set(); out = set()
    for i in _md.disasm(r15(a15, 160), a15):
        m = i.mnemonic
        if m.startswith(('j', 'call', 'ret', 'loop')): break
        if m == 'push' and i.operands and i.operands[0].type == _x86.X86_OP_REG and _RN.get(i.operands[0].reg) in ('ebx', 'esi', 'edi', 'ebp'): continue
        rd, wr = i.regs_access()
        if m in ('xor', 'sub') and len(i.operands) == 2 and i.operands[0].type == _x86.X86_OP_REG and i.operands[1].type == _x86.X86_OP_REG and i.operands[0].reg == i.operands[1].reg: rd = []
        for r in rd:
            n = _RN.get(r)
            if n and n not in seen_w: out.add(n)
        for r in wr:
            n = _RN.get(r)
            if n: seen_w.add(n)
    return out
def settle(k10, e, forward):
    e = dict(e)
    c = CS.get(k10)
    if c and c['result'] == 'settled' and not c.get('anchors'): c = None
    if not (c and c['result'] == 'settled') and k10 not in MANUAL and TT.get(k10, {}).get('ok'):
        c = dict(result='settled', regs=TT[k10]['regs'], stack=TT[k10]['stack'], anchors=1)
    # a register the derivation set aside as the liveness analysis' over-reach, but which 1.5's function reads in its
    # very first block, is an argument (0x122b40: eax, 'ignored' while ecx was taken for the argument)
    a15e = e.get('a15') or (int(c15key[k10], 16) if k10 in c15key else None)
    if a15e and k10 not in MANUAL and not (c and c['result'] == 'settled'):
        E = early_regs(a15e)
        live = set(e.get('live15') or e.get('live') or [])
        n = e.get('nargs') or 0; pop = e.get('pop')
        if set(e.get('ignored') or []) & E or (e.get('regs') and not set(e['regs']) <= E | {'ecx', 'edx'}):
            e['kind'] = 'trap'
        if pop is not None and pop % 4 == 0 and e.get('cc') != '__cdecl' and n == 1 and pop == 0 and len(E & live) == 1:
            e['regs'] = {next(iter(E & live)): 0}; e['stack'] = []; e['kind'] = 'adapter'; e['how'] = 'first block'
            return e
        if e['kind'] == 'trap': return e
    if c and c['result'] == 'settled' and not c.get('anchors'): c = None      # (a count that worked out is no evidence)
    if k10 in MANUAL and MANUAL[k10].get('jmp'):
        e['kind'] = 'jmp'; return e
    if k10 in MANUAL:
        c = dict(result='settled', **{x: y for x, y in MANUAL[k10].items() if x in ('regs', 'stack')})
        for x in ('cc', 'decl_pop'):
            if x in MANUAL[k10]: e[x] = MANUAL[k10][x]
    if c and c['result'] == 'settled':
        e['regs'] = {r: int(i) for r, i in c['regs'].items()}; e['stack'] = c['stack']
        cc = e.get('cc'); n = e.get('nargs', 0)
        decl = {'ecx': 0} if cc == '__thiscall' else ({'ecx': 0, 'edx': 1} if cc == '__fastcall' else {})
        decl = {r: i for r, i in decl.items() if i < n}
        same = e['regs'] == decl and e['stack'] == [i for i in range(n) if i not in decl.values()] and e['pop'] == (e.get('decl_pop') or 0)
        if not forward: same = same and not e.get('keep')
        e['kind'] = 'plain' if same else 'adapter'; e['how'] = 'call sites'
        return e
    if e.get('kind') == 'plain' or k10 in MANUAL_KEYS: return e
    # counting alone settles it when only one assignment exists: no register at all (everything on the stack, in
    # order), or one argument and one register
    live = [r for r in (e.get('live15') or e.get('live') or []) if r not in e.get('ignored', [])]
    n = e.get('nargs') or 0; pop = e.get('pop')
    if pop is not None and pop % 4 == 0 and e.get('cc') != '__cdecl':
        if not live and pop == 4 * n:
            e['regs'] = {}; e['stack'] = list(range(n)); e['kind'] = 'adapter'; e['how'] = 'counting'
            if not forward and e.get('keep'): pass
            return e
        if len(live) == 1 and n == 1 and pop == 0:
            e['regs'] = {live[0]: 0}; e['stack'] = []; e['kind'] = 'adapter'; e['how'] = 'counting'; return e
    e['kind'] = 'trap'; return e
c15key = {}
A = {k: settle(k, e, True) for k, e in A.items()}
BKA = json.load(open('/home/claude/v15/back15_adapt.json')); backs = []
c15key.update({'%x' % e['a10']: k for k, e in BKA.items()})
BKA = {k: settle('%x' % e['a10'], e, False) if 'cc' in e else dict(e, kind='trap') for k, e in BKA.items()}
from fmap_lib import load as _load
_D15, _S15 = _load('/home/claude/v15/default_v15.xbe'); _D10, _S10 = _load('/home/claude/xbe/default.xbe')
def lib_place(a10):
    """a library entry point the function map has no same twin for: an XOnline thunk paired by the function it jumps
    to (xonline15.json), a place inside a same function, or the one place in 1.5's section with the same 24 bytes"""
    if '%x' % a10 in XON: return XON['%x' % a10]
    if a10 == 0x3303f3: return 0x33a4d3            # GetSystemTimeAsFileTime: jmp [kernel slot] (the slot moved with .rdata)
    p = inside15(a10)
    if p:
        b = r10(a10, 16); q = r15(p, 16)
        if b and q and all((not y) or x == z for x, y, z in zip(b, mask(b), q)): return p
    s10 = [s for s in _S10 if s['va'] <= a10 < s['va'] + s['rs']]
    if not s10: return None
    s15 = [s for s in _S15 if s['name'] == s10[0]['name']]
    if not s15: return None
    b = r10(a10, 24); m = mask(b); sec = _D15[s15[0]['raw']:s15[0]['raw'] + s15[0]['rs']]; hits = []
    lit = bytes(b[:6]) if all(m[:6]) else None
    for i in range(len(sec) - 24):
        if all((not y) or x == sec[i + j] for j, (x, y) in enumerate(zip(b, m))):
            hits.append(s15[0]['va'] + i)
            if len(hits) > 1: return None
    return hits[0] if hits else None
# changed functions whose 1.5 code calls the console's Direct3D itself (back15.py): ours runs instead, for now -
#   0x12560, the game's start-up (rasterizer, fonts, the device's first states): 1.5 differs only in passing the 4:3
#   sizes 0x30 / 0x24 to function_16a30 whatever the video flags say (1.0 passes 0x20 / 0x18 when g_485ac5 and g_485ac6
#   are set, which H2N_WIDE=1 sets) - TODO: the source's 1.5 variant of that line
KEEP_OURS = {0x12560}
out = []; section = None; st = collections.Counter(); lc_next = [10 ** 6]
def lc(text):
    lc_next[0] += 1; extra_strings.append('LC%d: .asciz "%s"' % (lc_next[0], text)); return 'LC%d' % lc_next[0]
extra_strings = []; adapters = []; skip_imports = [False]; new_kt = []
for line in src:
    m = re.match(r'^\.globl (_h2n_\w+)', line)
    if m: section = m.group(1)
    if line.startswith('/* generated by gen_code.py'):
        out.append('/* generated by gen_code15.py from gen_code.py\'s tables (v1.0) and the 1.5 title update\'s XBE */'); continue
    if section == '_h2n_code_sections' and line.startswith('  .long') and not line.startswith('  .long 0,0,0,0'):
        continue                                             # 1.5's own, written at the end of the table
    if section == '_h2n_code_sections' and line.startswith('  .long 0,0,0,0'):
        for s in x.sections:
            if s['va'] >= 0x41b000 or not s['rsize']: continue
            raw = x.x[s['raw']:s['raw'] + s['rsize']]
            out.append('  .long %s, 0x%x, 0x%x, 0x%x' % (lc(s['name']), s['va'], s['rsize'], zlib.crc32(raw) & 0xffffffff))
        out.append(line); continue
    if line.startswith('_h2n_kernel_table:'):
        out.append('_h2n_kernel_table: .long 0x%x' % x.kernel_thunk)
        # the ordinals must be 1.0's, in 1.0's order (the traps below are named by them)
        t = x.kernel_thunk; o15 = []
        while x.u32(t + 4 * len(o15)): o15.append(x.u32(t + 4 * len(o15)) & 0x7fffffff)
        o10 = [int(q) for q in re.findall(r'^  \.long (\d+), LC\d+, KT\d+$', '\n'.join(src), re.M)]
        kn = json.load(open('/home/claude/native/kernel_names.json'))
        if set(o10) - set(o15): report.append('kernel imports 1.5 lacks: %s' % sorted(set(o10) - set(o15)))
        out.append('.globl _h2n_kernel_imports'); out.append('_h2n_kernel_imports:')
        for k in o15: out.append('  .long %d, %s, KT%d' % (k, lc(kn.get(str(k), '?')), k))
        out.append('  .long 0,0,0')
        st['kernel imports'] = len(o15); st['kernel imports new in 1.5'] = len(set(o15) - set(o10)); new_kt.extend(sorted(set(o15) - set(o10)))
        skip_imports[0] = True
        continue
    if skip_imports[0] and section == '_h2n_kernel_imports':
        if line.startswith('  .long 0,0,0'): skip_imports[0] = False
        continue
    if section == '_h2n_kernel_imports' and line.startswith('.globl'): continue
    if section == '_h2n_routes':
        m = re.match(r'^  \.long ("[^"]*"), (0x[0-9a-f]+|AD[0-9a-f]+), (LC\d+), (\d+)$', line)
        if m:
            name, t, l, kind = m.groups()
            if t.startswith('AD'):
                out.append(line); continue
            a10 = int(t, 16); a15 = f15(a10)
            if a15 is None: st['1.0 route without a 1.5 place: left out'] += 1; report.append('route %s %s: no 1.5 place' % (name, t)); continue
            if not is_same(a10): st['1.0 route to a function 1.5 changed'] += 1; report.append('route %s 0x%x -> 0x%x: 1.5 changed it (same callers)' % (name, a10, a15))
            out.append('  .long %s, 0x%x, %s, %s' % (name, a15, l, kind)); st['1.0 routes'] += 1; continue
        if line.startswith('  .long 0,0,0,0'):
            done = {int(q, 16) for q in re.findall(r'^  \.long "[^"]*", (0x[0-9a-f]+)', '\n'.join(src[:]), re.M)}
            linked = set()
            for l2 in open('/home/claude/native/out/halo2_native.map', errors='replace'):
                q2 = l2.split()
                if len(q2) >= 4 and q2[0].startswith('0001:'): linked.add(q2[1])
            for k, e in sorted(A.items(), key=lambda q: int(q[0], 16)):
                a10 = int(k, 16)
                if a10 in done: continue
                if a10 in KEEP_OURS: st['changed but kept ours (calls Direct3D itself)'] += 1; report.append('kept ours: %s' % e['name']); continue
                if e['name'] not in linked: st['boundary not in our link'] += 1; continue
                nm = e['name']; a15 = e['a15']
                if e['kind'] == 'jmp':
                    out.append('  .long "%s", 0x%x, %s, 0x100' % (nm, a15, lc(nm))); st['boundary straight jump (static, ours already calls it 1.5\'s way)'] += 1
                elif e['kind'] == 'plain':
                    # (s10p) through the adapter all the same: it keeps ebx, esi, edi and ebp, which 1.5's own code, compiled
                    # knowing all its callers, need not keep (ours expect them kept)
                    out.append('  .long "%s", AV%x, %s, 0x100' % (nm, a10, lc(nm))); st['boundary plain (adapter keeps registers)'] += 1
                    adapters.append((a10, dict(e, kind='adapter')))
                elif e['kind'] == 'adapter':
                    out.append('  .long "%s", AV%x, %s, 0x100' % (nm, a10, lc(nm))); st['boundary adapter'] += 1
                    adapters.append((a10, e))
                else:
                    out.append('  .long "%s", TV%x, %s, 0x100' % (nm, a10, lc(nm))); st['boundary not settled (trap)'] += 1
                    adapters.append((a10, dict(e, kind='trap')))
                if e['kind'] in ('plain', 'adapter'):
                    for cn, cp in STATIC_COPIES.items():
                        if cp['of'] == nm:
                            out.append('  .long "%s", SV%x, %s, 0x100' % (cn, a10, lc(cn))); st['boundary adapter for a file-local copy'] += 1
                            copies.append((a10, e, cp))
            for k in B:
                a10 = int(k, 16)
                if k in A or a10 in done or R.get(k) == 'same' or a10 not in fm: continue
                nm = fm[a10]['name']
                if nm not in linked: continue
                out.append('  .long "%s", TV%x, %s, 0x100' % (nm, a10, lc(nm))); st['boundary not analysed (trap)'] += 1
                adapters.append((a10, dict(name=nm, kind='trap')))
            out.append(line); continue
    if line.startswith('RA') and ': .long 0x' in line:
        m = re.match(r'^(RA[0-9a-f]+): \.long 0x([0-9a-f]+)$', line); a10 = int(m.group(2), 16)
        out.append('%s: .long 0x%x' % (m.group(1), f15(a10))); continue
    if section == '_h2n_game_patches':
        if line.startswith('_h2n_game_patches:'):
            out.append(line)
            for k, e in sorted(BKA.items(), key=lambda q: int(q[0], 16)):
                a15 = int(k, 16)
                if e['kind'] == 'plain':
                    out.append('  .long 0x%x, BV%x, %s' % (a15, e['a10'], lc(e['name']))); st['send back plain (adapter keeps registers)'] += 1
                    backs.append(dict(e, kind='adapter'))
                elif e['kind'] == 'adapter': out.append('  .long 0x%x, BV%x, %s' % (a15, e['a10'], lc(e['name']))); st['send back adapter'] += 1; backs.append(e)
                else: out.append('  .long 0x%x, BT%x, %s' % (a15, e['a10'], lc(e['name']))); st['send back unresolved (trap)'] += 1; backs.append(dict(e, kind='trap'))
            continue
        if line.startswith('  .long 0x'): continue      # 1.0's list: back15.py's covers it
    if section in ('_h2n_library_patches', '_h2n_game_patches'):
        m = re.match(r'^  \.long 0x([0-9a-f]+), (.*)$', line)
        if m and line != '  .long 0,0,0':
            a10 = int(m.group(1), 16); a15 = f15(a10)
            if a15 is None or not is_same(a10):
                a15 = lib_place(a10)
                if a15: out.append('  .long 0x%08x, %s' % (a15, m.group(2))); st[section + ' (found by its bytes)'] += 1; continue
                st[section + ' left out (no same twin)'] += 1; report.append('%s 0x%x %s: left out' % (section, a10, m.group(2)[:60])); continue
            out.append('  .long 0x%08x, %s' % (a15, m.group(2))); st[section] += 1; continue
    if section == '_h2n_pointer_retail':
        m = re.match(r'^  \.long 0x([0-9a-f]+)$', line)
        if m:
            a10 = int(m.group(1), 16); a15 = f15(a10)
            if a15: out.append('  .long 0x%x' % a15); st['pointer routes'] += 1
            continue
    if section == '_h2n_code_immediates' and line.strip() == '.long 0,0,0':
        # (s10r) and the same in 1.5's own code, wherever it is: the list above only carries a place over when its
        # function is the same in 1.5 - but the places that count are in the functions that run as 1.5's code (0x235d74
        # in the Press Start screen pushes main_menu_open's address: with it left as 1.5's, 1.5's own main menu was
        # built - five items - and ours, with its sixth, walked off its end)
        have = {int(q, 16) for q in re.findall(r'^  \.long 0x([0-9a-f]+), 0x[0-9a-f]+, "', '\n'.join(out[imm_first[0]:]), re.M)}
        for fld, t15, nm in scan15_immediates():
            if fld not in have: out.append('  .long 0x%x, 0x%x, "%s"' % (fld, t15, nm)); st["code immediates found in 1.5's code"] += 1
    if section == '_h2n_code_immediates' and line.startswith('_h2n_code_immediates:'): imm_first[0] = len(out)
    if section == '_h2n_code_immediates':
        m = re.match(r'^  \.long 0x([0-9a-f]+), 0x([0-9a-f]+), (".*")$', line)
        if m:
            fld10, t10 = int(m.group(1), 16), int(m.group(2), 16)
            fld15 = inside15(fld10); t15 = f15(t10)
            w = r15(fld15, 4) if fld15 else None
            if not (fld15 and t15 and w and struct.unpack('<I', w)[0] == t15 and is_same(t10)):
                st['code immediates left out'] += 1; continue
            out.append('  .long 0x%x, 0x%x, %s' % (fld15, t15, m.group(3))); st['code immediates'] += 1; continue
    if section == '_h2n_all_functions':
        m = re.match(r'^  \.long 0x([0-9a-f]+), (".*")$', line)
        if m:
            a15 = f15(int(m.group(1), 16))
            if a15: out.append('  .long 0x%x, %s' % (a15, m.group(2)))
            continue
    if line.startswith('.text'):
        out += extra_strings; extra_strings = []
    out.append(line)
REGS = ('eax', 'ecx', 'edx', 'ebx', 'esi', 'edi', 'ebp')
checks = []
body = []
for a10, e in adapters:
    if e['kind'] == 'trap':
        body += ['TV%x:' % a10, '  pushl $0x%x' % a10, '  call _h2n_v15_unwired', '  int3']; continue
    cc = e['cc']; n = e['nargs']
    # where our callers left argument i (declplaces.py: a float or wider argument of a __fastcall function is on the
    # stack, not in edx); a static function's own convention (MANUAL 'cc') overrides the declaration
    _cc, _pl = declplaces.places(e['name'])
    if e.get('cc') != _cc or not _pl or len(_pl) != n:
        _pl = (['ecx'] if cc == '__thiscall' else []) + [('stk', k) for k in range(n - (1 if cc == '__thiscall' else 0))]
    # frame: push ebp ebx esi edi (16), push ecx edx (8): edx at 0, ecx at 4, the return address at 24, stack args from 28
    def loc(i, pushed):
        p = _pl[i]
        if p == 'edx': return '%d(%%esp)' % (0 + pushed)
        if p == 'ecx': return '%d(%%esp)' % (4 + pushed)
        return '%d(%%esp)' % (28 + 4 * p[1] + pushed)
    b = ['AV%x:' % a10, '  pushl %ebp', '  pushl %ebx', '  pushl %esi', '  pushl %edi', '  pushl %ecx', '  pushl %edx']
    pushed = 0
    for i in reversed(e['stack']):
        b.append('  pushl %s' % loc(i, pushed)); pushed += 4
    for r, i in sorted(e['regs'].items()): b.append('  movl %s, %%%s' % (loc(int(i), pushed), r))
    # (s10p) the stack check: after 1.5's function returns, esp must be where its pop leaves it - a wrong pop in the
    # tables would otherwise surface much later as a return to nowhere
    b.append('  movl %%esp, AE%x' % a10); b.append('  addl $%d, AE%x' % (e['pop'], a10))
    b.append('  call *RV%x' % a10)
    b.append('  cmpl AE%x, %%esp' % a10); b.append('  jne AB%x' % a10)
    checks.append((a10, 'forward'))
    left = pushed - e['pop']
    if left: b.append('  addl $%d, %%esp' % left)
    if MANUAL.get('%x' % a10, {}).get('fret') == 'xmm0':
        # 1.5's function hands its float result back in xmm0 (link-time code generation with SSE); our callers,
        # compiled one file at a time, take a float result from the x87 stack
        b += ['  subl $4, %esp', '  movss %xmm0, (%esp)', '  flds (%esp)', '  addl $4, %esp']
    b += ['  addl $8, %esp', '  popl %edi', '  popl %esi', '  popl %ebx', '  popl %ebp']
    dp = e['decl_pop'] if e['decl_pop'] is not None else 0
    b.append('  ret $%d' % dp if dp else '  ret')
    body += b
for a10, e, cp in copies:
    assert not e['stack'] and all(isinstance(v, str) for v in cp['ours'].values()) and e['pop'] == 0
    b = ['SV%x:' % a10, '  pushl %ebp', '  pushl %ebx', '  pushl %esi', '  pushl %edi', '  pushl %ecx', '  pushl %edx']
    moves = [(cp['ours'][int(i)], r) for r, i in e['regs'].items() if cp['ours'][int(i)] != r]
    assert len(moves) <= 1
    for src, dst in moves: b.append('  movl %%%s, %%%s' % (src, dst))
    b += ['  call *RV%x' % a10, '  popl %edx', '  popl %ecx', '  popl %edi', '  popl %esi', '  popl %ebx', '  popl %ebp', '  ret']
    body += b
for k in new_kt: body += ['KT%d: pushl $%d' % (k, k), '  jmp _h2n_kernel_trap_entry']
# back to ours: retail code calls one of our functions its own way (back15.py / backadapt15.py)
ORDER = ['eax', 'ecx', 'edx', 'ebx', 'esi', 'edi', 'ebp']
for e in backs:
    a10 = e['a10']
    if e['kind'] == 'trap':
        body += ['BT%x:' % a10, '  pushl $0x%x' % a10, '  call _h2n_v15_unwired_back', '  int3']; continue
    cc = e['cc']; n = e['nargs']
    _cc, _pl = declplaces.places(e['name'])
    if _cc != cc or not _pl or len(_pl) != n:
        _pl = (['ecx'] if cc == '__thiscall' else []) + [('stk', k) for k in range(n - (1 if cc == '__thiscall' else 0))]
    regarg = {i: p for i, p in enumerate(_pl) if isinstance(p, str)}
    first_stack = len(regarg)
    where = {int(i): r for r, i in e['regs'].items()}
    for j, i in enumerate(e['stack']): where[int(i)] = j
    b = ['BV%x:' % a10] + ['  pushl %%%s' % r for r in ORDER]
    def loc(i, pushed):
        w = where[i]
        if isinstance(w, str): return '%d(%%esp)' % (4 * (6 - ORDER.index(w)) + pushed)
        return '%d(%%esp)' % (32 + 4 * w + pushed)
    pushed = 0
    for i in sorted((i for i, p in enumerate(_pl) if not isinstance(p, str)), key=lambda i: -_pl[i][1]):
        if where[i] == 'xmm0': b += ['  subl $4, %esp', '  movss %xmm0, (%esp)']      # (a float argument 1.5 passes in xmm0)
        else: b.append('  pushl %s' % loc(i, pushed))
        pushed += 4
    for i, r in regarg.items(): b.append('  movl %s, %%%s' % (loc(i, pushed), r))
    dpop = e['decl_pop'] if e.get('decl_pop') is not None else (0 if cc == '__cdecl' else 4 * sum(1 for p in _pl if not isinstance(p, str)))
    b.append('  movl %%esp, BE%x' % a10); b.append('  addl $%d, BE%x' % (dpop, a10))
    b.append('  call "%s"' % e['name'])
    b.append('  cmpl BE%x, %%esp' % a10); b.append('  jne BB%x' % a10)
    checks.append((a10, 'back'))
    if cc == '__cdecl' and pushed: b.append('  addl $%d, %%esp' % pushed)
    b += ['  popl %ebp', '  popl %edi', '  popl %esi', '  popl %ebx']
    # keep every register 1.5's caller might hold a value in across the call except the result: eax unless the
    # function returns nothing, edx unless it returns 64 bits
    und = e.get('und') or ''
    head = und.split('(')[0]
    # (s10p) the return type is what comes before the calling convention, without access or storage words: "void *"
    # is a pointer (function_1d5e0's send-back restored eax over its result: the DLC map image was copied to the
    # bitmap's own header and the texture cache entry after it)
    rtype = re.split(r'\s__(?:cdecl|stdcall|fastcall|thiscall)\b', ' ' + head)[0].strip()
    rtype = re.sub(r'^(?:(?:public|private|protected):\s*)?(?:(?:static|virtual)\s+)*', '', rtype)
    void = rtype == 'void'
    wide = head.lstrip().startswith(('__int64', 'unsigned __int64')) if und else False   # (edx: a 64-bit result's high half)
    keep = {'ecx'} | ({'edx'} if not wide else set()) | ({'eax'} if void else set())
    for r in ('edx', 'ecx', 'eax'):
        b.append('  popl %%%s' % r if r in keep else '  addl $4, %esp')
    b.append('  ret $%d' % e['pop'] if e['pop'] else '  ret')
    body += b
rv = ['RV%x: .long 0x%x' % (a10, e['a15']) for a10, e in adapters if e['kind'] != 'trap']
for a10, how in checks:
    p = 'AE' if how == 'forward' else 'BE'; q = 'AB' if how == 'forward' else 'BB'
    rv.append('%s%x: .long 0' % (p, a10))
    body += ['%s%x:' % (q, a10), '  pushl %esp', '  pushl $%d' % (how == 'back'), '  pushl $0x%x' % a10, '  call _h2n_v15_bad_pop', '  int3']
text = '\n'.join(out)
text = text.replace('\n.text\n', '\n.data\n' + '\n'.join(rv) + '\n.text\n' + '\n'.join(body) + '\n', 1) if '\n.text\n' in text else text + '\n.text\n' + '\n'.join(body)
open('/home/claude/native15/layer/retail_code.S', 'w').write(text + '\n')
open('/home/claude/v15/gen_code15_report.txt', 'w').write('\n'.join(report) + '\n')
# (s10p) the conventions as settled, for auditing (v15tools/convaudit15.py)
json.dump(dict(forward=[dict(a10=a10, **{x: e.get(x) for x in ('name', 'regs', 'stack', 'pop', 'decl_pop', 'cc', 'nargs', 'kind', 'how')}) for a10, e in adapters],
               back=[{x: e.get(x) for x in ('a10', 'name', 'regs', 'stack', 'pop', 'decl_pop', 'cc', 'nargs', 'kind', 'how')} for e in backs]),
          open('/home/claude/v15/settled15.json', 'w'), indent=0)
print(dict(st))
