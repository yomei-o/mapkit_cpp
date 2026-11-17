# mapkit_cpp — 軽い地図アプリを作るためのヘッダオンリー C++/WASM ライブラリ

地図データ（経緯度の線・面・点）を、**GPU を使わず C++ のソフト描画**で画面に塗る土台です。
metro / tobus / quake / reach で毎回書いていた「投影・パン/ズーム・レイヤ描画・WASM 繋ぎ」をまとめたもの。
WebGL もベクタタイルエンジンも使わないので、数十 KB の wasm で一瞬で開き、古い端末でも軽い。

**デモ（OSM 首都圏ビューア）**: https://yomei-o.github.io/mapkit_cpp/

デモでできること:

- 首都圏（**東京23区＋多摩・川崎・横浜・千葉・埼玉**）の道路・鉄道・川・水域を、寄れば住宅街の細い道まで表示
- **タイルを動的読み込み**（見えている分だけ取る。総量は大きくても1画面は数十KB）
- **現在地**を赤い玉で表示（スマホの位置情報）
- **住所で検索**→その場所へ移動して赤ピン（ジオコーディング）
- **♨ 銭湯レイヤ**：OSM の銭湯を暖色で光らせ、タップで名前を吹き出し表示

画面の描画はすべて C++（WASM）側。JS は「データを取ってきて渡す」「指の操作を渡す」だけです。

---

## ライブラリ（ヘッダ 2 つ）

| ファイル | 中身 |
|---|---|
| `raster.hpp` | ソフト描画：`Canvas`・`polyline`・`disc`・`glow_dot`・`fill_rings`・`bloom`・`round_rect`・`Font`（線形空間で合成→sRGB 出力） |
| `mapview.hpp` | `Map`（投影・pan/zoom/fit・`to_screen`/`to_world`/`project`/`unproject`）、`Layer`（線データ）、`Labels`（文字）、`TiledLayer`（スリッピーマップのタイル） |

### 座標系と投影

`Map` は3つの座標系を行き来します。

```
経緯度(lon,lat) ──project──▶ ワールド(x,y) ──to_screen──▶ 画面(px)
              ◀─unproject──            ◀─to_world──
```

- **投影**: 既定は Web メルカトル（`MERCATOR`）。正距円筒（`EQUIRECT`）も選べる。
- `project(lon,lat)` / `unproject(x,y)` … 経緯度 ⇄ ワールド座標
- `to_screen(x,y)` / `to_world(px,py)` … ワールド ⇄ 画面（`cx,cy,scale` で決まる）
- `pan(dx,dy)` `zoom(f,px,py)` `fit(x0,y0,x1,y1)` … 移動・拡大・範囲合わせ

メルカトルは高緯度ほど縦に伸びるため、`view_tiles` では緯度を **±85.0511°** に、経度を ±180° にクランプしてから計算します（これを忘れると引ききった時にタイル座標が爆発する）。

### 使い方（最小・ネイティブ）

```cpp
mk::Map m; m.resize(1200, 800);
mk::Layer lay; lay.load(text, m);                 // "L <種類> lon,lat lon,lat ..." の行
m.fit(lay.x0, lay.y0, lay.x1, lay.y1);
std::vector<mk::Style> st = { {{1,0.8,0.4}, 2, 1}, /* 種類ごとの色・太さ・最小スケール */ };
m.clear();
lay.draw(m.base, m, st);
const uint8_t* rgba = m.to_rgba();                // W*H*4、そのまま PNG や Canvas へ
```

データ形式（テキスト）:

```
L <種類> lon,lat lon,lat lon,lat ...     ← 折れ線（道路・鉄道・川…）。種類ごとに色/太さ
P <種類> lon,lat 名前                      ← ラベル（駅名・地名・施設名）
```

---

## タイルで地図データを置く仕組み（重要）

首都圏ぜんぶの道路を1つのファイルにすると数十MBになり、開くのに時間がかかります。そこで
**スリッピーマップのタイル**（Google マップ等と同じ z/x/y 方式）に切り分け、**見えている分だけ**読みます。

### タイルの座標（z/x/y）

世界全体を、ズーム段 `z` で縦横 `2^z` 枚の正方形に分割します。各タイルは `(z, x, y)` で表します。

```
x = floor( (lon + 180) / 360 * 2^z )
y = floor( (1 - asinh(tan(lat)) / π) / 2 * 2^z )     ← Web メルカトル
```

`z` が 1 増えると 1 枚が 4 枚に割れ、細かくなります。本デモは **z13（詳しい）と z11（粗い）の2段**を用意。

### 焼き方 — `tools/build_tiles.py`

Overpass の生 JSON を受け取り、各タイルの範囲に掛かる線・ラベルだけを
`docs/tiles/<z>/<x>/<y>.txt` に書き出します。

- **タイル境界でクリップ**（Cohen–Sutherland）。長い道が全タイルに重複して入らないので軽い。
- **間引き**（Douglas–Peucker, `TOL`）で頂点を削減。
- **複数 JSON をまとめて渡せる**（エリアを足すとき、分割取得した json を全部渡す）。
- `--coarse` … 生活道・町丁ラベルを落とし、強めに間引いた**粗いタイル**（z11 用）。

```sh
# 詳しいタイル（z13）
python tools/build_tiles.py dl/*.json --out docs/tiles --z 13
# 粗いタイル（z11・引いたとき用）
python tools/build_tiles.py dl/*.json --out docs/tiles --z 11 --coarse
```

### 読み方（動的ロード）— ビューアと WASM の往復

画面に描くのは C++。JS は「**見えているのにまだ無いタイル**」を取ってきて渡すだけ、という往復をします。

```
(操作で画面が動く)
   │
   ├─ C++  osm_wanted()   → いま見えていて未取得のタイル一覧 JSON  例 [[13,7278,3224],...]
   │
   ├─ JS   fetch tiles/z/x/y.txt            ← 404 は「本当に無い」として記録し二度と取りに行かない
   │                                           503 等は一時エラーとして後で再試行
   │
   └─ C++  osm_tile(z,x,y, text)  → タイルを登録 → 再描画
```

C++ 側（`TiledLayer`）の肝:

- `view_tiles(map)` … 可視タイルを列挙。緯度経度クランプ＋タイル番号クランプ＋**枚数上限（cap）**。引きすぎて枚数が爆発する時は**空を返す**（＝「このズームでこの層は使わない」の合図）。
- `add(x,y,text,map)` / `has(x,y)` … 登録・有無。
- `evict()` … **LRU** で古いタイルを捨ててメモリを一定に保つ。

どの層を使うかは WASM の `active()` が自動で選びます:

```
詳細(z13) が cap 内  → 詳細タイルを描く
だめ(引きすぎ)       → 粗い(z11) タイルを描く
それも多すぎる       → タイルは描かず、47都道府県の外形(japan.txt)だけ
```

### エリアを増やすには

Overpass は residential 込みだと広域1発でタイムアウト（502）するので、**bbox を数セルに分割**して順次取得し、
取れた json を全部 `build_tiles.py` に渡して焼き直すだけ。現在の収録:

```
23区 → +多摩・川崎・横浜 → +千葉・埼玉
= z13 詳細 369 枚 + z11 粗い 38 枚 ≒ 39MB（サーバ側合計）
```

1画面あたりの転送は gzip で数十KB〜200KB 程度（動的ロードなので総量が増えても1画面は軽い）。

---

## 住所から位置を出す（ジオコーディング）

「住所の文字列 → 緯度経度」をジオコーディングと言います。本デモは**国土地理院の検索API**を使います。
**API キー不要・CORS 許可済み**なので、ブラウザから直接 `fetch` できます。

### エンドポイント

```
https://msearch.gsi.go.jp/address-search/AddressSearch?q=<住所>
```

- 返りは GeoJSON の Feature 配列。座標は **`geometry.coordinates = [経度, 緯度]`**（lon が先、lat が後。順番に注意）。
- `properties.title` に正規化後の住所。
- レスポンスヘッダに `Access-Control-Allow-Origin: *` が付くので、GitHub Pages 等の別オリジンからでも叩けます。

### JS から呼ぶ

```js
async function geocode(q){
  const url = 'https://msearch.gsi.go.jp/address-search/AddressSearch?q=' + encodeURIComponent(q);
  const r = await fetch(url);
  if (!r.ok) return null;
  const list = await r.json();
  if (!list.length) return null;
  const [lon, lat] = list[0].geometry.coordinates;   // ← [lon, lat] の順
  return { lon, lat, title: list[0].properties.title };
}

// 地図と繋ぐ：住所→座標→赤ピン＋その場所へ移動
const h = await geocode('東京都葛飾区東新小岩5-15-18');
if (h){ M._osm_mark(h.lat, h.lon); M._osm_goto_mark(); redraw(); }   // osm_mark は (lat, lon) 順
```

### 注意点（実際にハマった所）

- **施設名ではなく住所で引く**。「東京タワー」のような名前を入れると曖昧マッチで全然違う場所（北海道など）を拾う。番地まで入った住所が確実。
- **精度**は丁目・街区レベルは正確、建物ピンポイントはややズレることあり。地図のピンには十分。
- **文字コード**：ブラウザは UTF-8 で問題なし。ただし **Windows のコマンドライン（cmd/PowerShell/Git Bash）経由だと日本語引数が化けて `[]` が返る**ので、ツールで叩くときは UTF-8 のファイルに住所を書いて Python 等から投げる。
- **行儀**：無料の公的APIなので、1文字ごとの自動連打などは避け、ユーザーが確定したときに1回叩く程度に。
- **出典表示**：地図の隅に「住所検索 © 国土地理院」を出しておく。

---

## 現在地・銭湯レイヤ・吹き出し

### 現在地

`navigator.geolocation` で取った緯度経度を `osm_here(lat,lon)` に渡すと、赤い玉が出ます。

- iPhone Safari 対策で `{ enableHighAccuracy:false, timeout:15000, maximumAge:60000 }`（高精度オフ）にしている。高精度＋タイムアウト無しだと屋内で固まって何も返らないことがある。
- `osm_goto_here()` でその位置へ移動。

### 銭湯レイヤ（♨）

- `tools/build_sento.py` が OSM の銭湯（`amenity=public_bath`）を取得し `docs/sento.txt`（`lon lat 名前`）に焼く。**足湯 `foot_bath` だけ除外**、温泉・スーパー銭湯は含む（23区で327件）。
- WASM `osm_sento(text)` で読み込み、`osm_sento_toggle(1)` で表示。`glow_dot`＋`disc` で暖色に光らせ、画面外はカリング。
- **タップで名前を吹き出し**：JS がドラッグ（5px 以上動いた）とタップを区別し、タップ時だけ `osm_pick(sx,sy)` を呼ぶ。C++ 側で最寄りの銭湯を選び、`round_rect`＋`Font::draw` で名前を描く（暖色の枠＋濃紺の地＋白文字、画面端で位置調整、上に入らなければ下に出す）。

---

## 文字（フォント）

ラベルや吹き出しの日本語は `Font`（stb_truetype）で描きます。フォントは **JIS 第1・第2水準をほぼ網羅した
サブセット `NotoSansJP-Bold-jis.ttf`（約6974字 / 2.1MB）**。

- 以前は「データに出てくる文字だけ」の軽量サブセット（約780字）だったが、**データ外の漢字（例「庄」）が □ に化けた**。これは変換の不具合ではなく**フォントがその字を持っていない**だけ。
- JIS 版にしたことで、ラベル名・銭湯名はもちろん、**今後の住所名・施設名でも化けない**。
- もっと軽くしたい場合は `tools/subset_font.py` でデータ駆動サブセットに戻せる（`sento.txt` 等の追加テキストも取り込める）。レアな異体字は `build_sento.py` で正規化（例 `俱`→`倶`）。

---

## キャッシュ対策（更新が端末に届く仕組み）

`osm.js` / `osm.wasm` / `font.ttf` / `sento.txt` はブラウザにキャッシュされます。更新したのに古いのが使われると、
例えば「新しい `index.html` が呼ぶ関数が古い `osm.js` に無い」→ `is not a function` エラーになる。
これを防ぐため、全アセットを **`?v=<ASSET_V>`** 付きで読み、更新時に `index.html` の `ASSET_V` を上げます
（特に **WASM に関数を増やしたら必ず上げる**）。iOS Safari はスーパーリロードでもサブ資源を取り直さないことがあるため、このバージョン付けが効きます。

---

## WASM API（`examples/osm_tokyo/wasm/osm_wasm.cpp`）

`EMSCRIPTEN_KEEPALIVE` で公開。JS からは `M._関数名(...)` で呼ぶ。

| 関数 | 役割 |
|---|---|
| `osm_init(fontPtr, fontLen)` | 初期化（フォント読み込み） |
| `osm_pref(text)` | 都道府県外形（japan.txt）を読む |
| `osm_resize(w,h,ui)` | キャンバスサイズ・DPR |
| `osm_fit()` / `osm_pan(dx,dy)` / `osm_zoom(f,x,y)` | 表示操作 |
| `osm_wanted()` → JSON | 可視で未取得のタイル一覧 `[[z,x,y],...]` |
| `osm_tile(z,x,y,text)` | タイルを登録 |
| `osm_here(lat,lon)` / `osm_here_off()` / `osm_goto_here()` | 現在地 |
| `osm_mark(lat,lon)` / `osm_mark_off()` / `osm_goto_mark()` | 住所検索の赤ピン |
| `osm_sento(text)` / `osm_sento_toggle(on)` / `osm_sento_is_on()` | 銭湯レイヤ |
| `osm_pick(sx,sy)` → 0/1 | タップ位置の最寄り銭湯を選ぶ（吹き出し） |
| `osm_render()` → uint8* | 1 フレーム描いて RGBA を返す |

---

## ツール（`examples/osm_tokyo/tools/`）

| スクリプト | 役割 |
|---|---|
| `build_osm.py` | Overpass で OSM を取り、`osm23.txt`（線）＋`osm23_labels.txt`（ラベル）に焼く（単一ファイル版） |
| `build_tiles.py` | OSM を z/x/y タイルに切ってクリップ・間引き（`--coarse` で粗い層） |
| `build_sento.py` | OSM の銭湯を `docs/sento.txt` に焼く（足湯のみ除外） |
| `subset_font.py` | フォントのサブセット生成（軽量化したい場合・追加テキスト対応） |

Overpass は **User-Agent 必須**（無いと 406）。混雑時は 502/タイムアウトするので bbox を分割＋リトライ。

---

## ビルド

```sh
# ネイティブ（確認用 PNG。exe は VS2022 で）
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release          # → osm_tokyo.exe

# WASM（Pages 配信物 docs/osm.js, docs/osm.wasm）
sh examples/osm_tokyo/wasm/build.sh           # EMSDK で emsdk の場所を指定可
```

Pages はリポジトリ直下の **`docs/`** を配信。`docs/` に `osm.js` `osm.wasm` `index.html` `font.ttf`
`japan.txt` `sento.txt` と `tiles/` を置く。

---

## 出典とライセンス

- コード: BSD-3-Clause（`LICENSE`）
- 地図データ: © OpenStreetMap contributors（ODbL）。`data/osm23*.txt` と `docs/tiles/` はそれを加工したもの
- 住所検索: © 国土地理院（地理院地図 住所検索API）
- 文字: Noto Sans JP（SIL Open Font License 1.1）
- stb_truetype / stb_image_write: パブリックドメイン（または MIT）
