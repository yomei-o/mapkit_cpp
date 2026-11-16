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
EMSCRIPTEN_KEEPALIVE void osm_resize(int w, int h, double ui) { g.ui = ui; g.resize(w, h); dirty = true; }
EMSCRIPTEN_KEEPALIVE void osm_fit() { g.fit(lay.x0, lay.y0, lay.x1, lay.y1); dirty = true; }
EMSCRIPTEN_KEEPALIVE void osm_pan(double dx, double dy) { g.pan(dx, dy); dirty = true; }
EMSCRIPTEN_KEEPALIVE void osm_zoom(double f, double x, double y) { g.zoom(f, x, y); dirty = true; }
EMSCRIPTEN_KEEPALIVE const uint8_t* osm_render() {
    if (dirty) { g.clear(); lay.draw(g.base, g, st, order); labels.draw(g.base, g, font, ls); dirty = false; }
    return g.to_rgba();
}
}
