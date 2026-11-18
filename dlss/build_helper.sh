#!/bin/sh
# H2Classic: build H2Classic_DLSS.exe, the 64-bit DLSS helper (dlss/h2classic_dlss.c).
# Needs NVIDIA's DLSS SDK (github.com/NVIDIA/DLSS, the NVIDIA RTX SDKs licence): its include folder and
# lib/Windows_x86_64/x64/nvsdk_ngx_d.lib, at $DLSS_SDK (default /home/claude/ext/DLSS). To fetch only those:
#   git clone --depth 1 --filter=blob:none --sparse https://github.com/NVIDIA/DLSS /home/claude/ext/DLSS
#   cd /home/claude/ext/DLSS && git sparse-checkout set --no-cone '/include/*' '/lib/Windows_x86_64/x64/*' '/LICENSE*'
# The library is compiled by Microsoft's compiler for the dynamic C runtime: msvc_shims.c gives what it expects from
# that runtime's static part, the empty libs stand in for its default-library directives, and the rest comes from the
# Windows' own universal C runtime (api-ms-win-crt-*): the helper needs no Visual C++ redistributable.
# Usage: build_helper.sh [out.exe] [-DH2N_DLSS_NO_NGX for a test build without NGX (never sent)]
set -e
cd "$(dirname "$0")"
SDK=${DLSS_SDK:-/home/claude/ext/DLSS}
OUT=${1:-../out/H2Classic_DLSS.exe}
shift 2>/dev/null || true
Z="python3 -m ziglang"
mkdir -p emptylibs
if [ ! -f emptylibs/libMSVCRT.a ]; then
  echo 'int h2n_empty_lib_marker;' > emptylibs/e.c
  $Z cc -target x86_64-windows-gnu -c emptylibs/e.c -o emptylibs/e.o
  for n in msvcprt MSVCRT OLDNAMES uuid; do rm -f emptylibs/lib$n.a; llvm-ar rcs emptylibs/lib$n.a emptylibs/e.o; done
fi
NGXLIB=$SDK/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib
case " $* " in *H2N_DLSS_NO_NGX*) NGXLIB=;; esac
$Z cc -target x86_64-windows-gnu -O2 -Wall -Wno-unused-function -Wno-missing-braces "$@" -I "$SDK/include" -o "$OUT" \
  h2classic_dlss.c msvc_shims.c ../res/h2classic_dlss.rc $NGXLIB -L emptylibs -ld3d11 -ldxgi -lshell32 -ladvapi32 -luser32
echo "built $OUT"
