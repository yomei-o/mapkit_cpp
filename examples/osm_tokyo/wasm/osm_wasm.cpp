// OSM 東京 23 区ビューアの WASM 入口。画面は全部 mapkit（C++）で描き、JS はデータを渡して
// パン/ズームを渡すだけ。build: sh examples/osm_tokyo/wasm/build.sh → docs/osm.js, docs/osm.wasm
#include "osm_tokyo.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <emscripten/emscripten.h>

static mk::Map g;
static mk::Layer lay;
static mk::Labels labels;
static mk::Font font;
static std::vector<mk::Style> st;
static std::vector<int> order;
static std::vector<mk::LabelStyle> ls;
static bool dirty = true;
static bool here_on = false;       // 現在地（ページが渡す）
static mk::P2 here_w;
static mk::Layer pref;             // 47 都道府県の外形（引いたとき用）
static std::vector<mk::Style> pref_st;

extern "C" {
EMSCRIPTEN_KEEPALIVE int osm_init(const char* roads, const char* labs, const unsigned char* f, int fn) {
    st = osm_styles(); order = osm_order(); ls = osm_label_styles();
    int n = lay.load(roads, g);
    labels.load(labs, g);
    if (f && fn) font.load(f, (size_t)fn);
    g.fit(lay.x0, lay.y0, lay.x1, lay.y1);
    dirty = true;
    return n;
}
// 47 都道府県の外形（data/japan.txt）。戻り値は輪の数
EMSCRIPTEN_KEEPALIVE int osm_pref(const char* text) {
    int n = pref.load(text, g, 0);
    pref_st = {{{0.34f, 0.41f, 0.54f}, 1.0f, 0.6f}};
    dirty = true; return n;
}
EMSCRIPTEN_KEEPALIVE void osm_resize(int w, int h, double ui) { g.ui = ui; g.resize(w, h); dirty = true; }
EMSCRIPTEN_KEEPALIVE void osm_fit() { g.fit(lay.x0, lay.y0, lay.x1, lay.y1); dirty = true; }
EMSCRIPTEN_KEEPALIVE void osm_pan(double dx, double dy) { g.pan(dx, dy); dirty = true; }
EMSCRIPTEN_KEEPALIVE void osm_zoom(double f, double x, double y) { g.zoom(f, x, y); dirty = true; }
// 現在地（緯度・経度）。23区の外でも地図上の位置に玉を出す。osm_here_off で消す
EMSCRIPTEN_KEEPALIVE void osm_here(double lat, double lon) { here_w = g.project(lon, lat); here_on = true; dirty = true; }
EMSCRIPTEN_KEEPALIVE void osm_here_off() { here_on = false; dirty = true; }
// 現在地へ寄る（取れていれば 1）
EMSCRIPTEN_KEEPALIVE int osm_goto_here() { if (!here_on) return 0; g.cx = here_w.x; g.cy = here_w.y; g.scale = 0.45; dirty = true; return 1; }
EMSCRIPTEN_KEEPALIVE const uint8_t* osm_render() {
    if (dirty) {
        g.clear();
        if (!pref.feats.empty()) pref.draw(g.base, g, pref_st);   // 都道府県の外形（下に薄く）
        lay.draw(g.base, g, st, order); labels.draw(g.base, g, font, ls);
        if (here_on) { mk::V2 p = g.to_screen(here_w.x, here_w.y);
            disc(g.base, p, 8.f * (float)g.ui, {1.f, 0.3f, 0.3f}, 0.30f);
            disc(g.base, p, 3.5f * (float)g.ui, {1.f, 0.38f, 0.35f}, 1.f); }
        dirty = false;
    }
    return g.to_rgba();
}
}
