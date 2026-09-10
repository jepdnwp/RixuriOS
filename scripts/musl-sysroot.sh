#!/usr/bin/env bash
# Assemble the RixuriOS bootstrap sysroot from the freestanding libc.
#
# This is the Phase 22 musl stepping stone, not a full musl port: it lays
# out the sysroot shape (usr/include, usr/lib, startup objects, ABI
# record) that the musl integration will fill once the Phase 23 dynamic
# loader exists. Contents are copied from user/libc, never fabricated.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CROSS="${CROSS:-x86_64-linux-gnu-}"
CC="${CROSS}gcc"
SYSROOT="$ROOT/build/sysroot"
rm -rf "$SYSROOT"
mkdir -p "$SYSROOT/usr/include" "$SYSROOT/usr/lib" "$SYSROOT/usr/src/rixurios-libc"
cp -r "$ROOT/user/libc/include/." "$SYSROOT/usr/include/"
cp "$ROOT/user/libc/src/libc.c" "$ROOT/user/libc/src/unistd.c" "$SYSROOT/usr/src/rixurios-libc/"
"$CC" -std=c17 -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -m64 \
    -O2 -I"$SYSROOT/usr/include" -c "$ROOT/user/programs/start.S" -o "$SYSROOT/usr/lib/crt0.o"
"$CC" -std=c17 -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -m64 \
    -O2 -I"$SYSROOT/usr/include" -c "$ROOT/user/libc/src/libc.c" -o "$SYSROOT/usr/lib/libc.o"
"$CC" -std=c17 -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -m64 \
    -O2 -I"$SYSROOT/usr/include" -c "$ROOT/user/libc/src/unistd.c" -o "$SYSROOT/usr/lib/libunix.o"
{
    echo "RixuriOS bootstrap sysroot"
    echo "abi_version=$(grep -E 'RIX_SYSCALL_ABI_VERSION' "$ROOT/kernel/syscall/syscall.h" | grep -oE '[0-9]+')"
    echo "arch=x86_64"
    echo "vendor_libc=rixurios-bootstrap (musl port pending Phase 23)"
    echo "dynamic_linker=UNSUPPORTED (no PT_INTERP/PT_TLS until Phase 23)"
    echo "threads=process-local spinlocks only (no kernel thread primitive)"
    echo "tls=UNSUPPORTED (no PT_TLS loader or %fs setup)"
} > "$SYSROOT/usr/lib/ABI.txt"
echo "sysroot: $SYSROOT"
ls "$SYSROOT/usr/include" | head -n 40
cat "$SYSROOT/usr/lib/ABI.txt"
