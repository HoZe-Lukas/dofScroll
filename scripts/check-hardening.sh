#!/bin/sh
# Prueft ELF-Binaries auf Hardening (nur readelf/objdump/nm, kein checksec).
# Aufruf: scripts/check-hardening.sh BINARY [BINARY ...]
# Exit 0 = alle Pruefungen bestanden, 1 = mindestens eine fehlgeschlagen, 2 = Aufruffehler.

[ $# -ge 1 ] || { echo "Aufruf: $0 BINARY [BINARY ...]" >&2; exit 2; }
for t in readelf objdump nm; do
  command -v "$t" >/dev/null 2>&1 || { echo "FEHLER: '$t' nicht gefunden" >&2; exit 2; }
done

rc=0
for bin in "$@"; do
  if [ ! -f "$bin" ]; then echo "FEHLER: $bin existiert nicht" >&2; rc=1; continue; fi
  echo "== Hardening: $bin"
  hdr=$(readelf -h "$bin" 2>&1) || { echo "  FEHLER: kein ELF: $bin" >&2; rc=1; continue; }
  phdr=$(readelf -lW "$bin" 2>&1)
  dyn=$(readelf -dW "$bin" 2>&1)
  syms=$( { nm -D "$bin" 2>/dev/null; nm "$bin" 2>/dev/null; } )

  fail() { echo "  FEHLT  $1 - $2"; rc=1; }
  ok()   { echo "  ok     $1"; }

  # PIE: ELF-Typ DYN (nicht EXEC)
  if echo "$hdr" | grep -Eq 'Type:[[:space:]]+DYN'; then ok "PIE (Type DYN)"
  else fail "PIE" "Typ ist nicht DYN; mit -fPIE/-pie bauen"; fi

  # Full RELRO: GNU_RELRO + BIND_NOW
  if echo "$phdr" | grep -q 'GNU_RELRO'; then relro=1; else relro=0; fi
  if echo "$dyn" | grep -Eq 'BIND_NOW|FLAGS.*NOW|FLAGS_1.*NOW'; then now=1; else now=0; fi
  if [ $relro -eq 1 ] && [ $now -eq 1 ]; then ok "Full RELRO"
  else fail "Full RELRO" "GNU_RELRO=$relro BIND_NOW=$now; Linker: -Wl,-z,relro,-z,now"; fi

  # NX-Stack: GNU_STACK ohne X-Flag
  stack=$(echo "$phdr" | grep 'GNU_STACK')
  if [ -z "$stack" ]; then fail "NX-Stack" "kein GNU_STACK-Header (Stack evtl. ausfuehrbar)"
  elif echo "$stack" | grep -Eq 'RWE|[[:space:]]E[[:space:]]'; then
    fail "NX-Stack" "GNU_STACK ist ausfuehrbar; -Wl,-z,noexecstack"
  else ok "NX-Stack"; fi

  # Stack-Protector
  if echo "$syms" | grep -q '__stack_chk_fail'; then ok "Stack-Protector"
  else fail "Stack-Protector" "Symbol __stack_chk_fail fehlt; -fstack-protector-strong"; fi

  # FORTIFY: *_chk-Symbole (ausser __stack_chk_*)
  if echo "$syms" | grep -E '_chk(@|[[:space:]]|$)' | grep -v '__stack_chk' | grep -q .; then
    ok "FORTIFY (*_chk-Symbole)"
  else fail "FORTIFY" "keine *_chk-Symbole; -D_FORTIFY_SOURCE=2 mit -O2"; fi
done

if [ $rc -ne 0 ]; then echo "check-hardening: FEHLGESCHLAGEN" >&2; exit 1; fi
echo "check-hardening: alles ok"
