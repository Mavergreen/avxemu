#!/bin/sh
# platform: host-agnostic -- Darwin and glibc Linux
set -eu
lib=${1:?usage: preload.sh LIB PROGRAM [ARG...]}
shift
case "$(uname -s)" in
  Darwin) exec env DYLD_INSERT_LIBRARIES="$lib" "$@" ;;
  Linux) exec env LD_PRELOAD="$lib" "$@" ;;
  *) echo "preload: unsupported platform" >&2; exit 2 ;;
esac
