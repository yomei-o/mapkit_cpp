// OSM の東京 23 区（道路・鉄道・水域＋駅/地名ラベル）を mapkit で PNG に描く。
//   osm_tokyo <osm23.txt> <osm23_labels.txt> <font.ttf> <out_prefix> [W H]
//   → <out>_all.png（全体）, <out>_shinkoiwa.png（新小岩駅あたりに寄る）
#include <chrono>
#include <fstream>
#include <sstream>

#include "osm_tokyo.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_truetype.h"
#include "stb_image_write.h"

static std::string read_all(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

int main(int argc, char** argv) {
    std::string base = "examples/osm_tokyo/";
    std::string roads_p = argc > 1 ? argv[1] : base + "data/osm23.txt";
    std::string labels_p = argc > 2 ? argv[2] : base + "data/osm23_labels.txt";
    std::string font_p = argc > 3 ? argv[3] : base + "data/font.ttf";
    std::string out = argc > 4 ? argv[4] : "out/osm";
    int W = argc > 5 ? atoi(argv[5]) : 1500, H = argc > 6 ? atoi(argv[6]) : 1100;

    std::string roads = read_all(roads_p), labtxt = read_all(labels_p), font = read_all(font_p);
    if (roads.empty()) { fprintf(stderr, "cannot read %s\n", roads_p.c_str()); return 1; }
    mk::Map m; m.resize(W, H);
    mk::Layer lay; int nf = lay.load(roads, m);
    mk::Labels labels; int nl = labels.load(labtxt, m);
    mk::Font font_obj; if (!font.empty()) font_obj.load((const unsigned char*)font.data(), font.size());
    auto st = osm_styles(); auto order = osm_order(); auto ls = osm_label_styles();
    printf("features %d, labels %d, font %s\n", nf, nl, font_obj.ok ? "ok" : "none");

    auto shot = [&](const char* name) {
        m.clear();
        lay.draw(m.base, m, st, order);
        labels.draw(m.base, m, font_obj, ls);
        const uint8_t* px = m.to_rgba();
        std::string f = out + "_" + name + ".png";
        stbi_write_png(f.c_str(), W, H, 4, px, W * 4);
        printf("  %s -> %s (scale %.3f)\n", name, f.c_str(), m.scale);
    };
    m.fit(lay.x0, lay.y0, lay.x1, lay.y1);
    shot("all");
    // 新小岩駅あたりに寄る（経度 139.8585, 緯度 35.7166）
    mk::P2 c = m.project(139.8585, 35.7166);
    m.cx = c.x; m.cy = c.y; m.scale = 0.45;
    shot("shinkoiwa");

    // タイル経路のテスト：新小岩の view で、見えるタイルを docs/tiles から読んで描く
    mk::TiledLayer T; T.z = 13; T.cap = 64;
    int nt = 0;
    for (auto& xy : T.view_tiles(m)) {
        std::string tp = "docs/tiles/13/" + std::to_string(xy.first) + "/" + std::to_string(xy.second) + ".txt";
        std::string tt = read_all(tp);
        if (!tt.empty()) { T.add(xy.first, xy.second, tt, m); ++nt; }
    }
    m.clear();
    T.draw(m.base, m, st, order, font_obj, ls);
    { const uint8_t* px = m.to_rgba(); std::string f = out + "_tiled.png"; stbi_write_png(f.c_str(), W, H, 4, px, W * 4);
      printf("  tiled: %d tiles -> %s\n", nt, f.c_str()); }
    return 0;
}
