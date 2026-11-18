#!/bin/sh
# h2native: compile the layer and link the game objects against it, at the retail image's own base address.
set -e
N=/home/claude/native; L=$N/layer2; M=$N/mingw; O=$N/out; B=$L/build
mkdir -p $O $B
Z="python3 -m ziglang"
BUILD_ID="${1:-dev}"
CF="-target x86-windows-gnu -O1 -Wall -Wno-unused-function -DH2N_BUILD=\"$BUILD_ID\""
CSRC="main xapi crt physmem timing dump retail gfx9 gfx_aa gfx_dlss input xsign xiso interp apu apu_dsp xnet xnetx xkernel xcrypt"
CF2="-target x86-windows-gnu -O2 -Wall -Wno-unused-function -DH2N_BUILD=\"$BUILD_ID\""     # the sound runs in real time
# H2N_ANALYSIS=1 ./build2.sh <id>: a build for analysis here only (never sent): it can write the effects processor's state to disk
XDEF=""; [ -n "$H2N_ANALYSIS" ] && { CF2="$CF2 -DH2N_ANALYSIS"; XDEF="/DH2N_ANALYSIS"; }
# H2N_CDEFS="-D..." ./build2.sh <id>: test builds here only (e.g. -DH2N_SHIELD_TEST); never sent
[ -n "$H2N_CDEFS" ] && CF="$CF $H2N_CDEFS"
for c in $CSRC; do
  case $c in apu|apu_dsp) $Z cc $CF2 -c $L/$c.c -o $B/$c.o ;;
  *) $Z cc $CF -c $L/$c.c -o $B/$c.o ;; esac
done
# the Xbox-facing files are compiled by the Xbox SDK's compiler against its headers
export H2N_NOLAUNCHER=1 WINEDEBUG=-all WINEPREFIX=/home/claude/.wine32
XSRC="xd3d xdsound xsave selftest xpad"
for c in $XSRC; do
  (cd $L && INCLUDE='Z:\home\claude\sdk\xbox\include' /home/claude/xdkw/bin/vc71/CL.Exe /nologo /c /O2 /Zl /W3 $XDEF $c.cpp /Fo$c.obj > $B/$c.cl.log 2>&1) || { cat $B/$c.cl.log; exit 1; }
  grep -E "error|warning C4(0|1|7)" $B/$c.cl.log | head -20 || true
  mv $L/$c.obj $B/$c.obj
done
$Z cc -target x86-windows-gnu -O1 -c $L/helpers64.c -o $B/helpers64.o
for s in reserve helpers nettramp; do $Z cc -target x86-windows-gnu $H2N_CDEFS -c $L/$s.S -o $B/$s.o; done
$Z cc -target x86-windows-gnu -c $N/layer/retail_syms.S -o $B/retail_syms.o
$Z cc -target x86-windows-gnu -c $N/layer/retail_tables.S -o $B/retail_tables.o
$Z cc -target x86-windows-gnu -c $N/layer/retail_code.S -o $B/retail_code.o
LAYER_OBJS=""; for c in $CSRC helpers64 helpers nettramp retail_syms retail_tables retail_code; do LAYER_OBJS="$LAYER_OBJS $B/$c.o"; done
for c in $XSRC; do LAYER_OBJS="$LAYER_OBJS $B/$c.obj"; done
# stand-ins for whatever Xbox calls the layer does not define yet
llvm-nm $LAYER_OBJS 2>/dev/null | awk 'NF==3 && $2 ~ /^[TDBR]$/ {print $3}' | sort -u > $B/defined.txt
# (written with the SDK's own prototypes where its headers declare them, so that OUT parameters can be cleared; a
# function whose stand-in does not compile falls back to a plain one)
echo "${H2N_PLAIN_STUBS:-[]}" > $B/stubs_skip.json     # (for experiments: a JSON list of stand-ins to leave plain)
for attempt in 1 2 3 4 5 6; do
  python3 -I /home/claude/scripts/mkstubs3.py $N/unresolved3.json $B/defined.txt $B/stubs.S $B/stubs_typed.cpp $N/stub_returns.json /home/claude/sdk/xbox/include $B/stubs_skip.json
  if (cd $B && INCLUDE='Z:\home\claude\sdk\xbox\include' /home/claude/xdkw/bin/vc71/CL.Exe /nologo /c /O2 /Zl /W3 stubs_typed.cpp /Fostubs_typed.obj > stubs_typed.cl.log 2>&1); then break; fi
  python3 -I - $B <<'PY' || { cat $B/stubs_typed.cl.log | head -20; exit 1; }
import json,re,sys
b=sys.argv[1]; lines=json.load(open(b+'/stubs_typed.cpp.lines.json')); skip=set(json.load(open(b+'/stubs_skip.json'))); n=len(skip)
for l in open(b+'/stubs_typed.cl.log',errors='replace'):
    m=re.search(r'stubs_typed\.cpp\((\d+)\)\s*:\s*(?:fatal )?error',l)
    if m and m.group(1) in lines: skip.add(lines[m.group(1)])
json.dump(sorted(skip),open(b+'/stubs_skip.json','w'))
print('typed stand-ins that do not compile, now plain:',len(skip)); sys.exit(0 if len(skip)>n else 1)
PY
done
$Z cc -target x86-windows-gnu -c $B/stubs.S -o $B/stubs.o
LAYER_OBJS="$LAYER_OBJS $B/stubs_typed.obj"
lld-link -lldmingw -NOLOGO -DEBUG -PDB:$O/halo2_native.pdb -STACK:16777216 -BASE:0x10000 -FIXED -DYNAMICBASE:NO -LARGEADDRESSAWARE \
  -MACHINE:X86 -SUBSYSTEM:WINDOWS -ENTRY:mainCRTStartup -MAP:$O/halo2_native.map -OUT:$O/halo2_native.exe -LIBPATH:$N/layer/lib \
  $B/reserve.o @$N/objects_r.rsp $LAYER_OBJS $B/stubs.o $N/res/h2classic.res \
  -ALTERNATENAME:__image_base__=___ImageBase $M/crt2.obj $M/libmingw32.lib $M/zigc.lib $M/compiler_rt.lib $M/ws2_32.lib \
  $M/api-ms-win-crt-*.lib $M/advapi32.lib $M/kernel32.lib $M/ntdll.lib $M/shell32.lib $M/user32.lib $EXTRA_LIBS > $O/link.log 2>&1 || { grep -v 'ignoring section' $O/link.log | head -40; exit 1; }
grep -v 'ignoring section' $O/link.log | head -5
grep -E " _h2n_retail_range(_end)? " $O/halo2_native.map | awk '{print $2, $3}'
ls -la $O/halo2_native.exe
