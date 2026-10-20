"""Export the upstream CJK glyphs into a bounded SD-card LVGL font resource."""
from pathlib import Path
import re
import struct
import zlib
import json
import hashlib

ROOT=Path(__file__).resolve().parents[1]

def integers(text):
    text=re.sub(r"/\*.*?\*/|//[^\n]*","",text,flags=re.S)
    return [int(x,0) for x in re.findall(r"-?0x[0-9a-fA-F]+|-?\d+",text)]

def export_data():
    path=ROOT/"Middlewares/LVGL/src/font/lv_font_simsun_16_cjk.c"
    source=path.read_text(encoding="utf-8")
    def array(name):
        return re.search(r"\b"+name+r"\[\]\s*=\s*\{(.*?)\};",source,re.S).group(1)
    bitmap=bytes(integers(array("glyph_bitmap")))
    glyphs=[]
    for raw in re.findall(r"\{(.*?)\}",array("glyph_dsc"),re.S):
        glyphs.append({key:int(value) for key,value in re.findall(r"\.(\w+)\s*=\s*(-?\d+)",raw)})
    glyph_map={}
    for raw in re.findall(r"\{(.*?)\}",array("cmaps"),re.S):
        fields=dict(re.findall(r"\.(\w+)\s*=\s*([^,\n}]+)",raw))
        fields={k:v.strip() for k,v in fields.items()}
        start=int(fields["range_start"]);gid=int(fields["glyph_id_start"])
        offsets=integers(array(fields["glyph_id_ofs_list"])) if fields["glyph_id_ofs_list"]!="NULL" else None
        points=integers(array(fields["unicode_list"])) if fields["unicode_list"]!="NULL" else range(int(fields["range_length"]))
        for index,point in enumerate(points):
            glyph_id=gid+(offsets[index] if offsets is not None else index)
            glyph= glyphs[glyph_id]
            size=(glyph["box_w"]*glyph["box_h"]+1)//2
            glyph_map[start+point]=(glyph,bitmap[glyph["bitmap_index"]:glyph["bitmap_index"]+size])
    return glyph_map,path

def pack_font(glyphs):
    records=bytearray();bitmaps=bytearray()
    for code,(glyph,bmp) in sorted(glyphs.items()):
        records+=struct.pack("<IIHHHhhH",code,len(bitmaps),(glyph["adv_w"]+8)//16,
                             glyph["box_w"],glyph["box_h"],glyph["ofs_x"],glyph["ofs_y"],0)
        bitmaps+=bmp
    body=records+bitmaps
    return struct.pack("<8I",0x31464f53,1,len(glyphs),len(bitmaps),19,3,4,zlib.crc32(body))+body

def main():
    glyphs,source=export_data()
    folder=ROOT/"SD_Card/ota_ui";folder.mkdir(parents=True,exist_ok=True)
    data=pack_font(glyphs); (folder/"cjk16.bin").write_bytes(data)
    fallback=pack_font({code:glyphs[code] for code in (0x672a,0x63d0,0x4f9b)})
    text="/* Minimal built-in fallback: the three glyphs in 未提供. */\nstatic const uint8_t fallback_font_data[] = {\n"
    text+="\n".join("    "+",".join(f"0x{x:02x}" for x in fallback[i:i+16])+"," for i in range(0,len(fallback),16))
    text+="\n};\n"
    (ROOT/"Appli/Core/Inc/ota_font_fallback.inc").write_text(text,encoding="utf-8")
    theme=folder/"theme.cfg"
    if not theme.exists():
        theme.write_text("background=0B1420\npanel=182536\naccent=39D598\ntext=F4F8FC\nmuted=8DA5BD\n",encoding="ascii")
    manifest={"source":str(source.relative_to(ROOT)),"source_sha256":hashlib.sha256(source.read_bytes()).hexdigest(),
              "glyphs":len(glyphs),"font_bytes":len(data),"font_sha256":hashlib.sha256(data).hexdigest(),
              "fallback_bytes":len(fallback),"path_on_card":"/ota_ui/cjk16.bin"}
    (ROOT/"SD_Card/manifest.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(manifest,ensure_ascii=True))

if __name__=="__main__":main()
