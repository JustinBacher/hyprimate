#!/usr/bin/env python3
"""Private nested-session specimen; stdin JSON commands are the sole controller."""
import json
import math
import os
import sys
import threading
import gi
import cairo

gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, GLib

windows = {}

def draw(widget, context, spec):
    width, height = widget.get_allocated_width(), widget.get_allocated_height()
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
        window.connect('delete-event', lambda *_: False)
        windows[spec['id']] = window
        window.show_all()
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
