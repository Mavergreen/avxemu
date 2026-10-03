#!/bin/sh
# platform: macOS-only -- sw_vers decides which macOS a test is running on
#   usage: run.sh --cpuprobe PROBE [--needs mavericks] [--fails-when COND --because WHY] -- CMD [ARG]...
#          COND is no-avx2, modern-macos, rosetta or always. An unmet --needs exits 77 (ctest's SKIP). With
#          --fails-when, CMD must fail where COND holds and pass where it does not; either
#          surprise fails, so a mark cannot outlive its cause. CMD exiting 77 skips under any mark.
set -eu
probe=""; needs=""; cond=""; because=""
while [ $# -gt 0 ]; do
  case "$1" in
    --cpuprobe) probe=$2; shift 2 ;;
    --needs) needs=$2; shift 2 ;;
    --fails-when) cond=$2; shift 2 ;;
    --because) because=$2; shift 2 ;;
    --) shift; break ;;
    *) echo "run.sh: unknown option $1" >&2; exit 2 ;;
  esac
done
[ $# -gt 0 ] || { echo "run.sh: no command" >&2; exit 2; }
[ -n "$probe" ] || { echo "run.sh: --cpuprobe is required" >&2; exit 2; }
if [ -n "$cond" ] && [ -z "$because" ]; then echo "run.sh: --fails-when needs --because" >&2; exit 2; fi

holds() {
  case "$1" in
    mavericks)
      case "$(sw_vers -productVersion)" in 10.9|10.9.*) return 0 ;; *) return 1 ;; esac ;;
    modern-macos)
      if holds mavericks; then return 1; else return 0; fi ;;
    always) return 0 ;;
    no-avx2)
      feats=$("$probe") || { echo "run.sh: $probe failed" >&2; exit 2; }
      case " $feats " in *" avx2 "*) return 1 ;; *) return 0 ;; esac ;;
    rosetta)
      feats=$("$probe") || { echo "run.sh: $probe failed" >&2; exit 2; }
      case " $feats " in *" translated "*) return 0 ;; *) return 1 ;; esac ;;
    *) echo "run.sh: unknown condition $1" >&2; exit 2 ;;
  esac
}

if [ -n "$needs" ]; then
  if holds "$needs"; then :; else echo "SKIP: needs $needs"; exit 77; fi
fi
[ -n "$cond" ] || exec "$@"

rc=0; "$@" || rc=$?
[ "$rc" -ne 77 ] || exit 77
if holds "$cond"; then
  if [ "$rc" -eq 0 ]; then
    echo "UNEXPECTED PASS where $cond holds: $because -- remove this test's --fails-when mark"
    exit 1
  fi
  echo "KNOWN FAILURE (exit $rc) where $cond holds: $because"
  exit 0
fi
[ "$rc" -eq 0 ] || echo "FAILED (exit $rc) where $cond does not hold -- not the known failure"
exit "$rc"
