#!/usr/bin/env bash
# SymphonyStation5 - fetch LLVM 18's x86_64 compiler-rt builtins for macOS hosts.
# Copyright (C) 2026 tsvetomirgt
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The OpenGL link group (tools/prepare-opengl.sh) needs
# libclang_rt.builtins-x86_64.a: target-independent helpers (128-bit
# division, float conversion, emulated TLS) for x86-64 ELF code. Linux clang
# ships it; Homebrew's llvm@18 only has the Darwin runtimes. This takes it from
# Ubuntu's libclang-rt-18-dev package, pinned by SHA-256, into .deps/ and
# prints its path.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
package=libclang-rt-18-dev_18.1.8-17_amd64.deb
url="http://archive.ubuntu.com/ubuntu/pool/universe/l/llvm-toolchain-18/$package"
package_sha256=8755d9f08dae363de1ce0a6466ca8389cb5b36dd5dc002ca7ce05839b4fd9081
builtins_sha256=e0dbac6d2d0fc61333cd86046cc82f4302cd957ea7b4ca6a59e36dcf83a8085d
member=./usr/lib/llvm-18/lib/clang/18/lib/linux/libclang_rt.builtins-x86_64.a

cache="$root/.deps/compiler-rt"
output="$cache/libclang_rt.builtins-x86_64.a"

if [[ -f $output ]] && sha256sum --check --status <<<"$builtins_sha256  $output"; then
    printf '%s\n' "$output"
    exit 0
fi

mkdir -p "$cache"
work=$(mktemp -d "$cache/.extract.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
printf '==> [compiler-rt] Downloading %s\n' "$package" >&2
curl -fsSL --retry 3 -o "$work/$package" "$url"
sha256sum --check --status <<<"$package_sha256  $work/$package" || {
    echo "compiler-rt package checksum mismatch" >&2
    exit 2
}
(cd "$work" && ar x "$package" && tar -xf data.tar.* "$member")
sha256sum --check --status <<<"$builtins_sha256  $work/$member" || {
    echo "compiler-rt builtins checksum mismatch" >&2
    exit 2
}
mv -- "$work/$member" "$output"
printf '%s\n' "$output"
