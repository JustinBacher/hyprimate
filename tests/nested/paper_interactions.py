"""Live resize/move scenarios for paper_evidence.py --interactions (private compositor)."""
import json
import subprocess
import time

from PIL import Image, ImageChops, ImageStat


def exercise(hc, send, event, env, out):
    ident = 900
    title = f'PaperEvidence-{ident}'
    base = (230, 140, 540, 380)

    def config(style, enabled=True):
        settings = ('style="paper",enabled=true,open={enabled=false},close={enabled=true},'
                    'movement={enabled=' + str(enabled).lower() + '},'
                    'options={paper={move={style="' + style + '"}}}')
        hc('eval', 'hl.config({plugin={hyprimate={window={' + settings + '}}}})')

    def screenshot(name):
        path = out / (name + '.png')
        subprocess.run(['grim', str(path)], env=env, check=True)
        with Image.open(path) as source:
            return source.convert('RGB')

    config('flutter')
    hc('eval', 'hl.animation({leaf="windowsMove",enabled=false})')
    send(dict(op='open', id=ident, width=base[2], height=base[3], image=True, new_column=560))
    deadline = time.monotonic() + 8
    while True:
        client = next((c for c in json.loads(hc('-j', 'clients')) if c['title'] == title), None)
        if client and client['mapped']:
            break
        if time.monotonic() >= deadline:
            raise RuntimeError('interaction specimen did not map')
        time.sleep(.05)
    selector = 'window="address:' + client['address'] + '"'
    previous = None

    def box(target):
        nonlocal previous
        calls = []
        for name, offset in [('resize', 2), ('move', 0)]:
            xy = target[offset:offset + 2]
            # Native resize is centred. Reapply the requested origin in the same
            # dispatch even when its coordinates have not changed (right/bottom grips).
            if previous is None or xy != previous[offset:offset + 2] or (name == 'move' and calls):
                x, y = xy
                calls.append(f'assert(hl.dispatch(hl.dsp.window.{name}({{x={x},y={y},{selector}}})).ok)')
        if calls:
            hc('eval', ';'.join(calls))
        previous = target

    def sweep(target, frames=10, interval=.018):
        start = previous
        for i in range(1, frames + 1):
            box(tuple(round(a + (b - a) * i / frames) for a, b in zip(start, target)))
            time.sleep(interval)

    def endpoint(style, name):
        active = screenshot(name + '-active')
        if name in ('right-grow', 'right-shrink'):
            r, g, b = active.split()
            green = ImageChops.subtract(g, ImageChops.lighter(r, b)).point(lambda v: 255 if v > 100 else 0)
            if bool(green.getbbox()) != (name == 'right-grow'):
                raise RuntimeError(('incorrect live buffer during resize', name, green.getbbox()))
        time.sleep(1.8)
        settled = screenshot(name + '-settled')
        c = next(c for c in json.loads(hc('-j', 'clients')) if c['title'] == title)
        if tuple(c['at'] + c['size']) != previous:
            raise RuntimeError(('compositor geometry differs', name, previous, c['at'], c['size']))
        config(style, False)
        time.sleep(.12)
        native = screenshot(name + '-native')
        error = ImageStat.Stat(ImageChops.difference(settled, native)).mean
        changed = ImageChops.difference(active, native).getbbox()
        event('live-endpoint', case=name, geometry=previous, active_difference=changed, settled_rgb_mae=error)
        if max(error) > .1 or not changed:
            raise RuntimeError(('missing deformation or incorrect final frame', name, changed, error))
        config(style)

    box(base)
    time.sleep(.8)
    for style in ('flutter', 'glide'):
        config(style)
        # Horizontal/vertical/diagonal, accel/decel, reversal and re-grab before settling.
        for target, n, dt in [((255, 140, 540, 380), 12, .04), ((490, 140, 540, 380), 6, .014),
                              ((490, 275, 540, 380), 7, .02), ((100, 70, 540, 380), 8, .013),
                              ((145, 115, 540, 380), 2, .05)]:
            sweep(target, n, dt)
        endpoint(style, style + '-travel')
        sweep((205, 80, 630, 330), 6)
        sweep((155, 125, 500, 435), 3)
        endpoint(style, style + '-travel-resize-reverse')

    for edge, (sx, sy) in {'left': (-1, 0), 'right': (1, 0), 'top': (0, -1), 'bottom': (0, 1),
                          'tl': (-1, -1), 'tr': (1, -1), 'bl': (-1, 1), 'br': (1, 1)}.items():
        box(base)
        time.sleep(.85)
        for name, delta, n in [('grow', 110, 9), ('shrink', -85, 6)]:
            target = (base[0] - delta * (sx < 0), base[1] - delta * (sy < 0) // 2,
                      base[2] + delta * abs(sx), base[3] + delta * abs(sy) // 2)
            sweep(target, n)
            endpoint('glide', f'{edge}-{name}')

    for name, size in [('small', (140, 96)), ('wide', (990, 200)), ('tall', (225, 580)),
                       ('square', (410, 410)), ('large', (1110, 650))]:
        sweep((45, 40, *size), 8)
        endpoint('glide', 'aspect-' + name)
    # Transfer the displayed deformation to the close worker rather than closing from rest.
    sweep((90, 85, 690, 390), 5)
    send(dict(op='close', id=ident))
    time.sleep(2)
    if any(c['title'] == title for c in json.loads(hc('-j', 'clients'))):
        raise RuntimeError('close during resize retained a live window')
