"""Render the five parameter comparisons: python parameters.py OUTPUT_DIR."""
import argparse
from pathlib import Path
import subprocess as sp

from PIL import Image, ImageDraw, ImageFont

from desktops import make_desktops, make_neighbors
from render import ENCODE, NAMES, REVISIONS, timeline, variant_draw
from renderer import Scene

SIZE = (1920, 1080)


def tilt_grid(zooms):
    return [(f'{tilt:+d}° tilt' if tilt else '0° tilt   ·   level', f'zoom {zoom:.2f}',
             dict(cube_tilt=tilt, zoom=zoom)) for zoom in zooms for tilt in (-18, 0, 18)]


def chapters():
    yield ('solid', 'Exterior turn', 'Positive tilt tips the workspace face up; negative tips it down. Zoom adds dolly travel in face widths.',
           .50, tilt_grid((.06, .72)))
    yield ('inside-out', 'Interior turn', 'Zoom is a fraction of the safe retreat inside the room. Look at the floor, ceiling, and common wall corner.',
           .50, tilt_grid((.08, .92)))
    yield ('dimensional-fold', 'Workspace inventory', 'Ordered fixtures:  01  Fern   /   02  Mountain   /   03  Desert   /   04  Plum. The switch is always 02 → 03.',
           .50, [('Neighbors off', '02 → 03 only', dict(show_other_workspaces=False)),
                 ('Neighbors on', '01 and 04 available', dict(show_other_workspaces=True))])
    yield ('tesseract', 'Nested turn', 'Equal starting size in all six views. Zoom adds retreat beyond clearance for the moving cells.',
           .50, tilt_grid((.06, .72)))
    import tesseract
    yield ('tesseract', 'Destination start size', 'Size is a linear fraction, not an area fraction. Each view has the same level presentation and zoom.',
           tesseract.REVEAL, [(f'{s:.2f}  starting size', '0° tilt   /   zoom 0.25', dict(next_workspace_start_size=s)) for s in (0.,.20,.65,1.)])


def layout(name, title, detail, cases, index, total):
    # Titles and settings live outside the viewports; no desktop pixels covered.
    plate = Image.new('RGB', SIZE, '#161c22')
    pen = ImageDraw.Draw(plate)
    def text(x, y, value, px, color, bold=False, anchor='lt'):
        face = 'DejaVuSans-Bold.ttf' if bold else 'DejaVuSans.ttf'
        pen.text((x,y), value, font=ImageFont.truetype(face,px), fill=color, anchor=anchor)
    text(44, 27, 'HYPRIMATE    /    PARAMETER STUDIES    •    REVISION '+REVISIONS[name], 20, '#a6cec4', True)
    text(44, 65, f'{name}    /    {title}', 40, '#edf2f0', True)
    text(1872, 40, f'{index:02d}  /  {total:02d}', 24, '#b9cfce', anchor='rt')
    count = len(cases)
    w,h = {2:(896,504),4:(720,405),6:(592,333)}[count]
    cols = 3 if count==6 else 2
    rows = count//cols
    gutter = 28
    left = (SIZE[0] - cols*w - (cols-1)*gutter)//2
    top = (112 + 1007 - rows*(h+40) - (rows-1)*14)//2
    origins = []
    for i, (label, value, _) in enumerate(cases):
        x = left + (i%cols)*(w+gutter)
        y = top + (i//cols)*(h+54)
        text(x, y, label, 25, '#edf1f1', True)
        text(x+w, y+3, value, 21, '#bfd1d1', anchor='rt')
        origins.append((x,y+38))
    pen.line((44,1004,1876,1004), fill='#47515b')
    text(44, 1028, detail, 22, '#c1cdcf')
    return plate, (w,h), origins


def render_parameters(out, names=NAMES):
    out.mkdir(exist_ok=True, parents=True)
    images = make_desktops()
    neighbors = make_neighbors()
    path = timeline()[4,0,False]
    selected = [entry for entry in chapters() if entry[0] in names]
    args = ['ffmpeg','-v','error','-y','-f','rawvideo','-pixel_format','rgb24','-video_size','1920x1080',
            '-framerate','60','-i','-', '-vf','scale=out_color_matrix=bt709:out_range=tv',*ENCODE,
            '-metadata','title=Hyprimate / parameter studies', out/'parameters.mp4']
    with sp.Popen(args, stdin=sp.PIPE) as enc:
        try:
            for i,(name,title,detail,pose,cases) in enumerate(selected,1):
                print(f'{i}: {title}  ({4*i-4}–{4*i}s)', flush=True)
                plate, size, origins = layout(name,title,detail,cases,i,len(selected))
                scene = Scene((*images,*neighbors) if name=='dimensional-fold' else images, size)
                draws = [variant_draw(name,**opts) for _,_,opts in cases]
                hold = min(range(len(path)), key=lambda k: abs(path[k]-pose))
                for k,p in enumerate(path):
                    if k == hold:
                        p = pose  # compare exact reveal size, not the nearest movie frame
                    for xy,draw in zip(origins, draws):
                        plate.paste(Image.frombytes('RGB',size,scene.frame(draw,p)),xy)
                    count = 1 + (18 if k == 0 else 48 if k == hold else 240-len(path)-66 if k == len(path)-1 else 0)
                    frame = plate.tobytes()
                    for _ in range(count):
                        enc.stdin.write(frame)
                scene.ctx.release()
        finally:
            enc.stdin.close()
        if enc.wait():
            raise RuntimeError('parameter reel encode failed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--only', choices=NAMES, nargs='+', default=NAMES)
    args = parser.parse_args()
    render_parameters(args.output, args.only)
