# OpenStreetMap の東京 23 区を、mapkit が読むコンパクトな線＋ラベルに焼く。
#
#   # 1) Overpass でデータを取る（下の QUERY を overpass-api.de に投げる。User-Agent 必須）
#   curl -s -A "mapkit-cpp/1.0" --data-urlencode "data@query.txt" \
#        https://overpass-api.de/api/interpreter -o osm.json
#   # 2) 焼く
#   python tools/build_osm.py osm.json data/osm23.txt data/osm23_labels.txt
#
# 出力:
#   osm23.txt         L <種類> lon,lat ...   線 kind 0=幹線1=主要道2=生活道3=鉄道4=川5=水域6=海岸
#   osm23_labels.txt  P <種類> lon,lat 名前  ラベル kind 0=駅1=市/町2=suburb3=町丁
# 出典表示: © OpenStreetMap contributors（ODbL）
import json, sys

QUERY = """[out:json][timeout:300];
(
  way["highway"~"^(motorway|trunk|primary|secondary|tertiary|residential|living_street|unclassified)$"](35.50,139.55,35.82,139.93);
  way["railway"="rail"](35.50,139.55,35.82,139.93);
  way["waterway"="river"](35.50,139.55,35.82,139.93);
  way["natural"="water"](35.50,139.55,35.82,139.93);
  way["natural"="coastline"](35.50,139.55,35.82,139.93);
  node["railway"="station"](35.50,139.55,35.82,139.93);
  node["place"~"^(city|town|suburb|neighbourhood|quarter)$"](35.50,139.55,35.82,139.93);
);
out geom;"""

TOL = 0.00004   # 線の間引き（度）。約 4 m

def road_kind(t):
    h = t.get("highway")
    if h in ("motorway", "trunk"): return 0
    if h in ("primary", "secondary"): return 1
    if h in ("tertiary", "residential", "living_street", "unclassified"): return 2
    if t.get("railway") == "rail": return 3
    if t.get("waterway") == "river": return 4
    if t.get("natural") == "water": return 5
    if t.get("natural") == "coastline": return 6
    return -1

def label_kind(t):
    if t.get("railway") == "station": return 0
    p = t.get("place")
    if p in ("city", "town"): return 1
    if p == "suburb": return 2
    if p in ("neighbourhood", "quarter"): return 3
    return -1

def simplify(pts, tol):
    if len(pts) < 3: return pts
    keep = [False] * len(pts); keep[0] = keep[-1] = True; st = [(0, len(pts) - 1)]
    while st:
        a, b = st.pop(); ax, ay = pts[a]; bx, by = pts[b]; dx, dy = bx - ax, by - ay
        L = (dx * dx + dy * dy) ** 0.5; best, bi = -1, -1
        for i in range(a + 1, b):
            px, py = pts[i]; d = abs(dy * (px - ax) - dx * (py - ay)) / L if L > 0 else ((px - ax) ** 2 + (py - ay) ** 2) ** 0.5
            if d > best: best, bi = d, i
        if best > tol: keep[bi] = True; st += [(a, bi), (bi, b)]
    return [p for p, k in zip(pts, keep) if k]

def main(src, out_lines, out_labels):
    d = json.load(open(src, encoding="utf-8"))
    lines, labels = [], []
    for e in d.get("elements", []):
        t = e.get("tags", {})
        if e.get("type") == "way" and "geometry" in e:
            k = road_kind(t)
            if k < 0: continue
            pts = simplify([(g["lon"], g["lat"]) for g in e["geometry"]], TOL)
            if len(pts) >= 2: lines.append((k, pts))
        elif e.get("type") == "node":
            k = label_kind(t)
            if k < 0: continue
            name = t.get("name:ja") or t.get("name")
            if not name: continue
            labels.append((k, e["lon"], e["lat"], name))
    lo = ["# OSM 東京23区の線 © OpenStreetMap contributors (ODbL)。L <kind> lon,lat ...  0幹線1主要2生活道3鉄道4川5水域6海岸"]
    for k, pts in lines:
        lo.append("L %d " % k + " ".join("%.5f,%.5f" % (x, y) for x, y in pts))
    la = ["# OSM 東京23区のラベル © OpenStreetMap contributors。P <kind> lon,lat 名前  0駅1市町2suburb3町丁"]
    for k, lon, lat, name in labels:
        la.append("P %d %.5f,%.5f %s" % (k, lon, lat, name))
    open(out_lines, "w", encoding="utf-8", newline="\n").write("\n".join(lo))
    open(out_labels, "w", encoding="utf-8", newline="\n").write("\n".join(la))
    sys.stderr.write("lines %d, labels %d\n" % (len(lines), len(labels)))

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3])
