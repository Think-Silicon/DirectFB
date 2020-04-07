#include "ublender.h"

//not tested thoroughly
#define FAST_FONTS

typedef union
{
    unsigned rgba32;
    unsigned char RGBA[4];
    struct
    {
        unsigned char r;
        unsigned char g;
        unsigned char b;
        unsigned char a;
    } rgba8;
} RGBA;

#define MODULATE(a,b)    do { (a) = (((int)(a) * ((int)(b) + 1)) >> 8); } while (0)

void precalculate_source_colour(unsigned DrawFlags, unsigned SrcBlend) {
    DEBUG_PROC_ENTRY;

    RGBA precalc_src, src;
    src.rgba32         = st_dev.gl_color;
    DEBUG_OP("before premult: 0x%08x\n", src.rgba32);

    if ( DrawFlags & DSDRAW_SRC_PREMULTIPLY ) {
        MODULATE( src.rgba8.r, src.rgba8.a );
        MODULATE( src.rgba8.g, src.rgba8.a );
        MODULATE( src.rgba8.b, src.rgba8.a );
    }

    precalc_src.rgba32 = src.rgba32;

    DEBUG_OP("Precalc for srcblend 0x%08x\n", SrcBlend);


    ///got it from tools/dfbfx.c
    if (st_dev.drawingflags == DSDRAW_BLEND)
    switch( SrcBlend ) {
            /* Sargb *= 0.0 */
            case DSBF_ZERO:
                 precalc_src.rgba8.r = 0;
                 precalc_src.rgba8.g = 0;
                 precalc_src.rgba8.b = 0;
                 break;

//            nothing to do here... just go to default
//            /* Sargb *= 1.0 */
//            case DSBF_ONE:
//                 break;

            /* Sargb *= Sargb */
            case DSBF_SRCCOLOR:
//                 MODULATE( precalc_src.rgba8.a, src.rgba8.a );
                 MODULATE( precalc_src.rgba8.r, src.rgba8.r );
                 MODULATE( precalc_src.rgba8.g, src.rgba8.g );
                 MODULATE( precalc_src.rgba8.b, src.rgba8.b );
                 break;

            /* Sargb *= 1.0 - Sargb */
            case DSBF_INVSRCCOLOR:
//                 MODULATE( precalc_src.rgba8.a, src.rgba8.a ^ 0xff );
                 MODULATE( precalc_src.rgba8.r, src.rgba8.r ^ 0xff );
                 MODULATE( precalc_src.rgba8.g, src.rgba8.g ^ 0xff );
                 MODULATE( precalc_src.rgba8.b, src.rgba8.b ^ 0xff );
                 break;

            /* Sargb *= Saaaa */
            case DSBF_SRCALPHA:
//                 MODULATE( precalc_src.rgba8.a, src.rgba8.a );
                 MODULATE( precalc_src.rgba8.r, src.rgba8.a );
                 MODULATE( precalc_src.rgba8.g, src.rgba8.a );
                 MODULATE( precalc_src.rgba8.b, src.rgba8.a );
                 break;

            /* Sargb *= 1.0 - Saaaa */
            case DSBF_INVSRCALPHA:
//                 MODULATE( precalc_src.rgba8.a, src.rgba8.a ^ 0xff );
                 MODULATE( precalc_src.rgba8.r, src.rgba8.a ^ 0xff );
                 MODULATE( precalc_src.rgba8.g, src.rgba8.a ^ 0xff );
                 MODULATE( precalc_src.rgba8.b, src.rgba8.a ^ 0xff );
                 break;

            default:
                 break;
    }

    st_dev.c2_is_gl_col   = 1;
    st_dev.c2_precalc_col = precalc_src.rgba32;

#ifdef FAST_FONTS
    nema_set_tex_color(st_dev.gl_color);
#endif
    nema_set_const_color(st_dev.gl_color);
    nema_set_raster_color(precalc_src.rgba32);
}

#undef MODULATE

void TSgfx_setDrawBlend(void) {
    DEBUG_PROC_ENTRY;

    const unsigned drawingflags = st_dev.drawingflags;

    st_dev.lastDrawFlags     = drawingflags;
    st_dev.lastDrawSrcBlend  = st_dev.src_blend;

    DEBUG_OP("drawblend: SRC: %d, DST: %d\n", st_dev.src_blend, st_dev.dst_blend);
    DEBUG_OP("drawblend: flags: 0x%x -> dckey %d spre %d dpre %d blend %d dmul %d xor %d\n",
                st_dev.drawingflags,
                st_dev.drawingflags & DSDRAW_DST_COLORKEY,
                st_dev.drawingflags & DSDRAW_SRC_PREMULTIPLY,
                st_dev.drawingflags & DSDRAW_DST_PREMULTIPLY,
                st_dev.drawingflags & DSDRAW_BLEND,
                st_dev.drawingflags & DSDRAW_DEMULTIPLY,
                st_dev.drawingflags & DSDRAW_XOR                   );

    int src_blend = NEMA_BF_ONE;
    int dst_blend = NEMA_BF_ZERO;
    int blend_ops = 0;

    precalculate_source_colour(drawingflags, st_dev.src_blend);

    if ( st_dev.drawingflags & DSDRAW_DST_COLORKEY    )
        blend_ops |= NEMA_BLOP_DST_CKEY;

    if ( st_dev.drawingflags & DSDRAW_DST_PREMULTIPLY )
        printf("%s:%d -> not supported\n", __FILE__, __LINE__);
    if ( st_dev.drawingflags & DSDRAW_BLEND           ) {
        if ( st_dev.src_blend == DSBF_ZERO || st_dev.src_blend > DSBF_INVSRCALPHA)
        	src_blend = st_dev.src_blend-1;
        dst_blend = st_dev.dst_blend-1;
    }
    if ( st_dev.drawingflags & DSDRAW_DEMULTIPLY      )
        printf("%s:%d -> not supported\n", __FILE__, __LINE__);
    if ( st_dev.drawingflags & DSDRAW_XOR             )
        printf("%s:%d -> not supported\n", __FILE__, __LINE__);

    nema_set_blend_fill( nema_blending_mode(src_blend, dst_blend, blend_ops) );
}

void TSgfx_setBlitBlend(void) {
    DEBUG_PROC_ENTRY;

    DEBUG_OP("blitblend: SRC: %d, DST: %d\n", st_dev.src_blend, st_dev.dst_blend);
    DEBUG_OP("blitblend: flags: 0x%x -> sckey %d dckey %d Acol %d Achan %d colorize %d spre %d dpre %d sprecol %d dmul %d xor %d\n",
                st_dev.blittingflags,
                st_dev.blittingflags & DSBLIT_SRC_COLORKEY,
                st_dev.blittingflags & DSBLIT_DST_COLORKEY,
                st_dev.blittingflags & DSBLIT_BLEND_COLORALPHA,
                st_dev.blittingflags & DSBLIT_BLEND_ALPHACHANNEL,
                st_dev.blittingflags & DSBLIT_COLORIZE,
                st_dev.blittingflags & DSBLIT_SRC_PREMULTIPLY,
                st_dev.blittingflags & DSBLIT_DST_PREMULTIPLY,
                st_dev.blittingflags & DSBLIT_SRC_PREMULTCOLOR,
                st_dev.blittingflags & DSBLIT_DEMULTIPLY,
                st_dev.blittingflags & DSBLIT_XOR                   );

    DEBUG_OP("colour: 0x%08x\n", st_dev.gl_color);

    int blend_ops = 0;
    int src_blend = NEMA_BF_ONE;
    int dst_blend = NEMA_BF_ZERO;

    if ( st_dev.blittingflags & (DSBLIT_BLEND_ALPHACHANNEL|DSBLIT_BLEND_COLORALPHA) ) {
        src_blend = st_dev.src_blend-1;
        dst_blend = st_dev.dst_blend-1;
    }

    if ( st_dev.blittingflags & DSBLIT_BLEND_COLORALPHA   )
        blend_ops |= NEMA_BLOP_MODULATE_A;

    if ( st_dev.blittingflags & DSBLIT_COLORIZE && st_dev.src_format != NEMA_RGBA0008 )
        blend_ops |= NEMA_BLOP_MODULATE_RGB;

    if ( st_dev.blittingflags & DSBLIT_SRC_COLORKEY       )
        blend_ops |= NEMA_BLOP_SRC_CKEY;
    if ( st_dev.blittingflags & DSBLIT_DST_COLORKEY       )
        blend_ops |= NEMA_BLOP_DST_CKEY;

    if ( st_dev.blittingflags & DSBLIT_SRC_PREMULTIPLY    )
        blend_ops |= NEMA_BLOP_SRC_PREMULT;

    if ( st_dev.blittingflags & DSBLIT_DST_PREMULTIPLY    )
        printf("%s:%d -> not supported\n", __FILE__, __LINE__);
    if ( st_dev.blittingflags & DSBLIT_SRC_PREMULTCOLOR   )
        printf("%s:%d -> not supported\n", __FILE__, __LINE__);
    if ( st_dev.blittingflags & DSBLIT_DEMULTIPLY         )
        printf("%s:%d -> not supported\n", __FILE__, __LINE__);
    if ( st_dev.blittingflags & DSBLIT_XOR                )
        printf("%s:%d -> not supported\n", __FILE__, __LINE__);

    nema_set_blend_blit( nema_blending_mode(src_blend, dst_blend, blend_ops) );
}
