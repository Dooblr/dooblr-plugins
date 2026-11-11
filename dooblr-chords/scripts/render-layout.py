#!/usr/bin/env python3
"""Dev tool: draws the device's presentation layout to a PNG (needs Pillow).

    node scripts/render-layout.js | python3 scripts/render-layout.py out.png

Live's own widgets are approximated (live.dial, live.menu, live.tab, ...);
the chord display is replayed from the real jsui paint calls. It is a
mockup for reviewing the layout, not a screenshot of Live.
"""
import json, math, sys
from PIL import Image, ImageDraw, ImageFont

S = 3  # pixel scale
FONTS = {
    False: '/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf',
    True: '/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf',
}
_cache = {}
def font(pt, bold=False):
    key = (round(pt * S * 1.0), bold)
    if key not in _cache:
        _cache[key] = ImageFont.truetype(FONTS[bold], key[0])
    return _cache[key]

def rgba(c, alpha_mul=1.0):
    c = list(c) + [1] * (4 - len(c))
    return tuple(int(round(max(0, min(1, v)) * 255)) for v in c[:3]) + (int(round(c[3] * alpha_mul * 255)),)

def hexc(h, a=1.0):
    h = h.lstrip('#')
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), int(round(a * 255)))

NOTE = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']

def value_text(b, value):
    p = b.get('param') or {}
    if p.get('parameter_enum'):
        return p['parameter_enum'][int(value)]
    u = p.get('parameter_unitstyle', 0)
    if u == 5:
        return '%d %%' % value
    if u == 8:
        return NOTE[int(value) % 12] + str(int(value) // 12 - 2)
    return str(int(value))

def initial(b, values):
    if b.get('varname') in values:
        return values[b['varname']]
    p = b.get('param') or {}
    init = p.get('parameter_initial')
    if init:
        return init[0]
    return p.get('parameter_mmin', 0)

class Canvas:
    def __init__(self, img, ox, oy):
        self.img = img
        self.ox, self.oy = ox, oy
        self.d = ImageDraw.Draw(img, 'RGBA')

    def xy(self, x, y):
        return (self.ox + x * S, self.oy + y * S)

    def rect(self, r, fill=None, outline=None, radius=0, width=1):
        x, y, w, h = r
        box = [self.xy(x, y), (self.ox + (x + w) * S - 1, self.oy + (y + h) * S - 1)]
        if radius:
            self.d.rounded_rectangle(box, radius=radius * S, fill=fill, outline=outline, width=int(width * S))
        else:
            self.d.rectangle(box, fill=fill, outline=outline, width=int(width * S) if outline else 0)

    def text(self, s, x, y, pt, color, bold=False, anchor='ls'):
        if len(color) < 4 or color[3] == 255:
            self.d.text(self.xy(x, y), s, font=font(pt, bold), fill=color, anchor=anchor)
            return
        # ImageDraw ignores the ink alpha for text, so blend through a layer.
        layer = Image.new('RGBA', self.img.size, (0, 0, 0, 0))
        ImageDraw.Draw(layer).text(self.xy(x, y), s, font=font(pt, bold), fill=color, anchor=anchor)
        self.img.paste(layer, (0, 0), layer)

    def text_w(self, s, pt, bold=False):
        return self.d.textlength(s, font=font(pt, bold)) / S

    def wrap(self, s, r, pt, color, bold=False):
        x, y, w, h = r
        words, lines, cur = s.split(' '), [], ''
        for word in words:
            t = (cur + ' ' + word).strip()
            if self.text_w(t, pt, bold) <= w - 4 or not cur:
                cur = t
            else:
                lines.append(cur)
                cur = word
        lines.append(cur)
        lh = pt * 1.25
        for i, line in enumerate(lines):
            if (i + 1) * lh > h + 2:
                break
            self.text(line, x + 2, y + 2 + pt + i * lh, pt, color, bold)

def visible(b, state):
    panel = b.get('panel')
    if panel:
        return state.get('panel') == panel
    if b.get('varname') == 'srcbadge':
        return bool(state.get('badge'))
    return True

def draw_box(c, b, state):
    if not visible(b, state):
        return
    cls = b['maxclass']
    r = b['presentation_rect']
    x, y, w, h = r
    on = b.get('varname') not in state.get('inactive', [])
    pt = b.get('fontsize', 9)
    bold = 'Bold' in b.get('fontname', '')
    if cls == 'panel':
        c.rect(r, fill=rgba(b['bgcolor']))
    elif cls == 'comment':
        text = b.get('text', '')
        if b.get('varname') == 'srcinfo':
            text = state.get('srcinfo', text)
        if b.get('varname') == 'srcbadge':
            text = state.get('badge') or text
        c.wrap(text, r, pt, rgba(b.get('textcolor', [0, 0, 0, 1])), bold)
    elif cls == 'live.text':
        val = initial(b, state['values']) if b.get('mode') == 1 else 0
        if on:
            bg = b['activebgoncolor'] if val else b['activebgcolor']
            tc = b['activetextoncolor'] if val else b['activetextcolor']
        else:
            bg = b['bgoncolor'] if val else b['bgcolor']
            tc = b['textoncolor'] if val else b['textcolor']
        c.rect(r, fill=rgba(bg), outline=rgba(b.get('bordercolor', bg)), radius=2)
        c.text(b.get('texton') if val else b.get('text', ''), x + w / 2, y + h / 2, pt, rgba(tc), bold, 'mm')
    elif cls in ('live.menu', 'live.numbox'):
        val = initial(b, state['values'])
        if b.get('varname') == 'seed':
            val = 48213
        a = 1 if on else 0.38
        c.rect(r, fill=rgba(b['activebgcolor'], a), outline=rgba(b.get('bordercolor', [0, 0, 0, 1])), radius=2)
        c.text(value_text(b, val), x + 5, y + h / 2, pt, rgba(b['textcolor'], a), bold, 'lm')
        tri = rgba(b.get('activetricolor' if cls == 'live.menu' else 'tricolor', [0.5, 0.5, 0.5, 1]), a)
        tx, ty, ts = (x + w - 8, y + h / 2, 3) if cls == 'live.menu' else (x + w - 6, y + h / 2, 2.5)
        if cls == 'live.menu':
            c.d.polygon([c.xy(tx - ts, ty - ts / 2), c.xy(tx + ts, ty - ts / 2), c.xy(tx, ty + ts / 2 + 0.5)], fill=tri)
        else:
            c.d.polygon([c.xy(tx - ts / 2, ty - ts), c.xy(tx - ts / 2, ty + ts), c.xy(tx + ts / 2, ty)], fill=tri)
    elif cls == 'live.tab':
        enum = b['param']['parameter_enum']
        val = int(initial(b, state['values']))
        n = len(enum)
        sw = w / n
        for i, label in enumerate(enum):
            sel = i == val
            if on:
                bg = b['activebgoncolor'] if sel else b['activebgcolor']
                tc = b['activetextoncolor'] if sel else b['activetextcolor']
            else:
                bg = b['bgoncolor'] if sel else b['bgcolor']
                tc = b['textoncolor'] if sel else b['textcolor']
            seg = [x + i * sw + (1 if i else 0), y, sw - (1 if i else 0), h]
            c.rect(seg, fill=rgba(bg), outline=rgba(b.get('bordercolor', bg)))
            c.text(label, seg[0] + seg[2] / 2, y + h / 2, pt, rgba(tc), bold, 'mm')
    elif cls == 'live.dial':
        val = initial(b, state['values'])
        p = b['param']
        lo, hi = p.get('parameter_mmin', 0), p.get('parameter_mmax', 127)
        frac = (val - lo) / float(hi - lo or 1)
        tc = rgba(b['textcolor'])
        th = pt + 2
        show_name = b.get('showname', 1)
        show_num = b.get('shownumber', 1)
        top = th if show_name else 2
        bot = th if show_num else 2
        if show_name:
            c.text(p['parameter_shortname'], x + w / 2, y + th / 2, pt, tc, False, 'mm')
        if show_num:
            c.text(value_text(b, val), x + w / 2, y + h - bot / 2, pt, tc, False, 'mm')
        rad = max(4, min(w, h - top - bot) / 2 - 2)
        cx, cy = x + w / 2, y + top + (h - top - bot) / 2
        box = [c.xy(cx - rad, cy - rad), c.xy(cx + rad, cy + rad)]
        lw = max(2, int(round(max(1.5, rad * 0.16) * S)))
        c.d.arc(box, 135, 405, fill=rgba(b['dialcolor']), width=lw)
        if frac > 0:
            c.d.arc(box, 135, 135 + 270 * frac, fill=rgba(b['activedialcolor']), width=lw)
        ang = math.radians(135 + 270 * frac)
        c.d.line([c.xy(cx + math.cos(ang) * rad * 0.2, cy + math.sin(ang) * rad * 0.2),
                  c.xy(cx + math.cos(ang) * (rad - 0.5), cy + math.sin(ang) * (rad - 0.5))],
                 fill=rgba(b['needlecolor']), width=max(2, int(1.5 * S)))
    elif cls == 'live.drop':
        c.rect(r, fill=rgba(b['bgcolor']), outline=rgba(b['bordercolor']), radius=2)
        text = state.get('dropped') or b.get('legend', '')
        c.text(text, x + w / 2, y + h / 2, pt, rgba(b['textcolor'] if not state.get('dropped') else [0.902, 0.91, 0.922, 1]), False, 'mm')
    elif cls == 'jsui':
        replay(c, state['jsui'])

def replay(c, js):
    x0, y0 = js['rect'][0], js['rect'][1]
    color = (0, 0, 0, 255)
    path = []
    face, pt, lw = 'Arial', 10, 1
    pos = (0, 0)
    for op in js['ops']:
        k, a = op[0], op[1:]
        if k == 'set_source_rgba':
            color = rgba(a)
        elif k == 'rectangle':
            path.append(('rect', a, 0))
        elif k == 'rectangle_rounded':
            path.append(('rect', a[:4], a[4] / 2.0))
        elif k == 'ellipse':
            path.append(('ellipse', a))
        elif k == 'arc':
            path.append(('arc', a))
        elif k in ('fill', 'stroke'):
            for p in path:
                if p[0] == 'rect':
                    rx, ry, rw, rh = p[1]
                    rr = [x0 + rx, y0 + ry, rw, rh]
                    if k == 'fill':
                        c.rect(rr, fill=color, radius=p[2])
                    else:
                        c.rect(rr, outline=color, radius=p[2], width=lw)
                elif p[0] == 'ellipse':
                    ex, ey, ew, eh = p[1]
                    c.d.ellipse([c.xy(x0 + ex, y0 + ey), c.xy(x0 + ex + ew, y0 + ey + eh)], fill=color)
                elif p[0] == 'arc':
                    ax, ay, ar, a1, a2 = p[1]
                    bb = [c.xy(x0 + ax - ar, y0 + ay - ar), c.xy(x0 + ax + ar, y0 + ay + ar)]
                    c.d.arc(bb, math.degrees(a1), math.degrees(a2), fill=color, width=max(1, int(lw * S)))
            path = []
        elif k == 'set_line_width':
            lw = a[0]
        elif k == 'select_font_face':
            face = a[0]
        elif k == 'set_font_size':
            pt = a[0]
        elif k == 'move_to':
            pos = (a[0], a[1])
        elif k == 'show_text':
            c.text(a[0], x0 + pos[0], y0 + pos[1], pt, color, 'Bold' in face, 'ls')

def main():
    data = json.load(sys.stdin)
    out = sys.argv[1]
    W, H = data['width'], data['height']
    cap = 22
    pad = 16
    n = len(data['states'])
    img = Image.new('RGB', (W * S + 2 * pad * S // 2, n * (H * S + cap * S) + pad * S // 2), (40, 42, 46))
    for i, st in enumerate(data['states']):
        oy = i * (H * S + cap * S) + cap * S
        ox = pad * S // 2
        c = Canvas(img, ox, oy)
        c.text('%d. %s  (%d x %d px)' % (i + 1, st['title'], W, H), 0, -7, 10, (200, 204, 210, 255), True)
        for b in data['boxes']:
            draw_box(c, b, st)
    img.convert('RGB').save(out)
    print('wrote %s (%dx%d)' % (out, img.size[0], img.size[1]))

if __name__ == '__main__':
    main()
