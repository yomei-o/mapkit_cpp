// mapview.hpp — 地図アプリの土台（ヘッダオンリー）。投影・パン/ズーム・レイヤ描画。
//
// 使い方:
//   mk::Map m;                  m.resize(1200, 800);
//   mk::Layer lay; lay.load(text, m);          // "L <種類> lon,lat lon,lat ..." を投影して持つ
//   m.fit(lay.x0, lay.y0, lay.x1, lay.y1);
//   std::vector<mk::Style> st = { {color,width,alpha}, ... };   // 種類ごとの見た目
//   m.clear(); lay.draw(m.base, m, st); const uint8_t* rgba = m.to_rgba();
//
// 世界座標 = 投影後の単位（メルカトルなら m）。scale は 1 世界単位あたりの px。
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "raster.hpp"

namespace mk {

#ifndef MK_PI
#define MK_PI 3.14159265358979323846
#endif

struct P2 { double x = 0, y = 0; };
enum Proj { MERCATOR = 0, EQUIRECT = 1 };

struct Map {
    int W = 0, H = 0;
    double ui = 1;                 // 画面 px と描画 px の比（Retina は 2 など）
    double cx = 0, cy = 0;         // 画面中心の世界座標
    double scale = 1;              // 1 世界単位あたりの px
    int proj = MERCATOR;
    double lat0 = 35.7;            // EQUIRECT の基準緯度
    Canvas base;
    std::vector<uint8_t> rgba;
    Rgb bg{0.03f, 0.04f, 0.06f};

    void resize(int w, int h) { W = w; H = h; base.resize(w, h); rgba.assign((size_t)w * h * 4, 255); }

    // 経緯度 → 世界座標（y は北が負）
    P2 project(double lon, double lat) const {
        const double d2r = MK_PI / 180.0;
        if (proj == MERCATOR) {
            const double R = 6378137.0;
            return {R * lon * d2r, -R * std::log(std::tan(MK_PI / 4 + lat * d2r / 2))};
        }
        double kx = 111320.0 * std::cos(lat0 * d2r), ky = 110574.0;
        return {lon * kx, -lat * ky};
    }
    V2 to_screen(double wx, double wy) const { return {(float)((wx - cx) * scale + W / 2.0), (float)((wy - cy) * scale + H / 2.0)}; }
    void to_world(double sx, double sy, double& wx, double& wy) const { wx = cx + (sx - W / 2.0) / scale; wy = cy + (sy - H / 2.0) / scale; }
    // 世界座標 → 経緯度（投影の逆）
    void unproject(double wx, double wy, double& lon, double& lat) const {
        const double d2r = MK_PI / 180.0;
        if (proj == MERCATOR) {
            const double R = 6378137.0;
            lon = wx / R / d2r; lat = (2 * std::atan(std::exp(-wy / R)) - MK_PI / 2) / d2r;
        } else {
            double kx = 111320.0 * std::cos(lat0 * d2r), ky = 110574.0;
            lon = wx / kx; lat = -wy / ky;
        }
    }

    void pan(double dx, double dy) { cx -= dx / scale; cy -= dy / scale; }
    void zoom(double f, double sx, double sy) {
        double wx, wy; to_world(sx, sy, wx, wy);
        scale = std::clamp(scale * f, 1e-9, 1e12);
        double a, b; to_world(sx, sy, a, b);
        cx += wx - a; cy += wy - b;
    }
    void fit(double x0, double y0, double x1, double y1, double margin = 0.92) {
        cx = (x0 + x1) / 2; cy = (y0 + y1) / 2;
        scale = std::min(W / std::max(1e-9, x1 - x0), H / std::max(1e-9, y1 - y0)) * margin;
    }
    void clear() { base.clear(lin(bg)); }
    const uint8_t* to_rgba() { base.to_rgba(rgba.data()); return rgba.data(); }
};

// 線/面の見た目（種類ごと）
struct Style { Rgb color{1, 1, 1}; float width = 1; float alpha = 1; bool add = false; };

// 1 本の線/輪（世界座標）＋外接矩形（画面外カリング用）＋種類
struct Feature {
    int kind = 0;
    std::vector<P2> pts;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

struct Layer {
    std::vector<Feature> feats;
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;   // レイヤ全体の外接矩形

    // "L <種類> lon,lat lon,lat ..." または "ring lon,lat ..."（種類 = ring_kind）を読む。m の投影で世界座標へ
    int load(const std::string& text, const Map& m, int ring_kind = 0) {
        feats.clear(); x0 = y0 = 1e18; x1 = y1 = -1e18;
        std::istringstream in(text); std::string ln;
        while (std::getline(in, ln)) {
            if (ln.empty() || ln[0] == '#') continue;
            std::istringstream s(ln); std::string head; s >> head;
            int kind;
            if (head == "L") { if (!(s >> kind)) continue; }
            else if (head == "ring") { kind = ring_kind; }
            else continue;
            Feature f; f.kind = kind;
            f.x0 = f.y0 = 1e18; f.x1 = f.y1 = -1e18;
            std::string tok;
            while (s >> tok) {
                size_t c = tok.find(','); if (c == std::string::npos) continue;
                P2 w = m.project(std::stod(tok.substr(0, c)), std::stod(tok.substr(c + 1)));
                f.pts.push_back(w);
                f.x0 = std::min(f.x0, w.x); f.y0 = std::min(f.y0, w.y); f.x1 = std::max(f.x1, w.x); f.y1 = std::max(f.y1, w.y);
            }
            if (f.pts.size() < 2) continue;
            x0 = std::min(x0, f.x0); y0 = std::min(y0, f.y0); x1 = std::max(x1, f.x1); y1 = std::max(y1, f.y1);
            feats.push_back(std::move(f));
        }
        return (int)feats.size();
    }

    // 種類ごとの Style で描く。画面外の線は飛ばす。描画順は kinds_order（省略で feats 順）
    void draw(Canvas& cv, const Map& m, const std::vector<Style>& styles, const std::vector<int>& kinds_order = {}) const {
        double wx0, wy0, wx1, wy1; m.to_world(-20, -20, wx0, wy0); m.to_world(m.W + 20, m.H + 20, wx1, wy1);
        auto drawk = [&](int only) {
            for (const Feature& f : feats) {
                if (only >= 0 && f.kind != only) continue;
                if (f.x1 < wx0 || f.x0 > wx1 || f.y1 < wy0 || f.y0 > wy1) continue;
                if (f.kind < 0 || f.kind >= (int)styles.size()) continue;
                const Style& st = styles[f.kind];
                if (st.alpha <= 0) continue;
                std::vector<V2> sp; sp.reserve(f.pts.size());
                for (const P2& p : f.pts) sp.push_back(m.to_screen(p.x, p.y));
                polyline(cv, sp, std::max(1.f, st.width * (float)m.ui), st.color, st.alpha, st.add);
            }
        };
        if (kinds_order.empty()) drawk(-1);
        else for (int k : kinds_order) drawk(k);
    }
};

// ---- ラベル（駅名・地名などの点＋文字）----
struct LabelPt { P2 w; int kind = 0; std::string text; };

struct LabelStyle {
    int px = 12;          // 文字サイズ（ui を掛ける前）
    Rgb color{1, 1, 1};
    double min_scale = 0; // この拡大率以上のときだけ出す（0 = いつでも）
    bool dot = false;     // 点も打つ（駅など）
    Rgb dot_color{1, 0.3f, 0.3f};
};

struct Labels {
    std::vector<LabelPt> pts;

    // "P <kind> lon,lat 名前" を読む（名前は空白を含んでよい＝行末まで）
    int load(const std::string& text, const Map& m) {
        pts.clear();
        std::istringstream in(text); std::string ln;
        while (std::getline(in, ln)) {
            if (ln.empty() || ln[0] == '#') continue;
            std::istringstream s(ln); std::string head; s >> head;
            if (head != "P") continue;
            LabelPt L; std::string coord;
            if (!(s >> L.kind >> coord)) continue;
            size_t c = coord.find(','); if (c == std::string::npos) continue;
            std::getline(s >> std::ws, L.text);
            if (L.text.empty()) continue;
            L.w = m.project(std::stod(coord.substr(0, c)), std::stod(coord.substr(c + 1)));
            pts.push_back(std::move(L));
        }
        return (int)pts.size();
    }

    // 種類ごとの LabelStyle で描く。重なるものは後のを飛ばす（collision）。font は raster.hpp の Font
    void draw(Canvas& cv, const Map& m, Font& font, const std::vector<LabelStyle>& styles) const {
        if (!font.ok) return;
        struct Box { float x0, y0, x1, y1; };
        std::vector<Box> used;
        for (const LabelPt& L : pts) {
            if (L.kind < 0 || L.kind >= (int)styles.size()) continue;
            const LabelStyle& st = styles[L.kind];
            if (m.scale < st.min_scale) continue;
            V2 p = m.to_screen(L.w.x, L.w.y);
            if (p.x < 0 || p.y < 0 || p.x > m.W || p.y > m.H) continue;
            int fp = std::max(8, (int)std::lround(st.px * m.ui));
            float tw = font.width(L.text, fp), th = (float)fp;
            float g = st.dot ? 5.f * (float)m.ui : 0.f;
            float tx = p.x + g, ty = p.y + th * 0.35f;
            Box b{tx - 2, ty - th, tx + tw + 2, ty + 3};
            bool hit = false;
            for (const Box& u : used) if (!(b.x1 < u.x0 || b.x0 > u.x1 || b.y1 < u.y0 || b.y0 > u.y1)) { hit = true; break; }
            if (hit) continue;
            if (st.dot) disc(cv, p, 3.f * (float)m.ui, st.dot_color);
            font.draw(cv, tx, ty, L.text, fp, st.color, {0.02f, 0.03f, 0.05f}, 2.5f);
            used.push_back(b);
        }
    }
};

// ---- タイル地図（z/x/y を動的に読み込む）----
// タイル 1 枚のテキストには L（線）と P（ラベル）が混在。見えているタイルだけ読み、
// 遠いタイルは捨てる（LRU）。ビューアは wanted() の欠けタイルを取ってきて add() する。
struct Tile { Layer lay; Labels lab; long long used = 0; };

struct TiledLayer {
    int z = 13;
    int cap = 24;                 // 一度に読むタイル数の上限（これを超える＝引きすぎ→detail を出さない）
    size_t keep = 64;             // メモリに残すタイル数（LRU）
    long long clock = 0;
    std::map<long long, Tile> tiles;

    static long long key(int x, int y) { return ((long long)x << 20) | (unsigned)y; }

    // いま見えているタイルの (x,y)。引きすぎ（cap 超）なら空。範囲はメルカトルの有効域にクランプ
    std::vector<std::pair<int, int>> view_tiles(const Map& m) const {
        double w0x, w0y, w1x, w1y; m.to_world(0, 0, w0x, w0y); m.to_world(m.W, m.H, w1x, w1y);
        double lonA, latA, lonB, latB; m.unproject(w0x, w0y, lonA, latA); m.unproject(w1x, w1y, lonB, latB);
        double lon0 = std::clamp(std::min(lonA, lonB), -180.0, 180.0), lon1 = std::clamp(std::max(lonA, lonB), -180.0, 180.0);
        double lat0 = std::clamp(std::min(latA, latB), -85.0511, 85.0511), lat1 = std::clamp(std::max(latA, latB), -85.0511, 85.0511);
        double n = std::pow(2.0, z);
        auto xt = [&](double lon) { return std::clamp((int)std::floor((lon + 180.0) / 360.0 * n), 0, (int)n - 1); };
        auto yt = [&](double lat) { return std::clamp((int)std::floor((1.0 - std::asinh(std::tan(lat * MK_PI / 180.0)) / MK_PI) / 2.0 * n), 0, (int)n - 1); };
        int xa = xt(lon0), xb = xt(lon1), ya = yt(lat1), yb = yt(lat0);   // 北(lat1)→小さい y
        if (xb < xa || yb < ya) return {};
        if ((long long)(xb - xa + 1) * (yb - ya + 1) > cap) return {};
        std::vector<std::pair<int, int>> out;
        for (int x = xa; x <= xb; ++x) for (int y = ya; y <= yb; ++y) out.push_back({x, y});
        return out;
    }
    bool has(int x, int y) const { return tiles.count(key(x, y)) != 0; }
    void add(int x, int y, const std::string& text, const Map& m) {
        Tile& t = tiles[key(x, y)];
        t.lay.load(text, m); t.lab.load(text, m); t.used = ++clock;
    }
    void touch(int x, int y) { auto it = tiles.find(key(x, y)); if (it != tiles.end()) it->second.used = ++clock; }
    void evict() {
        while (tiles.size() > keep) {
            auto lru = tiles.begin();
            for (auto it = tiles.begin(); it != tiles.end(); ++it) if (it->second.used < lru->second.used) lru = it;
            tiles.erase(lru);
        }
    }
    void draw(Canvas& cv, const Map& m, const std::vector<Style>& st, const std::vector<int>& order,
              Font& font, const std::vector<LabelStyle>& ls) {
        auto vt = view_tiles(m);
        for (auto& xy : vt) { auto it = tiles.find(key(xy.first, xy.second)); if (it != tiles.end()) { it->second.lay.draw(cv, m, st, order); it->second.used = ++clock; } }
        for (auto& xy : vt) { auto it = tiles.find(key(xy.first, xy.second)); if (it != tiles.end()) it->second.lab.draw(cv, m, font, ls); }
    }
};

}  // namespace mk
