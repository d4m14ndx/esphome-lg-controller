#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/lg-native-tests.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -I. \
  tests/lg_protocol_test.cpp -o "$build_dir/protocol"
"$build_dir/protocol"
# Existing temperature conversion compares int with sizeof; retain the scoped
# suppression here rather than changing unrelated production conversion code.
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Wno-sign-compare -pedantic \
  -Itests/fakes -I. tests/lg_controller_test.cpp -o "$build_dir/controller"
"$build_dir/controller"
