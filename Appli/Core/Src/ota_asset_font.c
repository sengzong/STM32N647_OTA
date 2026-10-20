/* Bounded on-card bitmap font; no filesystem access occurs during rendering. */
#include "ota_assets.h"
#include <string.h>

static uint16_t u16(const uint8_t *p) { return (uint16_t)p[0]|(uint16_t)p[1]<<8; }
static uint32_t u32(const uint8_t *p) { return (uint32_t)u16(p)|(uint32_t)u16(p+2)<<16; }
static uint32_t crc32(const uint8_t *p,uint32_t n)
{
    uint32_t crc=~0u;
    while(n--) { crc^=*p++; for(unsigned j=0;j<8;j++) crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u); }
    return ~crc;
}
static const uint8_t *find_record(const OtaAssetFont *font,uint32_t code)
{
    uint32_t first=0u,last=font->count;
    while(first<last) {
        uint32_t middle=first+(last-first)/2u;
        const uint8_t *record=font->records+middle*20u;
        uint32_t value=u32(record);
        if(value==code) return record;
        if(value<code) first=middle+1u; else last=middle;
    }
    return NULL;
}
static bool glyph_descriptor(const lv_font_t *font,lv_font_glyph_dsc_t *out,uint32_t code,uint32_t next)
{
    const uint8_t *r=find_record(font->dsc,code);(void)next;
    if(!r) return false;
    out->adv_w=u16(r+8);out->box_w=u16(r+10);out->box_h=u16(r+12);
    out->ofs_x=(int16_t)u16(r+14);out->ofs_y=(int16_t)u16(r+16);
    out->bpp=4u;out->is_placeholder=0u;
    return true;
}
static const uint8_t *glyph_bitmap(const lv_font_t *font,uint32_t code)
{
    const OtaAssetFont *state=font->dsc;
    const uint8_t *r=find_record(state,code);
    return r ? state->bitmap+u32(r+4) : NULL;
}
int ota_asset_font_bind(OtaAssetFont *font,const uint8_t *data,uint32_t length)
{
    uint32_t count,bitmap_length,header=32u,previous=0u;
    if(!font || !data || length<32u || length>256u*1024u) return -1;
    count=u32(data+8);bitmap_length=u32(data+12);
    if(u32(data)!=0x31464f53u || u32(data+4)!=1u || !count || count>4096u ||
       u32(data+16)<8u || u32(data+16)>64u || u32(data+20)>u32(data+16) ||
       u32(data+24)!=4u || count>(length-header)/20u ||
       bitmap_length!=length-header-count*20u || crc32(data+header,length-header)!=u32(data+28)) return -1;
    for(uint32_t i=0u;i<count;i++) {
        const uint8_t *r=data+header+i*20u;
        uint32_t code=u32(r),offset=u32(r+4),w=u16(r+10),h=u16(r+12),bytes=(w*h+1u)/2u;
        if(!code || code>0x10ffffu || (code>=0xd800u && code<=0xdfffu) ||
           (i && code<=previous) || u16(r+8)>128u || w>64u || h>64u ||
           offset>bitmap_length || bytes>bitmap_length-offset) return -1;
        previous=code;
    }
    /* Publish only after every offset and the entire payload have been checked. */
    memset(font,0,sizeof(*font));
    font->count=count;font->records=data+header;font->bitmap=data+header+count*20u;
    font->font.get_glyph_dsc=glyph_descriptor;font->font.get_glyph_bitmap=glyph_bitmap;
    font->font.line_height=(lv_coord_t)u32(data+16);font->font.base_line=(lv_coord_t)u32(data+20);
    font->font.underline_position=-2;font->font.underline_thickness=1;
    font->font.dsc=font;
    return 0;
}
