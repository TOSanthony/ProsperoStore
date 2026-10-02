#!/usr/bin/env bash
# ProsperoStore - Sanitized store-specific host checks.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/build/store-tests"
mkdir -p "$build"
flags=(-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
objects=()
for name in monocypher/monocypher monocypher/monocypher-ed25519 yyjson/yyjson; do
    object="$build/${name//\//_}.o"
    clang -std=c11 "${flags[@]}" -c "$root/src/third_party/$name.c" -o "$object"
    objects+=("$object")
done
clang++ -std=c++20 "${flags[@]}" -Wall -Wextra -Wpedantic -Werror -I"$root/src" \
    "$root/tests/store_catalog_test.cpp" "$root/src/catalog/catalog.cpp" "${objects[@]}" \
    -o "$build/catalog-test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/catalog-test"
clang++ -std=c++20 "${flags[@]}" -Wall -Wextra -Wpedantic -Werror -I"$root/src" \
    "$root/tests/store_cache_test.cpp" "$root/src/catalog/catalog.cpp" \
    "$root/src/catalog/client.cpp" "$root/src/core/save_file.cpp" "${objects[@]}" \
    -o "$build/cache-test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/cache-test"
clang++ -std=c++20 "${flags[@]}" -Wall -Wextra -Wpedantic -Werror -I"$root/src" \
    "$root/tests/store_http_test.cpp" "$root/src/catalog/catalog.cpp" \
    "$root/src/net/http.cpp" "${objects[@]}" -o "$build/http-test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/http-test"
