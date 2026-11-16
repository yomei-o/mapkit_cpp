# ラベル（駅名・地名）と画面の文字だけに絞った太字フォントを作る。
#   python tools/subset_font.py <NotoSansJP[wght].ttf> data/osm23_labels.txt data/font.ttf
import sys
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from fontTools import subset
src, labels, out = sys.argv[1:4]
chars = set(chr(c) for c in range(0x20, 0x7F))
for l in open(labels, encoding="utf-8"):
    if l.startswith("P "):
        t = l.rstrip("\n").split(" ", 3)
        if len(t) >= 4: chars.update(t[3])
chars.update("ＯＳＭ東京２３区全体幹線主要道生活鉄道川水域海岸地図駅市町丁")
f = instancer.instantiateVariableFont(TTFont(src), {"wght": 700})
opt = subset.Options(); opt.layout_features = []; opt.notdef_outline = True; opt.name_IDs = ["*"]; opt.name_languages = ["*"]; opt.hinting = False
s = subset.Subsetter(opt); s.populate(text="".join(chars)); s.subset(f); f.save(out)
print(len(chars), "chars ->", out)
