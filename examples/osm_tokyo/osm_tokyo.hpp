// OSM 東京 23 区デモの共通部分（種類ごとの見た目と描画順）。native も wasm もこれを使う。
// 線 kind: 0=幹線 1=主要道 2=生活道 3=鉄道 4=川 5=水域 6=海岸
#pragma once
#include <vector>
#include "mapview.hpp"

inline std::vector<mk::Style> osm_styles() {
    std::vector<mk::Style> st(7);
    st[0] = {{0.96f, 0.76f, 0.40f}, 2.2f, 1.0f};   // 幹線（金）
    st[1] = {{0.80f, 0.68f, 0.46f}, 1.4f, 0.9f};   // 主要道（薄い金）
    st[2] = {{0.42f, 0.47f, 0.56f}, 1.0f, 0.5f};   // 生活道（灰・細い・多い）
    st[3] = {{1.00f, 0.47f, 0.24f}, 1.2f, 0.9f};   // 鉄道（橙）
    st[4] = {{0.28f, 0.50f, 0.80f}, 1.0f, 0.85f};  // 川（青）
    st[5] = {{0.18f, 0.36f, 0.64f}, 1.0f, 0.8f};   // 水域（濃青）
    st[6] = {{0.30f, 0.64f, 0.80f}, 1.2f, 0.9f};   // 海岸（水色）
    return st;
}
// 水域→川→海岸→生活道→主要道→鉄道→幹線（下から上へ）
inline std::vector<int> osm_order() { return {5, 4, 6, 2, 1, 3, 0}; }

// ラベルの種類: 0=駅 1=市/町 2=区など(suburb) 3=町丁(neighbourhood/quarter)
inline std::vector<mk::LabelStyle> osm_label_styles() {
    std::vector<mk::LabelStyle> s(4);
    s[0] = {12, {1.0f, 0.92f, 0.80f}, 0.09, true, {1.0f, 0.35f, 0.30f}};   // 駅（赤点＋名、少し寄ると）
    s[1] = {16, {0.98f, 0.98f, 1.0f}, 0.0,  false};                        // 市/町（いつでも）
    s[2] = {13, {0.80f, 0.86f, 0.96f}, 0.05, false};                       // suburb
    s[3] = {11, {0.70f, 0.76f, 0.88f}, 0.28, false};                       // 町丁（寄ったときだけ）
    return s;
}
