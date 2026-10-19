#!/bin/sh
# Verlinkt den Pre-commit-Hook des Repos: .git/hooks/pre-commit -> scripts/pre-commit
#
# Aufruf:  scripts/install-hooks.sh [--force]
#   - idempotent: ist der Link schon gesetzt, passiert nichts
#   - ein vorhandener fremder Hook wird nie still ueberschrieben: interaktiv wird
#     nachgefragt, ohne Terminal nur mit --force; in beiden Faellen wird er
#     vorher nach pre-commit.bak.<Zeitstempel> gesichert
#   - Umgehung des Hooks im Einzelfall: git commit --no-verify

set -eu

force=0
case "${1:-}" in
  "") ;;
  --force) force=1 ;;
  -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
  *) echo "Aufruf: $0 [--force]" >&2; exit 2 ;;
esac

top=$(git rev-parse --show-toplevel 2>/dev/null) || {
  echo "install-hooks: nicht in einem Git-Repository" >&2
  exit 1
}
src="$top/scripts/pre-commit"
[ -f "$src" ] || { echo "install-hooks: $src fehlt" >&2; exit 1; }
chmod +x "$src"

if hp=$(git config --get core.hooksPath 2>/dev/null) && [ -n "$hp" ]; then
  echo "install-hooks: WARNUNG - core.hooksPath=$hp ist gesetzt; Git nutzt dann" >&2
  echo "  NICHT .git/hooks. Hook dort selbst einbinden oder core.hooksPath entfernen." >&2
fi

# Hook-Verzeichnis (beruecksichtigt auch Worktrees / abweichendes GIT_DIR)
hooks=$(git rev-parse --git-path hooks)
case "$hooks" in /*) ;; *) hooks="$(pwd)/$hooks" ;; esac
mkdir -p "$hooks"
dst="$hooks/pre-commit"

# Relativer Link, wenn das Hook-Verzeichnis das uebliche <repo>/.git/hooks ist
# (ueberlebt Verschieben des Repos), sonst absolut.
if [ "$(cd "$hooks" && pwd -P)" = "$(cd "$top" && pwd -P)/.git/hooks" ]; then
  link="../../scripts/pre-commit"
else
  link="$src"
fi

if [ -L "$dst" ] && [ "$(readlink "$dst")" = "$link" ]; then
  echo "install-hooks: bereits installiert ($dst -> $link)"
  exit 0
fi

if [ -e "$dst" ] || [ -L "$dst" ]; then
  echo "install-hooks: es existiert bereits ein anderer Hook: $dst"
  if [ -L "$dst" ]; then echo "  (Symlink -> $(readlink "$dst"))"; fi
  if [ "$force" -ne 1 ]; then
    if [ -t 0 ] && [ -t 1 ]; then
      printf "Sichern und durch Link auf scripts/pre-commit ersetzen? [j/N] "
      read -r ans || ans=""
      case "$ans" in j|J|y|Y|ja|Ja|yes) ;; *) echo "abgebrochen, nichts geaendert."; exit 1 ;; esac
    else
      echo "install-hooks: kein Terminal - nichts geaendert (ersetzen mit --force)." >&2
      exit 1
    fi
  fi
  bak="$dst.bak.$(date +%Y%m%d%H%M%S)"
  mv "$dst" "$bak"
  echo "install-hooks: alter Hook gesichert nach $bak"
fi

ln -s "$link" "$dst"
echo "install-hooks: installiert ($dst -> $link)"
