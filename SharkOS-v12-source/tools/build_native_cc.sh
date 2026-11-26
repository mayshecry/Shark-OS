#!/bin/sh
set -eu
sha256sum -c vendor/SHA256SUMS
root=$(pwd)
d="$root/cc-build"
mkdir -p "$d/musl" "$d/tcc"
if [ ! -e "$d/lib/libc.a" ] || [ vendor/musl.tar.gz -nt "$d/lib/libc.a" ]; then
    tar xf vendor/musl.tar.gz -C "$d/musl" --strip-components=1
    (cd "$d/musl" && ./configure --disable-shared --prefix="$d" CC="$CC" CFLAGS='-O2 -mno-red-zone -fno-pie -fno-stack-protector -ffunction-sections -fdata-sections' && make -s lib/libc.a && make -s install-headers && mkdir -p "$d/lib" && cp lib/libc.a "$d/lib/") > "$d/musl.log"
fi
tar xf vendor/tinycc.tar.bz2 -C "$d/tcc"
sed -i '/^#define CONFIG_TCC_SEMLOCK$/d; /#if defined TCC_IS_NATIVE \&\& !defined CONFIG_TCCBOOT/i #undef TCC_IS_NATIVE' "$d/tcc/tcc.h"
sed -i 's/sscanf(TCC_VERSION, "%d.%d.%d", \&a, \&b, \&c);/a=0; b=9; c=27;/' "$d/tcc/libtcc.c"
printf '%s\n' '#define TCC_VERSION "0.9.27"' '#define CONFIG_TCC_SYSINCLUDEPATHS "/System/Include"' > "$d/tcc/config.h"
for f in compiler port; do
    "$CC" -O2 -m64 -mno-red-zone -fno-pie -fno-stack-protector -ffreestanding -ffunction-sections -fdata-sections -Dfwrite=cc_write -DCONFIG_TCC_STATIC -DNDEBUG -nostdinc -isystem "$d/include" -I"$d/tcc" -c "src/cc/$f.c" -o "$d/$f.o"
done
ld -r -T src/cc/sections.ld --gc-sections -u compile "$d/compiler.o" "$d/port.o" "$d/lib/libc.a" -o "$d/core.o"
test -z "$(nm -u "$d/core.o")"
if objdump -d "$d/core.o" | grep -Eq '(%fs:|%gs:|[[:space:]]syscall([[:space:]]|$))'; then echo 'unexpected hosted dependency in compiler' >&2; exit 1; fi
objcopy --prefix-symbols=sharkcc_ "$d/core.o" "$d/native.o"
