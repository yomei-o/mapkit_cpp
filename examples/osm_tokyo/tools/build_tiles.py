# OSM（Overpass の out geom JSON）を、スリッピーマップのタイル（z/x/y）に切って焼く。
#   python tools/build_tiles.py <osm.json> [<osm2.json> ...] --out docs/tiles --z 13
# 各タイル docs/tiles/<z>/<x>/<y>.txt に、そのタイルに掛かる線（L）とラベル（P）だけを入れる。
# 線はタイル境界でクリップするので、長い道が全タイルに重複しない。ビューアは見える
# タイルだけを読み込む（動的ロード）。出典: © OpenStreetMap contributors (ODbL)
import json, sys, os, math

TOL = 0.00004

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

def deg2tile(lon, lat, z):
    n = 2 ** z
    xt = (lon + 180.0) / 360.0 * n
    yt = (1.0 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2.0 * n
    return xt, yt

def tile_bounds(x, y, z):
    n = 2 ** z
    lon0 = x / n * 360.0 - 180.0; lon1 = (x + 1) / n * 360.0 - 180.0
    latN = math.degrees(math.atan(math.sinh(math.pi * (1 - 2.0 * y / n))))
    latS = math.degrees(math.atan(math.sinh(math.pi * (1 - 2.0 * (y + 1) / n))))
    return lon0, latS, lon1, latN

# Cohen–Sutherland で線分を矩形 [x0,x1]x[y0,y1] にクリップ
def _code(x, y, x0, y0, x1, y1):
    c = 0
    if x < x0: c |= 1
    elif x > x1: c |= 2
    if y < y0: c |= 4
    elif y > y1: c |= 8
    return c

def clip_seg(ax, ay, bx, by, x0, y0, x1, y1):
    ca, cb = _code(ax, ay, x0, y0, x1, y1), _code(bx, by, x0, y0, x1, y1)
    while True:
        if not (ca | cb): return (ax, ay, bx, by)
        if ca & cb: return None
        c = ca or cb
        if c & 8:   x = ax + (bx - ax) * (y1 - ay) / (by - ay); y = y1
        elif c & 4: x = ax + (bx - ax) * (y0 - ay) / (by - ay); y = y0
        elif c & 2: y = ay + (by - ay) * (x1 - ax) / (bx - ax); x = x1
        else:       y = ay + (by - ay) * (x0 - ax) / (bx - ax); x = x0
        if c == ca: ax, ay = x, y; ca = _code(ax, ay, x0, y0, x1, y1)
        else:       bx, by = x, y; cb = _code(bx, by, x0, y0, x1, y1)

# ポリラインを矩形にクリップして、矩形内の連続した部分列のリストを返す
def clip_poly(pts, x0, y0, x1, y1):
    out = []; cur = []
    for i in range(len(pts) - 1):
        ax, ay = pts[i]; bx, by = pts[i + 1]
        r = clip_seg(ax, ay, bx, by, x0, y0, x1, y1)
        if r is None:
            if cur: out.append(cur); cur = []
            continue
        cx0, cy0, cx1, cy1 = r
        if not cur: cur = [(cx0, cy0), (cx1, cy1)]
        else:
            if abs(cur[-1][0] - cx0) > 1e-12 or abs(cur[-1][1] - cy0) > 1e-12:
                out.append(cur); cur = [(cx0, cy0), (cx1, cy1)]
            else: cur.append((cx1, cy1))
    if cur: out.append(cur)
    return out

def main():
    global TOL
    args = sys.argv[1:]
    out_dir = "docs/tiles"; z = 13; srcs = []; coarse = False
    i = 0
    while i < len(args):
        if args[i] == "--out": out_dir = args[i + 1]; i += 2
        elif args[i] == "--z": z = int(args[i + 1]); i += 2
        elif args[i] == "--coarse": coarse = True; i += 1
        else: srcs.append(args[i]); i += 1
    # 粗いタイル（引いたとき用）: 生活道と町丁ラベルを落とし、間引きを強く
    road_ok = (lambda k: k != 2) if coarse else (lambda k: True)
    label_ok = (lambda k: k in (1, 2)) if coarse else (lambda k: True)
    if coarse: TOL = 0.0002
    tiles = {}   # (x,y) -> list of text lines
    def add(x, y, line):
        tiles.setdefault((x, y), []).append(line)
    nways = nlabels = 0
    for src in srcs:
        d = json.load(open(src, encoding="utf-8"))
        for e in d.get("elements", []):
            t = e.get("tags", {})
            if e.get("type") == "way" and "geometry" in e:
                k = road_kind(t)
                if k < 0 or not road_ok(k): continue
                pts = simplify([(g["lon"], g["lat"]) for g in e["geometry"]], TOL)
                if len(pts) < 2: continue
                minlon = min(p[0] for p in pts); maxlon = max(p[0] for p in pts)
                minlat = min(p[1] for p in pts); maxlat = max(p[1] for p in pts)
                x0t, _ = deg2tile(minlon, 0, z); x1t, _ = deg2tile(maxlon, 0, z)
                _, y0t = deg2tile(0, maxlat, z); _, y1t = deg2tile(0, minlat, z)
                for tx in range(int(x0t), int(x1t) + 1):
                    for ty in range(int(y0t), int(y1t) + 1):
                        bx0, by0, bx1, by1 = tile_bounds(tx, ty, z)
                        for seg in clip_poly(pts, bx0, by0, bx1, by1):
                            if len(seg) >= 2:
                                add(tx, ty, "L %d " % k + " ".join("%.5f,%.5f" % (px, py) for px, py in seg))
                nways += 1
            elif e.get("type") == "node":
                k = label_kind(t)
                if k < 0 or not label_ok(k): continue
                name = t.get("name:ja") or t.get("name")
                if not name: continue
                xt, yt = deg2tile(e["lon"], e["lat"], z)
                add(int(xt), int(yt), "P %d %.5f,%.5f %s" % (k, e["lon"], e["lat"], name))
                nlabels += 1
    # 書き出し
    n = 0; total = 0
    xs = []; ys = []
    for (x, y), lines in tiles.items():
        d = os.path.join(out_dir, str(z), str(x)); os.makedirs(d, exist_ok=True)
        p = os.path.join(d, "%d.txt" % y)
        body = "\n".join(lines)
        open(p, "w", encoding="utf-8", newline="\n").write(body)
        total += len(body); n += 1; xs.append(x); ys.append(y)
    # index（ビューアが範囲を知る用）
    os.makedirs(out_dir, exist_ok=True)
    idx = {"z": z, "xmin": min(xs), "xmax": max(xs), "ymin": min(ys), "ymax": max(ys), "count": n}
    open(os.path.join(out_dir, "index.json"), "w", encoding="utf-8", newline="\n").write(json.dumps(idx))
    sys.stderr.write("ways %d, labels %d -> %d tiles, %.1f MB total (z=%d)\n" % (nways, nlabels, n, total / 1048576, z))

if __name__ == "__main__":
    main()
