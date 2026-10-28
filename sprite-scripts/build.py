import json, math, os
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops
W,H=320,240; B=16; S=240/(1024-2*B); OX=(W-240)//2
FONT=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',22)
SHIFTS={'thinking':(-12,4),'pet':(0,4),'listening':(-16,16)}
def snap(im):
    lut=[0 if v<90 else 255 if v>165 else int((v-90)*255/75) for v in range(256)]
    return im.point(lut)
def key(name):
    p=f'keys/{name}.png' if os.path.exists(f'keys/{name}.png') else f'keys/{name}.jpg'
    im=Image.open(p).convert('L').resize((1024,1024),Image.LANCZOS)
    dx,dy=SHIFTS.get(name,(0,0))
    if dx or dy:
        c=Image.new('L',(1024,1024),255); c.paste(im,(dx,dy)); im=c
    return snap(im)
def patch(base,src,box,feather=10):
    m=Image.new('L',base.size,0); d=ImageDraw.Draw(m)
    x0,y0,x1,y1=box; d.rounded_rectangle((x0+feather,y0+feather,x1-feather,y1-feather),radius=feather,fill=255)
    m=m.filter(ImageFilter.GaussianBlur(feather/2))
    return Image.composite(src,base,m)
def frame(k, dx=0, dy=0, scale=1.0, rot=0.0):
    im=k.crop((B,B,1024-B,1024-B))
    if rot: im=im.rotate(rot,resample=Image.BICUBIC,fillcolor=255,center=(496,900))
    sz=int(round(240*scale))
    im=im.resize((sz,sz),Image.LANCZOS)
    c=Image.new('L',(W,H),255)
    c.paste(im,(OX+dx-(sz-240)//2, dy-(sz-240)))
    return c.convert('RGB')
def heart(d,cx,cy,r):
    pts=[(cx+r*16*math.sin(t)**3/16, cy-r*(13*math.cos(t)-5*math.cos(2*t)-2*math.cos(3*t)-math.cos(4*t))/16) for t in np.linspace(0,2*math.pi,60)]
    d.polygon(pts,fill=(0,0,0),outline=(255,255,255),width=2)
def star(d,cx,cy,r,spikes=5,fill=(0,0,0)):
    pts=[]
    for i in range(spikes*2):
        a=math.pi*i/spikes-math.pi/2; rr=r if i%2==0 else r*0.45
        pts.append((cx+rr*math.cos(a),cy+rr*math.sin(a)))
    d.polygon(pts,fill=fill,outline=(255,255,255) if fill==(0,0,0) else (0,0,0),width=2)
def sparkle(d,cx,cy,r):
    star(d,cx,cy,r,4)
def text(d,xy,s,size):
    f=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',size)
    d.text(xy,s,font=f,fill=(0,0,0),stroke_width=2,stroke_fill=(255,255,255))
def dot(d,cx,cy,r):
    d.ellipse((cx-r,cy-r,cx+r,cy+r),fill=(0,0,0),outline=(255,255,255),width=2)

N=key('neutral')
anims={}
# neutral: blink
blink=patch(N,key('blink'),(170,395,470,545))
anims['neutral']=([frame(N)]*4+[frame(blink),frame(N)], [600,600,600,600,120,600])
# happy
Hk=key('happy'); fr=[]
for i,(dy) in enumerate([0,-3,-6,-3]):
    f=frame(Hk,dy=dy+6); d=ImageDraw.Draw(f)
    for j,(x,y) in enumerate([(26,40),(296,60),(30,150),(292,170)]):
        r=[5,9,12,8][(i+j)%4]; sparkle(d,x,y,r)
    fr.append(f)
anims['happy']=(fr,[160]*4)
# sad: tear drop sliding
Sk=key('sad'); fr=[]
for i in range(5):
    f=frame(Sk); d=ImageDraw.Draw(f)
    y=142+i*10; x=144+i*0.6
    d.ellipse((x-3,y,x+3,y+7),fill=(255,255,255),outline=(0,0,0),width=1)
    d.polygon([(x-3,y+3),(x,y-5),(x+3,y+3)],fill=(255,255,255),outline=(0,0,0))
    fr.append(f)
anims['sad']=(fr,[300,220,220,220,400])
# thinking: dots
Tk=key('thinking'); fr=[]
for n in range(5):
    f=frame(Tk); d=ImageDraw.Draw(f)
    for j,(x,y,r) in enumerate([(286,78,4),(298,54,6),(306,26,9)]):
        if j<n: dot(d,x,y,r)
    if n==4:
        d=ImageDraw.Draw(f); text(d,(298,6),'?',18) if False else None
    fr.append(f)
anims['thinking']=(fr,[350,350,350,700,300])
# sleeping: breathing + Zzz
Zk=key('sleeping'); fr=[]
for i in range(6):
    sc=1.0+0.012*math.sin(2*math.pi*i/6)
    f=frame(Zk,scale=sc); d=ImageDraw.Draw(f)
    for j in range(3):
        t=((i+j*2)%6)/6
        x=262+j*0+t*30; y=100-t*85; s=int(12+t*16)
        text(d,(x,y),'Z',s)
    fr.append(f)
anims['sleeping']=(fr,[420]*6)
# pet: hearts floating up
Pk=key('pet'); fr=[]
for i in range(6):
    f=frame(Pk,dy=(-2 if i%2 else 0)); d=ImageDraw.Draw(f)
    for j,(x0,ph) in enumerate([(30,0),(290,2),(60,4),(270,1)]):
        t=((i+ph)%6)/6; y=200-t*170; x=x0+6*math.sin(t*6.28)
        heart(d,x,y,8+t*9)
    fr.append(f)
anims['pet']=(fr,[160]*6)
# shake: wobble + orbiting stars
Kk=key('shake'); fr=[]
offs=[-8,8,-6,6,-3,3,0,0]; rots=[2,-2,1.5,-1.5,1,-1,0,0]
for i in range(8):
    f=frame(Kk,dx=offs[i],rot=rots[i]); d=ImageDraw.Draw(f)
    for j in range(3):
        a=2*math.pi*(i/8+j/3)
        star(d,160+110*math.cos(a),30+14*math.sin(a),12)
    fr.append(f)
anims['shake']=(fr,[90]*8)
# speaking: mouth cycle (mouth patch from open key onto neutral)
O=key('speaking_open')
mbox=(205,580,350,710)
op=patch(N,O,mbox,8)
anims['speaking']=([frame(N),frame(op),frame(N),frame(op),frame(op),frame(N)],[140,160,120,180,120,160])
# listening: glow lines from headphone
Lk=key('listening'); fr=[]
hx,hy=40+(640-16)*S, (500-16)*S
for i in range(4):
    f=frame(Lk); d=ImageDraw.Draw(f)
    for j in range(i%4+1 if i<3 else 3):
        r=18+j*10
        d.arc((hx-r,hy-r,hx+r,hy+r),-50,40,fill=(255,255,255),width=3)
    fr.append(f)
anims['listening']=(fr,[220,220,220,400])
# look_left: head turn toward left
fr=[]
for (dx,rot) in [(0,0),(-4,1),(-8,2),(-10,2.5),(-10,2.5),(-8,2),(-4,1)]:
    fr.append(frame(N,dx=dx,rot=rot))
anims['look_left']=(fr,[200,90,90,700,300,90,90])
# look_right: eye patch from look_right key, plus slight shift
R=key('look_right'); rp=patch(N,R,(170,395,470,545))
anims['look_right']=([frame(N),frame(rp,dx=2),frame(rp,dx=5,rot=-1),frame(rp,dx=5,rot=-1),frame(rp,dx=2),frame(N)],[200,90,90,700,300,200])

# email: composing -> sending -> sent
def envelope(d,cx,cy,w,h,rot=0,open_=False):
    import math as m
    a=m.radians(rot); ca,sa=m.cos(a),m.sin(a)
    P=lambda x,y:(cx+x*ca-y*sa, cy+x*sa+y*ca)
    x0,y0,x1,y1=-w/2,-h/2,w/2,h/2
    d.polygon([P(x0,y0),P(x1,y0),P(x1,y1),P(x0,y1)],fill=(255,255,255),outline=(0,0,0),width=3)
    d.line([P(x0,y0),P(0,h*0.12),P(x1,y0)],fill=(0,0,0),width=3)
    d.line([P(x0,y1),P(-w*0.12,0)],fill=(0,0,0),width=2); d.line([P(x1,y1),P(w*0.12,0)],fill=(0,0,0),width=2)
def letter(d,x0,y0,nlines,pen):
    d.rectangle((x0,y0,x0+54,y0+70),fill=(255,255,255),outline=(0,0,0),width=3)
    for j in range(nlines):
        y=y0+12+j*12; ln=[38,30,36,22,0][j]
        if j==nlines-1: ln=int(ln*pen)
        if ln: d.line((x0+8,y,x0+8+ln,y),fill=(0,0,0),width=3)
    j=nlines-1; y=y0+12+j*12; px=x0+8+int([38,30,36,22,0][j]*pen)
    # pencil, tip at (px,y)
    d.polygon([(px,y),(px+6,y-10),(px+30,y-40),(px+38,y-34),(px+12,y-4)],fill=(0,0,0),outline=(255,255,255))
    d.polygon([(px,y),(px+6,y-10),(px+12,y-4)],fill=(255,255,255),outline=(0,0,0))
def check(d,cx,cy,r,prog=1.0):
    d.ellipse((cx-r,cy-r,cx+r,cy+r),fill=(0,0,0),outline=(255,255,255),width=3)
    pts=[(cx-r*0.5,cy),(cx-r*0.12,cy+r*0.4),(cx+r*0.55,cy-r*0.4)]
    if prog<=0.5:
        t=prog/0.5; d.line([pts[0],(pts[0][0]+(pts[1][0]-pts[0][0])*t,pts[0][1]+(pts[1][1]-pts[0][1])*t)],fill=(255,255,255),width=6)
    else:
        t=(prog-0.5)/0.5; d.line([pts[0],pts[1],(pts[1][0]+(pts[2][0]-pts[1][0])*t,pts[1][1]+(pts[2][1]-pts[1][1])*t)],fill=(255,255,255),width=6,joint='curve')
fr=[];dur=[]
for i,(n,p) in enumerate([(1,0.5),(2,0.6),(3,0.7),(4,1.0)]):
    f=frame(Tk); d=ImageDraw.Draw(f); letter(d,258,132,n,p); fr.append(f); dur.append(260)
dur[-1]=360
# fold into envelope
f=frame(Tk); d=ImageDraw.Draw(f); envelope(d,286,170,58,40); fr.append(f); dur.append(300)
# sending: envelope flies up-right and away
for i,(x,y,w,rot) in enumerate([(276,150,54,-8),(292,100,44,-16),(306,52,32,-24),(318,14,22,-30)]):
    f=frame(N); d=ImageDraw.Draw(f)
    for k in range(3):
        o=(k-1)*9; L=22+8*i
        d.line((x-w*0.6-L+o*0.3,y+L*0.6+o,x-w*0.6+o*0.3,y+o*0.4+6),fill=(0,0,0),width=3)
    envelope(d,x,y,w,w*0.68,rot); fr.append(f); dur.append(110)
# sent: check mark pops, happy face
for i,(pr,r) in enumerate([(0.5,18),(1.0,24),(1.0,22)]):
    f=frame(Hk,dy=6+(-3 if i%2 else 0)); d=ImageDraw.Draw(f)
    check(d,286,44,r,pr)
    for j,(x,y) in enumerate([(30,150),(292,170),(26,40)]):
        sparkle(d,x,y,[5,9,12,8][(i+j)%4])
    fr.append(f); dur.append(160 if i<2 else 900)
anims['email']=(fr,dur)

os.makedirs('frames',exist_ok=True); os.makedirs('gifs',exist_ok=True)
order=['neutral','happy','sad','thinking','sleeping','pet','shake','speaking','listening','look_left','look_right','email']
maxf=max(len(anims[k][0]) for k in order)
sheet=Image.new('RGB',(W*maxf,H*len(order)),(255,255,255))
meta={'cell':{'w':W,'h':H},'background':'white','rows':[]}
for r,k in enumerate(order):
    fr,dur=anims[k]
    names=[]
    for i,f in enumerate(fr):
        n=f'frames/{k}_{i:02d}.png'; f.save(n,optimize=True); names.append(os.path.basename(n))
        sheet.paste(f,(i*W,r*H))
    pal=[f.convert('L').convert('P',palette=Image.ADAPTIVE,colors=16) for f in fr]
    q=[f.quantize(colors=16,method=Image.MEDIANCUT) for f in fr]
    q[0].save(f'gifs/{k}.gif',save_all=True,append_images=q[1:],duration=dur,loop=0,optimize=True,disposal=1)
    meta['rows'].append({'row':r,'name':k,'frames':names,'frame_count':len(fr),'ms_per_frame':dur,'gif':f'{k}.gif'})
sheet.save('sprite_sheet.png',optimize=True)
json.dump(meta,open('sprite_sheet.json','w'),indent=2)
print('ok')
