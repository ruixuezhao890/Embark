import sys
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
img = Image.open(src).convert('RGB')
img = img.resize((img.width * 2, img.height * 2), Image.NEAREST)
img.save(dst)
print('ok', img.size)
