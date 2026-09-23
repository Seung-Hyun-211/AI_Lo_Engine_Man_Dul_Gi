# Regenerates assets/templates/*.png (Circular image templates, docs/circular-art-guide.md 4.1) from the
# current data: player.csv body sizes, balance.csv mob_radius, one template per weapons.csv row.
# Python 3 stdlib only (no PIL). Run from the repo root: python3 tools/gen_templates.py
# (Python is not installed on the Windows dev machine - run it in a Linux/cloud session.)
import csv, math, os, struct, zlib
OUT = "assets/templates"
BORDER=(255,0,255,255); HIT=(0,220,255,70); HIT_EDGE=(0,220,255,255); PIVOT=(255,230,0,255); ARROW=(255,60,60,255)

class Img:
    def __init__(s,w,h): s.w,s.h=w,h; s.p=[[(0,0,0,0)]*w for _ in range(h)]
    def put(s,x,y,c):
        if 0<=x<s.w and 0<=y<s.h:
            r,g,b,a=c; R,G,B,A=s.p[y][x]
            if a==255 or A==0: s.p[y][x]=c
            else:  # simple over
                t=a/255; s.p[y][x]=(int(r*t+R*(1-t)),int(g*t+G*(1-t)),int(b*t+B*(1-t)),max(a,A))
    def fill(s,test,c,edge):
        for y in range(s.h):
            for x in range(s.w):
                cx,cy=x+0.5,y+0.5
                if test(cx,cy):
                    inner=all(test(cx+dx,cy+dy) for dx,dy in((1,0),(-1,0),(0,1),(0,-1)))
                    s.put(x,y,c if inner else edge)
    def border(s):
        for x in range(s.w): s.put(x,0,BORDER); s.put(x,s.h-1,BORDER)
        for y in range(s.h): s.put(0,y,BORDER); s.put(s.w-1,y,BORDER)
    def cross(s,x,y,n=3):
        for d in range(-n,n+1): s.put(int(x)+d,int(y),PIVOT); s.put(int(x),int(y)+d,PIVOT)
    def arrow(s,x,y,length):  # pointing right (+x) = default facing
        for d in range(int(length)): s.put(int(x)+d,int(y),ARROW)
        for k in range(1,4): s.put(int(x+length)-k,int(y)-k,ARROW); s.put(int(x+length)-k,int(y)+k,ARROW)
    def save(s,path):
        raw=b"".join(b"\x00"+bytes(v for px in row for v in px) for row in s.p)
        def chunk(t,d): return struct.pack(">I",len(d))+t+d+struct.pack(">I",zlib.crc32(t+d)&0xffffffff)
        os.makedirs(os.path.dirname(path),exist_ok=True)
        with open(path,"wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",s.w,s.h,8,6,0,0,0))+chunk(b"IDAT",zlib.compress(raw,9))+chunk(b"IEND",b""))
        print(f"{path}  {s.w}x{s.h}")

def circle_img(r):
    d=max(2,round(2*r)); im=Img(d,d)
    im.fill(lambda x,y:(x-d/2)**2+(y-d/2)**2<=r*r,HIT,HIT_EDGE); im.border(); im.cross(d/2,d/2,min(3,d//4)); return im

def kv(path):
    out={}
    for row in csv.reader(l for l in open(path,encoding="utf-8") if l.strip() and not l.startswith("#")):
        if len(row)>=2: out[row[0]]=row[1]
    return out

# --- player: sprite canvas, hitbox standing at the bottom ---
p=kv("assets/data/circular/player.csv"); sw,sh,hw,hh=(int(float(p[k])) for k in("sprite_width","sprite_height","hitbox_width","hitbox_height"))
im=Img(sw,sh); lift=int(float(p.get("hitbox_lift","0"))); x0=(sw-hw)//2; y0=sh-hh-lift
im.fill(lambda x,y:x0<=x<x0+hw and y0<=y<y0+hh,HIT,HIT_EDGE); im.border(); im.cross(sw/2,y0+hh/2); im.arrow(sw/2,y0-8,sw/2-4)
im.save(f"{OUT}/char/char_template.png")

# --- mob: 32x32 canvas, collision disc (mob_radius) standing at the bottom ---
b=kv("assets/data/circular/balance.csv"); r=float(b["mob_radius"]); d=32
im=Img(d,d); cy=d-r
im.fill(lambda x,y:(x-d/2)**2+(y-cy)**2<=r*r,HIT,HIT_EDGE); im.border(); im.cross(d/2,cy,2); im.arrow(d/2,cy-r-3,d/2-3)
im.save(f"{OUT}/mob/mob_template.png")

# --- weapons: one template per row, at level-1 hit size (1 px = 1 world unit) ---
rows=list(csv.DictReader(l for l in open("assets/data/circular/weapons.csv",encoding="utf-8") if not l.startswith("#")))
def f(row,k): return float(row[k]) if row.get(k) else 0.0
for w in rows:
    wid,eff,path=w["id"],w["effect"],w["path"] or ""
    rng,hr=f(w,"range"),(f(w,"hit_radius") or 6.0)
    moving = path in("straight","polar") or eff in("explodingbolt","piercingshot","randomdamageshot")
    if eff=="nearestbolt" or moving and eff!="radialpulse" and eff!="arcswing" and eff!="lineswing":
        im=circle_img(hr); im.save(f"{OUT}/weapon/prj_{wid}_template.png")
    elif eff=="radialpulse":
        im=circle_img(hr if moving else rng); im.save(f"{OUT}/weapon/prj_{wid}_template.png")
    elif eff=="arcswing":
        R=rng; half=math.radians(f(w,"cone_half_angle_deg")); d=round(2*R); c=d/2; im=Img(d,d)
        im.fill(lambda x,y:(x-c)**2+(y-c)**2<=R*R and ((x-c)**2+(y-c)**2<1 or math.acos(max(-1,min(1,(x-c)/math.hypot(x-c,y-c))))<=half),HIT,HIT_EDGE)
        im.border(); im.cross(c,c); im.arrow(c,c,R*0.5); im.save(f"{OUT}/weapon/prj_{wid}_template.png")
    elif eff=="lineswing":
        L=rng; W=f(w,"line_half_width"); im=Img(round(L+2*W),round(2*W)); ox,oy=W,W
        im.fill(lambda x,y:math.hypot(x-min(max(x,ox),ox+L),y-oy)<=W,HIT,HIT_EDGE)
        im.border(); im.cross(ox,oy); im.arrow(ox,oy,L*0.5); im.save(f"{OUT}/weapon/prj_{wid}_template.png")
    if eff=="explodingbolt":
        circle_img(f(w,"explode_radius")).save(f"{OUT}/fx/fx_{wid}_explode_template.png")

# --- generic: weapon/accessory icon 48x48 (4 px safe margin), hit effect 32x32 ---
im=Img(48,48); im.fill(lambda x,y:4<=x<44 and 4<=y<44,(0,0,0,0),HIT_EDGE); im.border(); im.save(f"{OUT}/weapon/wpn_icon_template.png")
im=Img(32,32); im.border(); im.cross(16,16); im.save(f"{OUT}/fx/fx_hit_template.png")
