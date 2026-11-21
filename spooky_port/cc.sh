#!/bin/sh
# cc.sh: compile the port's spooky.c the way the build will; errors in /tmp/sg_err.txt
cd /home/claude/native && python3 -m ziglang cc -target x86-windows-gnu -march=i686 -mno-sse -mno-mmx -O2 -Wall ${SGW:--Wno-unused} -ferror-limit=0 -c layer3/sg/spooky.c -o /tmp/sg_spooky.o 2> /tmp/sg_err.txt
echo "errors: $(grep -c ' error: ' /tmp/sg_err.txt)"
