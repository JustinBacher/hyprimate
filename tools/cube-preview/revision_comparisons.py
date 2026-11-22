"""Compare the actual archived R1/R2 movies with the R3 renders, without re-rendering history."""
import argparse
from itertools import chain, repeat
from math import acos, degrees
from pathlib import Path
import subprocess as sp
import tempfile

import numpy as np
from PIL import Image, ImageDraw, ImageFont

import dimensional_fold
import inside_out
from render import CLEAN_LEAD, CLEAN_TAIL, CUE_FADE, CUE_HOLD, ENCODE, timeline

BLUE, DESERT, EYE = '#79b5d9', '#edaa77', '#b6e6cf'
PAPER, TEXT, MUTED = '#182028', '#ecf1ef', '#adbdc2'
EYE_Z = 3.4  # renderer.VERTEX: the pinhole is at x=0, z=3.4 in view space


def room_eye(p):
    # Recover the eye from the two drawn walls, rather than duplicating the approved camera path.
    faces = {}
    class Walls:
        def quad(self, vertices, material, **_):
            if isinstance(material, str):
                faces[material] = np.asarray(vertices)[:2, ::2]  # TL / TR, projected view-space x and z
    inside_out.draw(Walls(), p)
    a, b = faces['A'], faces['B']
    basis = np.column_stack((a[1]-a[0], b[1]-b[0]))
    pos = np.linalg.solve(basis, (0., EYE_Z) - a[0])
    aim = np.linalg.solve(basis, (0., -1.))
    return pos, aim / np.linalg.norm(aim)


def camera(pen, position, direction, size):
    normal = np.array((-direction[1], direction[0]))
    tip = position + size * direction
    pen.polygon([tuple(position+size*.55*normal), tuple(position-size*.55*normal), tuple(tip)], fill=EYE)


def plan_strip():
    plate = Image.new('RGB', (1920, 236), PAPER)
    pen = ImageDraw.Draw(plate)
    small = ImageFont.truetype('DejaVuSans-Bold.ttf', 17)
    heading = ImageFont.truetype('DejaVuSans-Bold.ttf', 32)
    body = ImageFont.truetype('DejaVuSans.ttf', 21)
    pen.line((960,20,960,216),fill='#3a4851')
    for x, label in ((32,'01    /    FIXED WALLS'),(992,'02    /    MOVING PANELS')):
        pen.text((x,24),label,fill=MUTED,font=small)
    pen.text((32,57),'90° corner',fill=TEXT,font=heading)
    pen.text((32,112),'The eye retreats, turns and approaches.',fill=TEXT,font=body)
    pen.text((992,112),'The eye faces straight ahead. The leaves rotate.',fill=TEXT,font=body)
    for x in (32,992):
        for dx,ink,label in ((0,BLUE,'02 Mountain'),(179,DESERT,'03 Desert'),(332,EYE,'Eye')):
            pen.ellipse((x+dx,186,x+dx+10,196),fill=ink)
            pen.text((x+dx+17,176),label,font=body,fill=MUTED)
    room_origin, scale = np.array((675.,34.)), 162.
    pen.rectangle((*room_origin,*(room_origin+scale)),outline='#52616a',width=3)
    pen.line((675,34,837,34),fill=BLUE,width=6)
    pen.line((837,34,837,196),fill=DESERT,width=6)
    def draw(p, backward):
        img = plate.copy(); ink = ImageDraw.Draw(img)
        pos, aim = room_eye(p)
        if backward:  # same fixed room; exchanged wall owners and mirrored travel
            pos, aim = 1.-pos[::-1], -aim[::-1]
        camera(ink,room_origin+scale*pos,aim,14.)
        points = np.array(dimensional_fold._geometry(p))  # midlines of the actual rigid leaves (x,z)
        if backward:
            points[:,0] *= -1
        u,v = points[[0,2]]-points[1]
        angle = degrees(acos(np.clip(u@v / (np.linalg.norm(u)*np.linalg.norm(v)), -1., 1.)))
        ink.text((992,57),f'{angle:.0f}° hinge opening',font=heading,fill=TEXT)
        centre, units = np.array((1710.,108.)), 31.
        left,hinge,right = centre + points*units
        c1, c2 = (DESERT,BLUE) if backward else (BLUE,DESERT)
        ink.line([tuple(left),tuple(hinge)],fill=c1,width=6)
        ink.line([tuple(hinge),tuple(right)],fill=c2,width=6)
        x,y=hinge
        ink.ellipse((x-4,y-4,x+4,y+4),fill=TEXT)
        camera(ink,centre+np.array((0.,EYE_Z*units)),np.array((0.,-1.)),14.)
        return img.tobytes()
    return draw


def floor_plans(path):
    draw = plan_strip()
    command = ['ffmpeg','-v','error','-y','-f','rawvideo','-pix_fmt','rgb24','-s','1920x236','-r','60',
               '-i','-','-vf','scale=out_color_matrix=bt709:out_range=tv','-c:v','libx264','-crf','12',
               '-preset','veryfast','-threads','3','-pix_fmt','yuv420p',path]
    with sp.Popen(command,stdin=sp.PIPE) as ff:
        with ff.stdin as output:
            for (_,_,backward), poses in timeline().items():
                last = None
                for p in chain(repeat(poses[0],CUE_HOLD+CUE_FADE+CLEAN_LEAD),poses,repeat(poses[-1],CLEAN_TAIL)):
                    if p != last:
                        frame = draw(p,backward); last = p
                    output.write(frame)
        if ff.wait():
            raise RuntimeError('plan strip encoding failed')


def montage(path, panels, strip=None):
    args = ['ffmpeg','-v','error','-y','-filter_complex_threads','4']
    filters = []
    for i,(movie,label) in enumerate(panels):
        args += ['-threads','2','-i',movie]
        filters.append(f'[{i}:v]scale=960:540:flags=lanczos,pad=960:612:0:72:color=0x182028,'
                       f"drawtext=font='DejaVu Sans':fontsize=27:fontcolor=0xecf1ef:x=28:y=19:text='{label}'[p{i}]")
    n = len(panels)
    filters.append(''.join(f'[p{i}]' for i in range(n))+f'hstack=inputs={n}[row]')
    if strip:
        args += ['-i',strip]
        filters.append(f'[row][{n}:v]vstack[out]')
    sp.run([*args,'-filter_complex',';'.join(filters),'-map','[out]' if strip else '[row]',*ENCODE,path],check=True)


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('earlier',type=Path); cli.add_argument('previous',type=Path); cli.add_argument('revised',type=Path)
    cli.add_argument('--only',choices=('hinge','exchange'))
    a = cli.parse_args()
    if a.only != 'exchange':
        with tempfile.TemporaryDirectory() as tmp:
            strip = Path(tmp)/'plans.mp4'
            floor_plans(strip)
            montage(a.revised/'inside-out-vs-fold.mp4',[(a.previous/'inside-out.mp4','INSIDE-OUT    /    Approved R2'),
                    (a.revised/'dimensional-fold.mp4','DIMENSIONAL-FOLD    /    Revised R3')],strip)
    if a.only != 'hinge':
        montage(a.revised/'tesseract-history.mp4',[(folder/'tesseract.mp4',label) for folder,label in
                ((a.earlier,'01    Earlier    /    Nested aperture'),(a.previous,'02    Previous    /    Counterfold'),
                 (a.revised,'03    Revised    /    Single exchange'))])


if __name__ == '__main__':
    main()
