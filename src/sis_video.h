/* $XFree86$ */
/* $XdotOrg$ */
/*
 * Xv driver for SiS 300, 315 and 330 series.
 *
 * Copyright (C) 2001-2005 by Thomas Winischhofer, Vienna, Austria.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1) Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2) Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3) The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Author:    Thomas Winischhofer <thomas@winischhofer.net>
 *
 * Formerly based on a mostly non-working code fragment for the 630 by
 * Silicon Integrated Systems Corp, Inc., HsinChu, Taiwan which is
 * Copyright (C) 2000 Silicon Integrated Systems Corp, Inc.
 *
 * Basic structure based on the mga Xv driver by Mark Vojkovich
 * and i810 Xv driver by Jonathan Bian <jonathan.bian@intel.com>.
 *
 * All comments in this file are by Thomas Winischhofer.
 *
 * The overlay adaptor supports the following chipsets:
 *  SiS300: No registers >0x65, two overlays (one used for CRT1, one for CRT2)
 *  SiS630/730: No registers >0x6b, two overlays (one used for CRT1, one for CRT2)
 *  SiS550: Full register range, two overlays (one used for CRT1, one for CRT2)
 *  SiS315: Full register range, one overlay (used for both CRT1 and CRT2 alt.)
 *  SiS650/740: Full register range, one overlay (used for both CRT1 and CRT2 alt.)
 *  SiSM650/651: Full register range, two overlays (one used for CRT1, one for CRT2)
 *  SiS330: Full register range, one overlay (used for both CRT1 and CRT2 alt.)
 *  SiS661/741/760: Full register range, two overlays (one used for CRT1, one for CRT2)
 *  SiS340: - ? overlay(s)? Extended registers for DDA?
 *  SiS761: - ? overlay(s)? Extended registers for DDA.
 *  XGI V5/V8/Z7 - 1 overlay, extended registers for DDA.
 *
 * Help for reading the code:
 * 315/550/650/740/M650/651/330/661/741/76x/340/XGI = SIS_315_VGA
 * 300/630/730                                      = SIS_300_VGA
 * For chipsets with 2 overlays, hasTwoOverlays will be true
 *
 * Notes on display modes:
 *
 * -) dual head mode:
 *    DISPMODE is either SINGLE1 or SINGLE2, hence you need to check dualHeadMode flag
 *    DISPMODE is _never_ MIRROR.
 *    a) Chipsets with 2 overlays:
 *       315/330 series: Only half sized overlays available (width 960), 660: 1536
 *       Overlay 1 is used on CRT1, overlay 2 for CRT2.
 *    b) Chipsets with 1 overlay:
 *       Full size overlays available.
 *       Overlay is used for either CRT1 or CRT2
 * -) merged fb mode:
 *    a) Chipsets with 2 overlays:
 *       315/330 series: Only half sized overlays available (width 960), 660: 1536
 *       DISPMODE is always MIRROR. Overlay 1 is used for CRT1, overlay 2 for CRT2.
 *    b) Chipsets with 1 overlay:
 *       Full size overlays available.
 *       DISPMODE is either SINGLE1 or SINGLE2. Overlay is used accordingly on either
 *       CRT1 or CRT2 (automatically, where it is located)
 * -) mirror mode (without dualhead or mergedfb)
 *    a) Chipsets with 2 overlays:
 *       315/330 series: Only half sized overlays available (width 960), 660: 1536
 *       DISPMODE is MIRROR. Overlay 1 is used for CRT1, overlay 2 for CRT2.
 *    b) Chipsets with 1 overlay:
 *       Full size overlays available.
 *       DISPMODE is either SINGLE1 or SINGLE2. Overlay is used depending on
 * 	 XvOnCRT2 flag.
 *
 * About the video blitter:
 * The video blitter adaptor supports 16 ports. By default, adaptor 0 will
 * be the overlay adaptor, adaptor 1 the video blitter. The option XvDefaultAdaptor
 * allows reversing this.
 * Since SiS does not provide information on the 3D engine, I could not
 * implement scaling. Instead, the driver paints a black border around the unscaled
 * video if the destination area is bigger than the video.
 *
 */

#ifndef _SIS_VIDEO_H_
#define _SIS_VIDEO_H_

#include "sis_videostr.h"

static		XF86VideoAdaptorPtr SISSetupImageVideo(ScreenPtr);
static void 	SISStopVideo(ScrnInfoPtr, pointer, Bool);
static int 	SISSetPortAttribute(ScrnInfoPtr, Atom, INT32, pointer);
static int 	SISGetPortAttribute(ScrnInfoPtr, Atom ,INT32 *, pointer);
static void 	SISQueryBestSize(ScrnInfoPtr, Bool, short, short, short,
			short, unsigned int *,unsigned int *, pointer);
static int 	SISPutImage(ScrnInfoPtr,
			short, short, short, short, short, short, short, short,
			int, UChar *, short, short, Bool, RegionPtr, pointer, DrawablePtr);
static int	SISReputImage(ScrnInfoPtr pScrn,
			short src_x, short src_y,
			short drw_x, short drw_y,
			short src_w, short src_h,
			short drw_w, short drw_h,
			RegionPtr clipBoxes, pointer data, DrawablePtr pDraw);
static int 	SISQueryImageAttributes(ScrnInfoPtr,
			int, UShort *, UShort *, int *, int *);
static void 	SISVideoTimerCallback(ScrnInfoPtr pScrn, Time now);
static void     SISInitOffscreenImages(ScreenPtr pScrn);
static void	set_dda_regs(SISPtr pSiS, float scale);
unsigned int	SISAllocateFBMemory(ScrnInfoPtr pScrn, void **handle, int bytesize);
void		SISFreeFBMemory(ScrnInfoPtr pScrn, void **handle);
void 		SISSetPortDefaults(ScrnInfoPtr pScrn, SISPortPrivPtr pPriv);
void		SISUpdateVideoParms(SISPtr pSiS, SISPortPrivPtr pPriv);
void		SiSUpdateXvGamma(SISPtr pSiS, SISPortPrivPtr pPriv);
extern Bool     SiSBridgeIsInSlaveMode(ScrnInfoPtr pScrn);

#ifdef INCL_YUV_BLIT_ADAPTOR
static 		XF86VideoAdaptorPtr SISSetupBlitVideo(ScreenPtr);
static void 	SISStopVideoBlit(ScrnInfoPtr, ULong, Bool);
static int 	SISSetPortAttributeBlit(ScrnInfoPtr, Atom, INT32, ULong);
static int 	SISGetPortAttributeBlit(ScrnInfoPtr, Atom ,INT32 *, ULong);
static void 	SISQueryBestSizeBlit(ScrnInfoPtr, Bool, short, short, short,
			short, unsigned int *,unsigned int *, ULong);
static int 	SISPutImageBlit_671( ScrnInfoPtr,
			short, short, short, short, short, short, short, short,
			int, UChar *, short, short, Bool, RegionPtr, ULong);
static int 	SISPutImageBlit( ScrnInfoPtr,
			short, short, short, short, short, short, short, short,
			int, UChar *, short, short, Bool, RegionPtr, ULong);
static int 	SISQueryImageAttributesBlit(ScrnInfoPtr,
			int, UShort *, UShort *, int *, int *);
extern void     SISWriteBlitPacket(SISPtr pSiS, CARD32 *packet);
#endif

#ifdef TWDEBUG
extern int	SISSetPortUtilAttribute(ScrnInfoPtr pScrn, Atom attribute,
					INT32 value, SISPortPrivPtr pPriv);
#endif


#define OFF_DELAY   	200    /* milliseconds */
#define FREE_DELAY  	30000

#define OFF_TIMER   	0x01
#define FREE_TIMER  	0x02
#define CLIENT_VIDEO_ON 0x04
#define TIMER_MASK      (OFF_TIMER | FREE_TIMER)

#define WATCHDOG_DELAY  200000 /* Watchdog counter for Vertical Restrace waiting */

#define MAKE_ATOM(a) MakeAtom(a, sizeof(a) - 1, TRUE)

#define IMAGE_MIN_WIDTH         32  	/* Minimum and maximum source image sizes */
#define IMAGE_MIN_HEIGHT        24
#define IMAGE_MAX_WIDTH_300    768	/* was 720, wrong */
#define IMAGE_MAX_HEIGHT_300   576
#define IMAGE_MAX_WIDTH_315   1920
#define IMAGE_MAX_WIDTH_340   1920	/* ? */
#define IMAGE_MAX_WIDTH_761   1920	/* ? */
#define IMAGE_MAX_HEIGHT_315  1080

#define OVERLAY_MIN_WIDTH       32  	/* Minimum overlay sizes */
#define OVERLAY_MIN_HEIGHT      24

#define DISPMODE_SINGLE1 0x1		/* CRT1 only */
#define DISPMODE_SINGLE2 0x2		/* CRT2 only */
#define DISPMODE_MIRROR  0x4		/* CRT1 + CRT2 MIRROR (see note below) */

#define SISPRIVLINID 0x53495337		/* Private ID for private linears */

#define FBOFFSET (pSiS->dhmOffset)

/* Note on "MIRROR":
 * When using VESA on machines with an enabled video bridge, this means
 * a real mirror. CRT1 and CRT2 have the exact same resolution and
 * refresh rate. The same applies to modes which require the bridge to
 * operate in slave mode.
 * When not using VESA and the bridge is not in slave mode otherwise,
 * CRT1 and CRT2 have the same resolution but possibly a different
 * refresh rate.
 */

#define NUM_FORMATS 3

static XF86VideoFormatRec SISFormats[NUM_FORMATS] =
{
   { 8, PseudoColor},
   {16, TrueColor},
   {24, TrueColor}
};

static char sisxvcolorkey[] 				= "XV_COLORKEY";
static char sisxvbrightness[] 				= "XV_BRIGHTNESS";
static char sisxvcontrast[] 				= "XV_CONTRAST";
static char sisxvsaturation[] 				= "XV_SATURATION";
static char sisxvhue[] 					= "XV_HUE";
static char sisxvautopaintcolorkey[] 			= "XV_AUTOPAINT_COLORKEY";
static char sisxvsetdefaults[] 				= "XV_SET_DEFAULTS";
static char sisxvswitchcrt[] 				= "XV_SWITCHCRT";
static char sisxvtvxposition[] 				= "XV_TVXPOSITION";
static char sisxvtvyposition[] 				= "XV_TVYPOSITION";
static char sisxvgammared[] 				= "XV_GAMMA_RED";
static char sisxvgammagreen[] 				= "XV_GAMMA_GREEN";
static char sisxvgammablue[] 				= "XV_GAMMA_BLUE";
static char sisxvdisablegfx[] 				= "XV_DISABLE_GRAPHICS";
static char sisxvdisablegfxlr[] 			= "XV_DISABLE_GRAPHICS_LR";
static char sisxvdisablecolorkey[] 			= "XV_DISABLE_COLORKEY";
static char sisxvusechromakey[] 			= "XV_USE_CHROMAKEY";
static char sisxvinsidechromakey[] 			= "XV_INSIDE_CHROMAKEY";
static char sisxvyuvchromakey[] 			= "XV_YUV_CHROMAKEY";
static char sisxvchromamin[] 				= "XV_CHROMAMIN";
static char sisxvchromamax[] 				= "XV_CHROMAMAX";
#ifdef TWDEBUG
static char sisxvsetreg[]				= "XV_SD_SETREG";
#endif

#ifdef INCL_YUV_BLIT_ADAPTOR
#if 0
static char sisxvvsync[]				= "XV_SYNC_TO_VBLANK";
#endif
#endif

/***********************************************/
/*               OVERLAY ADAPTOR               */
/***********************************************/

/* client libraries expect an encoding */
static XF86VideoEncodingRec DummyEncoding =
{
   0,
   "XV_IMAGE",
   0, 0,		/* Will be filled in */
   {1, 1}
};

static XF86AttributeRec SISAttributes_300[] =
{
   {XvSettable | XvGettable, 0, (1 << 24) - 1, sisxvcolorkey},
   {XvSettable | XvGettable, -128, 127,        sisxvbrightness},
   {XvSettable | XvGettable, 0, 7,             sisxvcontrast},
   {XvSettable | XvGettable, 0, 1,             sisxvautopaintcolorkey},
   {XvSettable             , 0, 0,             sisxvsetdefaults},
   {XvSettable | XvGettable, -32, 32,          sisxvtvxposition},
   {XvSettable | XvGettable, -32, 32,          sisxvtvyposition},
   {XvSettable | XvGettable, 0, 1,             sisxvdisablegfx},
   {XvSettable | XvGettable, 0, 1,             sisxvdisablegfxlr},
   {XvSettable | XvGettable, 0, 1,             sisxvdisablecolorkey},
   {XvSettable | XvGettable, 0, 1,             sisxvusechromakey},
   {XvSettable | XvGettable, 0, 1,             sisxvinsidechromakey},
   {XvSettable | XvGettable, 0, 1,             sisxvyuvchromakey},
   {XvSettable | XvGettable, 0, (1 << 24) - 1, sisxvchromamin},
   {XvSettable | XvGettable, 0, (1 << 24) - 1, sisxvchromamax},
   {0                      , 0, 0,             NULL}
};

static XF86AttributeRec SISAttributes_315[] =
{
   {XvSettable | XvGettable, 0, (1 << 24) - 1, sisxvcolorkey},
   {XvSettable | XvGettable, -128, 127,        sisxvbrightness},
   {XvSettable | XvGettable, 0, 7,             sisxvcontrast},
   {XvSettable | XvGettable, -7, 7,            sisxvsaturation},
   {XvSettable | XvGettable, -8, 7,            sisxvhue},
   {XvSettable | XvGettable, 0, 1,             sisxvautopaintcolorkey},
   {XvSettable             , 0, 0,             sisxvsetdefaults},
   {XvSettable | XvGettable, -32, 32,          sisxvtvxposition},
   {XvSettable | XvGettable, -32, 32,          sisxvtvyposition},
   {XvSettable | XvGettable, 100, 10000,       sisxvgammared},
   {XvSettable | XvGettable, 100, 10000,       sisxvgammagreen},
   {XvSettable | XvGettable, 100, 10000,       sisxvgammablue},
   {XvSettable | XvGettable, 0, 1,             sisxvdisablegfx},
   {XvSettable | XvGettable, 0, 1,             sisxvdisablegfxlr},
   {XvSettable | XvGettable, 0, 1,             sisxvdisablecolorkey},
   {XvSettable | XvGettable, 0, 1,             sisxvusechromakey},
   {XvSettable | XvGettable, 0, 1,             sisxvinsidechromakey},
   {XvSettable | XvGettable, 0, (1 << 24) - 1, sisxvchromamin},
   {XvSettable | XvGettable, 0, (1 << 24) - 1, sisxvchromamax},
#ifdef TWDEBUG
   {XvSettable             , 0, -1,   	       sisxvsetreg},
#endif
   {XvSettable | XvGettable, 0, 1,             sisxvswitchcrt},
   {0                      , 0, 0,             NULL}
};

#define NUM_IMAGES_300 6
#define NUM_IMAGES_315 7	    /* basically NV12 only - but does not work */
#define NUM_IMAGES_330 9  	    /* NV12 and NV21 */

#define PIXEL_FMT_YV12 FOURCC_YV12  /* 0x32315659 */
#define PIXEL_FMT_UYVY FOURCC_UYVY  /* 0x59565955 */
#define PIXEL_FMT_YUY2 FOURCC_YUY2  /* 0x32595559 */
#define PIXEL_FMT_I420 FOURCC_I420  /* 0x30323449 */
#define PIXEL_FMT_RGB5 0x35315652
#define PIXEL_FMT_RGB6 0x36315652
#define PIXEL_FMT_YVYU 0x55595659   /* 315/330+ only */
#define PIXEL_FMT_NV12 0x3231564e   /* 330+ only */
#define PIXEL_FMT_NV21 0x3132564e   /* 330+ only */
#define PIXEL_FMT_IA44 FOURCC_IA44 /* 0x34344149 */
#define PIXEL_FMT_AI44 FOURCC_AI44 /* 0x34344941 */

/* TODO: */
#define PIXEL_FMT_RAW8 0x38574152

static XF86ImageRec SISImages[NUM_IMAGES_330] =
{
    XVIMAGE_YUY2, /* If order is changed, SISOffscreenImages must be adapted */
    XVIMAGE_YV12,
    XVIMAGE_UYVY,
    XVIMAGE_I420
    ,
    { /* RGB 555 */
      PIXEL_FMT_RGB5,
      XvRGB,
      LSBFirst,
      {'R','V','1','5',
       0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
      16,
      XvPacked,
      1,
      15, 0x7C00, 0x03E0, 0x001F,
      0, 0, 0,
      0, 0, 0,
      0, 0, 0,
      {'R', 'V', 'B',0,
       0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
      XvTopToBottom
    },
    { /* RGB 565 */
      PIXEL_FMT_RGB6,
      XvRGB,
      LSBFirst,
      {'R','V','1','6',
       0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
      16,
      XvPacked,
      1,
      16, 0xF800, 0x07E0, 0x001F,
      0, 0, 0,
      0, 0, 0,
      0, 0, 0,
      {'R', 'V', 'B',0,
       0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
      XvTopToBottom
    },
    {  /* YVYU */
      PIXEL_FMT_YVYU, \
      XvYUV, \
      LSBFirst, \
      {'Y','V','Y','U',
	0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71},
      16,
      XvPacked,
      1,
      0, 0, 0, 0,
      8, 8, 8,
      1, 2, 2,
      1, 1, 1,
      {'Y','V','Y','U',
       0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
      XvTopToBottom
   },
   {   /* NV12 */
      PIXEL_FMT_NV12,
      XvYUV,
      LSBFirst,
      {'N','V','1','2',
       0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71},
      12,
      XvPlanar,
      2,
      0, 0, 0, 0,
      8, 8, 8,
      1, 2, 2,
      1, 2, 2,
      {'Y','U','V',0,
       0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
      XvTopToBottom
   },
   {   /* NV21 */
      PIXEL_FMT_NV21,
      XvYUV,
      LSBFirst,
      {'N','V','2','1',
       0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71},
      12,
      XvPlanar,
      2,
      0, 0, 0, 0,
      8, 8, 8,
      1, 2, 2,
      1, 2, 2,
      {'Y','V','U',0,
       0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
      XvTopToBottom
   },
};


typedef struct {
    DisplayModePtr  currentmode;

    int pixelFormat;

    CARD16  pitch;
    CARD16  origPitch;

    int     srcOffsetX, srcOffsetY;

    CARD8   keyOP;
    CARD16  HUSF;
    CARD16  VUSF;
    CARD8   IntBit;
    CARD8   wHPre;

    float   tap_scale, tap_scale_old;

    CARD16  srcW;
    CARD16  srcH;

    BoxRec  dstBox;

    CARD32  PSY;
    CARD32  PSV;
    CARD32  PSU;

    CARD16  SCREENheight;

    CARD16  lineBufSize;

#ifdef SISMERGED
    CARD16  pitch2;
    CARD16  HUSF2;
    CARD16  VUSF2;
    CARD8   IntBit2;
    CARD8   wHPre2;

    float   tap_scale2, tap_scale2_old;

    CARD16  srcW2;
    CARD16  srcH2;

    int     srcOffsetX2, srcOffsetY2;

    BoxRec  dstBox2;
    CARD32  PSY2;
    CARD32  PSV2;
    CARD32  PSU2;
    CARD16  SCREENheight2;
    CARD16  lineBufSize2;

    DisplayModePtr  currentmode2;

    Bool    DoFirst, DoSecond;
#endif

    CARD8   bobEnable;

    CARD8   planar;
    CARD8   planar_shiftpitch;

    CARD8   contrastCtrl;
    CARD8   contrastFactor;

    CARD16  oldLine, oldtop;

    CARD8   (*VBlankActiveFunc)(SISPtr, SISPortPrivPtr);
#if 0
    CARD32  (*GetScanLineFunc)(SISPtr pSiS);
#endif

} SISOverlayRec, *SISOverlayPtr;

/***********************************************/
/*               BLITTER ADAPTOR               */
/***********************************************/

#ifdef INCL_YUV_BLIT_ADAPTOR

#define NUM_BLIT_PORTS 16

#define IMAGE_MAX_WIDTH_BLIT    2046
#define IMAGE_MAX_HEIGHT_BLIT   2046

static XF86VideoEncodingRec DummyEncodingBlit =
{
   0,
   "XV_IMAGE",
   IMAGE_MAX_WIDTH_BLIT, IMAGE_MAX_HEIGHT_BLIT,
   {1, 1}
};

#define NUM_ATTRIBUTES_BLIT 1

static XF86AttributeRec SISAttributes_Blit[NUM_ATTRIBUTES_BLIT] =
{
#if 0
   {XvSettable | XvGettable, 0, 1,             sisxvvsync},
#endif
   {XvSettable             , 0, 0,             sisxvsetdefaults}
};

#define NUM_IMAGES_BLIT 7 /* 9 */

static XF86ImageRec SISImagesBlit[NUM_IMAGES_BLIT] =
{

   XVIMAGE_YUY2,
   XVIMAGE_YV12,
   XVIMAGE_UYVY,
   XVIMAGE_I420,
#if 0
   { /* RGB 555 */
      PIXEL_FMT_RGB5,
      XvRGB,
      LSBFirst,
      {'R','V','1','5',
       0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
      16,
      XvPacked,
      1,
      15, 0x7C00, 0x03E0, 0x001F,
      0, 0, 0,
      0, 0, 0,
      0, 0, 0,
      {'R', 'V', 'B',0,
       0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
      XvTopToBottom
   },
   { /* RGB 565 */
      PIXEL_FMT_RGB6,
      XvRGB,
      LSBFirst,
      {'R','V','1','6',
       0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
      16,
      XvPacked,
      1,
      16, 0xF800, 0x07E0, 0x001F,
      0, 0, 0,
      0, 0, 0,
      0, 0, 0,
      {'R', 'V', 'B',0,
       0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
      XvTopToBottom
   },
#endif
   {  /* YVYU */
      PIXEL_FMT_YVYU, \
      XvYUV, \
      LSBFirst, \
      {'Y','V','Y','U',
	0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71},
      16,
      XvPacked,
      1,
      0, 0, 0, 0,
      8, 8, 8,
      1, 2, 2,
      1, 1, 1,
      {'Y','V','Y','U',
       0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
      XvTopToBottom
   },
   {   /* NV12 */
      PIXEL_FMT_NV12,
      XvYUV,
      LSBFirst,
      {'N','V','1','2',
       0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71},
      12,
      XvPlanar,
      2,
      0, 0, 0, 0,
      8, 8, 8,
      1, 2, 2,
      1, 2, 2,
      {'Y','U','V',0,
       0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
      XvTopToBottom
   },
   {   /* NV21 */
      PIXEL_FMT_NV21,
      XvYUV,
      LSBFirst,
      {'N','V','2','1',
       0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71},
      12,
      XvPlanar,
      2,
      0, 0, 0, 0,
      8, 8, 8,
      1, 2, 2,
      1, 2, 2,
      {'Y','V','U',0,
       0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
      XvTopToBottom
   }
};

typedef struct {
    void *	 handle[NUM_BLIT_PORTS];
    CARD32       bufAddr[NUM_BLIT_PORTS][2];

    void *	 ScaleBufHandle[NUM_BLIT_PORTS];
    CARD32       ScaleBufAddr[NUM_BLIT_PORTS][2];

    UChar        currentBuf[NUM_BLIT_PORTS];

    RegionRec    blitClip[NUM_BLIT_PORTS];

    CARD32       videoStatus[NUM_BLIT_PORTS];
    Time         freeTime[NUM_BLIT_PORTS];

    Bool	 vsync;
    CARD32	 AccelCmd;
    CARD32       VBlankTriggerCRT1, VBlankTriggerCRT2;
} SISBPortPrivRec, *SISBPortPrivPtr;

#endif /* INCL_BLIT */

#endif


