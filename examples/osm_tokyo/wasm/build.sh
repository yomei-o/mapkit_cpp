#!/bin/sh
# OSM 東京 23 区ビューアの WASM（docs/osm.js, docs/osm.wasm）。EMSDK で emsdk の場所を変えられる。
# Pages はリポジトリ直下の docs/ を配信するので、出力と data は docs/ に置く。
set -e
cd "$(dirname "$0")/../../.."   # リポジトリのルートへ
EMSDK="${EMSDK:-/c/prog/emsdk/emsdk}"
EMCC="$EMSDK/upstream/emscripten/em++.exe"
[ -x "$EMCC" ] || EMCC="$EMSDK/upstream/emscripten/em++"
mkdir -p docs
"$EMCC" -std=c++17 -O3 -msimd128 -DNDEBUG -I. -Iexamples/osm_tokyo -Ithird_party -Ithird_party/nlohmann \
  -s MODULARIZE=1 -s EXPORT_NAME=createOSM -s ALLOW_MEMORY_GROWTH=1 -s STACK_SIZE=4194304 \
  -s EXPORTED_RUNTIME_METHODS='["HEAPU8","UTF8ToString","stringToUTF8","lengthBytesUTF8"]' -s EXPORTED_FUNCTIONS='["_malloc","_free"]' \
  examples/osm_tokyo/wasm/osm_wasm.cpp -o docs/osm.js
cp examples/osm_tokyo/data/osm23.txt docs/osm23.txt
cp examples/osm_tokyo/data/japan.txt docs/japan.txt
cp examples/osm_tokyo/data/osm23_labels.txt docs/osm23_labels.txt
cp examples/osm_tokyo/data/font.ttf docs/font.ttf
echo "built docs/osm.js"
