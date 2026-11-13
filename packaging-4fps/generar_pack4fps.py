import random, os
from PIL import Image
OUT='/home/claude/out/4fps/pack4fps'
os.makedirs(OUT+'/ropa',exist_ok=True); os.makedirs(OUT+'/capas',exist_ok=True)
R=random.Random(4)
def shade(c,amt=10):
    d=R.randint(-amt,amt); return tuple(max(0,min(255,v+d)) for v in c[:3])+(255,)
def fill(im,box,col,noise=8):
    x0,y0,x1,y1=box
    for x in range(x0,x1):
        for y in range(y0,y1): im.putpixel((x,y),shade(col,noise))
def hexc(h): h=h.lstrip('#'); return tuple(int(h[i:i+2],16) for i in (0,2,4))+(255,)
# regiones (capa externa)
HEAD_TOP=(40,0,48,8); HEAD_SIDES=(32,8,64,16)
BODY_TOP=(20,32,36,36); BODY=(16,36,40,48)
RARM_TOP=(44,32,52,36); RARM=(40,36,56,48)
LARM_TOP=(52,48,60,52); LARM=(48,52,64,64)
RLEG_TOP=(4,32,12,36); RLEG=(0,36,16,48)
LLEG_TOP=(4,48,12,52); LLEG=(0,52,16,64)
def rows(box,a,b):  # filas a..b (0=arriba) dentro de la caja de caras
    x0,y0,x1,y1=box; return (x0,y0+a,x1,y0+b)
def new(): return Image.new('RGBA',(64,64),(0,0,0,0))
def save(im,name): im.save(f'{OUT}/ropa/{name}.png')

def top(col,sleeves=12,noise=8,name=None,extra=None):
    im=new(); fill(im,BODY_TOP,col,noise); fill(im,BODY,col,noise)
    if sleeves:
        fill(im,RARM_TOP,col,noise); fill(im,LARM_TOP,col,noise)
        fill(im,rows(RARM,0,sleeves),col,noise); fill(im,rows(LARM,0,sleeves),col,noise)
    if extra: extra(im)
    return im
# 1 hoodie 4fps
def hood(im):
    g=hexc('#3f3f46')
    fill(im,(20,36,28,38),hexc('#27272a'),4)  # cuello
    four=["1.1.","1.1.","1111","..1.","..1."]
    for j,r in enumerate(four):
        for i,ch in enumerate(r):
            if ch=='1': im.putpixel((22+i,40+j),(235,235,235,255))
    for x in range(20,28): im.putpixel((x,47),hexc('#27272a'))  # puño/borde
    for b in (RARM,LARM):
        for x in range(b[0],b[2]): im.putpixel((x,b[3]-1),hexc('#27272a'))
save(top(hexc('#52525b'),12,6,extra=hood),'Hoodie 4fps gris')
save(top(hexc('#18181b'),4,4),'Polera negra')
save(top(hexc('#f4f4f5'),4,4),'Polera blanca')
def stripes(im):
    for y in range(36,48,3):
        for x in range(16,40): im.putpixel((x,y),hexc('#1e3a8a'))
save(top(hexc('#f4f4f5'),4,3,extra=stripes),'Polera a rayas')
def leather(im):
    for y in range(36,48): im.putpixel((24,y),hexc('#a1a1aa'))  # cierre
    for x in (21,26): 
        for y in (37,38): im.putpixel((x,y),hexc('#3f3f46'))
save(top(hexc('#1c1917'),12,6,extra=leather),'Chaqueta de cuero')
def varsity(im):
    for b in (RARM,LARM): fill(im,rows(b,0,12),hexc('#f4f4f5'),3)
    for b in (RARM,LARM):
        for x in range(b[0],b[2]): im.putpixel((x,b[3]-2),hexc('#b91c1c'))
save(top(hexc('#b91c1c'),12,5,extra=varsity),'Chaqueta universitaria')
# pantalones
def pants(col,noise=8,name='',extra=None,lenrows=12):
    im=new()
    for t,b in ((RLEG_TOP,RLEG),(LLEG_TOP,LLEG)): fill(im,t,col,noise); fill(im,rows(b,0,lenrows),col,noise)
    if extra: extra(im)
    save(im,name)
def jeans(im):
    for b in (RLEG,LLEG):
        for x in range(b[0],b[2]):
            if R.random()<0.15: im.putpixel((x,b[1]+R.randint(0,11)),hexc('#93c5fd'))
pants(hexc('#1d4ed8'),10,'Jeans azules',jeans)
pants(hexc('#4d5b2e'),8,'Pantalon cargo')
pants(hexc('#18181b'),5,'Pantalon negro')
pants(hexc('#a1a1aa'),6,'Shorts grises',lenrows=6)
# zapatillas
def shoes(col,sole,name):
    im=new()
    for b in (RLEG,LLEG):
        fill(im,rows(b,9,12),col,3)
        for x in range(b[0],b[2]): im.putpixel((x,b[3]-1),sole)
    fill(im,(8,32,12,36),sole,2); fill(im,(8,48,12,52),sole,2)
    save(im,name)
shoes(hexc('#f4f4f5'),hexc('#d4d4d8'),'Zapatillas blancas')
shoes(hexc('#18181b'),hexc('#f4f4f5'),'Zapatillas negras')
# cabeza
def head(name,fn):
    im=new(); fn(im); save(im,name)
def cap(im):
    fill(im,HEAD_TOP,hexc('#18181b'),4); fill(im,rows(HEAD_SIDES,0,3),hexc('#18181b'),4)
    for x in range(40,48): im.putpixel((x,10),hexc('#09090b'))  # visera
    im.putpixel((44,8),hexc('#e4e4e7'))
head('Gorra negra',cap)
def beanie(im):
    fill(im,HEAD_TOP,hexc('#dc2626'),8); fill(im,rows(HEAD_SIDES,0,3),hexc('#dc2626'),8)
    for x in range(32,64): im.putpixel((x,11),hexc('#f4f4f5'))
head('Gorro de lana rojo',beanie)
def glasses(im):
    for x in range(40,48):
        if x not in (43,44): im.putpixel((x,11),hexc('#09090b')); im.putpixel((x,12),hexc('#18181b'))
    im.putpixel((43,11),hexc('#27272a')); im.putpixel((44,11),hexc('#27272a'))
    for x in (32+7,48): im.putpixel((x,11),hexc('#27272a'))
head('Lentes de sol',glasses)
def phones(im):
    for x in range(40,48): im.putpixel((x,8),hexc('#27272a'))
    for (x0) in (32,48):
        fill(im,(x0+2,10,x0+6,14),hexc('#3f3f46'),3)
        for y in range(8,10): im.putpixel((x0+4,y),hexc('#27272a'))
head('Audifonos',phones)
def scarf(im):
    fill(im,BODY_TOP,hexc('#b91c1c'),6); fill(im,rows(BODY,0,2),hexc('#b91c1c'),6)
    fill(im,(22,38,24,43),hexc('#991b1b'),6)
    fill(im,rows(HEAD_SIDES,7,8),hexc('#b91c1c'),6)
head('Bufanda roja',scarf)

# ---- capas 64x32
def cape(name,painter):
    im=Image.new('RGBA',(64,32),(0,0,0,0))
    face=Image.new('RGBA',(10,16)); painter(face)
    im.paste(face,(1,1)); im.paste(face.transpose(Image.FLIP_LEFT_RIGHT),(12,1))
    edge=face.getpixel((0,0))
    for y in range(1,17): im.putpixel((0,y),face.getpixel((0,y-1))); im.putpixel((11,y),face.getpixel((9,y-1)))
    for x in range(1,11): im.putpixel((x,0),face.getpixel((x-1,0))); im.putpixel((x+10,0),face.getpixel((x-1,15)))
    im.save(f'{OUT}/capas/{name}.png')
def solid(col,noise=6):
    def p(f):
        for x in range(10):
            for y in range(16): f.putpixel((x,y),shade(col,noise))
    return p
def logo4(f):
    solid(hexc('#3f3f46'))(f)
    four=["1..1","1..1","1111","1...","1..."]
    for j,r in enumerate(four):
        for i,ch in enumerate(r):
            if ch=='1': f.putpixel((3+i,5+j),(240,240,240,255))
    for x in range(10): f.putpixel((x,0),hexc('#a1a1aa')); f.putpixel((x,15),hexc('#a1a1aa'))
cape('4fps',logo4)
cape('Negra',solid(hexc('#18181b'),4))
def bordered(f):
    solid(hexc('#f4f4f5'),3)(f)
    for x in range(10): f.putpixel((x,0),hexc('#71717a')); f.putpixel((x,15),hexc('#71717a'))
    for y in range(16): f.putpixel((0,y),hexc('#71717a')); f.putpixel((9,y),hexc('#71717a'))
cape('Blanca con borde',bordered)
def chile(f):
    for x in range(10):
        for y in range(16): f.putpixel((x,y),shade(hexc('#f4f4f5') if y<8 else hexc('#d52b1e'),3))
    for x in range(4):
        for y in range(8): f.putpixel((x,y),shade(hexc('#0039a6'),3))
    f.putpixel((1,3),(255,255,255,255)); f.putpixel((2,3),(255,255,255,255)); f.putpixel((1,4),(255,255,255,255)); f.putpixel((2,4),(255,255,255,255))
cape('Chile',chile)
def rainbow(f):
    cols=['#ef4444','#f97316','#facc15','#22c55e','#3b82f6','#8b5cf6']
    for y in range(16):
        for x in range(10): f.putpixel((x,y),shade(hexc(cols[min(5,y*6//16)]),4))
cape('Arcoiris',rainbow)
def flames(f):
    for y in range(16):
        for x in range(10):
            t=y/15+R.uniform(-0.15,0.15)
            c=hexc('#7f1d1d') if t<0.35 else hexc('#ea580c') if t<0.7 else hexc('#facc15')
            f.putpixel((x,y),shade(c,6))
cape('Llamas',flames)
def galaxy(f):
    for y in range(16):
        for x in range(10):
            c=hexc('#1e1b4b') if (x+y)%5 else hexc('#312e81')
            f.putpixel((x,y),shade(c,8))
    for _ in range(9): f.putpixel((R.randrange(10),R.randrange(16)),(255,255,255,255))
    f.putpixel((6,9),hexc('#c084fc'))
cape('Galaxia',galaxy)
def diag(f):
    for y in range(16):
        for x in range(10): f.putpixel((x,y),shade(hexc('#18181b') if ((x+y)//2)%2 else hexc('#a1a1aa'),3))
cape('Rayas diagonales',diag)
def checker(f):
    for y in range(16):
        for x in range(10): f.putpixel((x,y),shade(hexc('#f4f4f5') if (x//2+y//2)%2 else hexc('#18181b'),2))
cape('Cuadros',checker)
def grad(a,b):
    def p(f):
        A=hexc(a);B=hexc(b)
        for y in range(16):
            t=y/15; c=tuple(int(A[i]+(B[i]-A[i])*t) for i in range(3))+(255,)
            for x in range(10): f.putpixel((x,y),shade(c,4))
    return p
cape('Atardecer',grad('#f97316','#7c3aed'))
cape('Oceano',grad('#22d3ee','#1e3a8a'))
cape('Toxica',grad('#a3e635','#14532d'))
print(len(os.listdir(OUT+'/ropa')),len(os.listdir(OUT+'/capas')))

# ---------- pack 2: estilos extra (originales 4fps)
def matrix(f):
    for x in range(10):
        for y in range(16): f.putpixel((x,y),shade(hexc('#020a04'),3))
    for x in range(10):
        start=R.randrange(16); ln=R.randint(3,8)
        for k in range(ln):
            y=(start+k)%16; f.putpixel((x,y),hexc('#22c55e') if k<ln-1 else hexc('#bbf7d0'))
cape('Hacker',matrix)
def sakura(f):
    grad('#fbcfe8','#f472b6')(f)
    for _ in range(10):
        x,y=R.randrange(10),R.randrange(16); f.putpixel((x,y),(255,255,255,255))
    for y in range(16): f.putpixel((2 if y<9 else 3,y),hexc('#7c2d12'))
cape('Sakura',sakura)
def neon(f):
    solid(hexc('#09090b'),2)(f)
    for x in range(10): f.putpixel((x,0),hexc('#ec4899')); f.putpixel((x,15),hexc('#22d3ee'))
    for y in range(16): f.putpixel((0,y),hexc('#ec4899') if y<8 else hexc('#22d3ee')); f.putpixel((9,y),hexc('#22d3ee') if y<8 else hexc('#ec4899'))
    for i in range(6): f.putpixel((2+i,4+i),hexc('#a855f7'))
cape('Neon',neon)
def bluefire(f):
    for y in range(16):
        for x in range(10):
            t=y/15+R.uniform(-0.15,0.15)
            c=hexc('#0c1445') if t<0.35 else hexc('#2563eb') if t<0.7 else hexc('#67e8f9')
            f.putpixel((x,y),shade(c,6))
cape('Fuego azul',bluefire)
def bolt(f):
    solid(hexc('#1e293b'),4)(f)
    pts=[(6,1),(5,2),(5,3),(4,4),(4,5),(3,6),(4,7),(5,7),(6,7),(5,8),(5,9),(4,10),(4,11),(3,12),(3,13)]
    for x,y in pts: f.putpixel((x,y),hexc('#facc15'))
cape('Rayo',bolt)
def heart(f):
    solid(hexc('#fdf2f8'),3)(f)
    hp=[".11.11.","1111111","1111111",".11111.","..111..","...1..."]
    for j,r in enumerate(hp):
        for i,ch in enumerate(r):
            if ch=='1': f.putpixel((1+i,5+j),hexc('#e11d48'))
cape('Corazon',heart)
def gold4(f):
    grad('#fde68a','#b45309')(f)
    four=["1..1","1..1","1111","1...","1..."]
    for j,r in enumerate(four):
        for i,ch in enumerate(r):
            if ch=='1': f.putpixel((3+i,5+j),hexc('#451a03'))
cape('4fps dorada',gold4)
def camo(f):
    cols=['#3f6212','#4d7c0f','#365314','#57534e']
    for y in range(16):
        for x in range(10): f.putpixel((x,y),shade(hexc(cols[((x//3)*7+(y//3)*3+R.randint(0,1))%4]),4))
cape('Camuflaje',camo)
def night(f):
    grad('#0f172a','#1e1b4b')(f)
    for _ in range(7): f.putpixel((R.randrange(10),R.randrange(11)),(255,255,255,255))
    for (x,y) in [(6,2),(7,2),(6,3),(7,3),(8,3),(7,4)]: f.putpixel((x,y),hexc('#fef9c3'))
    for x in range(10):
        for y in range(13+(x%3==0),16): f.putpixel((x,y),hexc('#020617'))
cape('Noche',night)
def ice(f):
    grad('#e0f2fe','#7dd3fc')(f)
    for _ in range(12): f.putpixel((R.randrange(10),R.randrange(16)),hexc('#ffffff'))
cape('Hielo',ice)
# ropa extra
save(top(hexc('#18181b'),12,5,extra=lambda im:[im.putpixel((x,47),hexc('#3f3f46')) for x in range(16,40)]),'Hoodie negro')
save(top(hexc('#1e40af'),12,6),'Chaqueta azul')
def tank(im):
    for x in (20,27):
        for y in range(36,38): im.putpixel((x,y),(0,0,0,0))
save(top(hexc('#f4f4f5'),0,3,extra=tank),'Musculosa blanca')
def gold_chain(im):
    for x in range(21,27): im.putpixel((x,37 if x in (21,26) else 38 if x in (22,25) else 39),hexc('#facc15'))
head('Cadena dorada',gold_chain)
def bandana(im):
    fill(im,rows(HEAD_SIDES,0,2),hexc('#1d4ed8'),6); fill(im,HEAD_TOP,hexc('#1d4ed8'),6)
    for x in range(32,64,3): im.putpixel((x,9),(255,255,255,255))
head('Bandana azul',bandana)
pants(hexc('#57534e'),6,'Buzo gris')
print(len(os.listdir(OUT+'/ropa')),len(os.listdir(OUT+'/capas')))
