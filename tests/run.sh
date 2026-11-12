#!/bin/bash
# Build and run the host tests for the protocol code.
set -euo pipefail
cd "$(dirname "$0")"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -o "$out/test_protocol" test_protocol.cpp
"$out/test_protocol"
