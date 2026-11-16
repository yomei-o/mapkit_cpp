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
    // 中心（既定=新小岩駅あたり）。argv[7]=lon argv[8]=lat で上書き可
    double clon = argc > 7 ? atof(argv[7]) : 139.8585;
    double clat = argc > 8 ? atof(argv[8]) : 35.7166;
    mk::P2 c = m.project(clon, clat);

    // 中心に赤いピンを立てて描く
    auto pin_shot = [&](const char* name) {
        m.clear();
        lay.draw(m.base, m, st, order);
        labels.draw(m.base, m, font_obj, ls);
        mk::V2 p = m.to_screen(c.x, c.y);
        disc(m.base, p, 18.f, {1.f, 0.30f, 0.30f}, 0.22f);        // 外側のにじみ
        disc(m.base, p, 9.5f, {1.f, 1.f, 1.f}, 1.f);              // 白フチ
        disc(m.base, p, 7.0f, {0.93f, 0.17f, 0.17f}, 1.f);       // 赤い玉
        const uint8_t* px = m.to_rgba();
        std::string f = out + "_" + name + ".png";
        stbi_write_png(f.c_str(), W, H, 4, px, W * 4);
        printf("  %s(pin) -> %s (scale %.3f)\n", name, f.c_str(), m.scale);
    };
    m.cx = c.x; m.cy = c.y;
    m.scale = 0.9;  pin_shot("home");        // 近所（住宅街まで）
    m.cx = c.x; m.cy = c.y;
    m.scale = 0.45; pin_shot("home_wide");   // 少し引き（駅まで入る）

    // 銭湯レイヤの見た目確認（docs/sento.txt があれば、23区全体に光る点を重ねる）
    std::string sento = read_all("docs/sento.txt");
    if (!sento.empty()) {
        std::vector<mk::P2> sp;
        for (const char* p = sento.c_str(); *p; ) {
            char* e; double lon = strtod(p, &e); if (e == p) { while (*p && *p != '\n') ++p; if (*p) ++p; continue; }
            double lat = strtod(e, &e); sp.push_back(m.project(lon, lat));
            while (*e && *e != '\n') ++e; p = *e ? e + 1 : e;
        }
        m.fit(lay.x0, lay.y0, lay.x1, lay.y1);
        m.clear();
        lay.draw(m.base, m, st, order);
        labels.draw(m.base, m, font_obj, ls);
        for (auto& w : sp) { mk::V2 q = m.to_screen(w.x, w.y);
            mk::glow_dot(m.base, q, 9.f, {1.0f, 0.78f, 0.38f}, 0.95f);
            mk::disc(m.base, q, 2.3f, {1.0f, 0.93f, 0.72f}, 1.0f); }
        const uint8_t* px = m.to_rgba(); std::string f = out + "_sento.png";
        stbi_write_png(f.c_str(), W, H, 4, px, W * 4);
        printf("  sento: %d points -> %s\n", (int)sp.size(), f.c_str());
    }

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
