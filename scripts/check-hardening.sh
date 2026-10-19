#!/bin/sh
# Checks ELF binaries for hardening (readelf/objdump/nm only, no checksec).
# Usage: scripts/check-hardening.sh BINARY [BINARY ...]
# Exit 0 = all checks passed, 1 = at least one failed, 2 = usage error.

[ $# -ge 1 ] || { echo "Usage: $0 BINARY [BINARY ...]" >&2; exit 2; }
for t in readelf objdump nm; do
  command -v "$t" >/dev/null 2>&1 || { echo "ERROR: '$t' not found" >&2; exit 2; }
done

rc=0
for bin in "$@"; do
  if [ ! -f "$bin" ]; then echo "ERROR: $bin does not exist" >&2; rc=1; continue; fi
  echo "== Hardening: $bin"
  hdr=$(readelf -h "$bin" 2>&1) || { echo "  ERROR: not an ELF file: $bin" >&2; rc=1; continue; }
  phdr=$(readelf -lW "$bin" 2>&1)
  dyn=$(readelf -dW "$bin" 2>&1)
  syms=$( { nm -D "$bin" 2>/dev/null; nm "$bin" 2>/dev/null; } )

  fail() { echo "  MISSING  $1 - $2"; rc=1; }
  ok()   { echo "  ok     $1"; }

  # PIE: ELF type DYN (not EXEC)
  if echo "$hdr" | grep -Eq 'Type:[[:space:]]+DYN'; then ok "PIE (Type DYN)"
  else fail "PIE" "type is not DYN; build with -fPIE/-pie"; fi

  # Full RELRO: GNU_RELRO + BIND_NOW
  if echo "$phdr" | grep -q 'GNU_RELRO'; then relro=1; else relro=0; fi
  if echo "$dyn" | grep -Eq 'BIND_NOW|FLAGS.*NOW|FLAGS_1.*NOW'; then now=1; else now=0; fi
  if [ $relro -eq 1 ] && [ $now -eq 1 ]; then ok "Full RELRO"
  else fail "Full RELRO" "GNU_RELRO=$relro BIND_NOW=$now; Linker: -Wl,-z,relro,-z,now"; fi

  # NX stack: GNU_STACK without the X flag
  stack=$(echo "$phdr" | grep 'GNU_STACK')
  if [ -z "$stack" ]; then fail "NX-Stack" "no GNU_STACK header (stack possibly executable)"
  elif echo "$stack" | grep -Eq 'RWE|[[:space:]]E[[:space:]]'; then
    fail "NX-Stack" "GNU_STACK is executable; -Wl,-z,noexecstack"
  else ok "NX-Stack"; fi

  # Stack-Protector
  if echo "$syms" | grep -q '__stack_chk_fail'; then ok "Stack-Protector"
  else fail "Stack-Protector" "symbol __stack_chk_fail missing; -fstack-protector-strong"; fi

  # FORTIFY: *_chk symbols (except __stack_chk_*)
  if echo "$syms" | grep -E '_chk(@|[[:space:]]|$)' | grep -v '__stack_chk' | grep -q .; then
    ok "FORTIFY (*_chk symbols)"
  else fail "FORTIFY" "no *_chk symbols; -D_FORTIFY_SOURCE=2 with -O2"; fi
done

if [ $rc -ne 0 ]; then echo "check-hardening: FAILED" >&2; exit 1; fi
echo "check-hardening: all ok"
