from PIL import Image, ImageDraw

pairs = [("launcher", "bin_launcher"), ("launch26", "bin_launch26"), ("set128", "bin_set128")]
w, h = 240, 320
grid = Image.new("RGB", (w * 2 + 12, h * 3 + 24), (20, 20, 24))
for i, (ttf, binf) in enumerate(pairs):
    a = Image.open(f".scratch/font-ttf/shots/{binf}.bmp").convert("RGB")
    b = Image.open(f".scratch/font-ttf/shots/{ttf}.bmp").convert("RGB")
    y = i * (h + 8)
    grid.paste(a, (0, y))
    grid.paste(b, (w + 12, y))
grid = grid.resize((grid.width * 2, grid.height * 2), Image.NEAREST)
grid.save(".scratch/font-ttf/shots/compare_screens.png")
print("ok", grid.size)
