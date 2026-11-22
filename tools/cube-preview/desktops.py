"""Preview only. Original procedural scenery, application artwork and fictional content."""
from functools import lru_cache

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H = 1920, 1080
INK, MUTED, PAPER, PANEL = '#222b32', '#52616b', '#f5f6f6', '#252c34'


def _sky(top, bottom):
    ramp = np.linspace(top, bottom, H).astype('uint8')[:, None, :]
    return Image.fromarray(np.repeat(ramp, W, axis=1))


def _field(rng, w, h, nx, ny):
    samples = rng.uniform(-1, 1, (ny, nx)).astype('float32')
    return np.asarray(Image.fromarray(samples).resize((w, h), Image.Resampling.BICUBIC))


def _grain(im, rng):
    rgb = np.asarray(im).astype('int16')
    rgb += rng.normal(0, 1.65, (H, W, 1)).astype('int16')
    return Image.fromarray(rgb.clip(0, 255).astype('uint8'))


def _mountains():
    rng = np.random.default_rng(421)
    im = _sky((40, 98, 157), (207, 228, 233))
    d = ImageDraw.Draw(im)
    # Displaced relief mesh: the face normals modulate rock and determine where snow stays.
    nx, nz = 301, 68
    x = np.linspace(-16, W+16, nx)
    z = np.linspace(0, 1, nz)[:, None]
    for baseline, peaks, back in [
        (747, [(10, 325, 400), (680, 375, 450), (1070, 380, 295), (1790, 370, 330)], True),
        (855, [(-50, 345, 480), (520, 365, 480), (980, 390, 390),
               (1350, 665, 515), (1550, 470, 350), (1855, 425, 420)], False),
    ]:
        crest = np.maximum.reduce([height * np.maximum(0, 1-abs(x-c)/width)**0.86
                                    for c, height, width in peaks])
        ridge_z = .27 + .15*np.sin(x/155) + .06*np.cos(x/64)
        folds = np.sin(x/55 + 4*z) + .44*np.sin(x/23 - 3*z)
        height = np.maximum(0, crest * (1 - .91*abs(z-ridge_z))
                            + 19*folds + 22*_field(rng,nx,nz,45,12)
                            + 6*_field(rng,nx,nz,150,34))
        y = baseline + 120*z - height
        dz, dx = np.gradient(height, 5., x[1]-x[0])
        light = (.50 - .43*dx - .26*dz) / np.sqrt(1+dx*dx+dz*dz)
        light = np.clip(light+.25, 0, 1)
        snow = np.clip((height-270)/72 + .98*light - .95 - .34*abs(dx),0,1)
        if back:
            dark, sunny, ice = (78,119,157),(134,170,194),(211,230,234)
        else:
            dark, sunny, ice = (37,66,106),(138,164,178),(239,246,245)
        rock = np.array(dark) + light[...,None]*(np.array(sunny)-dark)
        frost = np.array(ice) - (1-light[...,None])*np.array([96,72,50])
        colors = (rock*(1-snow[...,None])+frost*snow[...,None]).astype('uint8')
        for j in range(nx-1):
            d.polygon([(x[j],y[-1,j]), (x[j+1],y[-1,j+1]),
                       (x[j+1],H), (x[j],H)],tuple(map(int,colors[-1,j])))
        for row in range(nz-1):
            top, bot = y[row], y[row+1]
            for j in range(nx-1):
                d.polygon([(x[j], top[j]), (x[j+1], top[j+1]),
                           (x[j+1], bot[j+1]), (x[j], bot[j+1])], tuple(map(int, colors[row,j])))
    # Steep forested banks open into a glacial lake. Ripples foreshorten at the far shore.
    xx = np.arange(W)
    shore = 753 + 19*np.sin(xx/520) + 8*np.cos(xx/165)
    for y in range(730,H):
        t=(y-730)/350
        rgb=tuple(np.rint(np.array([113,178,191])*(1-t)+np.array([26,92,117])*t).astype(int))
        d.line((0,y,W,y),rgb)
    for _ in range(2600):
        y=int(rng.integers(731,H)); x0=int(rng.integers(-35,W)); t=(y-730)/350
        c = int(157-73*t+rng.uniform(-10,12))
        d.line((x0,y,x0+int(rng.uniform(3,48)*(.2+t)),y),(c-45,c+5,c+12))
    for side, col, dy in [(1,'#416779',-47),(-1,'#31566b',-12),(1,'#214759',8)]:
        grade = (xx/W if side == 1 else 1-xx/W)**2.2
        boundary = shore - 210*grade + dy + 14*np.sin(xx/146+dy/12)
        bottom = shore+12+dy+90*grade
        d.polygon(list(zip(xx[::3],boundary[::3]))+list(zip(xx[::3][::-1],bottom[::3][::-1])),col)
        for i in range(0,W,4):
            h=rng.uniform(2,12)*(grade[i]+.12)
            d.polygon([(i-2,boundary[i]+3),(i,boundary[i]-h),(i+3,boundary[i]+3)],col)
    return _grain(im,rng)


def _desert():
    rng = np.random.default_rng(613)
    im = _sky((207,132,103),(255,222,163)); d = ImageDraw.Draw(im)
    d.ellipse((259,144,405,290),fill='#ffe7b7')
    # Cut sandstone faces. Adjacent faces share a skyline but turn the light differently.
    def mesa(vertices, base, shades, detail):
        for i, ((x,y),(u,v)) in enumerate(zip(vertices,vertices[1:])):
            tone=np.array(shades[i%len(shades)])
            d.polygon([(x,y),(u,v),(u,base),(x,base)], tuple(map(int,tone)))
            for _ in range(detail):
                yy=int(rng.integers(max(y,v)+1,base)); off=int(rng.integers(-13,14))
                l=int(rng.integers(x,u+1)); r=min(u,l+int(rng.uniform(6,80)))
                d.line((l,yy,r,yy+(r-l)*.025),tuple(map(int,(tone+off).clip(0,255))),int(rng.integers(1,4)))
    mesa([(0,555),(80,541),(125,510),(227,510),(250,540),(331,549),(410,522),
          (447,523),(484,465),(553,464),(568,489),(648,492),(710,571),(830,588),
          (1010,543),(1090,539),(1140,506),(1296,505),(1340,544),(1520,550),
          (1720,532),(1790,496),(1920,498)],700,[(184,123,112),(196,137,120)],12)
    mesa([(-10,599),(154,552),(206,485),(233,477),(245,389),(280,384),(285,317),
          (305,314),(312,278),(352,276),(358,320),(380,321),(392,410),(432,412),
          (442,337),(477,336),(486,381),(515,388),(532,472),(578,488),(628,563),
          (783,627)],750,[(169,86,66),(201,110,73),(182,90,63),(152,74,62)],46)
    mesa([(590,658),(702,599),(758,537),(775,460),(813,455),(826,417),
          (936,413),(950,452),(989,455),(1004,532),(1070,589),(1180,642)],745,
         [(160,80,66),(190,95,65),(200,110,72),(164,80,64)],39)
    x=np.arange(W)
    for k, (level, amp, top, bottom) in enumerate([
        (638,28,(229,151,91),(210,126,73)),(716,50,(244,177,102),(217,130,71)),
        (808,63,(201,105,66),(172,80,58)),(914,83,(243,162,91),(202,109,65)),
        (1035,65,(148,75,59),(120,62,55))]):
        edge=level+amp*(np.sin(x/390+.97*k)+.34*np.cos(x/187-.8*k))
        # Stripe follows the windward slip face, not image-space horizontal bands.
        step=3
        for j in range(0,260,step):
            t=min(1,j/170); col=tuple(map(int,np.array(top)*(1-t)+np.array(bottom)*t))
            a=edge+j; d.polygon(list(zip(x[::6],a[::6])) + list(zip(x[::6][::-1],(a[::6]+step+1)[::-1])),col)
        for _ in range(330):
            xx=int(rng.integers(0,W)); yy=int(edge[xx]+rng.uniform(5,150)); end=min(W-1, xx+int(rng.uniform(5,27)))
            d.line((xx,yy,end,yy+(edge[end]-edge[xx])*.75), tuple(int(v*.94) for v in top))
    return _grain(im,rng)


@lru_cache(None)
def _font(size, face):
    family={'regular':'NotoSans-Regular','bold':'NotoSans-Bold','mono':'NotoSansMono-Regular'}[face]
    for name in (f'/usr/share/fonts/noto/{family}.ttf',
                 {'regular':'DejaVuSans','bold':'DejaVuSans-Bold','mono':'DejaVuSansMono'}[face]+'.ttf'):
        try:
            return ImageFont.truetype(name,size)
        except OSError:
            pass
    return ImageFont.load_default(size=size)


def _text(d,x,y,text,size=18,fill=INK,face='regular',anchor='lm'):
    d.text((x,y),text,fill=fill,font=_font(size,face),anchor=anchor)


def _icon(d,x,y,s,kind):
    def p(a,b): return x+int(a*s),y+int(b*s)
    def box(a,b,c,e): return (*p(a,b),*p(c,e))
    def line(points,c,w): d.line([p(a,b) for a,b in points],fill=c,width=max(1,round(w*s)),joint='curve')
    if kind=='files':
        d.rounded_rectangle(box(.04,.18,.51,.55),int(s*.08),'#d59c44')
        d.rounded_rectangle(box(.04,.29,.96,.84),int(s*.08),'#f3c36f')
        d.line(box(.18,.37,.82,.37),'#ffe2a5',max(1,s//22)); return
    bg={'terminal':'#414b59','editor':'#417d9f','music':'#b46478','photo':'#5c8a79','web':'#558eb8'}[kind]
    d.rounded_rectangle(box(.04,.04,.96,.96),int(s*.19),bg)
    if kind=='terminal':
        line([(.2,.32),(.4,.5),(.2,.68)],'white',.065); line([(.49,.67),(.77,.67)],'#c9dfde',.055)
    elif kind=='editor':
        for i,w in enumerate((.72,.62,.77,.49)): line([(.24,.28+i*.14),(w,.28+i*.14)],'white',.055)
    elif kind=='music':
        line([(.4,.68),(.4,.35),(.73,.26),(.73,.59)],'white',.08)
        for a,b in [(.30,.7),(.63,.62)]: d.ellipse(box(a-.09,b-.085,a+.12,b+.07),'white')
    elif kind=='web':
        d.ellipse(box(.21,.21,.79,.79),outline='white',width=max(1,s//20)); d.ellipse(box(.38,.21,.62,.79),outline='white',width=max(1,s//22));line([(.22,.5),(.78,.5)],'white',.045)
    else:
        d.polygon([p(.12,.76),p(.43,.35),p(.59,.56),p(.7,.43),p(.91,.76)],'#d4ecdb');d.ellipse(box(.62,.2,.76,.34),'#f8dca0')


def _frame(im, x,y,w,h, title, icon, dark=False):
    # Blur only the local shadow mask; UI itself stays pixel aligned.
    shadow=Image.new('L',(w+80,h+80)); sd=ImageDraw.Draw(shadow)
    sd.rounded_rectangle((32,39,w+32,h+39),13,110)
    im.paste((9,17,24),(x-32,y-30,x+w+48,y+h+50),shadow.filter(ImageFilter.GaussianBlur(13)))
    d=ImageDraw.Draw(im); d.rounded_rectangle((x,y,x+w,y+h),12,PANEL if dark else PAPER,outline='#44515d')
    d.rounded_rectangle((x+1,y+1,x+w-1,y+52),11,'#333c46' if dark else '#e5e9eb'); d.rectangle((x+1,y+34,x+w-1,y+52),'#333c46' if dark else '#e5e9eb')
    c='white' if dark else INK
    _icon(d,x+14,y+11,29,icon); _text(d,x+53,y+26,title,18,c,'bold')
    for j in range(3):
        a=x+w-98+j*34; b=y+26
        if j==0:d.line((a-5,b+3,a+5,b+3),c,2)
        elif j==1:d.rectangle((a-5,b-5,a+5,b+5),outline=c,width=1)
        else:
            d.line((a-5,b-5,a+5,b+5),c,2);d.line((a-5,b+5,a+5,b-5),c,2)
    return d


def _shell(im,workspace,active):
    d=ImageDraw.Draw(im); d.rectangle((0,0,W,42),PANEL)
    for i in range(4):
        x,y=20+i%2*9,12+i//2*9; d.rounded_rectangle((x,y,x+6,y+6),2,'#e1eff4')
    _text(d,52,21,'Applications',17,'#eef2f5','bold')
    for i in range(2):
        x=203+38*i
        if i==workspace:d.rounded_rectangle((x-16,6,x+16,35),6,'#535f6d')
        _text(d,x,20,str(i+1),16,'#f0f2f5',anchor='mm')
    d.line((274,10,274,32),'#57616d');_text(d,295,21,active,16,'#e1e6e9')
    _text(d,960,21,'Tue 16 Apr   10:42',17,'#f1f4f5','bold','mm')
    for radius in (6,11,16):d.arc((1726-radius,34-radius,1726+radius,34+radius),226,314,'#e4ebed',2)
    d.ellipse((1724,31,1728,35),'#e4ebed');d.polygon([(1756,18),(1761,18),(1768,12),(1768,30),(1761,25),(1756,25)],'#e4ebed');d.arc((1761,12,1779,30),305,55,'#e4ebed',2)
    d.rounded_rectangle((1800,15,1825,27),2,outline='#e4ebed',width=2); d.rectangle((1803,18,1819,24),'#d2e3d8');d.rectangle((1826,18,1828,24),'#e4ebed');_text(d,1841,21,'82%',16,'#e4ebed')
    icons=('files','web','terminal','editor','music','photo'); start=790
    d.rounded_rectangle((start-14,993,start+340,1061),18,'#2c353d',outline='#57626a')
    for i,k in enumerate(icons):
        xx=start+i*56;_icon(d,xx,1002,44,k)
        if k in (('files','terminal') if workspace==0 else ('editor','music')):d.rounded_rectangle((xx+17,1051,xx+27,1054),2,'#bcdbe9')


def _files(im,wall):
    x,y,w,h=105,148,756,410
    d=_frame(im,x,y,w,h,'Pictures','files'); d.rectangle((x+1,y+53,x+172,y+h-12),'#e9edef')
    for i,name in enumerate(('Home','Documents','Downloads','Pictures','Music')):
        yy=y+99+i*46
        if i==3:d.rounded_rectangle((x+9,yy-18,x+163,yy+18),6,'#cbdfed')
        _icon(d,x+17,yy-12,24,('files','editor','files','photo','music')[i]);_text(d,x+53,yy,name,17)
    _text(d,x+193,y+80,'Home   /   Pictures   /   Alpine',18,INK,'bold')
    d.line((x+173,y+104,x+w-1,y+104),'#d4dadd')
    for i,(nm,region) in enumerate([('North ridge',(970,135,1650,600)),('Glacial lake',(810,650,1600,1060)),('Blue hour',(1200,280,1910,720))]):
        xx=x+191+i*184;yy=y+133
        thumb=wall.crop(region).resize((163,119),Image.Resampling.LANCZOS); im.paste(thumb,(xx,yy))
        _text(d,xx+81,yy+143,nm+'.jpg',16,anchor='mm')
    d.line((x+173,y+h-44,x+w-1,y+h-44),'#d6dcdf');_text(d,x+192,y+h-21,'3 images',15,MUTED)


def _terminal(im):
    x,y,w,h=339,589,633,292
    d=_frame(im,x,y,w,h,'Terminal','terminal',True)
    rows=[('mira@studio  ~/Pictures/Alpine','#9bdac7'),('$ du -h *.jpg','#f5f6f6'),('4.8M    Blue hour.jpg','#c2ccd4'),('6.1M    Glacial lake.jpg','#c2ccd4'),('5.2M    North ridge.jpg','#c2ccd4'),('$','#f5f6f6')]
    for i,(txt,c) in enumerate(rows):_text(d,x+22,y+82+i*32,txt,18,c,'mono')
    d.rectangle((x+42,y+231,x+51,y+250),'#bfd9da')


def _editor(im):
    x,y,w,h=1093,122,711,505
    d=_frame(im,x,y,w,h,'Field notes.md','editor'); _text(d,x+24,y+80,'File      Edit      View      Search      Tools',16)
    d.line((x+1,y+104,x+w-1,y+104),'#d3dadd');d.rectangle((x+1,y+105,x+48,y+h-42),'#edf0f1')
    lines=[('# Sandstone traverse','#2b6b88'),('',INK),('Late afternoon, west escarpment.',INK),('Wind is lifting sand along the crest.',INK),('',INK),('## Next walk','#2b6b88'),('- Follow the wash to the saddle.',INK),('- Photograph the east face at first light.',INK),('- Pack the wide lens and spare battery.',INK)]
    for i,(txt,c) in enumerate(lines):
        yy=y+137+34*i;_text(d,x+29,yy,str(i+1),15,MUTED,'mono','mm');_text(d,x+68,yy,txt,19,c,'mono')
    d.line((x+1,y+h-42,x+w-1,y+h-42),'#d0d6da');_text(d,x+18,y+h-22,'Markdown',15,MUTED);_text(d,x+w-16,y+h-22,'Ln 9, Col 39     UTF-8',15,MUTED,anchor='rm')


def _player(im,wall):
    x,y,w,h=802,696,575,225
    d=_frame(im,x,y,w,h,'Music','music',True)
    cover=wall.crop((130,225,720,815)).resize((127,127),Image.Resampling.LANCZOS);im.paste(cover,(x+20,y+73))
    _text(d,x+167,y+88,'Evening in the Wash',22,'#f3f4f5','bold');_text(d,x+167,y+121,'Silt Choir  /  Dry Season',17,'#c1cbd1')
    d.rounded_rectangle((x+168,y+151,x+w-27,y+155),2,'#5a646e');d.rounded_rectangle((x+168,y+151,x+311,y+155),2,'#edb18e');d.ellipse((x+307,y+147,x+319,y+159),'#f2d1b9')
    _text(d,x+167,y+184,'1:28',15,'#c6d0d5');_text(d,x+w-26,y+184,'3:52',15,'#c6d0d5',anchor='rm')
    for xx in (x+350,x+359):d.rectangle((xx,y+175,xx+4,y+191),'#edf1f3')
    for xx,sign in [(x+320,-1),(x+394,1)]:
        d.polygon([(xx+6*sign,y+183),(xx-5*sign,y+176),(xx-5*sign,y+190)],'#d5dde0');d.line((xx+8*sign,y+176,xx+8*sign,y+190),'#d5dde0',2)


def make_desktops() -> tuple[Image.Image, Image.Image]:
    """Return two independent 1920×1080 RGB desktop textures, without video overlays."""
    mountains = _mountains(); a = mountains.copy()
    _files(a,mountains); _terminal(a); _shell(a,0,'Terminal')
    desert = _desert(); b = desert.copy()
    _editor(b); _player(b,desert); _shell(b,1,'Music')
    return a,b
