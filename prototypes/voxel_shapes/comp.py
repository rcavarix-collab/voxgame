import sys
from PIL import Image, ImageDraw, ImageFont
names=[("cube","Cubes"),("hex","Hex prisms"),("hexhalf","Hex prisms, half height"),("tri","Triangle prisms"),("rhombic","Rhombic dodecahedra"),("truncoct","Truncated octahedra")]
stats={}
for l in open("out/stats.txt"):
    p=l.split(); stats[p[0]]={k:float(v) for k,v in (x.split("=") for x in p[1:])}
base=stats["cube"]["tris"]
font=ImageFont.load_default(size=22); small=ImageFont.load_default(size=17)
for view in ["overview","tunnel","pit"]:
    W,H=720,480; band=54
    out=Image.new("RGB",(W*3,(H+band)*2),(24,26,30))
    d=ImageDraw.Draw(out)
    for i,(k,label) in enumerate(names):
        x=(i%3)*W; y=(i//3)*(H+band)
        out.paste(Image.open(f"out/{k}_{view}.png"),(x,y+band))
        d.text((x+12,y+6),label,font=font,fill=(235,235,235))
        d.text((x+12,y+32),f"triangles: {stats[k]['tris']/base:.1f}x cubes",font=small,fill=(170,190,210))
    out.save(f"out/compare_{view}.png",optimize=True)
