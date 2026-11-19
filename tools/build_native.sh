#!/bin/sh
# Link the game objects against the h2native layer. Run from anywhere.
set -e
cd /home/claude/native/layer
Z="python3 -m ziglang"
$Z cc -target x86-windows-gnu -O1 -Wall -c layer.c -o layer.o
python3 -I /home/claude/scripts/mkstubs.py /home/claude/native/unresolved2.json stubs.S layer.o | head -1
$Z cc -target x86-windows-gnu -c stubs.S -o stubs.o
$Z cc -target x86-windows-gnu -c helpers.S -o helpers.o
$Z cc -target x86-windows-gnu -O1 -c helpers64.c -o helpers64.o
$Z cc -target x86-windows-gnu -g -o ../halo2_native.exe @/home/claude/native/objects.rsp layer.o stubs.o helpers.o helpers64.o -Llib -lws2_32 > ../link.log 2>&1 || { grep -v 'ignoring section' ../link.log | head -30; exit 1; }
python3 - <<'PY'
import struct
p='/home/claude/native/halo2_native.exe'; d=bytearray(open(p,'rb').read())
pe=struct.unpack_from('<I',d,0x3c)[0]; assert d[pe:pe+4]==b'PE\0\0'
c=struct.unpack_from('<H',d,pe+22)[0]; struct.pack_into('<H',d,pe+22,c|0x20)   # large-address-aware
open(p,'wb').write(d); print('characteristics %04x -> %04x'%(c,c|0x20))
PY
ls -la ../halo2_native.exe
