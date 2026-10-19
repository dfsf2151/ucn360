from PIL import Image

def to_argb(im):
    im = im.convert("RGBA")
    out = []
    w,h = im.size
    for y in range(h):
        for x in range(w):
            r,g,b,a = im.getpixel((x,y))
            out.append((a<<24)|(r<<16)|(g<<8)|b)
    return out,w,h

def gen(name, im):
    arr,w,h = to_argb(im)
    with open(f"/home/mohamd/Projects/UCN_MVP_Xbox360/src/{name}.cpp","w") as f:
        f.write(f"#include \"gfx.h\"\n")
        f.write(f"const uint32_t {name}_pixels[{w*h}] = {{\n")
        for i,val in enumerate(arr):
            f.write(f"0x{val:08X}u,")
            if (i+1)%8==0: f.write("\n")
        f.write("};\n")
        f.write(f"const Image {name} = {{ {name}_pixels, {w}, {h} }};\n")
    with open(f"/home/mohamd/Projects/UCN_MVP_Xbox360/src/{name}.h","w") as f:
        f.write("#ifndef "+name.upper()+"_H\n#define "+name.upper()+"_H\n")
        f.write("#include \"gfx.h\"\n")
        f.write(f"extern const Image {name};\n")
        f.write("#endif\n")

office = Image.open("/home/mohamd/Downloads/ucn/resources/office.png").resize((640,360), Image.LANCZOS)
gen("bg_office", office)

# small sprites
freddy = Image.new("RGBA",(64,96),(90,60,30,255))
# eyes
for (x,y) in [(16,20),(36,20)]:
    for dx in range(4):
        for dy in range(4):
            freddy.putpixel((x+dx,y+dy),(255,220,50,255))
gen("spr_freddy", freddy)

foxy = Image.new("RGBA",(64,96),(170,40,40,255))
for (x,y) in [(16,20),(36,20)]:
    for dx in range(4):
        for dy in range(4):
            foxy.putpixel((x+dx,y+dy),(255,255,255,255))
gen("spr_foxy", foxy)
print("done")
