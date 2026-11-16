#!/usr/bin/env python
# OSM の銭湯 (amenity=public_bath) を取得して docs/sento.txt に焼く。
#   python build_sento.py [in.json | --fetch] [out.txt]
# 形式: 各行 "lon lat 名前"（名前は行末まで。空白を含んでよい）
# 足湯 (foot_bath) だけ除外。銭湯・温泉・スーパー銭湯は入れる。
import sys, json, urllib.request, urllib.parse

BBOX = "35.50,139.55,35.82,139.93"   # 東京 23 区あたり
QUERY = ('[out:json][timeout:120];('
         f'node["amenity"="public_bath"]({BBOX});'
         f'way["amenity"="public_bath"]({BBOX});'
         ');out tags center;')
DROP = {'foot_bath'}


def load(src):
    if src == '--fetch':
        req = urllib.request.Request(
            'https://overpass-api.de/api/interpreter',
            data=urllib.parse.urlencode({'data': QUERY}).encode(),
            headers={'User-Agent': 'mapkit_cpp hobby map (github.com/yomei-o)'})
        return json.load(urllib.request.urlopen(req, timeout=180))
    return json.load(open(src, encoding='utf-8'))


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else '--fetch'
    out = sys.argv[2] if len(sys.argv) > 2 else 'docs/sento.txt'
    d = load(src)
    rows = []
    for e in d['elements']:
        t = e.get('tags', {})
        if t.get('bath:type', '') in DROP:
            continue
        if e['type'] == 'node':
            lon, lat = e.get('lon'), e.get('lat')
        else:
            c = e.get('center', {}); lon, lat = c.get('lon'), c.get('lat')
        if lon is None or lat is None:
            continue
        name = (t.get('name') or '銭湯').replace('\n', ' ').replace('\r', ' ').strip()
        name = name.replace('俱', '倶')   # 俱→倶（JISフォントに合わせて異体字を正規化）
        rows.append(f'{lon:.6f} {lat:.6f} {name}')
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(rows) + '\n')
    print(f'wrote {len(rows)} sento -> {out}')


main()
