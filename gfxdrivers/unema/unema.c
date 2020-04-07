    /*
   (c) Copyright 2001-2009  The world wide DirectFB Open Source Community (directfb.org)
   (c) Copyright 2000-2004  Convergence (integrated media) GmbH

   All rights reserved.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/

#include <stdio.h>
#include <memory.h>
#include <fcntl.h>

#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fbdev/fbdev.h>

#include <directfb.h>
#include <core/graphics_driver.h>

#include "unema.h"

#include "nema_core.h"
#include "nema_raster.h"

DFB_GRAPHICS_DRIVER( unema )

#define TSI_DSBLIT_ROTATE_MASK                  (DSBLIT_ROTATE90 | DSBLIT_ROTATE180 | DSBLIT_ROTATE270 | DSBLIT_FLIP_HORIZONTAL | DSBLIT_FLIP_VERTICAL)

#define UNEMA_SUPPORTED_RENDER_OPTIONS          (DSRO_SMOOTH_UPSCALE | DSRO_SMOOTH_DOWNSCALE) //DSRO_MATRIX
#define UNEMA_SUPPORTED_DRAWINGFUNCTIONS        (DFXL_FILLTRAPEZOID | DFXL_FILLTRIANGLE | DFXL_FILLRECTANGLE | DFXL_DRAWRECTANGLE | DFXL_DRAWLINE)
#define UNEMA_SUPPORTED_DRAWINGFLAGS            (DSDRAW_BLEND | DSDRAW_SRC_PREMULTIPLY | DSDRAW_DST_COLORKEY) // | DSDRAW_DST_PREMULTIPLY | DSDRAW_DEMULTIPLY | DSDRAW_XOR
#define UNEMA_SUPPORTED_BLITTINGFUNCTIONS       (DFXL_BLIT | DFXL_STRETCHBLIT | DFXL_TEXTRIANGLES)
#define UNEMA_SUPPORTED_BLITTINGFLAGS           (DSBLIT_BLEND_ALPHACHANNEL | DSBLIT_BLEND_COLORALPHA | DSBLIT_COLORIZE | \
                                                 DSBLIT_SRC_PREMULTIPLY | DSBLIT_DST_COLORKEY | DSBLIT_SRC_COLORKEY |\
                                                 TSI_DSBLIT_ROTATE_MASK ) //  | DSBLIT_DST_PREMULTIPLY | DSBLIT_SRC_PREMULTCOLOR

#if 1
#define CUSTOM_EMIT do {\
    if ( !dfb_config->gfx_emit_early && st_dev.cl[st_dev.cur_cl].offset > st_dev.almost_full )\
        uNema_EmitCommands(drv, dev);\
} while(0)
#else
#define CUSTOM_EMIT

#endif


/**
 * \brief uNema driver data.
 */
typedef struct {
    int gfx_fd;                ///< uNema opened device file descriptor
    unsigned *gfx_regfile;
    unsigned *gfx_mem;
} uNema_DriverData;

typedef enum {
    LD_MAT_IDENTITY    = 0x1,
    LD_MAT_TRANSLATE   = 0x2,
    LD_MAT_SCALE       = 0x3,
    LD_MAT_AFFINE      = 0x4,
    LD_MAT_PERSPECTIVE = 0x5,
    LD_MAT_CUSTOM      = 0x6
} loaded_matrix;

/**
 * \brief uNema device data.
 *
 * Members are set in uNema_SetState() and callees and used in other functions.
 *
 * @see uNema_SetState()
 */
typedef struct {
    unsigned int            src_format;        // Current source surface format
    unsigned int            dst_format;        // Current destination surface format
    unsigned int            src_colorkey;      // Current source surface format
    unsigned int            dst_colorkey;      // Current destination surface format
    unsigned long           v_srcAddr;         // Current source surface physical address
    unsigned int            v_srcStride;       // Current source surface stride
    unsigned long           v_destAddr;        // Current destination surface physical address
    unsigned int            v_destStride;      // Current destination surface stride
    DFBSurfaceBlendFunction src_blend;         // Current source surface blend function
    DFBSurfaceBlendFunction dst_blend;         // Current destination surface blend function

    unsigned int            gl_color;          // global color

    DFBSurfaceDrawingFlags  drawingflags;      // Current drawing flags
    DFBSurfaceBlittingFlags blittingflags;     // Current blitting flags

    unsigned int            lastDrawFlags;
    unsigned int            lastDrawSrcBlend;

    unsigned int            c2_is_gl_col;
    unsigned int            c2_precalc_col;

    unsigned int            lastCodePtr;
    unsigned int            blitCodePtr;
    unsigned int            drawCodePtr;

    unsigned int            drawLoadRastCol;
    unsigned int            lastLoadRastCol;

    unsigned int            bypass_matmult;

    unsigned int            prev_src_blend;
    unsigned int            prev_dst_blend;
    unsigned int            dirty_blend_fill;
    unsigned int            dirty_blend_blit;

    DFBSurfaceRenderOptions render_options;

    nema_matrix3x3_t        matrix;
    nema_cmdlist_t          cl[2];
    int                     cur_cl;
    int                     almost_full;
} uNema_DeviceData;

static uNema_DriverData st_drv = {0};
static uNema_DeviceData st_dev = {0};

/**
 * Converts from DirectFB to uNema surface pixel format constants
 * and also performs validation for unsupported source or
 * destination formats.
 *
 * As of DirectFB 1.4.3, YUV pixel format defines are somewhat confusing, so
 * the following may need to change in the future.
 *
 * @param format A DirectFB surface pixel format constant, defined in include/directfb.h
 * @param flag Indicates whether it is a source of destination surface. Possible values are SMF_SOURCE, SMF_DESTINATION.
 */
static inline int
uNema_video_mode( DFBSurfacePixelFormat format,
                uint                  flag )
{
    switch (format)
    {
        case DSPF_RGB16:        return NEMA_RGB565;
        case DSPF_RGB32:        return NEMA_BGRX8888;
        case DSPF_ARGB:         return NEMA_BGRA8888;
        case DSPF_RGBA4444:     return NEMA_RGBA4444;
        case DSPF_RGBA5551:     return NEMA_RGBA5551;
        case DSPF_A8:           return NEMA_RGBA0008;
        case DSPF_YUY2:         return flag == SMF_SOURCE ? NEMA_UYVY : -1; // FIXME: DirectFB documentation is unclear about YUV surface pixel formats
        case DSPF_A1:           return flag == SMF_SOURCE ? NEMA_BW1 : -1;
        default:                return -1;
    }
}

static inline unsigned
uNema_convert_to_RGBA8888( int format, unsigned colour)
{
    if ( colour == 0 ) return 0;
    unsigned bit = colour & 0x1;
    unsigned conv_colour;
    switch (format)
    {
        case NEMA_RGBA5650: conv_colour =
                             (((colour&0xf800) >>  8)) |
                             (((colour&0xe000) >> 13)) |
                             (((colour&0x07e0) <<  5)) |
                             (((colour&0x0600) >>  1)) |
                             (((colour&0x001f) <<  3)) |
                             (((colour&0x001c) << 14)) ;
                             break;
        case NEMA_BGRX8888: conv_colour =
                             ((       0xff000000)      ) |      //A
                             ((colour&0x00ff0000) >> 16) |      //B
                             ((colour&0x0000ff00)      ) |      //G
                             ((colour&0x000000ff) << 16);       //R
                             break;
        case NEMA_BGRA8888: conv_colour =
                             ((colour&0xff000000)      ) |      //A
                             ((colour&0x00ff0000) >> 16) |      //B
                             ((colour&0x0000ff00)      ) |      //G
                             ((colour&0x000000ff) << 16);       //R
                             break;
        case NEMA_RGBA4444: conv_colour =
                             ((colour&0xf000) << 16) |
                             ((colour&0xf000) << 12) |
                             ((colour&0x0f00) << 12) |
                             ((colour&0x0f00) <<  8) |
                             ((colour&0x00f0) <<  8) |
                             ((colour&0x00f0) <<  4) |
                             ((colour&0x000f) <<  4) |
                             ((colour&0x000f) <<  0);
                             break;
        case NEMA_RGBA5551: conv_colour =
                             ((colour&0xf800) << 16) |
                             ((colour&0xe000) << 11) |
                             ((colour&0x07c0) << 13) |
                             ((colour&0x0700) <<  8) |
                             ((colour&0x003e) << 10) |
                             ((colour&0x0038) <<  5) |
                             (bit << 7) |
                             (bit << 6) |
                             (bit << 5) |
                             (bit << 4) |
                             (bit << 3) |
                             (bit << 2) |
                             (bit << 1) |
                             (bit << 0); break;
        case NEMA_RGBA0008: conv_colour =
                             (colour << 24) |
                             (colour << 16) |
                             (colour <<  8) |
                             (colour <<  0); break;
        case NEMA_BW1:      conv_colour = (colour ? 0xffffffff : 0); break;
        default:             conv_colour = colour; break;
    }

    return conv_colour;
}

#include "ublender.c"

/**
 * DirectFB callback that decides whether a drawing/blitting operation is supported in hardware.
 *
 * @param drv Driver data
 * @param dev Device data
 * @param state Card state
 * @param accel Acceleration mask
 */
static void
uNema_CheckState( void                *drv,
                  void                *dev,
                  CardState           *state,
                  DFBAccelerationMask  accel )
{
    DEBUG_PROC_ENTRY;
    DEBUG_OP("Checking for %d...\n", accel);

    CoreSurface *source      = state->source;
    CoreSurface *destination = state->destination;

    /* reject if we don't support the destination format */
    if (uNema_video_mode(destination->config.format, SMF_DESTINATION) == -1)
    {
        DEBUG_OP("Rejecting: bad destination video mode: %u\n", destination->config.format);
        return;
    }

    if (DFB_DRAWING_FUNCTION(accel))
    {
        /* reject if:
         *  - we don't support the drawing function
         *  - we don't support the drawing flag
         */
        if (accel & ~UNEMA_SUPPORTED_DRAWINGFUNCTIONS || state->drawingflags & ~UNEMA_SUPPORTED_DRAWINGFLAGS)
        {
            DEBUG_OP("Rejecting, cannot draw...\n");
            return;
        }

        state->accel |= UNEMA_SUPPORTED_DRAWINGFUNCTIONS;
    }
    else
    {
        /* reject if:
         *  - we don't support the blitting function
         *  - we don't support the blitting flag at all
         */

        if (accel & ~UNEMA_SUPPORTED_BLITTINGFUNCTIONS ||
            state->blittingflags & ~UNEMA_SUPPORTED_BLITTINGFLAGS ||
            state->render_options & ~UNEMA_SUPPORTED_RENDER_OPTIONS )
        {
            DEBUG_OP("Rejecting, cannot blit...\n");
            return;
        }

        if (uNema_video_mode(source->config.format, SMF_SOURCE) == -1)
        {
            DEBUG_OP("Rejecting: bad source video mode: %u\n", source->config.format);
            return;
        }

        state->accel |= UNEMA_SUPPORTED_BLITTINGFUNCTIONS;
    }

    DEBUG_OP("    ...we'll do it.\n");
}



/**
 * Sets destination surface registers.
 *
 * @param state Card state
 */
static inline void
uNema_set_src( CardState          *state )
{
    DEBUG_PROC_ENTRY;

    st_dev.v_srcAddr   = state->src.phys;
    st_dev.v_srcStride = state->src.pitch;
    st_dev.src_format  = uNema_video_mode(state->source->config.format, SMF_SOURCE);

    uint32_t flags = 0;
    if ( st_dev.render_options & (DSRO_SMOOTH_UPSCALE | DSRO_SMOOTH_DOWNSCALE) )
        flags = NEMA_FILTER_BL;

    nema_bind_src_tex(state->src.phys, state->source->config.size.w, state->source->config.size.h, \
                      st_dev.src_format, state->src.pitch, flags);
}

/**
 * Sets destination surface registers.
 *
 * @param state Card state
 */
static inline void
uNema_set_dest( CardState          *state )
{
    DEBUG_PROC_ENTRY;

    st_dev.v_destAddr   = state->dst.phys;
    st_dev.v_destStride = state->dst.pitch;
    st_dev.dst_format   = uNema_video_mode(state->destination->config.format, SMF_DESTINATION);

    nema_bind_dst_tex(state->dst.phys, state->destination->config.size.w, state->destination->config.size.h, \
                      st_dev.dst_format, state->dst.pitch);
}


/**
 * Sets the color for drawing operations.
 *
 * @param state Card state
 */
static inline void
uNema_set_draw_color( CardState          *state )
{
    DEBUG_PROC_ENTRY;

    st_dev.gl_color = nema_rgba(state->color.r, state->color.g, state->color.b, state->color.a);

    ///loads UNEMA_C2_REG and UNEMA_DRAW_COLOR
    precalculate_source_colour(st_dev.lastDrawFlags, st_dev.lastDrawSrcBlend);
}

/**
 * Sets the clipping registers.
 *
 * @param clip Clip region
 */
static inline void
uNema_set_clip( DFBRegion          *clip )
{
    DEBUG_PROC_ENTRY;

    nema_set_clip(clip->x1, clip->y1, (clip->x2-clip->x1+1), (clip->y2-clip->y1+1));
}


/**
 * DirectFB callback that emits buffered commands.
 *
 *
 * Only compiled if building with command list support.
 *
 * @param drv Driver data
 * @param dev Device data
 */
static void
uNema_EmitCommands( void *drv,
                    void *dev )
{
    DEBUG_PROC_ENTRY;

    //submit current CL
    nema_cl_submit( &st_dev.cl[st_dev.cur_cl] );

    st_dev.cur_cl = (st_dev.cur_cl+1)%2;
    //wait for previous CL
    nema_cl_wait(   &st_dev.cl[st_dev.cur_cl] );

    //rewind and bind new CL
    nema_cl_rewind( &st_dev.cl[st_dev.cur_cl] );
    nema_cl_bind  ( &st_dev.cl[st_dev.cur_cl] );

    st_dev.almost_full = st_dev.cl[st_dev.cur_cl].size-20;
}

/**
 * DirectFB callback that prepares the hardware for a drawing/blitting operation.
 *
 * @param drv Driver data
 * @param dev Device data
 * @param funcs Graphics device functions (may be altered)
 * @param state Card state
 * @param accel Acceleration mask
 */
static void
uNema_SetState( void                *drv,
                void                *dev,
                GraphicsDeviceFuncs *funcs,
                CardState           *state,
                DFBAccelerationMask  accel )
{
    DEBUG_PROC_ENTRY;

    StateModificationFlags mod_hw = state->mod_hw;

    /* update first the drawing and blitting flags */
    if (mod_hw & SMF_DRAWING_FLAGS) {
        if (st_dev.drawingflags != state->drawingflags) {
            st_dev.dirty_blend_fill = 1;
            st_dev.drawingflags = state->drawingflags;
        }
    }

    if (mod_hw & SMF_BLITTING_FLAGS) {
        if (st_dev.blittingflags != state->blittingflags) {
            st_dev.dirty_blend_blit = 1;
            st_dev.blittingflags = state->blittingflags;
        }
    }

    /* update other fields common between drawing and blitting */
    if (mod_hw & SMF_DESTINATION)
    {
        uNema_set_dest(state);
    }
    if (mod_hw & SMF_CLIP) {
        uNema_set_clip(&state->clip);
    }

    if (mod_hw & SMF_SRC_COLORKEY) {
        st_dev.src_colorkey = uNema_convert_to_RGBA8888( st_dev.dst_format, state->src_colorkey);
        nema_set_src_color_key(st_dev.src_colorkey);
    }
    if (mod_hw & SMF_DST_COLORKEY) {
        st_dev.dst_colorkey = uNema_convert_to_RGBA8888( st_dev.dst_format, state->dst_colorkey);
        nema_set_dst_color_key(st_dev.dst_colorkey);
    }
    if (mod_hw & (SMF_SRC_BLEND | SMF_DST_BLEND)) {
        if (state->src_blend != st_dev.prev_src_blend ||
            state->dst_blend != st_dev.prev_dst_blend) {
            st_dev.dirty_blend_fill = 1;
            st_dev.dirty_blend_blit = 1;

            st_dev.src_blend = state->src_blend;
            st_dev.dst_blend = state->dst_blend;
        }
    }

    if (mod_hw & SMF_COLOR) {
        uNema_set_draw_color(state);
    }

    if ( mod_hw & SMF_RENDER_OPTIONS )  {
        st_dev.render_options = state->render_options;
    }

    switch (accel)
    {
    case DFXL_FILLTRAPEZOID:
    case DFXL_FILLTRIANGLE:
    case DFXL_DRAWRECTANGLE:
    case DFXL_DRAWLINE:
    case DFXL_FILLRECTANGLE:
        if ( st_dev.dirty_blend_fill ) {
            TSgfx_setDrawBlend();
            st_dev.dirty_blend_fill = 0;
            st_dev.dirty_blend_blit = 1;
        }

        state->set = UNEMA_SUPPORTED_DRAWINGFUNCTIONS;
        break;

    case DFXL_BLIT:
    case DFXL_STRETCHBLIT:
    case DFXL_TEXTRIANGLES:
        uNema_set_src(state);
        if ( st_dev.dirty_blend_blit ) {
            TSgfx_setBlitBlend();
            st_dev.dirty_blend_blit = 0;
            st_dev.dirty_blend_fill = 1;
        }

        state->set = UNEMA_SUPPORTED_BLITTINGFUNCTIONS;
        break;
    default:
        D_BUG("Unexpected drawing/blitting function");
    }

    state->mod_hw = 0;
}

/**
 * DirectFB callback that draws a line.
 *
 * @param drv Driver data
 * @param dev Device data
 * @param line Line region
 */
static bool
uNema_DrawLine( void      *drv,
                void      *dev,
                DFBRegion *line )
{
    DEBUG_PROC_ENTRY;
    DEBUG_OP("DrawLine (%s) (%d, %d)-(%d, %d)\n", st_dev.drawingflags & DSDRAW_BLEND ? "A" : "", line->x1, line->y1, line->x2, line->y2);

    nema_raster_line(line->x1, line->y1, line->x2, line->y2);

    CUSTOM_EMIT;
    return true;
}


/**
 * DirectFB callback that draws a rectangle.
 *
 * Drawing the rectangle is implemented as drawing 4 lines
 * in driver level.
 *
 * @param drv Driver data
 * @param dev Device data
 * @param rect Rectangle coordinates
 */
static bool
uNema_DrawRectangle( void         *drv,
                     void         *dev,
                     DFBRectangle *rect )
{
    DEBUG_PROC_ENTRY;
    DEBUG_OP("DrawRectangle (%s) (%d, %d), %dx%d\n", st_dev.drawingflags & DSDRAW_BLEND ? "A" : "", rect->x, rect->y, rect->w, rect->h);

    int x0 = rect->x;
    int y0 = rect->y;
    int x2 = rect->x + rect->w - 1;
    int y2 = rect->y + rect->h - 1;

    nema_raster_line(x0, y0, x2, y0);
    nema_raster_line(x0, y2, x2, y2);
    ++y0, --y2;
    nema_raster_line(x0, y0, x0, y2);
    nema_raster_line(x2, y0, x2, y2);

    CUSTOM_EMIT;
    return true;
}

/**
 * DirectFB callback that draws a filled rectangle.
 *
 * @param drv Driver data
 * @param dev Device data
 * @param rect Rectangle coordinates
 */
static bool
uNema_FillRectangle( void         *drv,
                     void         *dev,
                     DFBRectangle *rect )
{
    DEBUG_PROC_ENTRY;
    DEBUG_OP("FillRectangle (%s) (%d, %d), %dx%d\n", st_dev.drawingflags & DSDRAW_BLEND ? "A" : "", rect->x, rect->y, rect->w, rect->h);

    nema_raster_rect(rect->x, rect->y, rect->w, rect->h);

    CUSTOM_EMIT;
    return true;
}

/**
 * DirectFB callback that draws a filled triangle.
 *
 * @param drv Driver data
 * @param dev Device data
 * @param rect Rectangle coordinates
 */
static bool
uNema_FillTriangle( void         *drv,
                    void         *dev,
                    DFBTriangle  *tri )
{
    DEBUG_PROC_ENTRY;

    nema_raster_triangle_fx(nema_i2fx(tri->x1), nema_i2fx(tri->y1),
                            nema_i2fx(tri->x2), nema_i2fx(tri->y2),
                            nema_i2fx(tri->x3), nema_i2fx(tri->y3));

    CUSTOM_EMIT;
    return true;
}

/**
 * DirectFB callback that draws a filled trapezoid.
 *
 * @param drv Driver data
 * @param dev Device data
 * @param rect Trapezoid coordinates
 */


static bool
uNema_FillTrapezoid( void         *drv,
                     void         *dev,
                     DFBTrapezoid *trap )
{
    DEBUG_PROC_ENTRY;

    nema_raster_quad_fx(nema_i2fx(trap->x1         ), nema_i2fx(trap->y1),
                        nema_i2fx(trap->x1+trap->w1), nema_i2fx(trap->y1),
                        nema_i2fx(trap->x2+trap->w2), nema_i2fx(trap->y2),
                        nema_i2fx(trap->x2         ), nema_i2fx(trap->y2) );

    CUSTOM_EMIT;
    return true;
}

const int32_t txty_shift   = 12;
const int32_t txty_fixed_1 = 1 << 12;


/*
 * Simplify blitting flags
 *
 * Allow any combination of DSBLIT_ROTATE_ and DSBLIT_FLIP_ flags to be reduced
 * to a combination of DSBLIT_ROTATE_90, DSBLIT_FLIP_HORIZONTAL and DSBLIT_FLIP_VERTICAL
 *
 */
static inline void simplify_blittingflags( DFBSurfaceBlittingFlags *flags )
{
     if (*flags & DSBLIT_ROTATE180)
          *flags = (DFBSurfaceBlittingFlags)(*flags ^ (DSBLIT_ROTATE180 | DSBLIT_FLIP_HORIZONTAL | DSBLIT_FLIP_VERTICAL));

     if (*flags & DSBLIT_ROTATE270) {
          if (*flags & DSBLIT_ROTATE90)
               *flags = (DFBSurfaceBlittingFlags)(*flags ^ (DSBLIT_ROTATE90 | DSBLIT_ROTATE270));
          else
               *flags = (DFBSurfaceBlittingFlags)(*flags ^ (DSBLIT_ROTATE90 | DSBLIT_ROTATE270 | DSBLIT_FLIP_HORIZONTAL | DSBLIT_FLIP_VERTICAL));
     }
}

/**
 * DirectFB callback that blits an image.
 *
 * @param drv Driver data
 * @param dev Device data
 * @param rect Source rectangle
 * @param dx Destination X
 * @param dy Destination Y
 */
static bool
uNema_Blit( void         *drv,
            void         *dev,
            DFBRectangle *rect,
            int           dx,
            int           dy )
{
    DEBUG_PROC_ENTRY;
    DEBUG_OP("Blit (%s%s%s%s%s) (%d,%d) -> (%d,%d), %dx%d\n", st_dev.blittingflags & (DSBLIT_ROTATE90 | DSBLIT_ROTATE180 | DSBLIT_ROTATE270 | DSBLIT_FLIP_HORIZONTAL | DSBLIT_FLIP_VERTICAL) ? "R" : "",
                                                              st_dev.blittingflags & DSBLIT_SRC_COLORKEY ? "K" : "",
                                                              st_dev.blittingflags & DSBLIT_DST_COLORKEY ? "D" : "",
                                                              st_dev.blittingflags & (DSBLIT_COLORIZE | DSBLIT_SRC_PREMULTCOLOR) ? "C" : "",
                                                              st_dev.blittingflags & (DSBLIT_BLEND_COLORALPHA | DSBLIT_BLEND_ALPHACHANNEL) ? "A" : ""
                            , rect->x, rect->y, dx, dy, rect->w, rect->h);

    if (st_dev.v_srcAddr == st_dev.v_destAddr && dy > rect->y) {
        printf("Blitting on same surface not supported atm\n");
        return false;
    }

    DFBRectangle dst_rect = { dx, dy, rect->w, rect->h };

    if ( (st_dev.blittingflags & TSI_DSBLIT_ROTATE_MASK) == 0) {
        nema_blit_subrect(dst_rect.x, dst_rect.y, dst_rect.w, dst_rect.h, rect->x, rect->y);
    } else {
        uint32_t rotation = 0;

        DFBSurfaceBlittingFlags rotflip_blittingflags = st_dev.blittingflags;
        simplify_blittingflags( &rotflip_blittingflags );
        if ( rotflip_blittingflags & DSBLIT_FLIP_HORIZONTAL )
            rotation |= NEMA_MIR_HOR;
        if ( rotflip_blittingflags & DSBLIT_FLIP_VERTICAL   )
            rotation |= NEMA_MIR_VERT;
        if ( rotflip_blittingflags & DSBLIT_ROTATE90        )
            rotation |= NEMA_ROT_090_CCW;

        nema_blit_rotate_partial(rect->x, rect->y,
                                 rect->w, rect->h,
                                 dx, dy,
                                 rotation);
    }


    CUSTOM_EMIT;
    return true;
}

/**
 * DirectFB callback that does a stretched blit.
 *
 * @param drv Driver data
 * @param dev Device data
 * @param srect Source rectangle
 * @param drect Destination rectangle
 */
static bool
uNema_StretchBlit(  void         *drv,
                    void         *dev,
                    DFBRectangle *srect,
                    DFBRectangle *drect )
{
    DEBUG_PROC_ENTRY;
    DEBUG_OP("StretchBlit (%d, %d), %dx%d\n", drect->x, drect->y, drect->w, drect->h);

    if (st_dev.v_srcAddr == st_dev.v_destAddr) {
        printf("Stretch Blitting on same surface not supported atm\n");
        return false;
    }

    if ( (st_dev.blittingflags & TSI_DSBLIT_ROTATE_MASK) == 0 ) {
        st_dev.matrix[0][2] = -drect->x;
        st_dev.matrix[1][2] = -drect->y;
    }

    nema_blit_subrect_fit( drect->x, drect->y, drect->w, drect->h,
                           srect->x, srect->y, srect->w, srect->h);


    CUSTOM_EMIT;
	return true;
}

/**
 * DirectFB callback that blits an image.
 *
 * @param drv Driver data
 * @param dev Device data
 * @param v Triangle Vertices
 * @param num Vertices Count
 * @param formation Triangle List Formation
 */
static bool
uNema_TextureTriangles( void *drv,
                        void *dev,
                        DFBVertex *v,
                        int num,
                        DFBTriangleFormation formation )
{
    DEBUG_PROC_ENTRY;

    int index = 0;
    int idx0 = 0, idx1 = 0, idx2 = 0;

    for (index = 2; index < num; ) {
        switch (formation) {
            case DTTF_LIST:         /* 0/1/2  3/4/5  6/7/8 ... */
                    idx0 = index-2;
                    idx1 = index-1;
                    idx2 = index;
                    index += 3;
                break;
            case DTTF_STRIP:        /* 0/1/2  1/2/3  2/3/4 ... */
                    idx0 = index-2;
                    idx1 = index-1;
                    idx2 = index;
                    ++index;
                break;
            case DTTF_FAN:          /* 0/1/2  0/2/3  0/3/4 ... */
                    idx0 = 0;
                    idx1 = index-1;
                    idx2 = index;
                    ++index;
                break;
            default:
                D_BUG("Unexpected DFBTriangleFormation %d", formation);
                break;
        }

        float DET = v[idx0].x * v[idx1].y -
                    v[idx1].x * v[idx0].y +
                    v[idx1].x * v[idx2].y -
                    v[idx2].x * v[idx1].y +
                    v[idx2].x * v[idx0].y -
                    v[idx0].x * v[idx2].y;

        float width = 100.f;
        float height = 100.f;

        st_dev.matrix[2][0] = ((v[idx1].y - v[idx2].y) * v[idx0].w +
                               (v[idx2].y - v[idx0].y) * v[idx1].w +
                               (v[idx0].y - v[idx1].y) * v[idx2].w) / DET;
        st_dev.matrix[2][1] = ((v[idx2].x - v[idx1].x) * v[idx0].w +
                               (v[idx0].x - v[idx2].x) * v[idx1].w +
                               (v[idx1].x - v[idx0].x) * v[idx2].w) / DET;
        st_dev.matrix[2][2] = ((v[idx1].x * v[idx2].y - v[idx2].x * v[idx1].y) * v[idx0].w +
                               (v[idx2].x * v[idx0].y - v[idx0].x * v[idx2].y) * v[idx1].w +
                               (v[idx0].x * v[idx1].y - v[idx1].x * v[idx0].y) * v[idx2].w) / DET;

        /// TX
        float uizx0 = v[idx0].s * v[idx0].w;
        float uizx1 = v[idx1].s * v[idx1].w;
        float uizx2 = v[idx2].s * v[idx2].w;

        st_dev.matrix[0][0] = width * ((v[idx1].y - v[idx2].y) * uizx0 +
                                       (v[idx2].y - v[idx0].y) * uizx1 +
                                       (v[idx0].y - v[idx1].y) * uizx2) / DET;
        st_dev.matrix[0][1] = width * ((v[idx2].x - v[idx1].x) * uizx0 +
                                       (v[idx0].x - v[idx2].x) * uizx1 +
                                       (v[idx1].x - v[idx0].x) * uizx2) / DET;
        st_dev.matrix[0][2] = width * ((v[idx1].x * v[idx2].y - v[idx2].x * v[idx1].y) * uizx0 +
                                       (v[idx2].x * v[idx0].y - v[idx0].x * v[idx2].y) * uizx1 +
                                       (v[idx0].x * v[idx1].y - v[idx1].x * v[idx0].y) * uizx2) / DET;

        /// TY
        float uizy0 = v[idx0].t * v[idx0].w;
        float uizy1 = v[idx1].t * v[idx1].w;
        float uizy2 = v[idx2].t * v[idx2].w;

        st_dev.matrix[1][0] = height * ((v[idx1].y - v[idx2].y) * uizy0 +
                                        (v[idx2].y - v[idx0].y) * uizy1 +
                                        (v[idx0].y - v[idx1].y) * uizy2) / DET;
        st_dev.matrix[1][1] = height * ((v[idx2].x - v[idx1].x) * uizy0 +
                                        (v[idx0].x - v[idx2].x) * uizy1 +
                                        (v[idx1].x - v[idx0].x) * uizy2) / DET;
        st_dev.matrix[1][2] = height * ((v[idx1].x * v[idx2].y - v[idx2].x * v[idx1].y) * uizy0 +
                                        (v[idx2].x * v[idx0].y - v[idx0].x * v[idx2].y) * uizy1 +
                                        (v[idx0].x * v[idx1].y - v[idx1].x * v[idx0].y) * uizy2) / DET;

        // nema_set_matrix(st_dev.matrix);

        // nema_raster_triangle_fx(nema_f2fx(v[idx0].x), nema_f2fx(v[idx0].y),
        //                         nema_f2fx(v[idx1].x), nema_f2fx(v[idx1].y),
        //                         nema_f2fx(v[idx2].x), nema_f2fx(v[idx2].y));

        nema_blit_tri_fit (v[idx0].x, v[idx0].y, idx0,
                           v[idx1].x, v[idx1].y, idx1,
                           v[idx2].x, v[idx2].y, idx2);

    }

    CUSTOM_EMIT;
    return true;
}

/**
 * DirectFB callback that blocks until hardware processing has finished.
 *
 *
 * @param drv Driver data
 * @param dev Device data
 */
static DFBResult
uNema_EngineSync( void *drv,
                  void *dev )
{
    DEBUG_PROC_ENTRY;

    //EnginSync runs on different thread
    //so don't change bound CL

    //submit current CL
    nema_cl_submit( &st_dev.cl[st_dev.cur_cl] );
    //wait for current CL
    nema_cl_wait(   &st_dev.cl[st_dev.cur_cl] );
    nema_cl_wait(   &st_dev.cl[(st_dev.cur_cl+1)%2] );
    //rewind and bind new CL
    nema_cl_rewind( &st_dev.cl[st_dev.cur_cl] );

    st_dev.almost_full = st_dev.cl[st_dev.cur_cl].size>>4;
    return DFB_OK;
}

/**
 * DirectFB driver framework function that probes a device for use with a driver.
 *
 * @param device Graphics device
 */
static int
driver_probe( CoreGraphicsDevice *device )
{
    DEBUG_PROC_ENTRY;
    return 1;
}

/**
 * DirectFB driver framework function that provides info about a driver.
 *
 * @param device Graphics device
 * @param info Driver info
 */
static void
driver_get_info( CoreGraphicsDevice *device,
                 GraphicsDriverInfo *info )
{
    DEBUG_PROC_ENTRY;

    snprintf( info->name,    DFB_GRAPHICS_DRIVER_INFO_NAME_LENGTH,    "TSi uNema" );
    snprintf( info->vendor,  DFB_GRAPHICS_DRIVER_INFO_VENDOR_LENGTH,  "Think Silicon Ltd." );
    snprintf( info->url,     DFB_GRAPHICS_DRIVER_INFO_URL_LENGTH,     "http://www.think-silicon.com" );
    snprintf( info->license, DFB_GRAPHICS_DRIVER_INFO_LICENSE_LENGTH, "LGPL" );

    info->version.major = 0;
    info->version.minor = 1;

    info->driver_data_size = sizeof(st_drv);
    info->device_data_size = sizeof(st_dev);
}

/**
 * DirectFB driver framework function that initializes a driver.
 *
 * @param device Graphics device
 * @param funcs Device functions
 * @param driver_data Driver data
 * @param device_data Device data
 * @param core DirectFB core object
 */
static DFBResult
driver_init_driver( CoreGraphicsDevice  *device,
                    GraphicsDeviceFuncs *funcs,
                    void                *driver_data,
                    void                *device_data,
                    CoreDFB             *core )
{
    DEBUG_PROC_ENTRY;
    int ret;

    //Initialize NemaGFX
    ret = nema_init();
    if (ret)
        return ret;

    st_dev.lastDrawFlags    = ~0;
    st_dev.lastDrawSrcBlend = ~0;

    st_dev.prev_src_blend   = ~0;
    st_dev.prev_dst_blend   = ~0;

    st_dev.dirty_blend_fill = 1;
    st_dev.dirty_blend_blit = 1;

    st_dev.cl[0] = nema_cl_create();
    st_dev.cl[1] = nema_cl_create();

    st_dev.cur_cl = 0;
    nema_cl_bind( &st_dev.cl[st_dev.cur_cl] );
    st_dev.almost_full = st_dev.cl[st_dev.cur_cl].size>>4;

    //set default tex color to white
    nema_set_tex_color(0xffffffff);

    /* assign system functions */
    funcs->AfterSetVar       = NULL;
    funcs->EngineReset       = NULL; //uNema_EngineReset;
    funcs->EngineSync        = uNema_EngineSync;
    funcs->InvalidateState   = NULL;
    funcs->FlushTextureCache = NULL;
    funcs->FlushReadCache    = NULL;
    funcs->SurfaceEnter      = NULL;
    funcs->SurfaceLeave      = NULL;
    funcs->GetSerial         = NULL;
    funcs->WaitSerial        = NULL;
    funcs->EmitCommands      = uNema_EmitCommands;
    funcs->CheckState        = uNema_CheckState;
    funcs->SetState          = uNema_SetState;

    funcs->FillRectangle     = uNema_FillRectangle;
    funcs->DrawRectangle     = uNema_DrawRectangle;
    funcs->DrawLine          = uNema_DrawLine;
    funcs->FillTriangle      = uNema_FillTriangle;
    funcs->FillTrapezoid     = uNema_FillTrapezoid;

    /* assign blitting functions */
    funcs->Blit              = uNema_Blit;
    funcs->StretchBlit       = uNema_StretchBlit;
    funcs->TextureTriangles  = uNema_TextureTriangles;

    /* assign other functions */
    funcs->StartDrawing      = NULL;
    funcs->StopDrawing       = NULL;

    return DFB_OK;
}


/**
 * DirectFB driver framework function that initializes a device for use with a driver.
 *
 * @param device Graphics device
 * @param device_info Graphics device info
 * @param drv Driver data
 * @param dev Device data
 */
static DFBResult
driver_init_device( CoreGraphicsDevice *device,
                    GraphicsDeviceInfo *device_info,
                    void               *drv,
                    void               *dev )
{
    DEBUG_PROC_ENTRY;

    snprintf( device_info->name,   DFB_GRAPHICS_DEVICE_INFO_NAME_LENGTH,   "Nema Series" );
    snprintf( device_info->vendor, DFB_GRAPHICS_DEVICE_INFO_VENDOR_LENGTH, "Think Silicon" );

    device_info->caps.flags    = CCF_CLIPPING /*| CCF_RENDEROPTS*/;
    device_info->caps.accel    = UNEMA_SUPPORTED_DRAWINGFUNCTIONS | UNEMA_SUPPORTED_BLITTINGFUNCTIONS;
    device_info->caps.drawing  = UNEMA_SUPPORTED_DRAWINGFLAGS;
    device_info->caps.blitting = UNEMA_SUPPORTED_BLITTINGFLAGS;

    device_info->limits.surface_byteoffset_alignment = 4;
    device_info->limits.surface_bytepitch_alignment = 4;

    return DFB_OK;
}


/**
 * DirectFB driver framework function that closes a device.
 *
 * @param device Graphics device
 * @param driver_data Driver data
 * @param device_data Device data
 */
static void
driver_close_device( CoreGraphicsDevice *device,
             void               *driver_data,
             void               *device_data )
{
    DEBUG_PROC_ENTRY;
}

/**
 * DirectFB driver framework function that closes a driver.
 *
 * @param device Graphics device
 * @param driver_data Driver data
 */
static void
driver_close_driver( CoreGraphicsDevice *device,
                     void               *driver_data )
{
    DEBUG_PROC_ENTRY;
}
