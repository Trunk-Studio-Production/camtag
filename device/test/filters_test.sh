#!/bin/sh
# Test host della schermata filtri: compila Canvas + Filters con un SdFat finto e
# li confronta con lo strumento web (filters_fixture.zip, da filters_fixture.js).
#   device/test/filters_test.sh [cartella-per-le-immagini]
set -e
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
unzip -q "$here/filters_fixture.zip" -d "$tmp/fixture"
c++ -std=c++17 -O1 -Wall -Wextra -I "$here/stubs" -I "$here/../src" \
  "$here/../src/Canvas.cpp" "$here/../src/Filters.cpp" "$here/filters_test.cpp" -o "$tmp/filters_test"
"$tmp/filters_test" "$tmp/fixture" ${1:+"$1"}
