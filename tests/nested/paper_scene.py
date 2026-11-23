#!/usr/bin/env python3
"""Private nested-session specimen; stdin JSON commands are the sole controller."""
import json
import math
import os
import sys
import threading
import time
import gi
import cairo

gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, GLib

windows = {}

def installation_print(context, width, height, spec):
    """Asymmetric print; the lifting panel carries the L, not the upright's edge code."""
    dark = spec['construction'] == 'dark'
    context.set_source_rgb(*((.09, .12, .17) if dark else (.94, .91, .81)))
    context.paint()
    context.select_font_face('DejaVu Sans Mono', cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_BOLD)
    context.set_font_size(14)
    for y in range(0, height, 48):
        for x in range(0, width, 90):
            context.set_source_rgb(*((.50, .69, .74) if dark else (.15, .30, .37)))
            context.rectangle(x, y, 72 - 8 * ((x // 90 + y // 48) % 4), 2)
            context.fill()
            context.move_to(x + 3, y + 24)
            context.show_text(f'{x // 90:02}:{y // 48:02}')
    # Independent privacy sentinel in the permanently installed upper bay.
    context.set_source_rgb(71/255, 13/255, 250/255)
    context.rectangle(width - 4, height * .27, 4, height * .19)
    context.fill()
    generation = spec.get('generation', 0)
    rgb = (240, 25, 186) if generation else (18, 244, 173)
    context.set_source_rgb(*(c / 255 for c in rgb))
    x, y = int(width * .76), int(height * .73)
    context.rectangle(x, y, 10, 28)
    context.rectangle(x + 10, y + 18, 21, 10)
    context.fill()
    # Optional public fixture marks: no change to ordinary opening specimens.
    for rgb, u, v in spec.get('evidence_marks', ()):
        context.set_source_rgb(*(c / 255 for c in rgb))
        x, y = round(width*u), round(height*v)
        context.rectangle(x, y, 13, 9)
        context.rectangle(x+3, y+9, 5, 7)
        context.fill()
    if spec.get('_painted') != generation:
        print(json.dumps(dict(id=spec['id'], generation=generation, width=width, height=height,
                              painted_monotonic_ns=time.monotonic_ns())), flush=True)
        spec['_painted'] = generation

def draw(widget, context, spec):
    width, height = widget.get_allocated_width(), widget.get_allocated_height()
    if 'construction' in spec:
        installation_print(context, width, height, spec)
        return True
    context.set_source_rgb(.96, .95, .90)
    context.paint()
    context.select_font_face('DejaVu Sans Mono', cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_NORMAL)
    context.set_font_size(10)
    for row in range(18, height - 8, 13):
        context.set_source_rgb(.08, .10, .14)
        context.move_to(9, row)
        label = spec.get('content_id', spec['id'])
        context.show_text(f'{row:04d} PAPER {label}  abcdefghijklmnopqrstuvwxyz 0123456789 ' * 8)
    if spec.get('image'):
        context.save()
        context.rectangle(width * .15, height * .22, width * .7, height * .56)
        context.clip()
        for i in range(70):
            context.set_source_rgb(.2 + .6 * (i % 7) / 7, .25 + .6 * (i % 11) / 11, .7)
            context.arc(width * (.5 + .35 * math.sin(i * 2.4)), height * (.5 + .25 * math.cos(i)), 20 + i % 20, 0, math.tau)
            context.fill()
        context.restore()
    if 'new_column' in spec and width > spec['new_column']:
        # These texels don't exist in the original buffer. A scaled snapshot
        # cannot pass the live-resize colour check in paper_interactions.
        context.set_source_rgb(0, 1, 0)
        context.rectangle(spec['new_column'], height * .4, min(24, width - spec['new_column']), 32)
        context.fill()
    return True

def command(spec):
    if spec['op'] == 'open':
        window = Gtk.Window(title='PaperEvidence-' + str(spec['id']))
        window.set_wmclass('paper-evidence', 'paper-evidence')
        window.set_decorated(False)
        window.set_default_size(spec['width'], spec['height'])
        window.connect('draw', draw, spec)
        if 'construction' in spec:
            window.construction_print = spec
        window.connect('delete-event', lambda *_: False)
        windows[spec['id']] = window
        window.show_all()
    elif spec['op'] == 'generation':
        window = windows.get(spec['id'])
        if window and hasattr(window, 'construction_print'):
            window.construction_print['generation'] = spec['generation']
            window.queue_draw()
    elif spec['op'] == 'close':
        window = windows.pop(spec['id'], None)
        if window:
            window.destroy()
    elif spec['op'] == 'quit':
        Gtk.main_quit()
    return False

def reader():
    for line in sys.stdin:
        GLib.idle_add(command, json.loads(line))
    GLib.idle_add(Gtk.main_quit)

threading.Thread(target=reader, daemon=True).start()
print(json.dumps({'ready': True, 'runtime': os.environ['XDG_RUNTIME_DIR']}), flush=True)
Gtk.main()
