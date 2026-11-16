# mapkit_cpp — 軽い地図アプリを作るためのヘッダオンリー C++/WASM ライブラリ

地図データ（経緯度の線・面・点）を、**GPU を使わず C++ のソフト描画**で画面に塗る土台です。
metro / tobus / quake / reach で毎回書いていた「投影・パン/ズーム・レイヤ描画・WASM 繋ぎ」をまとめたもの。
WebGL もベクタタイルエンジンも使わないので、数十 KB の wasm で一瞬で開き、古い端末でも軽い。

**デモ（OSM 東京23区ビューア）**: https://yomei-o.github.io/mapkit_cpp/

## ライブラリ（ヘッダ 2 つ）

| ファイル | 中身 |
|---|---|
| `raster.hpp` | ソフト描画：`Canvas`・`polyline`・`disc`・`fill_rings`・`bloom`・`round_rect`・`Font`（線形合成→sRGB） |
| `mapview.hpp` | `Map`（投影＝メルカトル/正距円筒・pan/zoom/fit・to_screen/to_world）、`Layer`（`L <種類> lon,lat …` を投影して種類ごとに描画＋画面外カリング）、`Labels`（`P <種類> lon,lat 名前` を重なり回避で描画） |

使い方（最小）:
```cpp
mk::Map m; m.resize(1200, 800);
mk::Layer lay; lay.load(text, m);          // 線データ
m.fit(lay.x0, lay.y0, lay.x1, lay.y1);
std::vector<mk::Style> st = { {{1,0.8,0.4}, 2, 1}, ... };  // 種類ごとの色・太さ
m.clear(); lay.draw(m.base, m, st); const uint8_t* rgba = m.to_rgba();
```

## 例: OSM 東京23区ビューア（`examples/osm_tokyo/`）

OpenStreetMap の道路・鉄道・水域と、駅名・地名ラベルを表示。ドラッグで移動・ホイール/ピンチで拡大し、
住宅街の細い道や駅（例: 新小岩）まで読めます。

- `tools/build_osm.py` … Overpass で取った OSM を `data/osm23.txt`（線）と `data/osm23_labels.txt`（ラベル）に焼く
- `tools/subset_font.py` … ラベルの文字だけに絞った太字フォント（約 180 KB）
- `main.cpp` … ネイティブで PNG（全体＋新小岩ズーム）
- `wasm/` … ブラウザ用。画面は全部 C++、JS はデータを渡してパン/ズームを渡すだけ

### ビルド
```sh
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release        # → osm_tokyo.exe（確認用 PNG）
sh examples/osm_tokyo/wasm/build.sh         # → docs/osm.js, docs/osm.wasm（Pages 配信）
```
データを取り直すとき: `tools/build_osm.py` の先頭にある Overpass クエリを投げて `osm.json` を作り、焼き直す。

## 出典とライセンス

- コード: BSD-3-Clause（`LICENSE`）
- 地図データ: © OpenStreetMap contributors（ODbL）。`data/osm23*.txt` はそれを加工したもの
- 文字: Noto Sans JP（SIL Open Font License 1.1）
- stb_truetype / stb_image_write: パブリックドメイン（または MIT）
