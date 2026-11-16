# ラベル（駅名・地名）と画面の文字だけに絞った太字フォントを作る。
#   python tools/subset_font.py <NotoSansJP[wght].ttf> data/osm23_labels.txt data/font.ttf [extra.txt ...]
#   extra.txt（例 docs/sento.txt）は各行の全文字を取り込む（銭湯名などラベル以外の文字用）。
import sys
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from fontTools import subset
src, labels, out = sys.argv[1:4]
extras = sys.argv[4:]
chars = set(chr(c) for c in range(0x20, 0x7F))
for l in open(labels, encoding="utf-8"):
    if l.startswith("P "):
        t = l.rstrip("\n").split(" ", 3)
        if len(t) >= 4: chars.update(t[3])
for ef in extras:                       # 追加テキスト（sento.txt など）は行の全文字を入れる
    for l in open(ef, encoding="utf-8"):
        chars.update(l.rstrip("\n").rstrip("\r"))
chars.update("ＯＳＭ東京２３区全体幹線主要道生活鉄道川水域海岸地図駅市町丁")
f = instancer.instantiateVariableFont(TTFont(src), {"wght": 700})
opt = subset.Options(); opt.layout_features = []; opt.notdef_outline = True; opt.name_IDs = ["*"]; opt.name_languages = ["*"]; opt.hinting = False
s = subset.Subsetter(opt); s.populate(text="".join(chars)); s.subset(f); f.save(out)
print(len(chars), "chars ->", out)
