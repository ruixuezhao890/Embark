from PIL import Image, ImageDraw, ImageFont
FONT = 'app/aidogui_ui/assets/font/ui_heiti.ttf'
W, H = 240, 320
WHITE, GRAY, GREEN, AMBER = (255,255,255), (0x6b,0x72,0x80), (0x4a,0xde,0x80), (0xfb,0xbf,0x24)
f = ImageFont.truetype(FONT, 14)
def render(path, checks, fonts_text, fonts_color, ui_text, ui_color, t):
    img = Image.new('RGB', (W, H), (0x10,0x10,0x10))
    d = ImageDraw.Draw(img)
    def centered(text, cy, color):
        w = d.textlength(text, font=f)
        d.text((W/2 - w/2, cy - 8), text, font=f, fill=color)
    def row(text, top, color):
        d.text((28, top), text, font=f, fill=color)
        return 28 + d.textlength(text, font=f)
    centered('EMBARK 0.1.0', 52, WHITE)
    centered('esp32 240x320', 74, GRAY)
    widest = 0
    for i, (text, color) in enumerate(checks):
        widest = max(widest, row(text, 96 + i*20, color))
    widest = max(widest, row(fonts_text, 96 + len(checks)*20, fonts_color))
    widest = max(widest, row(ui_text, 96 + (len(checks)+1)*20, ui_color))
    bar = 96 + (len(checks)+2)*20 + 12
    d.rectangle([28, bar, 212, bar+6], fill=(0x2e,0x33,0x3b))
    left = int(184 * (0.62 if 'fonts' in fonts_text else 1.0))
    d.rectangle([28, bar, 28+left, bar+6], fill=(0x4a,0xde,0x80) if 'ready' in fonts_text else AMBER)
    label = 't=%u ms' % t
    w = d.textlength(label, font=f)
    d.text((212 - w, bar + 22), label, font=f, fill=GRAY)
    img.resize((W*2, H*2), Image.NEAREST).save(path)
    return widest

mid = [('[OK] flash 16 MB', GREEN), ('[OK] psram 8 MB', GREEN), ('[OK] display 240x320', GREEN),
       ('[OK] touch cst328', GREEN), ('[..] sdcard SD16G 3504 MB', GRAY),
       ('[  ] font pool 192 KB', GRAY)]
w1 = render('.scratch/font-ttf/shots/post_mock3.png', mid, '[..] fonts 62%  2380 KB', AMBER, '[  ] ui', GRAY, 5340)
done = [(c[0].replace('[..]', '[OK]').replace('[  ]', '[OK]'), GREEN) for c in mid]
w2 = render('.scratch/font-ttf/shots/post_mock4.png', done, '[OK] fonts 7/7 ready', GREEN, '[OK] ui', GREEN, 9480)
print('widest mid', round(w1), 'widest done', round(w2), 'screen', W)
