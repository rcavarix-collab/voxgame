import sys
OUT=sys.argv[1] if len(sys.argv)>1 else "out3"
from PIL import Image, ImageDraw, ImageFont
st={}
for l in open(OUT+"/stats3.txt"):
    p=l.split(); st[p[0]]=float(p[1].split("=")[1])
base=13592.0
names=[("kuhn","Faceted, cube grid"),("nets","Even facets (1 m)"),("netsj","Even facets, hand-cut"),("nets15j","Hand-cut, 1.5 m"),("nets2","Large facets (2 m)"),("nets2j","Large facets, hand-cut")]
font=ImageFont.load_default(size=22); small=ImageFont.load_default(size=17)
for view in ["overview","cockpit","crater"]:
    W,H=720,480; band=54
    out=Image.new("RGB",(W*3,(H+band)*2),(24,26,30)); d=ImageDraw.Draw(out)
    for i,(k,label) in enumerate(names):
        x=(i%3)*W; y=(i//3)*(H+band)
        out.paste(Image.open(f"{OUT}/{k}_{view}.png"),(x,y+band))
        d.text((x+12,y+6),label,font=font,fill=(235,235,235))
        d.text((x+12,y+32),f"ground triangles: {st[k]/base:.2f}x cubes",font=small,fill=(170,190,210))
    out.save(f"{OUT}/round3_{view}.jpg",quality=88)
