// OSM 東京ビューアの WASM 入口。地図はタイル（z/x/y）を動的に読み込む。
// 画面は全部 mapkit（C++）で描き、JS は「見えてるのに無いタイル（osm_wanted）」を取ってきて渡すだけ。
// build: sh examples/osm_tokyo/wasm/build.sh → docs/osm.js, docs/osm.wasm
#include "osm_tokyo.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <emscripten/emscripten.h>
#include <cstdlib>
#include <string>

static mk::Map g;
static mk::TiledLayer tD, tC;   // 詳細(z13) と 粗い(z11)。寄ったら詳細、引いたら粗い
static mk::Font font;
static std::vector<mk::Style> st;
static std::vector<int> order;
static std::vector<mk::LabelStyle> ls;
static mk::Layer pref;
static std::vector<mk::Style> pref_st;
static bool here_on = false;
static mk::P2 here_w;
static bool mark_on = false;   // 住所検索で立てる赤ピン（現在地とは別）
static mk::P2 mark_w;
static std::vector<mk::P2> sento_pts;   // 銭湯（光る点）
static bool sento_on = false;
static bool dirty = true;
static std::string g_json;

static void default_view() {   // 23 区のあたり
    mk::P2 a = g.project(139.55, 35.83), b = g.project(139.93, 35.52);
    g.fit(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y));
}

// いま使うタイル層：詳細が cap 内なら詳細、だめなら粗い、どちらも多すぎれば無し（都道府県だけ）
static mk::TiledLayer* active() {
    if (!tD.view_tiles(g).empty()) return &tD;
    if (!tC.view_tiles(g).empty()) return &tC;
    return nullptr;
}

extern "C" {
EMSCRIPTEN_KEEPALIVE int osm_init(const unsigned char* f, int fn) {
    st = osm_styles(); order = osm_order(); ls = osm_label_styles();
    tD.z = 13; tC.z = 11;
    if (f && fn) font.load(f, (size_t)fn);
    default_view();
    dirty = true; return 1;
}
EMSCRIPTEN_KEEPALIVE int osm_pref(const char* text) {
    int n = pref.load(text, g, 0);
    pref_st = {{{0.34f, 0.41f, 0.54f}, 1.0f, 0.6f}};
    dirty = true; return n;
}
EMSCRIPTEN_KEEPALIVE void osm_resize(int w, int h, double ui) { g.ui = ui; g.resize(w, h); dirty = true; }
EMSCRIPTEN_KEEPALIVE void osm_fit() { default_view(); dirty = true; }
EMSCRIPTEN_KEEPALIVE void osm_pan(double dx, double dy) { g.pan(dx, dy); dirty = true; }
EMSCRIPTEN_KEEPALIVE void osm_zoom(double f, double x, double y) { g.zoom(f, x, y); dirty = true; }

// 見えているのにまだ無いタイルを JSON で返す（例: [[13,7278,3224],...]）。z 込み
EMSCRIPTEN_KEEPALIVE const char* osm_wanted() {
    g_json = "[";
    mk::TiledLayer* L = active();
    if (L) {
        bool first = true;
        for (auto& xy : L->view_tiles(g)) {
            if (L->has(xy.first, xy.second)) continue;
            g_json += (first ? "" : ",") + ("[" + std::to_string(L->z) + "," + std::to_string(xy.first) + "," + std::to_string(xy.second) + "]");
            first = false;
        }
    }
    return (g_json += "]").c_str();
}
// タイルのテキストを渡す（z で詳細/粗いに振り分け）
EMSCRIPTEN_KEEPALIVE void osm_tile(int z, int x, int y, const char* text) { (z >= 13 ? tD : tC).add(x, y, text, g); dirty = true; }

EMSCRIPTEN_KEEPALIVE void osm_here(double lat, double lon) { here_w = g.project(lon, lat); here_on = true; dirty = true; }
EMSCRIPTEN_KEEPALIVE void osm_here_off() { here_on = false; dirty = true; }
EMSCRIPTEN_KEEPALIVE int osm_goto_here() { if (!here_on) return 0; g.cx = here_w.x; g.cy = here_w.y; g.scale = 0.45; dirty = true; return 1; }

// 住所検索の赤ピン（現在地の玉とは独立）
EMSCRIPTEN_KEEPALIVE void osm_mark(double lat, double lon) { mark_w = g.project(lon, lat); mark_on = true; dirty = true; }
EMSCRIPTEN_KEEPALIVE void osm_mark_off() { mark_on = false; dirty = true; }
EMSCRIPTEN_KEEPALIVE int osm_goto_mark() { if (!mark_on) return 0; g.cx = mark_w.x; g.cy = mark_w.y; g.scale = 0.9; dirty = true; return 1; }

// 銭湯レイヤ（各行 "lon lat 名前"）を読む。点で光らせる
EMSCRIPTEN_KEEPALIVE int osm_sento(const char* text) {
    sento_pts.clear();
    for (const char* p = text; *p; ) {
        char* end;
        double lon = strtod(p, &end); if (end == p) { while (*p && *p != '\n') ++p; if (*p) ++p; continue; }
        double lat = strtod(end, &end);
        sento_pts.push_back(g.project(lon, lat));
        while (*end && *end != '\n') ++end;      // 行末（名前）まで飛ばす
        p = *end ? end + 1 : end;
    }
    dirty = true; return (int)sento_pts.size();
}
EMSCRIPTEN_KEEPALIVE void osm_sento_toggle(int on) { sento_on = on != 0; dirty = true; }
EMSCRIPTEN_KEEPALIVE int osm_sento_is_on() { return sento_on ? 1 : 0; }

EMSCRIPTEN_KEEPALIVE const uint8_t* osm_render() {
    if (dirty) {
        g.clear();
        if (!pref.feats.empty()) pref.draw(g.base, g, pref_st);
        mk::TiledLayer* L = active();
        if (L) L->draw(g.base, g, st, order, font, ls);
        if (sento_on) {                 // 銭湯＝暖色で光る点
            float u = (float)g.ui, m = 12.f * u;
            for (auto& w : sento_pts) {
                mk::V2 p = g.to_screen(w.x, w.y);
                if (p.x < -m || p.y < -m || p.x > g.W + m || p.y > g.H + m) continue;  // 画面外カリング
                glow_dot(g.base, p, 9.f * u, {1.0f, 0.78f, 0.38f}, 0.95f);   // ふわっとした光
                disc(g.base, p, 2.3f * u, {1.0f, 0.93f, 0.72f}, 1.0f);       // 芯
            }
        }
        if (here_on) { mk::V2 p = g.to_screen(here_w.x, here_w.y);
            disc(g.base, p, 8.f * (float)g.ui, {1.f, 0.3f, 0.3f}, 0.30f);
            disc(g.base, p, 3.5f * (float)g.ui, {1.f, 0.38f, 0.35f}, 1.f); }
        if (mark_on) { mk::V2 p = g.to_screen(mark_w.x, mark_w.y);   // 住所検索ピン
            float u = (float)g.ui;
            disc(g.base, p, 11.f * u, {1.f, 0.30f, 0.30f}, 0.22f);
            disc(g.base, p, 6.5f * u, {1.f, 1.f, 1.f}, 1.f);
            disc(g.base, p, 4.5f * u, {0.93f, 0.17f, 0.17f}, 1.f); }
        tD.evict(); tC.evict();
        dirty = false;
    }
    return g.to_rgba();
}
}
