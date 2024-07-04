/* $XFree86$ */
/* $XdotOrg$ */
/*
 * Xv driver for SiS 300, 315, 330 and 340(XGI Volari) series.
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
 *  SiS340: One overlay. Extended registers for 4-tap scaler.
 *  SiS761: One overlay. Extended registers for 4-tap scaler.
 *  SiS670/770: Two overlays. Extended registers for 4-tap scaler.
 *  XGI Volari V3XT/V5/V8: One overlay. Extended registers for 4-tap scaler.
 *
 * Help for reading the code:
 * >=315           = SIS_315_VGA
 * 300/540/630/730 = SIS_300_VGA
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
 * be the overlay adaptor, adaptor 1 the video blitter.
 * The option XvDefaultAdaptor allows reversing this.
 * The video blitter will automatically kick in when the overlay is not
 * available (such as in display modes with too high dotclocks) or not useful
 * (such as if the display mode is interlaced) or if the overlay is not
 * suitable for the video material (such as if the source width is beyond
 * hardware limits).
 * The video blitter supports scaling the video since 2005/09/25.
 * Naturally, it does not support the Xv properties.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
/*
#define XVDEBUG
*/
#include "sis.h"
#ifdef SIS_USE_XAA
#include "xf86fbman.h"
#endif
#include "regionstr.h"

#include "xf86xv.h"
#include <X11/extensions/Xv.h>
#include "dixstruct.h"
#include "fourcc.h"

#define SIS_NEED_inSISREG
#define SIS_NEED_outSISREG
#define SIS_NEED_inSISIDXREG
#define SIS_NEED_outSISIDXREG
#define SIS_NEED_setSISIDXREGmask
#define SIS_NEED_MYMMIO
#include "sis_regs.h"

#ifdef INCL_YUV_BLIT_ADAPTOR
#include "sis310_accel.h"
#endif

#include "sis_video.h"

extern void sis_print_registers(SISPtr pSiS);

void SiSInitMC(ScreenPtr pScreen);

/*********************************
 *       Raw register access     *
 *********************************/


static CARD32 _sisread(SISPtr pSiS, CARD32 reg)
{
    SIS_MMIO_IN32(pSiS->IOBase, reg);
}

static void _siswrite(SISPtr pSiS, CARD32 reg, CARD32 data)
{
    SIS_MMIO_OUT32(pSiS->IOBase, reg, data);
}


static CARD8 getsrreg(SISPtr pSiS, CARD8 reg)
{
    CARD8 ret;
    inSISIDXREG(SISSR, reg, ret);
    return ret;
}

static CARD8 getvideoreg(SISPtr pSiS, CARD8 reg)
{
    CARD8 ret;
    inSISIDXREG(SISVID, reg, ret);
    return ret;
}

static __inline void setvideoreg(SISPtr pSiS, CARD8 reg, CARD8 data)
{
    outSISIDXREG(SISVID, reg, data);
}

static __inline void setvideoregmask(SISPtr pSiS, CARD8 reg, CARD8 data, CARD8 mask)
{
    setSISIDXREGmask(SISVID, reg, data, mask);
}

static void setsrregmask(SISPtr pSiS, CARD8 reg, CARD8 data, CARD8 mask)
{
    setSISIDXREGmask(SISSR, reg, data, mask);
}

/* VBlank */
static CARD8 vblank_active_CRT1(SISPtr pSiS, SISPortPrivPtr pPriv)
{
    return(inSISREG(SISINPSTAT) & 0x08); /* Verified */
}

static CARD8 vblank_active_CRT2(SISPtr pSiS, SISPortPrivPtr pPriv)
{
    CARD8 ret;

    if(pPriv->bridgeIsSlave) return(vblank_active_CRT1(pSiS, pPriv));

    if(pSiS->VGAEngine == SIS_315_VGA) {
       inSISIDXREG(SISPART1, 0x30, ret);
    } else {
       inSISIDXREG(SISPART1, 0x25, ret);
    }
    return(ret & 0x02);  /* Verified */
}


static CARD16 get_scanline_CRT1(SISPtr pSiS)
{
    CARD32 line;

    _siswrite(pSiS, REG_PRIM_CRT_COUNTER, 0x00000001);
    line = _sisread(pSiS, REG_PRIM_CRT_COUNTER);

    return((CARD16)((line >> 16) & 0x07FF));
}

static CARD16 get_scanline_CRT2(SISPtr pSiS, SISPortPrivPtr pPriv)
{
    CARD8 reg1, reg2;

    if(pSiS->VGAEngine == SIS_315_VGA) {
       inSISIDXREG(SISPART1, 0x32, reg1);
       inSISIDXREG(SISPART1, 0x33, reg2);
    } else {
       inSISIDXREG(SISPART1, 0x27, reg1);
       inSISIDXREG(SISPART1, 0x28, reg2);
    }

    return((CARD16)(reg1 | ((reg2 & 0x70) << 4)));
}

/* Helper: Count attributes */
static int
SiSCountAttributes(XF86AttributeRec *attrs)
{
   int num = 0;

   while(attrs[num].name) num++;

   return num;
}

/*********************************
 *          Video gamma          *
 *********************************/

static void
SiSComputeXvGamma(SISPtr pSiS)
{
    int num = 255, i;
    double red = 1.0 / (double)((double)pSiS->XvGammaRed / 1000.0);
    double green = 1.0 / (double)((double)pSiS->XvGammaGreen / 1000.0);
    double blue = 1.0 / (double)((double)pSiS->XvGammaBlue / 1000.0);

    for(i = 0; i <= num; i++) {
        pSiS->XvGammaRampRed[i] =
	    (red == 1.0) ? i : (CARD8)(pow((double)i / (double)num, red) * (double)num + 0.5);

	pSiS->XvGammaRampGreen[i] =
	    (green == 1.0) ? i : (CARD8)(pow((double)i / (double)num, green) * (double)num + 0.5);

	pSiS->XvGammaRampBlue[i] =
	    (blue == 1.0) ? i : (CARD8)(pow((double)i / (double)num, blue) * (double)num + 0.5);
    }
}

static void
SiSSetXvGamma(SISPtr pSiS)
{
    int i;
    UChar backup = getsrreg(pSiS, 0x1f);
    setsrregmask(pSiS, 0x1f, 0x08, 0x18);
    for(i = 0; i <= 255; i++) {
       SIS_MMIO_OUT32(pSiS->IOBase, 0x8570,
			(i << 24)			 |
			(pSiS->XvGammaRampBlue[i] << 16) |
			(pSiS->XvGammaRampGreen[i] << 8) |
			pSiS->XvGammaRampRed[i]);
    }
    setsrregmask(pSiS, 0x1f, backup, 0xff);
}

void
SiSUpdateXvGamma(SISPtr pSiS, SISPortPrivPtr pPriv)
{
    UChar sr7 = getsrreg(pSiS, 0x07);

    if(!pSiS->XvGamma) return;
    if(!(pSiS->MiscFlags & MISC_CRT1OVERLAYGAMMA)) return;

#ifdef SISDUALHEAD
    if((pPriv->dualHeadMode) && (!pSiS->SecondHead)) return;
#endif

    if(!(sr7 & 0x04)) return;

    SiSComputeXvGamma(pSiS);
    SiSSetXvGamma(pSiS);
}

static void
SISResetXvGamma(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
    SISPortPrivPtr pPriv = GET_PORT_PRIVATE(pScrn);

    SiSUpdateXvGamma(pSiS, pPriv);
}

/*********************************
 *          InitVideo()          *
 *********************************/

void
SISInitVideo(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SISPtr pSiS = SISPTR(pScrn);
    XF86VideoAdaptorPtr *adaptors, *newAdaptors = NULL;
    XF86VideoAdaptorPtr newAdaptor = NULL, newBlitAdaptor = NULL;
    int num_adaptors;

    newAdaptor = SISSetupImageVideo(pScreen);
    if(newAdaptor) {
       SISInitOffscreenImages(pScreen);
    }

    pSiS->HaveBlitAdaptor = FALSE;

#ifdef INCL_YUV_BLIT_ADAPTOR
    if( ( (pSiS->ChipFlags & SiSCF_Is65x) ||
          (pSiS->ChipType >= SIS_330) )		&&
        (pSiS->ChipType != XGI_20)		&&
        (pScrn->bitsPerPixel != 8) ) {
       if((newBlitAdaptor = SISSetupBlitVideo(pScreen))) {
          pSiS->HaveBlitAdaptor = TRUE;
       }
    }
#endif

     /* There is a bug with the overlay on the TV of NTSC.
       * We suppose it is a HW issue.
       * Therefor we avoid it */
    if(pSiS->ChipType == SIS_662 && (pSiS->VBFlags & TV_NTSC) &&
		(pSiS->XvOnCRT2 || pSiS->MergedFB))
        pSiS->XvDefAdaptorBlit = TRUE;
		

    num_adaptors = xf86XVListGenericAdaptors(pScrn, &adaptors);

    if(newAdaptor || newBlitAdaptor) {
       int size = num_adaptors;

       if(newAdaptor)     size++;
       if(newBlitAdaptor) size++;

       newAdaptors = malloc(size * sizeof(XF86VideoAdaptorPtr*));
       if(newAdaptors) {
          if(num_adaptors) {
             memcpy(newAdaptors, adaptors, num_adaptors * sizeof(XF86VideoAdaptorPtr));
	  }
	  if(pSiS->XvDefAdaptorBlit) {
	     if(newBlitAdaptor) {
                newAdaptors[num_adaptors] = newBlitAdaptor;
                num_adaptors++;
             }
	  }
	  if(newAdaptor) {
             newAdaptors[num_adaptors] = newAdaptor;
             num_adaptors++;
          }
	  if(!pSiS->XvDefAdaptorBlit) {
	     if(newBlitAdaptor) {
                newAdaptors[num_adaptors] = newBlitAdaptor;
                num_adaptors++;
             }
	  }
	  adaptors = newAdaptors;
       }
    }

    if(num_adaptors) {
       xf86XVScreenInit(pScreen, adaptors, num_adaptors);
    }

    if(newAdaptors) {
       free(newAdaptors);
    }

#ifdef ENABLEXvMC
    SiSInitMC(pScreen);
#endif 
}

/*********************************
 *       SetPortsDefault()       *
 *********************************/

void
SISSetPortDefaults(ScrnInfoPtr pScrn, SISPortPrivPtr pPriv)
{
    SISPtr    pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
    SISEntPtr pSiSEnt = pSiS->entityPrivate;;
#endif

    pPriv->colorKey    = pSiS->colorKey = 0x000101fe;
    pPriv->brightness  = pSiS->XvDefBri;
    pPriv->contrast    = pSiS->XvDefCon;
    pPriv->hue         = pSiS->XvDefHue;
    pPriv->saturation  = pSiS->XvDefSat;
    pPriv->autopaintColorKey = TRUE;
    pPriv->disablegfx  = pSiS->XvDefDisableGfx;
    pPriv->disablegfxlr= pSiS->XvDefDisableGfxLR;
    pSiS->disablecolorkeycurrent = pSiS->XvDisableColorKey;
    pPriv->usechromakey    = pSiS->XvUseChromaKey;
    pPriv->insidechromakey = pSiS->XvInsideChromaKey;
    pPriv->yuvchromakey    = pSiS->XvYUVChromaKey;
    pPriv->chromamin       = pSiS->XvChromaMin;
    pPriv->chromamax       = pSiS->XvChromaMax;
    if(pPriv->dualHeadMode) {
#ifdef SISDUALHEAD
       if(!pSiS->SecondHead) {
          pPriv->tvxpos      = pSiS->tvxpos;
          pPriv->tvypos      = pSiS->tvypos;
	  pPriv->updatetvxpos = TRUE;
          pPriv->updatetvypos = TRUE;
       }
#endif
    } else {
       pPriv->tvxpos      = pSiS->tvxpos;
       pPriv->tvypos      = pSiS->tvypos;
       pPriv->updatetvxpos = TRUE;
       pPriv->updatetvypos = TRUE;
    }
    if(pPriv->dualHeadMode) {
#ifdef SISDUALHEAD
       pPriv->crtnum =
	  pSiSEnt->curxvcrtnum =
	     pSiSEnt->XvOnCRT2 ? 1 : 0;
#endif
    } else
       pPriv->crtnum = pSiS->XvOnCRT2 ? 1 : 0;

    pSiS->XvGammaRed = pSiS->XvGammaRedDef;
    pSiS->XvGammaGreen = pSiS->XvGammaGreenDef;
    pSiS->XvGammaBlue = pSiS->XvGammaBlueDef;
    SiSUpdateXvGamma(pSiS, pPriv);
}

/*********************************
 *          ResetVideo()         *
 *********************************/

static void
SISResetVideo(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
    SISPortPrivPtr pPriv = GET_PORT_PRIVATE(pScrn);
    int i;

    /* Unlock registers */
#ifdef UNLOCK_ALWAYS
    sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif
    if(getvideoreg (pSiS, Index_VI_Passwd) != 0xa1) {
       setvideoreg (pSiS, Index_VI_Passwd, 0x86);
       if(getvideoreg (pSiS, Index_VI_Passwd) != 0xa1)
	  xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		 "Xv: Failed to unlock video registers\n");
    }


    /* Initialize first overlay (CRT1) ------------------------------- */

    /* This bit has obviously a different meaning on 315 series (linebuffer-related) */
    if(pSiS->VGAEngine == SIS_300_VGA) {
       /* Write-enable video registers */
       setvideoregmask(pSiS, Index_VI_Control_Misc2,      0x80, 0x81);
    } else {
       /* Select overlay 2, clear all linebuffer related bits */
       setvideoregmask(pSiS, Index_VI_Control_Misc2,      0x00, 0xb1);
    }

    /* Disable overlay */
    setvideoregmask(pSiS, Index_VI_Control_Misc0,         0x00, 0x02);

    /* Disable bob de-interlacer and some strange bit */
    setvideoregmask(pSiS, Index_VI_Control_Misc1,         0x00, 0x82);

    /* Select RGB chroma key format (300 series only) */
    if(pSiS->VGAEngine == SIS_300_VGA) {
       setvideoregmask(pSiS, Index_VI_Control_Misc0,      0x00, 0x40);
    }

    /* Reset scale control and contrast */
    /* (Enable DDA (interpolation)) */
    setvideoregmask(pSiS, Index_VI_Scale_Control,         0x60, 0x60);
    setvideoregmask(pSiS, Index_VI_Contrast_Enh_Ctrl,     0x04, 0x1F);

    setvideoreg(pSiS, Index_VI_Disp_Y_Buf_Preset_Low,     0x00);
    setvideoreg(pSiS, Index_VI_Disp_Y_Buf_Preset_Middle,  0x00);
    setvideoreg(pSiS, Index_VI_UV_Buf_Preset_Low,         0x00);
    setvideoreg(pSiS, Index_VI_UV_Buf_Preset_Middle,      0x00);
    setvideoreg(pSiS, Index_VI_Disp_Y_UV_Buf_Preset_High, 0x00);
    setvideoreg(pSiS, Index_VI_Play_Threshold_Low,        0x00);
    setvideoreg(pSiS, Index_VI_Play_Threshold_High,       0x00);

    /* Clear multi-purpose bits in Overlay_OP */
    /* (On 550, this selects transparency, for others see below) */
    setvideoregmask(pSiS, Index_VI_Key_Overlay_OP, 0x00, 0xF0);

    if(pSiS->Chipset == PCI_CHIP_SIS330) {

       /* Disable contrast enhancement */
       setvideoregmask(pSiS, Index_VI_Key_Overlay_OP, 0x00, 0x10);

    } else if(pPriv->is661741760) {

       /* Disable contrast enhancement */
       setvideoregmask(pSiS, Index_VI_Key_Overlay_OP, 0x00, 0x10);

       /* Clear extended threshold and linebuffer size bits */
       setvideoregmask(pSiS, Index_VI_Key_Overlay_OP, 0x00, 0xE0);
       if(pPriv->is760) {
          setvideoregmask(pSiS, Index_VI_V_Buf_Start_Over, 0x3c, 0x3c);
       } else { /* 661, 741 */
          setvideoregmask(pSiS, Index_VI_V_Buf_Start_Over, 0x2c, 0x3c);
       }

    } else if((pSiS->Chipset == PCI_CHIP_SIS340) ||
	      (pSiS->Chipset == PCI_CHIP_XGIXG40)) {

       /* Disable contrast enhancement (?) */
       setvideoregmask(pSiS, Index_VI_Key_Overlay_OP, 0x00, 0x10);
       /* Threshold high */
       setvideoregmask(pSiS, 0xb5, 0x00, 0x01);
       setvideoregmask(pSiS, 0xb6, 0x00, 0x01);
       /* Enable horizontal, disable vertical 4-tap DDA scaler */
       setvideoregmask(pSiS, Index_VI_Key_Overlay_OP, 0x40, 0xc0);
       set_dda_regs(pSiS, 1.0);
       /* Enable software-flip */
       setvideoregmask(pSiS, Index_VI_Key_Overlay_OP, 0x20, 0x20);
       /* "Disable video processor" */
       setsrregmask(pSiS, 0x3f, 0x00, 0x02);

    } else if(pPriv->is761) {

       /* Disable contrast enhancement (?) */
       setvideoregmask(pSiS, Index_VI_Key_Overlay_OP, 0x00, 0x10);
       /* Threshold high */
       setvideoregmask(pSiS, 0xb5, 0x00, 0x01);
       setvideoregmask(pSiS, 0xb6, 0x00, 0x01);
       /* TODO: "bug of flash line and right side garbage in 761GX A1" */
       /* 761GX A0: VRB6[3] = 1, 761GX A1: ? */
       setvideoregmask(pSiS, 0xb5, 0x00, 0x06); /* = default */
       if(pSiS->ChipFlags & SiSCF_761A0) {
          setvideoregmask(pSiS, 0xb6, 0x08, 0x08); /* ? */
       } else {
          setvideoregmask(pSiS, 0xb6, 0x00, 0x08); /* ? */
       }
       /* ? */
       /* setvideoregmask(pSiS, 0xb6, 0x02, 0x02); */
       /* Enable horizontal, disable vertical 4-tap DDA scaler */
       setvideoregmask(pSiS, Index_VI_Key_Overlay_OP, 0x40, 0xC0);
       set_dda_regs(pSiS, 1.0);
       /* Enable software-flip - FIXME */
       setvideoregmask(pSiS, Index_VI_Key_Overlay_OP, 0x20, 0x20);
       /* Disable 661/741/760 workaround */
       setvideoregmask(pSiS, Index_VI_V_Buf_Start_Over, 0x00, 0x3c);
       /* "Disable video processor" */
       setsrregmask(pSiS, 0x3f, 0x00, 0x02);

	/* 	chaoyu add: fix error due to default values (from VBIOS?)  */
	if(pSiS->ChipType == SIS_662){
		setvideoreg(pSiS, Index_VI_Threshold_Ext_Low, 0x00);
		setvideoreg(pSiS, Index_VI_Threshold_Ext_High, 0x00);
		setvideoreg(pSiS, Index_VI_Line_Buffer_Size_High, 0x00);
		setvideoreg(pSiS, Index_VI_SubPict_Threshold_Ext, 0x08);		
	}

    } else if(pPriv->is670) {
    
       /* Disable contrast enhancement (?) */
       setvideoregmask(pSiS, Index_VI_Key_Overlay_OP, 0x00, 0x10);
       /* Threshold high */
       setvideoregmask(pSiS, 0xb5, 0x00, 0x01);
       setvideoregmask(pSiS, 0xb6, 0x00, 0x07);
       /* Enable horizontal, disable vertical 4-tap DDA scaler */
       setvideoregmask(pSiS, Index_VI_Key_Overlay_OP, 0x40, 0xC0);
       /* ? */
       /* setvideoregmask(pSiS, 0xb6, 0x02, 0x02); */
       set_dda_regs(pSiS, 1.0);
       setvideoregmask(pSiS, Index_VI_V_Buf_Start_Over, 0x00, 0x3c);

	/* chaoyu add: patch some errors */
	setvideoregmask(pSiS, Index_VI_Control_Misc4, 0x00, 0x01);/* Disable 4-taps DDA scaler */
	setvideoregmask(pSiS, Index_VI_MCE_Control_Misc1, 0x00, 0x01);/* fix the green screen error */
	setvideoreg(pSiS, Index_VI_FIFO_Max, 0x00);/* fix hanging error */
	
	setvideoreg(pSiS, Index_VI_Threshold_Ext_Low, 0x00);
	setvideoreg(pSiS, Index_VI_Threshold_Ext_High, 0x00);
	setvideoreg(pSiS, Index_VI_Line_Buffer_Size_High, 0x02);
	setvideoreg(pSiS, Index_VI_SubPict_Threshold_Ext, 0x00);/* fix the sawtooth error */	
	
	/* TODO: we set VRb9 & VRba fixed values, but we should not do so. */
	setvideoreg(pSiS, Source_VertLine_Number_Low, 0x28);
	setvideoreg(pSiS, Source_VertLine_Number_High, 0xf4);

	/* Disable MCE Subpicture*/
	setvideoreg(pSiS, Index_VI_MCE_Control_Misc3, 0x00);
	
    }

    /* Disable "video only" */
    if((pSiS->ChipFlags & SiSCF_Is65x)	||
       pPriv->is661741760		||
       pPriv->is761			||
       pPriv->is670) {
       setvideoregmask(pSiS, Index_VI_Control_Misc2,  0x00, 0x04);
    }

    /* Reset top window position for scanline check */
    setvideoreg(pSiS, Index_VI_Win_Ver_Disp_Start_Low, 0x00);
    setvideoreg(pSiS, Index_VI_Win_Ver_Over, 0x00);

    /* Initialize second overlay (CRT2) */
    if(pSiS->hasTwoOverlays) {

	if(pSiS->VGAEngine == SIS_300_VGA) {
	   /* Write-enable video registers */
	   setvideoregmask(pSiS, Index_VI_Control_Misc2,      0x81, 0x81);
	} else {
	   /* Select overlay 2, clear all linebuffer related bits */
	   setvideoregmask(pSiS, Index_VI_Control_Misc2,      0x01, 0xb1);
	}

	/* Disable overlay */
	setvideoregmask(pSiS, Index_VI_Control_Misc0,         0x00, 0x02);

	/* Disable bob de-interlacer and some strange bit */
	setvideoregmask(pSiS, Index_VI_Control_Misc1,         0x00, 0x82);

	/* Select RGB chroma key format */
	if(pSiS->VGAEngine == SIS_300_VGA) {
	   setvideoregmask(pSiS, Index_VI_Control_Misc0,      0x00, 0x40);
	}

	/* Reset scale control and contrast */
	/* (Enable DDA (interpolation)) */
	setvideoregmask(pSiS, Index_VI_Scale_Control,         0x60, 0x60);
	setvideoregmask(pSiS, Index_VI_Contrast_Enh_Ctrl,     0x04, 0x1F);

	setvideoreg(pSiS, Index_VI_Disp_Y_Buf_Preset_Low,     0x00);
	setvideoreg(pSiS, Index_VI_Disp_Y_Buf_Preset_Middle,  0x00);
	setvideoreg(pSiS, Index_VI_UV_Buf_Preset_Low,         0x00);
	setvideoreg(pSiS, Index_VI_UV_Buf_Preset_Middle,      0x00);
	setvideoreg(pSiS, Index_VI_Disp_Y_UV_Buf_Preset_High, 0x00);
	setvideoreg(pSiS, Index_VI_Play_Threshold_Low,        0x00);
	setvideoreg(pSiS, Index_VI_Play_Threshold_High,       0x00);

	/* Clear multi-purpose bits in Overlay_OP */
	/* (On 550, this selects transparency, for others see below) */
	setvideoregmask(pSiS, Index_VI_Key_Overlay_OP, 0x00, 0xF0);

	if(pPriv->is661741760) {

	   CARD8 temp;
	   setvideoregmask(pSiS, Index_VI_Key_Overlay_OP, 0x00, 0xE0);
	   switch(pSiS->ChipType) {
	   case SIS_661: temp = 0x24; break;
	   case SIS_741: temp = 0x2c; break;
	   default: 	 temp = 0x3c;
	   }
	   setvideoregmask(pSiS, Index_VI_V_Buf_Start_Over, temp, 0x3c);

	} else if(pPriv->is761) {	/* Perhaps only one overlay */

	   setvideoregmask(pSiS, Index_VI_V_Buf_Start_Over, 0x00, 0x3c);

	} else if(pPriv->is670) {

	   /* FIXME */
	   setvideoregmask(pSiS, Index_VI_V_Buf_Start_Over, 0x00, 0x3c);

	}

	setvideoreg(pSiS, Index_VI_Win_Ver_Disp_Start_Low, 0x00);
	setvideoreg(pSiS, Index_VI_Win_Ver_Over, 0x00);

    }

    /* set default properties for overlay 1 (CRT1) -------------------------- */
    setvideoregmask(pSiS, Index_VI_Control_Misc2,         0x00, 0x01);
    setvideoregmask(pSiS, Index_VI_Contrast_Enh_Ctrl,     0x04, 0x07);
    setvideoreg(pSiS, Index_VI_Brightness,                0x20);
    if(pSiS->VGAEngine == SIS_315_VGA) {
       setvideoreg(pSiS, Index_VI_Hue,          	  0x00);
       setvideoreg(pSiS, Index_VI_Saturation,             0x00);
    }

    /* set default properties for overlay 2(CRT2)  -------------------------- */
    if(pSiS->hasTwoOverlays) {
       setvideoregmask(pSiS, Index_VI_Control_Misc2,      0x01, 0x01);
       setvideoregmask(pSiS, Index_VI_Contrast_Enh_Ctrl,  0x04, 0x07);
       setvideoreg(pSiS, Index_VI_Brightness,             0x20);
       if(pSiS->VGAEngine == SIS_315_VGA) {
          setvideoreg(pSiS, Index_VI_Hue,                 0x00);
          setvideoreg(pSiS, Index_VI_Saturation,    	  0x00);
       }
    }

    /* Reset Xv gamma correction */
    if(pSiS->VGAEngine == SIS_315_VGA) {
       SiSUpdateXvGamma(pSiS, pPriv);
    }

    pPriv->mustresettap = TRUE;
#ifdef SISMERGED
    pPriv->mustresettap2 = TRUE;
#endif

    sis_print_registers(pSiS);
}



/*********************************
 *       Set displaymode         *
 *********************************/

/* Set display mode (single CRT1/CRT2, mirror).
 * MIRROR mode is only available on chipsets with two overlays.
 * On the other chipsets, if only CRT1 or only CRT2 are used,
 * the correct display CRT is chosen automatically. If both
 * CRT1 and CRT2 are connected, the user can choose between
 * CRT1 and CRT2 by using the option XvOnCRT2.
 */

static void
set_dispmode(ScrnInfoPtr pScrn, SISPortPrivPtr pPriv)
{
    SISPtr pSiS = SISPTR(pScrn);

    pPriv->dualHeadMode = pPriv->bridgeIsSlave = FALSE;

    if(SiSBridgeIsInSlaveMode(pScrn)) {
       pPriv->bridgeIsSlave = TRUE;
    }

    if( (pSiS->VBFlags & VB_DISPMODE_MIRROR) ||
        ((pPriv->bridgeIsSlave) && (pSiS->VBFlags & DISPTYPE_DISP2)) )  {
       if(pPriv->hasTwoOverlays)
	   pPriv->displayMode = DISPMODE_MIRROR;    /* CRT1+CRT2 (2 overlays) */
       else if(pPriv->crtnum)
	  pPriv->displayMode = DISPMODE_SINGLE2;    /* CRT2 only */
       else
	  pPriv->displayMode = DISPMODE_SINGLE1;    /* CRT1 only */
    } else {
#ifdef SISDUALHEAD
       if(pSiS->DualHeadMode) {
	  pPriv->dualHeadMode = TRUE;
	  if(pSiS->SecondHead)
	     pPriv->displayMode = DISPMODE_SINGLE1; /* CRT1 only */
	  else
	     pPriv->displayMode = DISPMODE_SINGLE2; /* CRT2 only */
       } else
#endif
       if(pSiS->VBFlags & DISPTYPE_DISP1) {
	  pPriv->displayMode = DISPMODE_SINGLE1;    /* CRT1 only */
       } else {
	  pPriv->displayMode = DISPMODE_SINGLE2;    /* CRT2 only */
       }
    }
}

static void
set_disptype_regs(ScrnInfoPtr pScrn, SISPortPrivPtr pPriv, Bool useoverlay1, Bool useoverlay2)
{
    SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
    SISEntPtr pSiSEnt = pSiS->entityPrivate;
    int crtnum = 0;

    if(pPriv->dualHeadMode) crtnum = pSiSEnt->curxvcrtnum;
#endif

    /*
     *     SR06[7:6]
     *	      Bit 7: Enable overlay 1 on CRT2
     *	      Bit 6: Enable overlay 0 on CRT2
     *     SR32[7:6]
     *        Bit 7: DCLK/TCLK overlay 1
     *               0=DCLK (overlay on CRT1)
     *               1=TCLK (overlay on CRT2)
     *        Bit 6: DCLK/TCLK overlay 0
     *               0=DCLK (overlay on CRT1)
     *               1=TCLK (overlay on CRT2)
     *
     * On chipsets with two overlays, we can freely select and also
     * have a mirror mode. However, we use overlay 0 for CRT1 and
     * overlay 1 for CRT2.
     * ATTENTION: CRT2 can only take up to 1 (one) overlay. Setting
     * SR06/32 to 0xc0 DOES NOT WORK. THAT'S CONFIRMED.
     * Therefore, we use overlay 0 on CRT2 if in SINGLE2 mode.
     *
     * Addendum: Since we can scale better if using dual line buffer
     * merge on 315 series, we use overlay 0 in MergedFB mode if
     * only one overlay is needed currently.
     *
     * For chipsets with only one overlay, user must choose whether
     * to display the overlay on CRT1 or CRT2 by setting XvOnCRT2
     * to TRUE (CRT2) or FALSE (CRT1). The driver does this auto-
     * matically if only CRT1 or only CRT2 is used.
     */
#ifdef UNLOCK_ALWAYS
    sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

    switch(pPriv->displayMode) {

       case DISPMODE_SINGLE1:				/* CRT1-only mode: */
	  if(pPriv->hasTwoOverlays) {
	      if(pPriv->dualHeadMode) {
		 setsrregmask(pSiS, 0x06, 0x00, 0x40);  /* overlay 0 -> CRT1 */
		 setsrregmask(pSiS, 0x32, 0x00, 0x40);
	      } else {
		 setsrregmask(pSiS, 0x06, 0x00, 0xc0);  /* both overlays -> CRT1 */
		 setsrregmask(pSiS, 0x32, 0x00, 0xc0);
	      }
	  } else {
#ifdef SISDUALHEAD
	      if((!pPriv->dualHeadMode) || (crtnum == 0)) {
#endif
		 setsrregmask(pSiS, 0x06, 0x00, 0xc0);  /* only overlay -> CRT1 */
		 setsrregmask(pSiS, 0x32, 0x00, 0xc0);
#ifdef SISDUALHEAD
	      }
#endif
	  }
	  break;

       case DISPMODE_SINGLE2:			/* CRT2-only mode: */
	  if(pPriv->hasTwoOverlays) {
	      if(pPriv->dualHeadMode) {
		 setsrregmask(pSiS, 0x06, 0x80, 0x80);  /* overlay 1 -> CRT2 */
		 setsrregmask(pSiS, 0x32, 0x80, 0x80);
	      } else {
		 setsrregmask(pSiS, 0x06, 0x40, 0xc0);  /* overlay 0 -> CRT2 */
		 setsrregmask(pSiS, 0x32, 0xc0, 0xc0);  /* (although both clocks for CRT2!) */
	      }
	  } else {
#ifdef SISDUALHEAD
	      if((!pPriv->dualHeadMode) || (crtnum == 1)) {
#endif
		 if(pSiS->MiscFlags & MISC_SIS760ONEOVERLAY) {
		    setsrregmask(pSiS, 0x06, 0x40, 0xc0);  /* overlay 0 -> CRT2 */
		    setsrregmask(pSiS, 0x32, 0xc0, 0xc0);  /* (although both clocks for CRT2!) */
		 } else {
		    setsrregmask(pSiS, 0x06, 0x40, 0xc0);  /* only overlay -> CRT2 */
		    setsrregmask(pSiS, 0x32, 0x40, 0xc0);
		 }
#ifdef SISDUALHEAD
              }
#endif
	  }
	  break;

       case DISPMODE_MIRROR:				/* CRT1+CRT2-mode: (only on chips with 2 overlays) */
       default:
#ifdef SISMERGED
	  if(!pSiS->MergedFB			||
	     pSiS->VGAEngine == SIS_300_VGA	||
	     (useoverlay1 && useoverlay2)) {
#endif
	     setsrregmask(pSiS, 0x06, 0x80, 0xc0);      /* overlay 0 -> CRT1, overlay 1 -> CRT2 */
	     setsrregmask(pSiS, 0x32, 0x80, 0xc0);
#ifdef SISMERGED
	  } else if(useoverlay1) {			/* (unless we only use one, make this 0 in any case) */
	     setsrregmask(pSiS, 0x06, 0x00, 0xc0);
	     setsrregmask(pSiS, 0x32, 0x00, 0xc0);
	  } else {
	     setsrregmask(pSiS, 0x06, 0x40, 0xc0);
	     setsrregmask(pSiS, 0x32, 0xc0, 0xc0);
	  }
#endif
	  break;
    }
}



/* interface for XvMC to reset overlay */
void
SISXvMCResetVideo(ScrnInfoPtr pScrn){

   SISPortPrivPtr pPriv = GET_PORT_PRIVATE(pScrn);

   pPriv->overlayStatus = TRUE;


   /* Choosing the overlay. */ 
   set_dispmode(pScrn, pPriv);
   set_disptype_regs(pScrn, pPriv, TRUE, TRUE);

   SISResetVideo(pScrn);
}


static void
disableoverlay(SISPtr pSiS, SISPortPrivPtr pPriv, int ovnum)
{
   int i;
   int ovcheck = ovnum ? 0x80 : 0x40;
   unsigned int watchdog;
   unsigned char temp = getvideoreg(pSiS, Index_VI_Control_Misc0);

  if(temp & 0x02) {
      temp = getsrreg(pSiS, 0x06);
      watchdog = WATCHDOG_DELAY;
      if(!(temp & ovcheck)) {
	 while((!vblank_active_CRT1(pSiS, pPriv)) && --watchdog);
	 watchdog = WATCHDOG_DELAY;
	 while(vblank_active_CRT1(pSiS, pPriv) && --watchdog);
      } else {
         while((!vblank_active_CRT2(pSiS, pPriv)) && --watchdog);
	 watchdog = WATCHDOG_DELAY;
	 while(vblank_active_CRT2(pSiS, pPriv) && --watchdog);
      }
      
      setvideoregmask(pSiS, Index_VI_Control_Misc0, 0x00, 0x02);
      /********************************************************
        Reset the poistion of overlay:
        On 662 (and later?), the position of overaly can't not be beyond the 
        screen, or the overlay crashes. However switching mode of lower 
        resolution may cause these situations. For safty, we shall trun off 
        the overlay by setting VR30[1] as 0. This value will be loaded actually 
        ONLY after VR74[0] is set as 1.
      *********************************************************/
      if(pSiS->ChipType == SIS_662){
         setvideoreg(pSiS, Index_VI_Control_Misc3, 0x01);
         setvideoreg(pSiS, Index_VI_Control_Misc3, 0x00);/* reset */
       }
   }
}

static void
set_hastwooverlays(SISPtr pSiS, SISPortPrivPtr pPriv)
{
    if(pSiS->hasTwoOverlays) {
       if(pSiS->MiscFlags & MISC_SIS760ONEOVERLAY) {
          if(pPriv->hasTwoOverlays) {
	     /* Disable overlay 1 on change */
	     setvideoregmask(pSiS, Index_VI_Control_Misc2, 0x01, 0x01);
	     setvideoregmask(pSiS, Index_VI_Control_Misc1, 0x00, 0x01);
	     disableoverlay(pSiS, pPriv, 1);
	  }
          pPriv->hasTwoOverlays = FALSE;
       } else {
          pPriv->hasTwoOverlays = TRUE;
       }
    } else {
       pPriv->hasTwoOverlays = FALSE;
    }
}

static void
set_allowswitchcrt(SISPtr pSiS, SISPortPrivPtr pPriv)
{
    if(pPriv->hasTwoOverlays) {
       pPriv->AllowSwitchCRT = FALSE;
    } else if((!(pSiS->VBFlags & DISPTYPE_DISP1)) || (!(pSiS->VBFlags & DISPTYPE_DISP2))) {
       pPriv->AllowSwitchCRT = FALSE;
       if(!(pSiS->VBFlags & DISPTYPE_DISP1)) pPriv->crtnum = 1;
       else                                  pPriv->crtnum = 0;
    } else {
       pPriv->AllowSwitchCRT = TRUE;
    }
}

static void
set_maxencoding(SISPtr pSiS, SISPortPrivPtr pPriv)
{
    int half;

    if(pSiS->VGAEngine == SIS_300_VGA) {
       DummyEncoding.width = IMAGE_MAX_WIDTH_300;
       DummyEncoding.height = IMAGE_MAX_HEIGHT_300;
    } else {
       DummyEncoding.width = IMAGE_MAX_WIDTH_315;
       DummyEncoding.height = IMAGE_MAX_HEIGHT_315;
       half = IMAGE_MAX_WIDTH_315 >> 1;
       if(pPriv->is661741760) {
          half = 768 * 2;
       } else if(pPriv->is340) { /* 2 overlays? */
          DummyEncoding.width = IMAGE_MAX_WIDTH_340;
       } else if(pPriv->is761 || pPriv->is670) {
          DummyEncoding.width = IMAGE_MAX_WIDTH_761;
          half = 1920; /* ? */
       }
       if(pPriv->hasTwoOverlays) {
#ifdef SISDUALHEAD
          if(pSiS->DualHeadMode) {
	     DummyEncoding.width = half;
          } else
#endif
#ifdef SISMERGED
          if(pSiS->MergedFB) {
	     DummyEncoding.width = half;
          } else
#endif
          if(pPriv->displayMode == DISPMODE_MIRROR) {
	     DummyEncoding.width = half;
          }
       }
    }
}

/*********************************
 *       ResetXvDisplay()        *
 *********************************/

static void
SISResetXvDisplay(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
   SISPortPrivPtr pPriv = GET_PORT_PRIVATE(pScrn);

   if(!pPriv) return;

   set_hastwooverlays(pSiS, pPriv);
   set_allowswitchcrt(pSiS, pPriv);
   set_dispmode(pScrn, pPriv);
   set_maxencoding(pSiS, pPriv);
}

/*********************************
 *       SetupImageVideo()       *
 *********************************/

static XF86VideoAdaptorPtr
SISSetupImageVideo(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SISPtr pSiS = SISPTR(pScrn);
    XF86VideoAdaptorPtr adapt;
    SISPortPrivPtr pPriv;

#if XF86_VERSION_CURRENT < XF86_VERSION_NUMERIC(4,1,99,1,0)
    XAAInfoRecPtr pXAA = pSiS->AccelInfoPtr;

    if(!pXAA || !pXAA->FillSolidRects) {
       return NULL;
    }
#endif

    if(!(adapt = calloc(1, sizeof(XF86VideoAdaptorRec) +
                            sizeof(SISPortPrivRec) +
                            sizeof(DevUnion)))) {
       return NULL;
    }

    adapt->type = XvWindowMask | XvInputMask | XvImageMask;
    adapt->flags = VIDEO_OVERLAID_IMAGES | VIDEO_CLIP_TO_VIEWPORT;
    adapt->name = "SIS 300/315/330 series Video Overlay";
    adapt->nEncodings = 1;
    adapt->pEncodings = &DummyEncoding;

    adapt->nFormats = NUM_FORMATS;
    adapt->pFormats = SISFormats;
    adapt->nPorts = 1;
    adapt->pPortPrivates = (DevUnion*)(&adapt[1]);

    pPriv = (SISPortPrivPtr)(&adapt->pPortPrivates[1]);

    pPriv->videoStatus = 0;
    pPriv->currentBuf  = 0;
    pPriv->handle      = NULL;
    pPriv->grabbedByV4L= FALSE;
    pPriv->NoOverlay   = FALSE;
    pPriv->PrevOverlay = FALSE;
    pPriv->is661741760 = ((pSiS->ChipType >= SIS_661)  &&
			  (pSiS->ChipType <= SIS_760)) ? TRUE : FALSE;
    pPriv->is760       = (pSiS->ChipType == SIS_760)   ? TRUE : FALSE;
    pPriv->is340       = ((pSiS->ChipType >= SIS_340)  &&
			  (pSiS->ChipType <= SIS_342)) ? TRUE : FALSE;
    pPriv->is761       = ((pSiS->ChipType >= SIS_761) &&
			  (pSiS->ChipType <= SIS_662)) ? TRUE : FALSE;
    pPriv->is670       = ((pSiS->ChipType >= SIS_670) && 
			  (pSiS->ChipType <= SIS_671)) ? TRUE : FALSE;
    pPriv->isXGI       = (pSiS->ChipType >= XGI_40)    ? TRUE : FALSE;

    /* Setup chipset type helpers */
    set_hastwooverlays(pSiS, pPriv);
    set_allowswitchcrt(pSiS, pPriv);

    pPriv->havetapscaler = FALSE;
    if(pPriv->is340 || pPriv->is761 || pPriv->is670 ||pPriv->isXGI) {
       pPriv->havetapscaler = TRUE;
    }
    if( pPriv->is761 || pPriv->is670)	pPriv->PitchAlignmentMask = 63; /* 256 alignment */	
    else /* old chips */  pPriv->PitchAlignmentMask = 7;
		

    adapt->pPortPrivates[0].ptr = (pointer)(pPriv);
    if(pSiS->VGAEngine == SIS_300_VGA) {
       adapt->nImages = NUM_IMAGES_300;
       adapt->pAttributes = SISAttributes_300;
       adapt->nAttributes = SiSCountAttributes(&SISAttributes_300[0]);
    } else {
       if(pSiS->ChipType >= SIS_330) {
	   	adapt->nImages = NUM_IMAGES_330;
       } else {
          adapt->nImages = NUM_IMAGES_315;
       }
       adapt->pAttributes = SISAttributes_315;
       adapt->nAttributes = SiSCountAttributes(&SISAttributes_315[0]);
       if((pSiS->hasTwoOverlays) && (!(pSiS->SiS_SD2_Flags & SiS_SD2_SUPPORT760OO))) {
          adapt->nAttributes--;
       }
    }

    adapt->pImages = SISImages;
    adapt->PutVideo = NULL;
    adapt->PutStill = NULL;
    adapt->GetVideo = NULL;
    adapt->GetStill = NULL;
    adapt->StopVideo = SISStopVideo;
    adapt->SetPortAttribute = SISSetPortAttribute;
    adapt->GetPortAttribute = SISGetPortAttribute;
    adapt->QueryBestSize = SISQueryBestSize;
    adapt->PutImage = SISPutImage;
    adapt->ReputImage = NULL;
    adapt->QueryImageAttributes = SISQueryImageAttributes;

    /* gotta uninit this someplace */
#if defined(REGION_NULL)
    REGION_NULL(pScreen, &pPriv->clip);
#else
    REGION_INIT(pScreen, &pPriv->clip, NullBox, 0);
#endif

    pSiS->adaptor = adapt;

    pSiS->xvBrightness        = MAKE_ATOM(sisxvbrightness);
    pSiS->xvContrast          = MAKE_ATOM(sisxvcontrast);
    pSiS->xvColorKey          = MAKE_ATOM(sisxvcolorkey);
    pSiS->xvSaturation        = MAKE_ATOM(sisxvsaturation);
    pSiS->xvHue               = MAKE_ATOM(sisxvhue);
    pSiS->xvSwitchCRT  	      = MAKE_ATOM(sisxvswitchcrt);
    pSiS->xvAutopaintColorKey = MAKE_ATOM(sisxvautopaintcolorkey);
    pSiS->xvSetDefaults       = MAKE_ATOM(sisxvsetdefaults);
    pSiS->xvDisableGfx        = MAKE_ATOM(sisxvdisablegfx);
    pSiS->xvDisableGfxLR      = MAKE_ATOM(sisxvdisablegfxlr);
    pSiS->xvTVXPosition       = MAKE_ATOM(sisxvtvxposition);
    pSiS->xvTVYPosition       = MAKE_ATOM(sisxvtvyposition);
    pSiS->xvGammaRed	      = MAKE_ATOM(sisxvgammared);
    pSiS->xvGammaGreen	      = MAKE_ATOM(sisxvgammagreen);
    pSiS->xvGammaBlue	      = MAKE_ATOM(sisxvgammablue);
    pSiS->xvDisableColorkey   = MAKE_ATOM(sisxvdisablecolorkey);
    pSiS->xvUseChromakey      = MAKE_ATOM(sisxvusechromakey);
    pSiS->xvInsideChromakey   = MAKE_ATOM(sisxvinsidechromakey);
    pSiS->xvYUVChromakey      = MAKE_ATOM(sisxvyuvchromakey);
    pSiS->xvChromaMin	      = MAKE_ATOM(sisxvchromamin);
    pSiS->xvChromaMax         = MAKE_ATOM(sisxvchromamax);
#ifdef TWDEBUG
    pSiS->xv_STR	      = MAKE_ATOM(sisxvsetreg);
#endif

    pSiS->xv_sisdirectunlocked = 0;

    /* 300 series require double words for addresses and pitches,
     * 315/330 series require word.
     * Start address and  pitch of Y,U,V of 770 and 670 is represented in 32 bytes unit.
     */
    switch (pSiS->VGAEngine) {
    case SIS_315_VGA:
	if (pPriv->is670) 	pPriv->shiftValue = 5;
	else 	pPriv->shiftValue = 1;
	break;
    case SIS_300_VGA:
    default:
	pPriv->shiftValue = 2;
	break;
    }

    /* Set displayMode according to VBFlags */
    set_dispmode(pScrn, pPriv);

    /* Now for the linebuffer stuff.
     * All chipsets have a certain number of linebuffers, each of a certain
     * size. The number of buffers is per overlay.
     * Chip        number      size     	  max video size
     *  300          2		 ?		     720x576
     *  630/730      2		 ?		     720x576
     *  315          2		960?		    1920x1080
     *  550	     2?		960?		    1920x1080?
     *  650/740      2		960 ("120x128")	    1920x1080
     *  M650/651..   4		480		    1920x1080
     *  330          2		960		    1920x1080
     *  661/741/760  4		768 		    1920x1080
     *  340          4	       1280?		    1920x1080?
     *  761          4         1536?		    1920x1080?
     * The unit of size is unknown; I just know that a size of 480 limits
     * the video source width to 384. Beyond that, line buffers must be
     * merged (otherwise the video output is garbled).
     * To use the maximum width (eg 1920x1080 on the 315 series, including
     * the M650, 651 and later), *all* line buffers must be merged. Hence,
     * we can only use one overlay. This should be set up for modes where
     * either only CRT1 or only CRT2 is used.
     * If both overlays are going to be used (such as in modes were both
     * CRT1 and CRT2 are active), we are limited to the half of the
     * maximum width, or 1536 on 661/741/760.
     * There is a known hardware problem with the 760 and 761 if the video
     * data is in the UMA area: The memory access latency is too big to
     * allow two overlays under some circumstances. Therefore, we must
     * support switching between hasTwoOverlays and !hasTwoOverlays on
     * the fly.
     */

    if(pSiS->VGAEngine == SIS_300_VGA) {
       pPriv->linebufmask = 0x11;
       pPriv->linebufMergeLimit = 384;
    } else {
       pPriv->linebufmask = 0xb1;
       pPriv->linebufMergeLimit = 384;			/* should be 480 */
       if(pPriv->is661741760) {
          pPriv->linebufMergeLimit = 576;		/* should be 768 */
       } else if(pPriv->is340) {
          pPriv->linebufMergeLimit = 1280;		/* should be 1280 */
       } else if(pPriv->is761) {
          pPriv->linebufMergeLimit = 1280;		/* should be 1536 */
       } else if(pPriv->is670) {
          pPriv->linebufMergeLimit = 1920;		/* FIXME */
       } else if(pPriv->isXGI) {
          pPriv->linebufMergeLimit = 1280;		/* FIXME */
       } else if(!(pPriv->hasTwoOverlays)) {
          pPriv->linebufMergeLimit = 720;		/* should be 960 */
       }
    }

    set_maxencoding(pSiS, pPriv);

    /* Reset the properties to their defaults */
    SISSetPortDefaults(pScrn, pPriv);

    /* Set SR(06, 32) registers according to DISPMODE */
    set_disptype_regs(pScrn, pPriv, TRUE, TRUE);

    SISResetVideo(pScrn);
    pSiS->ResetXv = SISResetVideo;
    pSiS->ResetXvDisplay = SISResetXvDisplay;
    if(pSiS->VGAEngine == SIS_315_VGA) {
       pSiS->ResetXvGamma = SISResetXvGamma;
    }

    return adapt;
}

#if XF86_VERSION_CURRENT < XF86_VERSION_NUMERIC(4,3,99,3,0)
static Bool
RegionsEqual(RegionPtr A, RegionPtr B)
{
    int *dataA, *dataB;
    int num;

    num = REGION_NUM_RECTS(A);
    if(num != REGION_NUM_RECTS(B))
       return FALSE;

    if((A->extents.x1 != B->extents.x1) ||
       (A->extents.x2 != B->extents.x2) ||
       (A->extents.y1 != B->extents.y1) ||
       (A->extents.y2 != B->extents.y2))
       return FALSE;

    dataA = (int*)REGION_RECTS(A);
    dataB = (int*)REGION_RECTS(B);

    while(num--) {
      if((dataA[0] != dataB[0]) || (dataA[1] != dataB[1]))
         return FALSE;
      dataA += 2;
      dataB += 2;
    }

    return TRUE;
}
#endif

/*********************************
 *       SetPortAttribute()      *
 *********************************/

void
SISUpdateVideoParms(SISPtr pSiS, SISPortPrivPtr pPriv)
{
  set_hastwooverlays(pSiS, pPriv);
  set_allowswitchcrt(pSiS, pPriv);
  set_dispmode(pSiS->pScrn, pPriv);
  set_maxencoding(pSiS, pPriv);
}

static int
SISSetPortAttribute(ScrnInfoPtr pScrn, Atom attribute,
		    INT32 value, pointer data)
{
  SISPortPrivPtr pPriv = (SISPortPrivPtr)data;
  SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
  SISEntPtr pSiSEnt = pSiS->entityPrivate;;
#endif

#ifdef XVDEBUG
	xf86DrvMsg(0, X_INFO, "[Xv] SetPortAttribute is called: attribute=%d, value=%d\n",
				attribute, value);
#endif

  if(attribute == pSiS->xvBrightness) {
     if((value < -128) || (value > 127))
        return BadValue;
     pPriv->brightness = value;
  } else if(attribute == pSiS->xvContrast) {
     if((value < 0) || (value > 7))
        return BadValue;
     pPriv->contrast = value;
  } else if(attribute == pSiS->xvColorKey) {
     pPriv->colorKey = pSiS->colorKey = value;
     REGION_EMPTY(pScrn->pScreen, &pPriv->clip);
  } else if(attribute == pSiS->xvAutopaintColorKey) {
     if((value < 0) || (value > 1))
        return BadValue;
     pPriv->autopaintColorKey = value;
  } else if(attribute == pSiS->xvSetDefaults) {
     SISSetPortDefaults(pScrn, pPriv);
  } else if(attribute == pSiS->xvDisableGfx) {
     if((value < 0) || (value > 1))
        return BadValue;
     pPriv->disablegfx = value;
  } else if(attribute == pSiS->xvDisableGfxLR) {
     if((value < 0) || (value > 1))
        return BadValue;
     pPriv->disablegfxlr = value;
  } else if(attribute == pSiS->xvTVXPosition) {
     if((value < -32) || (value > 32))
        return BadValue;
     pPriv->tvxpos = value;
     if(pSiS->xv_sisdirectunlocked) {
        SiS_SetTVxposoffset(pScrn, pPriv->tvxpos);
        pPriv->updatetvxpos = FALSE;
     } else {
        pSiS->tvxpos = pPriv->tvxpos;
#ifdef SISDUALHEAD
        if(pPriv->dualHeadMode) pSiSEnt->tvxpos = pPriv->tvxpos;
#endif
        pPriv->updatetvxpos = TRUE;
     }
  } else if(attribute == pSiS->xvTVYPosition) {
     if((value < -32) || (value > 32)) return BadValue;
     pPriv->tvypos = value;
     if(pSiS->xv_sisdirectunlocked) {
        SiS_SetTVyposoffset(pScrn, pPriv->tvypos);
        pPriv->updatetvypos = FALSE;
     } else {
        pSiS->tvypos = pPriv->tvypos;
#ifdef SISDUALHEAD
        if(pPriv->dualHeadMode) pSiSEnt->tvypos = pPriv->tvypos;
#endif
        pPriv->updatetvypos = TRUE;
     }
  } else if(attribute == pSiS->xvDisableColorkey) {
     if((value < 0) || (value > 1)) return BadValue;
     pSiS->disablecolorkeycurrent = value;
  } else if(attribute == pSiS->xvUseChromakey) {
     if((value < 0) || (value > 1)) return BadValue;
     pPriv->usechromakey = value;
  } else if(attribute == pSiS->xvInsideChromakey) {
     if((value < 0) || (value > 1)) return BadValue;
     pPriv->insidechromakey = value;
  } else if(attribute == pSiS->xvYUVChromakey) {
     if((value < 0) || (value > 1)) return BadValue;
     pPriv->yuvchromakey = value;
  } else if(attribute == pSiS->xvChromaMin) {
     pPriv->chromamin = value;
  } else if(attribute == pSiS->xvChromaMax) {
     pPriv->chromamax = value;
  } else if(attribute == pSiS->xvHue) {
     if(pSiS->VGAEngine == SIS_315_VGA) {
        if((value < -8) || (value > 7)) return BadValue;
        pPriv->hue = value;
     } else return BadMatch;
  } else if(attribute == pSiS->xvSaturation) {
     if(pSiS->VGAEngine == SIS_315_VGA) {
        if((value < -7) || (value > 7)) return BadValue;
        pPriv->saturation = value;
     } else return BadMatch;
  } else if(attribute == pSiS->xvGammaRed) {
     if(pSiS->VGAEngine == SIS_315_VGA) {
        if((value < 100) || (value > 10000))  return BadValue;
        pSiS->XvGammaRed = value;
        SiSUpdateXvGamma(pSiS, pPriv);
     } else return BadMatch;
  } else if(attribute == pSiS->xvGammaGreen) {
     if(pSiS->VGAEngine == SIS_315_VGA) {
        if((value < 100) || (value > 10000)) return BadValue;
        pSiS->XvGammaGreen = value;
        SiSUpdateXvGamma(pSiS, pPriv);
     } else return BadMatch;
  } else if(attribute == pSiS->xvGammaBlue) {
     if(pSiS->VGAEngine == SIS_315_VGA) {
        if((value < 100) || (value > 10000)) return BadValue;
        pSiS->XvGammaBlue = value;
        SiSUpdateXvGamma(pSiS, pPriv);
     } else return BadMatch;
  } else if(attribute == pSiS->xvSwitchCRT) {
     if(pSiS->VGAEngine == SIS_315_VGA) {
        if(pPriv->AllowSwitchCRT) {
           if((value < 0) || (value > 1))
              return BadValue;
	   pPriv->crtnum = value;
#ifdef SISDUALHEAD
           if(pPriv->dualHeadMode) pSiSEnt->curxvcrtnum = value;
#endif
        }
     } else return BadMatch;
  } else {
#ifdef TWDEBUG
     return(SISSetPortUtilAttribute(pScrn, attribute, value, pPriv));
#else
     return BadMatch;
#endif
  }
  return Success;
}

/*********************************
 *       GetPortAttribute()      *
 *********************************/

static int
SISGetPortAttribute(ScrnInfoPtr pScrn, Atom attribute,
			INT32 *value, pointer data)
{
  SISPortPrivPtr pPriv = (SISPortPrivPtr)data;
  SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
  SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

#ifdef XVDEBUG
	xf86DrvMsg(0, X_INFO, "[Xv] GetPortAttribute is called: attribute=%d\n",attribute);
#endif


  if(attribute == pSiS->xvBrightness) {
     *value = pPriv->brightness;
  } else if(attribute == pSiS->xvContrast) {
     *value = pPriv->contrast;
  } else if(attribute == pSiS->xvColorKey) {
     *value = pPriv->colorKey;
  } else if(attribute == pSiS->xvAutopaintColorKey) {
     *value = (pPriv->autopaintColorKey) ? 1 : 0;
  } else if(attribute == pSiS->xvDisableGfx) {
     *value = (pPriv->disablegfx) ? 1 : 0;
  } else if(attribute == pSiS->xvDisableGfxLR) {
     *value = (pPriv->disablegfxlr) ? 1 : 0;
  } else if(attribute == pSiS->xvTVXPosition) {
     *value = SiS_GetTVxposoffset(pScrn);
  } else if(attribute == pSiS->xvTVYPosition) {
     *value = SiS_GetTVyposoffset(pScrn);
  } else if(attribute == pSiS->xvDisableColorkey) {
     *value = (pSiS->disablecolorkeycurrent) ? 1 : 0;
  } else if(attribute == pSiS->xvUseChromakey) {
     *value = (pPriv->usechromakey) ? 1 : 0;
  } else if(attribute == pSiS->xvInsideChromakey) {
     *value = (pPriv->insidechromakey) ? 1 : 0;
  } else if(attribute == pSiS->xvYUVChromakey) {
     *value = (pPriv->yuvchromakey) ? 1 : 0;
  } else if(attribute == pSiS->xvChromaMin) {
     *value = pPriv->chromamin;
  } else if(attribute == pSiS->xvChromaMax) {
     *value = pPriv->chromamax;
  } else if(attribute == pSiS->xvHue) {
     if(pSiS->VGAEngine == SIS_315_VGA) {
        *value = pPriv->hue;
     } else return BadMatch;
  } else if(attribute == pSiS->xvSaturation) {
     if(pSiS->VGAEngine == SIS_315_VGA) {
        *value = pPriv->saturation;
     } else return BadMatch;
  } else if(attribute == pSiS->xvGammaRed) {
     if(pSiS->VGAEngine == SIS_315_VGA) {
        *value = pSiS->XvGammaRed;
     } else return BadMatch;
  } else if(attribute == pSiS->xvGammaGreen) {
     if(pSiS->VGAEngine == SIS_315_VGA) {
        *value = pSiS->XvGammaGreen;
     } else return BadMatch;
  } else if(attribute == pSiS->xvGammaBlue) {
     if(pSiS->VGAEngine == SIS_315_VGA) {
        *value = pSiS->XvGammaBlue;
     } else return BadMatch;
  } else if(attribute == pSiS->xvSwitchCRT) {
     if(pSiS->VGAEngine == SIS_315_VGA) {
#ifdef SISDUALHEAD
        if(pPriv->dualHeadMode)
           *value = pSiSEnt->curxvcrtnum;
        else
#endif
           *value = pPriv->crtnum;
     } else return BadMatch;
  } else {
     return BadMatch;
  }

#ifdef XVDEBUG
	xf86DrvMsg(0, X_INFO, "[Xv] GetPortAttribute is called: value=%d\n",value);
#endif
  return Success;
}

/*********************************
 *         QueryBestSize()       *
 *********************************/

static void
SISQueryBestSize(
  ScrnInfoPtr pScrn,
  Bool motion,
  short vid_w, short vid_h,
  short drw_w, short drw_h,
  unsigned int *p_w, unsigned int *p_h,
  pointer data
){
  *p_w = drw_w;
  *p_h = drw_h;

#ifdef XVDEBUG
	xf86DrvMsg(0, X_INFO, "[Xv] QueryBestSize is called: p_w=%d, p_h=%d\n",
				*p_w, *p_h);
#endif
}

/*********************************
 *       Calc scaling factor     *
 *********************************/

static void
calc_scale_factor(SISOverlayPtr pOverlay, ScrnInfoPtr pScrn,
                 SISPortPrivPtr pPriv, int iscrt2)
{
  SISPtr pSiS = SISPTR(pScrn);
  CARD32 I = 0, mult = 0;
  int flag = 0, flag2 = 0;

  int dstW = pOverlay->dstBox.x2 - pOverlay->dstBox.x1;
  int dstH = pOverlay->dstBox.y2 - pOverlay->dstBox.y1;
  int srcW = pOverlay->srcW;
  int srcH = pOverlay->srcH;
  CARD16 LCDheight = pSiS->LCDheight;
  int srcPitch = pOverlay->origPitch;
  int origdstH = dstH;
  int modeflags = pOverlay->currentmode->Flags;

  /* Stretch image due to panel link scaling */
  if(pSiS->VBFlags & (CRT2_LCD | CRT1_LCDA)) {
     if(pPriv->bridgeIsSlave) {
	if(pSiS->VBFlags2 & (VB2_LVDS | VB2_30xBDH)) {
	   if(pSiS->MiscFlags & MISC_PANELLINKSCALER) {
	      dstH = (dstH * LCDheight) / pOverlay->SCREENheight;
	   }
	}
     } else if((iscrt2 && (pSiS->VBFlags & CRT2_LCD)) ||
	       (!iscrt2 && (pSiS->VBFlags & CRT1_LCDA))) {
	if((pSiS->VBFlags2 & (VB2_LVDS | VB2_30xBDH)) || (pSiS->VBFlags & CRT1_LCDA)) {
	   if(pSiS->MiscFlags & MISC_PANELLINKSCALER) {
	      dstH = (dstH * LCDheight) / pOverlay->SCREENheight;
	      if(pPriv->displayMode == DISPMODE_MIRROR) flag = 1;
	   }
	}
     }
     if((pPriv->bridgeIsSlave || iscrt2) &&
        (pSiS->MiscFlags & MISC_STNMODE)) {
	flag2 = 1;
     }
  }

  /* For double scan modes, we need to double the height
   * On 315 and 550 (?), we need to double the width as well.
   * Interlace mode vice versa.
   */
  if((modeflags & V_DBLSCAN) && !flag2) {
     dstH = origdstH << 1;
     flag = 0;
     if((pSiS->ChipType >= SIS_315H) &&
	(pSiS->ChipType <= SIS_550)) {
	dstW <<= 1;
     }
  } else if((pSiS->MiscFlags & MISC_INTERLACE) && !iscrt2) {
     dstH = origdstH >> 1;
     flag = 0;
  }


  pOverlay->tap_scale = 1.0;

  if(dstW < OVERLAY_MIN_WIDTH) dstW = OVERLAY_MIN_WIDTH;

  if(dstW == srcW) {

     pOverlay->HUSF   = 0x00;
     pOverlay->IntBit = 0x05;
     pOverlay->wHPre  = 0;

  } else if(dstW > srcW) {

     pOverlay->IntBit = 0x04;
     pOverlay->wHPre  = 0;

     if(pPriv->havetapscaler) {
        if((dstW > 2) && (srcW > 2)) {
           pOverlay->HUSF = (((srcW - 2) << 16) + dstW - 3) / (dstW - 2);
        } else {
           pOverlay->HUSF = ((srcW << 16) + dstW - 1) / dstW;
        }
     } else {
        dstW += 2;
        pOverlay->HUSF = (srcW << 16) / dstW;
     }

  } else {

     int tmpW = dstW;

     /* It seems, the hardware can't scale below factor .125 (=1/8) if the
        pitch isn't a multiple of 256.
	TODO: Test this on the 315 series!
      */
     if((srcPitch % 256) || (srcPitch < 256)) {
        if(((dstW * 1000) / srcW) < 125) dstW = tmpW = ((srcW * 125) / 1000) + 1;
     }

     I = 0;
     pOverlay->IntBit = 0x01;
     while(srcW >= tmpW) {
        tmpW <<= 1;
        I++;
     }
     pOverlay->wHPre = (CARD8)(I - 1);
     dstW <<= (I - 1);

     pOverlay->tap_scale = (float)srcW / (float)dstW;
     if(pOverlay->tap_scale < 1.0) pOverlay->tap_scale = 1.0;

     if((srcW % dstW))
        pOverlay->HUSF = ((srcW - dstW) << 16) / dstW;
     else
        pOverlay->HUSF = 0;
  }

  if(dstH < OVERLAY_MIN_HEIGHT) dstH = OVERLAY_MIN_HEIGHT;

  if(dstH == srcH) {

     pOverlay->VUSF   = 0x00;
     pOverlay->IntBit |= 0x0A;

  } else if(dstH > srcH) {

     dstH += 2;
     pOverlay->IntBit |= 0x08;

     if(pPriv->havetapscaler) {
        if((dstH > 2) && (srcH > 2)) {
           pOverlay->VUSF = (((srcH - 2) << 16) - 32768 + dstH - 3) / (dstH - 2);
        } else {
           pOverlay->VUSF = ((srcH << 16) + dstH - 1) / dstH;
        }
     } else {
        pOverlay->VUSF = (srcH << 16) / dstH;
     }

  } else {

     I = srcH / dstH;
     pOverlay->IntBit |= 0x02;

     if(I < 2) {
	pOverlay->VUSF = ((srcH - dstH) << 16) / dstH;
	/* Needed for LCD-scaling modes */
	if((flag) && (mult = (srcH / origdstH)) >= 2) {
	   pOverlay->pitch /= mult;
	}
     } else {
#if 0
	if(((pOverlay->bobEnable & 0x08) == 0x00) &&
	   (((srcPitch * I) >> 2) > 0xFFF)){
	   pOverlay->bobEnable |= 0x08;
	   srcPitch >>= 1;
	}
#endif
	if(((srcPitch * I) >> 2) > 0xFFF) {
	   I = (0xFFF * 2 / srcPitch);
	   pOverlay->VUSF = 0xFFFF;
	} else {
	   dstH = I * dstH;
	   if(srcH % dstH)
	      pOverlay->VUSF = ((srcH - dstH) << 16) / dstH;
	   else
	      pOverlay->VUSF = 0;
	}
	/* set video frame buffer offset */
	pOverlay->pitch = (CARD16)(srcPitch * I);
     }
  }
}

#ifdef SISMERGED
static void
calc_scale_factor_2(SISOverlayPtr pOverlay, ScrnInfoPtr pScrn,
					SISPortPrivPtr pPriv)
{
  SISPtr pSiS = SISPTR(pScrn);
  CARD32 I=0,mult=0;
  int flag=0, flag2=0;

  int dstW = pOverlay->dstBox2.x2 - pOverlay->dstBox2.x1;
  int dstH = pOverlay->dstBox2.y2 - pOverlay->dstBox2.y1;
  int srcW = pOverlay->srcW2;
  int srcH = pOverlay->srcH2;
  CARD16 LCDheight = pSiS->LCDheight;
  int srcPitch = pOverlay->origPitch;
  int origdstH = dstH;
  int modeflags = pOverlay->currentmode2->Flags;

  /* Stretch image due to panel link scaling */
  if(pSiS->VBFlags & CRT2_LCD) {
     if(pSiS->VBFlags2 & (VB2_LVDS | VB2_30xBDH)) {
	if(pSiS->MiscFlags & MISC_PANELLINKSCALER) {
	   dstH = (dstH * LCDheight) / pOverlay->SCREENheight2;
	   flag = 1;
	}
	if(pSiS->MiscFlags & MISC_STNMODE) flag2 = 1;
     }
  }
  /* For double scan modes, we need to double the height
   * On 315 and 550 (?), we need to double the width as well.
   * Interlace mode vice versa.
   */
  if((modeflags & V_DBLSCAN) && !flag2) {
     dstH = origdstH << 1;
     flag = 0;
     if((pSiS->ChipType >= SIS_315H) &&
	(pSiS->ChipType <= SIS_550)) {
	dstW <<= 1;
     }
  }
  /* CRT2 is never interlace */

  pOverlay->tap_scale2 = 1.0;

  if(dstW < OVERLAY_MIN_WIDTH) dstW = OVERLAY_MIN_WIDTH;

  if(dstW == srcW) {

     pOverlay->HUSF2   = 0x00;
     pOverlay->IntBit2 = 0x05;
     pOverlay->wHPre2  = 0;

  } else if(dstW > srcW) {

     pOverlay->IntBit2 = 0x04;
     pOverlay->wHPre2  = 0;

     if(pPriv->havetapscaler) {
        if((dstW > 2) && (srcW > 2)) {
           pOverlay->HUSF2 = (((srcW - 2) << 16) + dstW - 3) / (dstW - 2);
        } else {
           pOverlay->HUSF2 = ((srcW << 16) + dstW - 1) / dstW;
        }
     } else {
        dstW += 2;
        pOverlay->HUSF2 = (srcW << 16) / dstW;
     }

  } else {

     int tmpW = dstW;

     /* It seems, the hardware can't scale below factor .125 (=1/8) if the
	pitch isn't a multiple of 256.
	TODO: Test this on the 315 series!
      */
     if((srcPitch % 256) || (srcPitch < 256)) {
	if(((dstW * 1000) / srcW) < 125) dstW = tmpW = ((srcW * 125) / 1000) + 1;
     }

     I = 0;
     pOverlay->IntBit2 = 0x01;
     while(srcW >= tmpW) {
        tmpW <<= 1;
        I++;
     }
     pOverlay->wHPre2 = (CARD8)(I - 1);
     dstW <<= (I - 1);

     pOverlay->tap_scale2 = (float)srcW / (float)dstW;
     if(pOverlay->tap_scale2 < 1.0) pOverlay->tap_scale2 = 1.0;

     if((srcW % dstW))
        pOverlay->HUSF2 = ((srcW - dstW) << 16) / dstW;
     else
        pOverlay->HUSF2 = 0x00;
  }

  if(dstH < OVERLAY_MIN_HEIGHT) dstH = OVERLAY_MIN_HEIGHT;

  if(dstH == srcH) {

     pOverlay->VUSF2   = 0x00;
     pOverlay->IntBit2 |= 0x0A;

  } else if(dstH > srcH) {

     dstH += 2;
     pOverlay->IntBit2 |= 0x08;

     if(pPriv->havetapscaler) {
        if((dstH > 2) && (srcH > 2)) {
           pOverlay->VUSF2 = (((srcH - 2) << 16) - 32768 + dstH - 3) / (dstH - 2);
        } else {
           pOverlay->VUSF2 = ((srcH << 16) + dstH - 1) / dstH;
        }
     } else {
        pOverlay->VUSF2 = (srcH << 16) / dstH;
     }

  } else {

     I = srcH / dstH;
     pOverlay->IntBit2 |= 0x02;

     if(I < 2) {
	pOverlay->VUSF2 = ((srcH - dstH) << 16) / dstH;
	/* Needed for LCD-scaling modes */
	if(flag && ((mult = (srcH / origdstH)) >= 2)) {
	   pOverlay->pitch2 /= mult;
	}
     } else {
#if 0
	if(((pOverlay->bobEnable & 0x08) == 0x00) &&
	   (((srcPitch * I)>>2) > 0xFFF)){
	   pOverlay->bobEnable |= 0x08;
	   srcPitch >>= 1;
	}
#endif
	if(((srcPitch * I) >> 2) > 0xFFF) {
	   I = (0xFFF * 2 / srcPitch);
	   pOverlay->VUSF2 = 0xFFFF;
	} else {
	   dstH = I * dstH;
	   if(srcH % dstH)
	      pOverlay->VUSF2 = ((srcH - dstH) << 16) / dstH;
	   else
	      pOverlay->VUSF2 = 0x00;
	}
	/* set video frame buffer offset */
	pOverlay->pitch2 = (CARD16)(srcPitch * I);
     }
  }
}
#endif

/*********************************
 *    Handle 4-tap scaler (340)  *
 *********************************/

static float
tap_dda_func(float x)
{
    double pi = 3.14159265358979;
    float  r = 0.5, y;

    if(x == 0.0) {
       y = 1.0;
    } else if(x == -1.0 || x == 1.0) {
       y = 0.0;
       /* case ((x == -1.0 / (r * 2.0)) || (x == 1.0 / (r * 2.0))): */
       /* y = (float)(r / 2.0 * sin(pi / (2.0 * r))); = 0.013700916287197;    */
    } else {
       y = sin(pi * x) / (pi * x) * cos(r * pi * x) / (1 - x * x);
       /* y = sin(pi * x) / (pi * x) * cos(r * pi * x) / (1 - 4 * r * r * x * x); */
    }

    return y;
}


void 
set_dda_regs_6tap(SISPtr pSiS, float scale)
{
	float WW, W[6], tempW[6];
	int i, j, w, k, m, *temp[6], *wtemp[6], WeightMat[16][6], idxw;
	int *wm1, *wm2, *wm3, *wm4, *wm5, *wm6, *tempadd;
	long top, bot;
	unsigned long dwScanLine, dwTopLine, dwBottomLine;
	

	for (i=0; i<16; i++)
	{
		/*The order of weights are inversed for convolution*/
		W[0] = tap_dda_func((float)((2.0+(i/16.0))/scale));
		W[1] = tap_dda_func((float)((1.0+(i/16.0))/scale));
		W[2] = tap_dda_func((float)((0.0+(i/16.0))/scale));
		W[3] = tap_dda_func((float)((-1.0+(i/16.0))/scale));
		W[4] = tap_dda_func((float)((-2.0+(i/16.0))/scale));
		W[5] = tap_dda_func((float)((-3.0+(i/16.0))/scale));

		/*Normalize the weights*/
		WW = W[0]+W[1]+W[2]+W[3]+W[4]+W[5];

		/*for rouding*/
		for(j=0; j<6; j++)
			tempW[j] = (float)((W[j]/WW*16)+0.5);

		WeightMat[i][0] = (int) tempW[0];
		WeightMat[i][1] = (int) tempW[1];
		WeightMat[i][2] = (int) tempW[2];
		WeightMat[i][3] = (int) tempW[3];
		WeightMat[i][4] = (int) tempW[4];
		WeightMat[i][5] = (int) tempW[5];

		/*check for display abnormal caused by rounding*/
		w = WeightMat[i][0] + WeightMat[i][1] + WeightMat[i][2] + WeightMat[i][3]+ WeightMat[i][4]+ WeightMat[i][5];
		/*initialize temp[] value*/
		temp[0] = &WeightMat[i][0];
		temp[1] = &WeightMat[i][1];
		temp[2] = &WeightMat[i][2];
		temp[3] = &WeightMat[i][3];
		temp[4] = &WeightMat[i][4];
		temp[5] = &WeightMat[i][5];
		if( w != 16 )
		{
			/*
			//6_tap sort
			//tempadd save temp[k], idxw save maximum value index
			//temp[0] -> temp[5] maximum to minimum
			*/
			for (k=0; k<5; k++)
			{
				idxw=k;
				for (m=k+1; m<6; m++)
				{
					if (*temp[k] < *temp[m])
						idxw=m;
				}
				tempadd=temp[k];
				temp[k]=temp[idxw];
				temp[idxw]=tempadd;
			}
			wm1=temp[0];
			wm2=temp[1];
			wm3=temp[2];
			wm4=temp[3];
			wm5=temp[4];
			wm6=temp[5];

			switch(w)
			{
		    		case 10:
					WeightMat[i][0]++;
					WeightMat[i][1]++;
					WeightMat[i][2]++;
					WeightMat[i][3]++;
					WeightMat[i][4]++;
					WeightMat[i][5]++;				
					break;
				
				case 11:
					(*wm1)++;
					(*wm2)++;
					(*wm3)++;
					(*wm5)++;
					(*wm6)++;
					break;
				
	                   	case 12:
					(*wm1)++;
					(*wm2)++;
					(*wm5)++;
					(*wm6)++;
	                            break;

       	              case 13:
					(*wm1)++;
					(*wm2)++;
					(*wm6)++;
					break;

			       case 14:
					(*wm1)++;
					(*wm6)++;
		    		    break;

			       case 15:
					(*wm1)++;
			    	    break;

			       case 17:
			        	(*wm6)--;
			        	break;

			       case 18:
					(*wm1)--;
					(*wm6)--;
		       	     break;

			       case 19:
					(*wm1)--;
					(*wm5)--;
					(*wm6)--;
                            	break;
	                     case 20:
					(*wm1)--;
					(*wm2)--;
					(*wm5)--;
					(*wm6)--;
	                            break;

				case 21:
					(*wm1)--;
					(*wm2)--;
					(*wm4)--;
					(*wm5)--;
					(*wm6)--;
					break;
				
				case 22:
					WeightMat[i][0]--;
					WeightMat[i][1]--;
					WeightMat[i][2]--;
					WeightMat[i][3]--;
					WeightMat[i][4]--;
					WeightMat[i][5]--;				
					break;

				default:
	                            break;
			}
		}
	}


	/* In 770/771/671, setting VR75/76/77/78 would update overlay HW immediately without setting VR74, 
	  * it cause garbage points/lines appear in Video. Add the waiting function to make sure driver would set VR75/76/77/78 in
	  * video blank to avoid this problem.
	  
	if( (ppdev->ulChipID == SIS_770) || (ppdev->ulChipID >= SIS_771) )
	{

			top  = pDD->OverlayStatus[pDD->bSetTimes].rectl.bottom;
			if ((top == 0xFFF0) || (top < 0) || (top > (long)*ppdev->DD.gDisp_VRes[pDD->bSetTimes]))
				top = (long)(*pDD->gDisp_VRes[pDD->bSetTimes]);
			bot  = *pDD->gDisp_VRes[pDD->bSetTimes];
			if (ppdev->bInterlaced)
				bot >>= 1;

			dwBottomLine = bot;        
			dwTopLine = top;

			if (dwTopLine >(dwBottomLine - 30))
			{
				do
				{
					dwScanLine = ppdev->DD.GET_SCAN_LINE(ppdev);
				}while (dwScanLine <= dwTopLine);
			}
			else
			{
				do
				{
					dwScanLine = ppdev->DD.GET_SCAN_LINE(ppdev);
				}while ((dwScanLine <= dwTopLine) || (dwScanLine >dwBottomLine - 20));
			}
	}
	*/

	/*set DDA registers*/
	for(i=0;i<16;i++)
		for(j=0;j<6;j++)
		{
			setvideoregmask(pSiS, Horizontal_6Tap_DDA_WeightingMatrix_Index, (6*i + j), 0x7F);
			setvideoregmask(pSiS, Horizontal_6Tap_DDA_WeightingMatrix_Value, WeightMat[i][j], 0x3F);
		}	
}



static void
set_dda_regs(SISPtr pSiS, float scale)
{
    float W[4], WS, myadd;
    int   *temp[4], *wm1, *wm2, *wm3, *wm4;
    int   i, j, w, tidx, weightmatrix[16][4];

    for(i = 0; i < 16; i++) {

       myadd = ((float)i) / 16.0;
       WS = W[0] = tap_dda_func((myadd + 1.0) / scale);
       W[1] = tap_dda_func(myadd / scale);
       WS += W[1];
       W[2] = tap_dda_func((myadd - 1.0) / scale);
       WS += W[2];
       W[3] = tap_dda_func((myadd - 2.0) / scale);
       WS += W[3];

       w = 0;
       for(j = 0; j < 4; j++) {
	  weightmatrix[i][j] = (int)(((float)((W[j] * 16.0 / WS) + 0.5)));
	  w += weightmatrix[i][j];
       }

       if(w == 12) {

	  weightmatrix[i][0]++;
	  weightmatrix[i][1]++;
	  weightmatrix[i][2]++;
	  weightmatrix[i][3]++;

       } else if(w == 20) {

	  weightmatrix[i][0]--;
	  weightmatrix[i][1]--;
	  weightmatrix[i][2]--;
	  weightmatrix[i][3]--;

       } else if(w != 16) {

	  tidx = (weightmatrix[i][0] > weightmatrix[i][1]) ? 0 : 1;
	  temp[0] = &weightmatrix[i][tidx];
	  temp[1] = &weightmatrix[i][tidx ^ 1];

	  tidx = (weightmatrix[i][2] > weightmatrix[i][3]) ? 2 : 3;
	  temp[2] = &weightmatrix[i][tidx];
	  temp[3] = &weightmatrix[i][tidx ^ 1];

	  tidx = (*(temp[0]) > *(temp[2])) ? 0 : 2;
	  wm1 = temp[tidx];
	  wm2 = temp[tidx ^ 2];

	  tidx = (*(temp[1]) > *(temp[3])) ? 1 : 3;
	  wm3 = temp[tidx];
	  wm4 = temp[tidx ^ 2];

	  switch(w) {
	     case 13:
		(*wm1)++;
		(*wm4)++;
		if(*wm2 > *wm3) (*wm2)++;
		else            (*wm3)++;
		break;
	     case 14:
		(*wm1)++;
		(*wm4)++;
		break;
	     case 15:
		(*wm1)++;
		break;
	     case 17:
		(*wm4)--;
		break;
	     case 18:
		(*wm1)--;
		(*wm4)--;
		break;
	     case 19:
		(*wm1)--;
		(*wm4)--;
		if(*wm2 > *wm3) (*wm3)--;
		else            (*wm2)--;
	  }
       }
    }

	if((pSiS->ChipType >= SIS_670) && (pSiS->ChipType <= SIS_671)){
		
		 for(i=0;i<16;i++){
		 	for(j=0;j<4;j++){
				setvideoregmask(pSiS, Vertical_4Tap_DDA_WeightingMatrix_Index, (4*i + j), 0x3F);
                		setvideoregmask(pSiS, Vertical_4Tap_DDA_WeightingMatrix_Value, weightmatrix[i][j], 0x3F);
            	}}
		set_dda_regs_6tap(pSiS, scale);

	}

	else{
		/* Set 4-tap scaler video regs 0x75-0xb4 */
		w = 0x75;
		for(i = 0; i < 16; i++) {
			for(j = 0; j < 4; j++, w++) {
				setvideoregmask(pSiS, w, weightmatrix[i][j], 0x3f);
	}}}
}

/*********************************
 *     Calc line buffer size     *
 *********************************/

static CARD16
calc_line_buf_size(CARD32 srcW, CARD8 wHPre, CARD8 planar, SISPortPrivPtr pPriv)
{
    CARD32 I, mask = 0xffffffff;
    CARD32 shift = (pPriv->is761 || pPriv->is670) ? 1 : 0;

    /* FIXME: Need to calc line buffer length not according
     * to total source width but width of source actually
     * visible on screen. Fixes flicker bug if overlay 1
     * (much) bigger than screen and being moved outside
     * screen.
     */

    if(planar) {

	switch(wHPre & 0x07) {
	    case 3:
		shift += 8;
		mask <<= shift;
		I = srcW >> shift;
		if((mask & srcW) != srcW) I++;
		I <<= 5;
		break;
	    case 4:
		shift += 9;
		mask <<= shift;
		I = srcW >> shift;
		if((mask & srcW) != srcW) I++;
		I <<= 6;
		break;
	    case 5:
		shift += 10;
		mask <<= shift;
		I = srcW >> shift;
		if((mask & srcW) != srcW) I++;
		I <<= 7;
		break;
	    case 6:
		if(pPriv->is661741760 || pPriv->is340 || pPriv->is761 || pPriv->is670 || pPriv->isXGI) {
		   shift += 11;
		   mask <<= shift;
		   I = srcW >> shift;
		   if((mask & srcW) != srcW) I++;
		   I <<= 8;
		   break;
		} else {
		   return((CARD16)(255));
		}
	    default:
		shift += 7;
		mask <<= shift;
		I = srcW >> shift;
		if((mask & srcW) != srcW) I++;
		I <<= 4;
		break;
	}

    } else { /* packed */

	shift += 3;
	mask <<= shift;
	I = srcW >> shift;
	if((mask & srcW) != srcW) I++;

    }

    if(I <= 3) I = 4;

    return((CARD16)(I - 1));
}

static __inline void
calc_line_buf_size_1(SISOverlayPtr pOverlay, SISPortPrivPtr pPriv)
{
    pOverlay->lineBufSize =
	calc_line_buf_size(pOverlay->srcW, pOverlay->wHPre, pOverlay->planar, pPriv);
}

#ifdef SISMERGED
static __inline void
calc_line_buf_size_2(SISOverlayPtr pOverlay, SISPortPrivPtr pPriv)
{
    pOverlay->lineBufSize2 =
	calc_line_buf_size(pOverlay->srcW2, pOverlay->wHPre2, pOverlay->planar, pPriv);
}

/**********************************
 *En/Disable merging of linebuffer*
 **********************************/

static void
merge_line_buf_mfb(SISPtr pSiS, SISPortPrivPtr pPriv, Bool enable1, Bool enable2,
			Bool useoverlay1, Bool useoverlay2,
			short width1, short width2, short limit)
{
    UChar misc1, misc2, mask = pPriv->linebufmask;

    if(pPriv->hasTwoOverlays) {

       /* Note: misc2 is shared! Hence, set it once only */

       misc1 = 0x00; misc2 = 0x00;
       if(enable1) {
	  if(useoverlay2 || pSiS->VGAEngine == SIS_300_VGA) misc1 = 0x04;
	  else						    misc2 = 0x10;
       }
       setvideoregmask(pSiS, Index_VI_Control_Misc2, misc2, 0x01);
       setvideoregmask(pSiS, Index_VI_Control_Misc1, misc1, 0x04);

       misc1 = 0x00; misc2 |= 0x01;
       if(enable2) {
	  if(useoverlay1 || pSiS->VGAEngine == SIS_300_VGA) misc1 = 0x04;
	  else						    misc2 |= 0x10;
       }
       setvideoregmask(pSiS, Index_VI_Control_Misc2, misc2, mask);
       setvideoregmask(pSiS, Index_VI_Control_Misc1, misc1, 0x04);

    } else {

       misc2 = 0x00;
       if(enable1 || enable2) {
          if(pSiS->MiscFlags & MISC_SIS760ONEOVERLAY) {
	     if((width1 > (limit * 2)) || (width2 > (limit * 2))) {
		 misc2 = 0x20;
	      } else {
		 misc2 = 0x10;
	      }
	      misc1 = 0x00;
	  } else {
             misc1 = 0x04;
	  }
       } else {
          misc1 = 0x00;
       }

       setvideoregmask(pSiS, Index_VI_Control_Misc2, misc2, mask);
       setvideoregmask(pSiS, Index_VI_Control_Misc1, misc1, 0x04);

    }
}
#endif

/* About linebuffer merging:
 *
 * For example the 651:
 * Each overlay has 4 line buffers, 384 bytes each (<-- Is that really correct? 1920 / 384 = 5 !!!)
 * If the source width is greater than 384, line buffers must be merged.
 * Dual merge: Only O1 usable (uses overlay 2's linebuffer), maximum width 384*2
 * Individual merge: Both overlays available, maximum width 384*2
 * All merge: Only overlay 1 available, maximum width = 384*4 (<--- should be 1920, is 1536...)
 *
 *
 *        Normally:                  Dual merge:                 Individual merge
 *  Overlay 1    Overlay 2         Overlay 1 only!                Both overlays
 *  ___1___      ___5___           ___1___ ___2___ -\         O1  ___1___ ___2___
 *  ___2___      ___6___           ___3___ ___4___   \_ O 1   O1  ___3___ ___4___
 *  ___3___      ___7___	   ___5___ ___6___   /        O2  ___5___ ___6___
 *  ___4___      ___8___           ___7___ ___8___ -/         O2  ___7___ ___8___
 *
 *
 * All merge:          ___1___ ___2___ ___3___ ___4___
 * (Overlay 1 only!)   ___5___ ___6___ ___7___ ___8___
 *
 * Individual merge is supported on all chipsets.
 * Dual merge is only supported on the 300 series and M650/651 and later.
 * All merge is only supported on the M650/651 and later.
 * Single-Overlay-chipsets only support Individual merge.
 *
 */

static void
merge_line_buf(SISPtr pSiS, SISPortPrivPtr pPriv, Bool enable, short width, short limit)
{
  UChar misc1, misc2, mask = pPriv->linebufmask;

  if(enable) { 		/* ----- enable linebuffer merge */

    switch(pPriv->displayMode){
    case DISPMODE_SINGLE1:
        if(pSiS->VGAEngine == SIS_300_VGA) {
           if(pPriv->dualHeadMode) {
	       misc2 = 0x00;
	       misc1 = 0x04;
	   } else {
	       misc2 = 0x10;
	       misc1 = 0x00;
	   }
        } else {
	   if(pPriv->hasTwoOverlays) {
	      if(pPriv->dualHeadMode) {
		 misc2 = 0x00;
		 misc1 = 0x04;
	      } else {
		 if(width > (limit * 2)) {
		    misc2 = 0x20;
		 } else {
		    misc2 = 0x10;
		 }
		 misc1 = 0x00;
	      }
	   } else if(pSiS->MiscFlags & MISC_SIS760ONEOVERLAY) {
	      if(width > (limit * 2)) {
		 misc2 = 0x20;
	      } else {
		 misc2 = 0x10;
	      }
	      misc1 = 0x00;
	   } else {
	      misc2 = 0x00;
	      misc1 = 0x04;
	   }
	}
	setvideoregmask(pSiS, Index_VI_Control_Misc2, misc2, mask);
	setvideoregmask(pSiS, Index_VI_Control_Misc1, misc1, 0x04);
      	break;

    case DISPMODE_SINGLE2:
        if(pSiS->VGAEngine == SIS_300_VGA) {
	   if(pPriv->dualHeadMode) {
	      misc2 = 0x01;
	      misc1 = 0x04;
	   } else {
	      misc2 = 0x10;
	      misc1 = 0x00;
	   }
	} else {
	   if(pPriv->hasTwoOverlays) {
	      if(pPriv->dualHeadMode) {
		 misc2 = 0x01;
		 misc1 = 0x04;
	      } else {
		 if(width > (limit * 2)) {
		    misc2 = 0x20;
		 } else {
		    misc2 = 0x10;
		 }
		 misc1 = 0x00;
	      }
	   } else if(pSiS->MiscFlags & MISC_SIS760ONEOVERLAY) {
	      if(width > (limit * 2)) {
		 misc2 = 0x20;
	      } else {
		 misc2 = 0x10;
	      }
	      misc1 = 0x00;
	   } else {
	      misc2 = 0x00;
	      misc1 = 0x04;
	   }
	}
	setvideoregmask(pSiS, Index_VI_Control_Misc2, misc2, mask);
	setvideoregmask(pSiS, Index_VI_Control_Misc1, misc1, 0x04);
     	break;

    case DISPMODE_MIRROR:   /* This can only be on chips with 2 overlays */
    default:
        setvideoregmask(pSiS, Index_VI_Control_Misc2, 0x00, mask);
      	setvideoregmask(pSiS, Index_VI_Control_Misc1, 0x04, 0x04);
	setvideoregmask(pSiS, Index_VI_Control_Misc2, 0x01, mask);
      	setvideoregmask(pSiS, Index_VI_Control_Misc1, 0x04, 0x04);
        break;
    }

  } else {		/* ----- disable linebuffer merge */

    switch(pPriv->displayMode) {

    case DISPMODE_SINGLE1:
	setvideoregmask(pSiS, Index_VI_Control_Misc2, 0x00, mask);
	setvideoregmask(pSiS, Index_VI_Control_Misc1, 0x00, 0x04);
	break;

    case DISPMODE_SINGLE2:
	if(pSiS->VGAEngine == SIS_300_VGA) {
	   if(pPriv->dualHeadMode) misc2 = 0x01;
	   else       		   misc2 = 0x00;
	} else {
	   if(pPriv->hasTwoOverlays) {
	      if(pPriv->dualHeadMode) misc2 = 0x01;
	      else                    misc2 = 0x00;
	   } else {
	      misc2 = 0x00;
	   }
	}
	setvideoregmask(pSiS, Index_VI_Control_Misc2, misc2, mask);
	setvideoregmask(pSiS, Index_VI_Control_Misc1, 0x00, 0x04);
	break;

    case DISPMODE_MIRROR:   /* This can only be on chips with 2 overlays */
    default:
	setvideoregmask(pSiS, Index_VI_Control_Misc2, 0x00, mask);
	setvideoregmask(pSiS, Index_VI_Control_Misc1, 0x00, 0x04);
	setvideoregmask(pSiS, Index_VI_Control_Misc2, 0x01, mask);
	setvideoregmask(pSiS, Index_VI_Control_Misc1, 0x00, 0x04);
	break;
    }
  }
}

/*********************************
 *      Select video format      *
 *********************************/

static __inline void
set_format(SISPtr pSiS, SISOverlayPtr pOverlay)
{
    CARD8 fmt;

    switch (pOverlay->pixelFormat){
    case PIXEL_FMT_YV12:
    case PIXEL_FMT_I420:
	fmt = 0x0c;
	break;
    case PIXEL_FMT_YUY2:
	fmt = 0x28;
	break;
    case PIXEL_FMT_UYVY:
	fmt = 0x08;
	break;
    case PIXEL_FMT_YVYU:
	fmt = 0x38;
	break;
    case PIXEL_FMT_NV12:
	fmt = 0x4c;
	break;
    case PIXEL_FMT_NV21:
	fmt = 0x5c;
	break;
    case PIXEL_FMT_RGB5:   /* D[5:4] : 00 RGB555, 01 RGB 565 */
	fmt = 0x00;
	break;
    case PIXEL_FMT_RGB6:
	fmt = 0x10;
	break;
    default:
	fmt = 0x00;
    }
    setvideoregmask(pSiS, Index_VI_Control_Misc0, fmt, 0xfc);
}

/*********************************
 *  Set various video registers  *
 *********************************/

static __inline void
set_colorkey(SISPtr pSiS, CARD32 colorkey)
{
    CARD8 r, g, b;

    b = (CARD8)(colorkey & 0xFF);
    g = (CARD8)((colorkey >> 8) & 0xFF);
    r = (CARD8)((colorkey >> 16) & 0xFF);

    setvideoreg(pSiS, Index_VI_Overlay_ColorKey_Blue_Min  ,(CARD8)b);
    setvideoreg(pSiS, Index_VI_Overlay_ColorKey_Green_Min ,(CARD8)g);
    setvideoreg(pSiS, Index_VI_Overlay_ColorKey_Red_Min   ,(CARD8)r);

    setvideoreg(pSiS, Index_VI_Overlay_ColorKey_Blue_Max  ,(CARD8)b);
    setvideoreg(pSiS, Index_VI_Overlay_ColorKey_Green_Max ,(CARD8)g);
    setvideoreg(pSiS, Index_VI_Overlay_ColorKey_Red_Max   ,(CARD8)r);
}

static __inline void
set_chromakey(SISPtr pSiS, CARD32 chromamin, CARD32 chromamax)
{
    CARD8 r1, g1, b1;
    CARD8 r2, g2, b2;

    b1 = (CARD8)(chromamin & 0xFF);
    g1 = (CARD8)((chromamin >> 8) & 0xFF);
    r1 = (CARD8)((chromamin >> 16) & 0xFF);
    b2 = (CARD8)(chromamax & 0xFF);
    g2 = (CARD8)((chromamax >> 8) & 0xFF);
    r2 = (CARD8)((chromamax >> 16) & 0xFF);

    setvideoreg(pSiS, Index_VI_Overlay_ChromaKey_Blue_V_Min  ,(CARD8)b1);
    setvideoreg(pSiS, Index_VI_Overlay_ChromaKey_Green_U_Min ,(CARD8)g1);
    setvideoreg(pSiS, Index_VI_Overlay_ChromaKey_Red_Y_Min   ,(CARD8)r1);

    setvideoreg(pSiS, Index_VI_Overlay_ChromaKey_Blue_V_Max  ,(CARD8)b2);
    setvideoreg(pSiS, Index_VI_Overlay_ChromaKey_Green_U_Max ,(CARD8)g2);
    setvideoreg(pSiS, Index_VI_Overlay_ChromaKey_Red_Y_Max   ,(CARD8)r2);
}

static __inline void
set_brightness(SISPtr pSiS, CARD8 brightness)
{
    setvideoreg(pSiS, Index_VI_Brightness, brightness);
}

static __inline void
set_contrast(SISPtr pSiS, CARD8 contrast)
{
   
   /* xf86DrvMsg(0,X_INFO,"[I_XvImage_con_val_entry]:chip==%d,con_val=%d \n",pSiS->ChipType,contrast);*/
    if((pSiS->ChipType == SIS_662)||(pSiS->ChipType == SIS_671))
    {
     CARD8  tmp;
     CARD8 reg_vrB7=0x02;
     CARD8 reg_vr2E=0;
     tmp = contrast;	    
     reg_vr2E  = tmp & 0x07;
     reg_vrB7 |= (tmp >> 1) & 0x7c;     
    /* xf86DrvMsg(0,X_INFO,"[I_XvImage_con_is_662]:con_val=%d,vr2E=%x, vrB7=%x \n",contrast,reg_vr2E,reg_vrB7);*/
     
     setvideoregmask(pSiS,Index_VI_Contrast_Enh_Ctrl,reg_vr2E,0x07);
    /* xf86DrvMsg(0,X_INFO,"[I_XvImage_con_LOW_3BITS]:set_con=%d \n",contrast);*/
      /*data = contrast >> 1;*/
    
     setvideoregmask(pSiS,0xB7,reg_vrB7,0x7E);
    /* xf86DrvMsg(0,X_INFO,"[I_XvImage_con_HI_5BITS]:set_con=%d, reg=%x \n",contrast,Index_VI_Line_Buffer_Size_High);*/


   /* var = getvideoreg(pSiS,0x2E); 
    xf86DrvMsg(0,X_INFO,"[I_XvImage_con_get_LOW_3BITS]:reg2E=%x \n",var);
    
    var1 = getvideoreg(pSiS,0xB7);
    xf86DrvMsg(0,X_INFO,"[I_XvImage_con_get_HI_5BITS]:regB7=%x \n",var1);*/
    }
    else
    { /* xf86DrvMsg(0,X_INFO,"[I_XvImage_con_NOT_662]:con_val=%d \n",contrast);*/	              setvideoregmask(pSiS,Index_VI_Contrast_Enh_Ctrl,contrast,0x07);
    }
}

/* 315 series and later only */
static __inline void
set_saturation(SISPtr pSiS, short saturation)
{
    CARD8 temp = 0;

    if(saturation < 0) {
    	temp |= 0x88;
	saturation = -saturation;
    }
    temp |= (saturation & 0x07);
    temp |= ((saturation & 0x07) << 4);

    setvideoreg(pSiS, Index_VI_Saturation, temp);
}

/* 315 series and later only */
static __inline void
set_hue(SISPtr pSiS, CARD8 hue)
{
    setvideoregmask(pSiS, Index_VI_Hue, (hue & 0x08) ? (hue ^ 0x07) : hue, 0x0F);
}

static __inline void
set_disablegfx(SISPtr pSiS, Bool mybool, SISOverlayPtr pOverlay)
{
    /* This is not supported on M65x, 65x (x>0) or later */
    /* For CRT1 ONLY!!! */
    if((!(pSiS->ChipFlags & SiSCF_Is65x)) &&
       (pSiS->Chipset != PCI_CHIP_SIS660) &&
       (pSiS->Chipset != PCI_CHIP_SIS340) &&
       (pSiS->Chipset != PCI_CHIP_SIS670) &&
       (pSiS->Chipset != PCI_CHIP_XGIXG20) &&
       (pSiS->Chipset != PCI_CHIP_XGIXG40)) {
       setvideoregmask(pSiS, Index_VI_Control_Misc2, mybool ? 0x04 : 0x00, 0x04);
       if(mybool) pOverlay->keyOP = VI_ROP_Always;
    }
}

static __inline void
set_disablegfxlr(SISPtr pSiS, Bool mybool, SISOverlayPtr pOverlay)
{
    setvideoregmask(pSiS, Index_VI_Control_Misc1, mybool ? 0x01 : 0x00, 0x01);
    if(mybool) pOverlay->keyOP = VI_ROP_Always;
}


/*********************************
 *   Set main overlay registers  *
 *********************************/

static void
set_overlay(SISPtr pSiS, SISOverlayPtr pOverlay, SISPortPrivPtr pPriv, int index, int iscrt2)
{
    CARD8  h_over, v_over;
    CARD16 top, bottom, left, right, pitch = 0;
    CARD16 screenX, screenY;
    CARD32 PSY;
    int    modeflags, totalPixels, confactor, sample, watchdog = 0;
    CARD8 alignMask;

#ifdef SISMERGED
    if(pSiS->MergedFB && iscrt2) {
       screenX = pOverlay->currentmode2->HDisplay;
       screenY = pOverlay->currentmode2->VDisplay;
       modeflags = pOverlay->currentmode2->Flags;
       top = pOverlay->dstBox2.y1;
       bottom = pOverlay->dstBox2.y2;
       left = pOverlay->dstBox2.x1;
       right = pOverlay->dstBox2.x2;
       pitch = pOverlay->pitch2 >> pPriv->shiftValue;
    } else {
#endif
       screenX = pOverlay->currentmode->HDisplay;
       screenY = pOverlay->currentmode->VDisplay;
       modeflags = pOverlay->currentmode->Flags;
       top = pOverlay->dstBox.y1;
       bottom = pOverlay->dstBox.y2;
       left = pOverlay->dstBox.x1;
       right = pOverlay->dstBox.x2;
       pitch = pOverlay->pitch >> pPriv->shiftValue;
#ifdef SISMERGED
    }
#endif

    if(bottom > screenY) bottom = screenY;
    if(right  > screenX) right  = screenX;

    /* calculate contrast factor */
    totalPixels = (right - left) * (bottom - top);
    confactor = (totalPixels - 10000) / 20000;
    if(confactor > 3) confactor = 3;
    switch(confactor) {
    case 1:  sample = 4096 << 10; break;
    case 2:
    case 3:  sample = 8192 << 10; break;
    default: sample = 2048 << 10;
    }
    sample /= totalPixels;
    confactor <<= 6;

    /* Correct coordinates for doublescan/interlace modes */
    if( (modeflags & V_DBLSCAN) &&
        (!((pPriv->bridgeIsSlave || iscrt2) && (pSiS->MiscFlags & MISC_STNMODE))) ) {
       /* DoubleScan modes require Y coordinates * 2 */
       top <<= 1;
       bottom <<= 1;
    } else if(!iscrt2 && (pSiS->MiscFlags & MISC_INTERLACE)) {
       /* Interlace modes require Y coordinates / 2 */
       top >>= 1;
       bottom >>= 1;
    }

    h_over = (((left >> 8) & 0x0f) | ((right >> 4) & 0xf0));
    v_over = (((top >> 8) & 0x0f) | ((bottom >> 4) & 0xf0));

    /* set line number ---- we only examined 662*/
    if(pSiS->ChipType == SIS_662 ){
	setvideoreg(pSiS, Source_VertLine_Number_Low, (CARD8)pOverlay->srcH);
	setvideoreg(pSiS, Source_VertLine_Number_High, pOverlay->srcH >> 8);
     }

    /* set line buffer size */
#ifdef SISMERGED
    if(pSiS->MergedFB && iscrt2) {
       setvideoreg(pSiS, Index_VI_Line_Buffer_Size, (CARD8)pOverlay->lineBufSize2);
       if(pPriv->is661741760) {
          setvideoregmask(pSiS, Index_VI_Key_Overlay_OP, (pOverlay->lineBufSize2 >> 1) & 0x80, 0x80);
       } else if(pPriv->is340 || pPriv->is761 || pPriv->is670 || pPriv->isXGI) {
          setvideoreg(pSiS, Index_VI_Line_Buffer_Size_High, (CARD8)(pOverlay->lineBufSize2 >> 8));
       }
    } else {
#endif
       setvideoreg(pSiS, Index_VI_Line_Buffer_Size, (CARD8)pOverlay->lineBufSize);
       if(pPriv->is661741760) {
          setvideoregmask(pSiS, Index_VI_Key_Overlay_OP, (pOverlay->lineBufSize >> 1) & 0x80, 0x80);
       } else if(pPriv->is340 || pPriv->isXGI) {
          setvideoreg(pSiS, Index_VI_Line_Buffer_Size_High, (CARD8)(pOverlay->lineBufSize >> 8));
       }
#ifdef SISMERGED
    }
#endif

    /* set color key mode */
    setvideoregmask(pSiS, Index_VI_Key_Overlay_OP, pOverlay->keyOP, 0x0f);

    /* We don't have to wait for vertical retrace in all cases */
    if(pPriv->mustwait) {
       if(pSiS->VGAEngine == SIS_315_VGA) {

          if(index) {
	     CARD16 mytop = getvideoreg(pSiS, Index_VI_Win_Ver_Disp_Start_Low);
	     mytop |= ((getvideoreg(pSiS, Index_VI_Win_Ver_Over) & 0x0f) << 8);
	     pOverlay->oldtop = mytop;
	     watchdog = 0xffff;
	     if(mytop < screenY - 2) {
		do {
		   watchdog = get_scanline_CRT2(pSiS, pPriv);
		} while((watchdog <= mytop) || (watchdog >= screenY));
	     }
	     pOverlay->oldLine = watchdog;
	  }

       } else {

	  watchdog = WATCHDOG_DELAY;
	  while(pOverlay->VBlankActiveFunc(pSiS, pPriv) && --watchdog);
	  watchdog = WATCHDOG_DELAY;
	  while((!pOverlay->VBlankActiveFunc(pSiS, pPriv)) && --watchdog);

       }
    }

    /* Unlock address registers */
    setvideoregmask(pSiS, Index_VI_Control_Misc1, 0x20, 0x20);

    /* Set destination window position */
    setvideoreg(pSiS, Index_VI_Win_Hor_Disp_Start_Low, (CARD8)left);
    setvideoreg(pSiS, Index_VI_Win_Hor_Disp_End_Low,   (CARD8)right);
    setvideoreg(pSiS, Index_VI_Win_Hor_Over,           (CARD8)h_over);

    setvideoreg(pSiS, Index_VI_Win_Ver_Disp_Start_Low, (CARD8)top);
    setvideoreg(pSiS, Index_VI_Win_Ver_Disp_End_Low,   (CARD8)bottom);
    setvideoreg(pSiS, Index_VI_Win_Ver_Over,           (CARD8)v_over);

    /* Contrast factor */
    setvideoregmask(pSiS, Index_VI_Contrast_Enh_Ctrl,  (CARD8)confactor, 0xc0);
    setvideoreg(pSiS, Index_VI_Contrast_Factor,        sample);

    /* Set Y buf pitch */
    setvideoreg(pSiS, Index_VI_Disp_Y_Buf_Pitch_Low, (CARD8)(pitch));
    setvideoregmask(pSiS, Index_VI_Disp_Y_UV_Buf_Pitch_Middle, (CARD8)(pitch >> 8), 0x0f);

    /* Set Y start address */
#ifdef SISMERGED
    if(pSiS->MergedFB && iscrt2) {
       PSY = pOverlay->PSY2;
    } else
#endif
       PSY = pOverlay->PSY;

    setvideoreg(pSiS, Index_VI_Disp_Y_Buf_Start_Low,    (CARD8)(PSY));
    setvideoreg(pSiS, Index_VI_Disp_Y_Buf_Start_Middle, (CARD8)(PSY >> 8));
    setvideoreg(pSiS, Index_VI_Disp_Y_Buf_Start_High,   (CARD8)(PSY >> 16));

    /* Set 315 series overflow bits for Y plane */
    if(pSiS->VGAEngine == SIS_315_VGA) {

	if(pPriv->is670)	alignMask = 0x07;
	else 	alignMask = 0x03;
	
       setvideoreg(pSiS, Index_VI_Disp_Y_Buf_Pitch_High, (CARD8)(pitch >> 12));
       setvideoreg(pSiS, Index_VI_Y_Buf_Start_Over, ((CARD8)(PSY >> 24) & alignMask));
    }

    /* Set U/V data if using planar formats */
    if(pOverlay->planar) {

	CARD32  PSU = pOverlay->PSU;
	CARD32  PSV = pOverlay->PSV;

#ifdef SISMERGED
	if(pSiS->MergedFB && iscrt2) {
	   PSU = pOverlay->PSU2;
	   PSV = pOverlay->PSV2;
	}
#endif

	if(pOverlay->planar_shiftpitch) pitch >>= 1;

	/* Set U/V pitch */
	setvideoreg(pSiS, Index_VI_Disp_UV_Buf_Pitch_Low, (CARD8)pitch);
	setvideoregmask(pSiS, Index_VI_Disp_Y_UV_Buf_Pitch_Middle, (CARD8)(pitch >> 4), 0xf0);

	/* Set U/V start address */
	setvideoreg(pSiS, Index_VI_U_Buf_Start_Low,   (CARD8)PSU);
	setvideoreg(pSiS, Index_VI_U_Buf_Start_Middle,(CARD8)(PSU >> 8));
	setvideoreg(pSiS, Index_VI_U_Buf_Start_High,  (CARD8)(PSU >> 16));

	setvideoreg(pSiS, Index_VI_V_Buf_Start_Low,   (CARD8)PSV);
	setvideoreg(pSiS, Index_VI_V_Buf_Start_Middle,(CARD8)(PSV >> 8));
	setvideoreg(pSiS, Index_VI_V_Buf_Start_High,  (CARD8)(PSV >> 16));

	/* 315 series overflow bits */
	if(pSiS->VGAEngine == SIS_315_VGA) {
	   setvideoreg(pSiS, Index_VI_Disp_UV_Buf_Pitch_High, (CARD8)(pitch >> 12));
	   setvideoreg(pSiS, Index_VI_U_Buf_Start_Over, ((CARD8)(PSU >> 24) & alignMask));
	   if(pPriv->is661741760) {
	      setvideoregmask(pSiS, Index_VI_V_Buf_Start_Over, ((CARD8)(PSV >> 24) & 0x03), 0xc3);
	   } else {
	      setvideoreg(pSiS, Index_VI_V_Buf_Start_Over, ((CARD8)(PSV >> 24) & alignMask));
	   }
	}
    }

    setvideoregmask(pSiS, Index_VI_Control_Misc1, pOverlay->bobEnable, 0x1a);

    /* Lock the address registers */
    setvideoregmask(pSiS, Index_VI_Control_Misc1, 0x00, 0x20);

    /* Set scale factor */
#ifdef SISMERGED
    if(pSiS->MergedFB && iscrt2) {
       setvideoreg(pSiS, Index_VI_Hor_Post_Up_Scale_Low, (CARD8)(pOverlay->HUSF2));
       setvideoreg(pSiS, Index_VI_Hor_Post_Up_Scale_High,(CARD8)((pOverlay->HUSF2) >> 8));
       setvideoreg(pSiS, Index_VI_Ver_Up_Scale_Low,      (CARD8)(pOverlay->VUSF2));
       setvideoreg(pSiS, Index_VI_Ver_Up_Scale_High,     (CARD8)((pOverlay->VUSF2) >> 8));

       setvideoregmask(pSiS, Index_VI_Scale_Control,     (pOverlay->IntBit2 << 3) |
                                                         (pOverlay->wHPre2), 0x7f);

       if(pPriv->havetapscaler) {
	  if((pOverlay->tap_scale2 != pOverlay->tap_scale2_old) || pPriv->mustresettap2) {
	     set_dda_regs(pSiS, pOverlay->tap_scale2);
	     pOverlay->tap_scale2_old = pOverlay->tap_scale2;
	     pPriv->mustresettap2 = FALSE;
	  }
       }
    } else {
#endif
       setvideoreg(pSiS, Index_VI_Hor_Post_Up_Scale_Low, (CARD8)(pOverlay->HUSF));
       setvideoreg(pSiS, Index_VI_Hor_Post_Up_Scale_High,(CARD8)((pOverlay->HUSF) >> 8));
       setvideoreg(pSiS, Index_VI_Ver_Up_Scale_Low,      (CARD8)(pOverlay->VUSF));
       setvideoreg(pSiS, Index_VI_Ver_Up_Scale_High,     (CARD8)((pOverlay->VUSF) >> 8));

       setvideoregmask(pSiS, Index_VI_Scale_Control,     (pOverlay->IntBit << 3) |
                                                         (pOverlay->wHPre), 0x7f);
       if(pPriv->havetapscaler) {
	  if((pOverlay->tap_scale != pOverlay->tap_scale_old) || pPriv->mustresettap) {
	     set_dda_regs(pSiS, pOverlay->tap_scale);
	     pOverlay->tap_scale_old = pOverlay->tap_scale;
	     pPriv->mustresettap = FALSE;
	  }
       }
	   
	/*  if vertically downscale, remember to set VR31[6] 1 
  	     Howerver so far, we only exam 662 & 671 */
	if(pSiS->ChipType >= SIS_662 && pSiS->ChipType < XGI_20){
		if(pOverlay->IntBit & 0x2)		setvideoregmask(pSiS, Index_VI_Control_Misc1, 0x40, 0x40);
		else		setvideoregmask(pSiS, Index_VI_Control_Misc1, 0x00, 0x40);
	}

	
#ifdef SISMERGED
    }
#endif

}

/*********************************
 *       Shut down overlay       *
 *********************************/

/* Overlay MUST NOT be switched off while beam is over it */
static void
close_overlay(SISPtr pSiS, SISPortPrivPtr pPriv)
{
  int ovnum = 0;

  if(!pPriv->overlayStatus) return;

  pPriv->overlayStatus = FALSE;

  pPriv->mustresettap = TRUE;
#ifdef SISMERGED
  pPriv->mustresettap2 = TRUE;
#endif

  if(pPriv->displayMode & (DISPMODE_MIRROR | DISPMODE_SINGLE2)) {

     /* CRT2: MIRROR or SINGLE2
      * 1 overlay:  Uses overlay 0
      * 2 overlays: Uses Overlay 1 if MIRROR or DUAL HEAD
      *             Uses Overlay 0 if SINGLE2 and not DUAL HEAD
      *
      * (No special treatment needed for MergedFB where CRT2
      * might be using overlay 0; it's switched off below anyway.
      * The only problem that might cause trouble is that it checks
      * the scanlines of CRT1. But since the chipsets where we use
      * this aren't sensible to disabling the overlay during display,
      * we keep it simple.)
      */

     if(pPriv->hasTwoOverlays) {

	if((pPriv->dualHeadMode) || (pPriv->displayMode == DISPMODE_MIRROR)) {
	   setvideoregmask(pSiS, Index_VI_Control_Misc2, 0x01, 0x01);
	   ovnum = 1;
	} else {
	   setvideoregmask(pSiS, Index_VI_Control_Misc2, 0x00, 0x01);
	}

     } else if(pPriv->displayMode == DISPMODE_SINGLE2) {

#ifdef SISDUALHEAD
	if(pPriv->dualHeadMode) {
	   /* Check if overlay already grabbed by other head */
	   if(!(getsrreg(pSiS, 0x06) & 0x40)) return;
	}
#endif
	setvideoregmask(pSiS, Index_VI_Control_Misc2, 0x00, 0x01);

     }

     setvideoregmask(pSiS, Index_VI_Control_Misc1, 0x00, 0x01);

     disableoverlay(pSiS, pPriv, ovnum);


  }

  if(pPriv->displayMode & (DISPMODE_SINGLE1 | DISPMODE_MIRROR)) {

     /* CRT1: Always uses overlay 0
      */

#ifdef SISDUALHEAD
     if(pPriv->dualHeadMode) {
	if(!pPriv->hasTwoOverlays) {
	   /* Check if overlay already grabbed by other head */
	   if(getsrreg(pSiS, 0x06) & 0x40) return;
	}
     }
#endif

     setvideoregmask(pSiS, Index_VI_Control_Misc2, 0x00, 0x05);
     setvideoregmask(pSiS, Index_VI_Control_Misc1, 0x00, 0x01);

     disableoverlay(pSiS, pPriv, 0);
  }
}


/* interface for XvMC to close overlay */
void 
SISXvMCCloseOverlay(ScrnInfoPtr pScrn){

   SISPortPrivPtr pPriv = GET_PORT_PRIVATE(pScrn);
   SISPtr pSiS = SISPTR(pScrn);
   
   /* disable subpicture*/ 
   setvideoregmask(pSiS, Index_VI_SubPict_Scale_Control, 0x00, 0x40);
   setvideoregmask(pSiS, Index_VI_Control_Misc3, 0x00, 0x04);
   
   close_overlay(pSiS, pPriv);
}

/*********************************
 *         CheckOverlay()        *
 *********************************/

 static int
 SISCheckOverlay(ScrnInfoPtr pScrn, SISPortPrivPtr pPriv, SISOverlayPtr overlay)
 {
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif
   int result = 1;
   UShort screenwidth;
#ifdef SISMERGED
   int notavail1 = 0, notavail2 = 0;
   UShort screen2width = 0;
#endif

   overlay->srcOffsetX = overlay->srcOffsetY = 0;

   /* Determine whether we have two overlays or only one */
   set_hastwooverlays(pSiS, pPriv);

   pPriv->NoOverlay = FALSE;
#ifdef SISDUALHEAD
   if(pPriv->dualHeadMode) {
      if(!pPriv->hasTwoOverlays) {
	 if(pSiS->SecondHead) {
	    if(pSiSEnt->curxvcrtnum != 0) {
	       return 0;
	    }
	 } else {
	    if(pSiSEnt->curxvcrtnum != 1) {
	       return 0;
	    }
	 }
      }
   }
#endif

   /* Determine dispmode (MIRROR, SINGLEx) */
   set_dispmode(pScrn, pPriv);

   /* Check if overlay is supported with current display mode */
#ifdef SISMERGED
   if(!pSiS->MergedFB) {
#endif
      if( ((pPriv->displayMode & DISPMODE_MIRROR) &&
           ((pSiS->MiscFlags & (MISC_CRT1OVERLAY|MISC_CRT2OVERLAY)) != (MISC_CRT1OVERLAY|MISC_CRT2OVERLAY))) ||
	  ((pPriv->displayMode & DISPMODE_SINGLE1) &&
	   (!(pSiS->MiscFlags & MISC_CRT1OVERLAY))) ||
	  ((pPriv->displayMode & DISPMODE_SINGLE2) &&
	   (!(pSiS->MiscFlags & MISC_CRT2OVERLAY))) ) {
	 return 0;
      }
#ifdef SISMERGED
   }
#endif


   overlay->pitch = overlay->origPitch = pPriv->srcPitch;

#ifdef SISMERGED
   if(pSiS->MergedFB) {
      overlay->srcOffsetX2 = overlay->srcOffsetY2 = 0;
      overlay->DoFirst = TRUE;
      overlay->DoSecond = TRUE;
      overlay->pitch2 = overlay->origPitch;
      overlay->currentmode = ((SiSMergedDisplayModePtr)pSiS->CurrentLayout.mode->Private)->CRT1;
      overlay->currentmode2 = ((SiSMergedDisplayModePtr)pSiS->CurrentLayout.mode->Private)->CRT2;
      overlay->SCREENheight  = overlay->currentmode->VDisplay;
      overlay->SCREENheight2 = overlay->currentmode2->VDisplay;
      screenwidth = overlay->currentmode->HDisplay;
      screen2width = overlay->currentmode2->HDisplay;
      overlay->dstBox.x1  = pPriv->drw_x - pSiS->CRT1frameX0;
      overlay->dstBox.x2  = overlay->dstBox.x1 + pPriv->drw_w;
      overlay->dstBox.y1  = pPriv->drw_y - pSiS->CRT1frameY0;
      overlay->dstBox.y2  = overlay->dstBox.y1 + pPriv->drw_h;
      overlay->dstBox2.x1 = pPriv->drw_x - pSiS->CRT2pScrn->frameX0;
      overlay->dstBox2.x2 = overlay->dstBox2.x1 + pPriv->drw_w;
      overlay->dstBox2.y1 = pPriv->drw_y - pSiS->CRT2pScrn->frameY0;
      overlay->dstBox2.y2 = overlay->dstBox2.y1 + pPriv->drw_h;
   } else {
#endif
      overlay->currentmode = pSiS->CurrentLayout.mode;
      overlay->SCREENheight = overlay->currentmode->VDisplay;
      screenwidth = overlay->currentmode->HDisplay;
      overlay->dstBox.x1 = pPriv->drw_x - pScrn->frameX0;
      overlay->dstBox.x2 = pPriv->drw_x + pPriv->drw_w - pScrn->frameX0;
      overlay->dstBox.y1 = pPriv->drw_y - pScrn->frameY0;
      overlay->dstBox.y2 = pPriv->drw_y + pPriv->drw_h - pScrn->frameY0;
#ifdef SISMERGED
   }
#endif

   /* Note: x2/y2 is actually real coordinate + 1 */

   if((overlay->dstBox.x1 >= overlay->dstBox.x2) ||
      (overlay->dstBox.y1 >= overlay->dstBox.y2)) {
#ifdef SISMERGED
      if(pSiS->MergedFB) overlay->DoFirst = FALSE;
      else
#endif
           return 2;
   }

   if((overlay->dstBox.x2 <= 0) || (overlay->dstBox.y2 <= 0)) {
#ifdef SISMERGED
      if(pSiS->MergedFB) overlay->DoFirst = FALSE;
      else
#endif
           return 2;
   }

   if((overlay->dstBox.x1 >= screenwidth) || (overlay->dstBox.y1 >= overlay->SCREENheight)) {
#ifdef SISMERGED
      if(pSiS->MergedFB) overlay->DoFirst = FALSE;
      else
#endif
           return 2;
   }

   if(overlay->dstBox.x1 < 0) {
      overlay->srcOffsetX = pPriv->src_w * (-overlay->dstBox.x1) / pPriv->drw_w;
      overlay->dstBox.x1 = 0;
   }

   if(overlay->dstBox.y1 < 0) {
      overlay->srcOffsetY = pPriv->src_h * (-overlay->dstBox.y1) / pPriv->drw_h;
      overlay->dstBox.y1 = 0;
   }

   if((overlay->dstBox.x1 >= overlay->dstBox.x2 - 2) ||
      (overlay->dstBox.x1 >= screenwidth - 2)        ||
      (overlay->dstBox.y1 >= overlay->dstBox.y2)) {
#ifdef SISMERGED
      if(pSiS->MergedFB) overlay->DoFirst = FALSE;
      else
#endif
           return 2;
   }

#ifdef SISMERGED
   if(pSiS->MergedFB) {

      /* Check if dotclock is within limits for CRT1 */
      if(overlay->DoFirst) {
         if(pPriv->displayMode & (DISPMODE_SINGLE1 | DISPMODE_MIRROR)) {
            if(!(pSiS->MiscFlags & MISC_CRT1OVERLAY)) {
               notavail1 = 1;
            }
         }
      }

      /* Now for CRT2 */

      if((overlay->dstBox2.x2 <= 0) || (overlay->dstBox2.y2 <= 0))
	 overlay->DoSecond = FALSE;

      if((overlay->dstBox2.x1 >= screen2width) || (overlay->dstBox2.y1 >= overlay->SCREENheight2))
	 overlay->DoSecond = FALSE;

      if(overlay->dstBox2.x1 < 0) {
	 overlay->srcOffsetX2 = pPriv->src_w * (-overlay->dstBox2.x1) / pPriv->drw_w;
	 overlay->dstBox2.x1 = 0;
      }

      if(overlay->dstBox2.y1 < 0) {
	 overlay->srcOffsetY2 = pPriv->src_h * (-overlay->dstBox2.y1) / pPriv->drw_h;
	 overlay->dstBox2.y1 = 0;
      }

      if((overlay->dstBox2.x1 >= overlay->dstBox2.x2 - 2) ||
	 (overlay->dstBox2.x1 >= screen2width - 2)        ||
	 (overlay->dstBox2.y1 >= overlay->dstBox2.y2))
	 overlay->DoSecond = FALSE;

      /* Check if dotclock is within limits for CRT2 */
      if(overlay->DoSecond) {
         if(pPriv->displayMode & (DISPMODE_SINGLE2 | DISPMODE_MIRROR)) {
	    if(!(pSiS->MiscFlags & MISC_CRT2OVERLAY)) {
	       notavail2 = 1;
	    }
	    /* CRT2 is never interlace */
         }
      }

      if((!overlay->DoFirst) && (!overlay->DoSecond)) {
         result = 2;
      } else if((overlay->DoFirst  && notavail1) ||
                (overlay->DoSecond && notavail2)) {
         overlay->DoFirst = overlay->DoSecond = FALSE;
         result = 0;
      }

      /* If neither overlay is to be displayed, disable them if they are currently enabled */
      if((!overlay->DoFirst) && (!overlay->DoSecond)) {
	 setvideoregmask(pSiS, Index_VI_Control_Misc2, 0x00, 0x05);
	 setvideoregmask(pSiS, Index_VI_Control_Misc1, 0x00, 0x01);
	 disableoverlay(pSiS, pPriv, 0);
	 if(pPriv->hasTwoOverlays) {
	    setvideoregmask(pSiS, Index_VI_Control_Misc2, 0x01, 0x01);
	    setvideoregmask(pSiS, Index_VI_Control_Misc1, 0x00, 0x01);
	    disableoverlay(pSiS, pPriv, 1);
	 }
	 pPriv->overlayStatus = FALSE;
      }
   }
#endif

   return result;

 }

/*********************************
 *         DisplayVideo()        *
 *********************************/

static void
SISDisplayVideo(ScrnInfoPtr pScrn, SISPortPrivPtr pPriv, SISOverlayPtr overlay)
{
   SISPtr pSiS = SISPTR(pScrn);
   short  srcPitch = pPriv->srcPitch;
   short  height = pPriv->height;
   int    sx = 0, sy = 0, watchdog;
   int    index = 0, iscrt2 = 0;
#ifdef SISMERGED
   int    sx2 = 0, sy2 = 0;
#endif

   overlay->pixelFormat = pPriv->id;

   if(pPriv->usechromakey) {
      overlay->keyOP = (pPriv->insidechromakey) ? VI_ROP_ChromaKey : VI_ROP_NotChromaKey;
   } else {
      overlay->keyOP = VI_ROP_DestKey;
   }

      overlay->bobEnable = 0x00;    /* Disable BOB de-interlacer */

   switch(pPriv->id) {

     case PIXEL_FMT_YV12:
       overlay->planar = 1;
       overlay->planar_shiftpitch = 1;
#ifdef SISMERGED
       if((!pSiS->MergedFB) || (overlay->DoFirst)) {
#endif
          sx = (pPriv->src_x + overlay->srcOffsetX) & ~7;
          sy = (pPriv->src_y + overlay->srcOffsetY) & ~1;
          overlay->PSY = pPriv->bufAddr[pPriv->currentBuf] + sx + sy*srcPitch;
          overlay->PSV = pPriv->bufAddr[pPriv->currentBuf] + height*srcPitch + ((sx + sy*srcPitch/2) >> 1);
          overlay->PSU = pPriv->bufAddr[pPriv->currentBuf] + height*srcPitch*5/4 + ((sx + sy*srcPitch/2) >> 1);
          overlay->PSY += FBOFFSET;
          overlay->PSV += FBOFFSET;
          overlay->PSU += FBOFFSET;
          overlay->PSY >>= pPriv->shiftValue;
          overlay->PSV >>= pPriv->shiftValue;
          overlay->PSU >>= pPriv->shiftValue;
#ifdef SISMERGED
       }
       if((pSiS->MergedFB) && (overlay->DoSecond)) {
          sx2 = (pPriv->src_x + overlay->srcOffsetX2) & ~7;
          sy2 = (pPriv->src_y + overlay->srcOffsetY2) & ~1;
          overlay->PSY2 = pPriv->bufAddr[pPriv->currentBuf] + sx2 + sy2*srcPitch;
          overlay->PSV2 = pPriv->bufAddr[pPriv->currentBuf] + height*srcPitch + ((sx2 + sy2*srcPitch/2) >> 1);
          overlay->PSU2 = pPriv->bufAddr[pPriv->currentBuf] + height*srcPitch*5/4 + ((sx2 + sy2*srcPitch/2) >> 1);
          overlay->PSY2 += FBOFFSET;
          overlay->PSV2 += FBOFFSET;
          overlay->PSU2 += FBOFFSET;
          overlay->PSY2 >>= pPriv->shiftValue;
          overlay->PSV2 >>= pPriv->shiftValue;
          overlay->PSU2 >>= pPriv->shiftValue;
       }
#endif
       break;

     case PIXEL_FMT_I420:
       overlay->planar = 1;
       overlay->planar_shiftpitch = 1;
#ifdef SISMERGED
       if((!pSiS->MergedFB) || (overlay->DoFirst)) {
#endif
          sx = (pPriv->src_x + overlay->srcOffsetX) & ~7;
          sy = (pPriv->src_y + overlay->srcOffsetY) & ~1;
          overlay->PSY = pPriv->bufAddr[pPriv->currentBuf] + sx + sy*srcPitch;
          overlay->PSV = pPriv->bufAddr[pPriv->currentBuf] + height*srcPitch*5/4 + ((sx + sy*srcPitch/2) >> 1);
          overlay->PSU = pPriv->bufAddr[pPriv->currentBuf] + height*srcPitch + ((sx + sy*srcPitch/2) >> 1);
          overlay->PSY += FBOFFSET;
          overlay->PSV += FBOFFSET;
          overlay->PSU += FBOFFSET;
          overlay->PSY >>= pPriv->shiftValue;
          overlay->PSV >>= pPriv->shiftValue;
          overlay->PSU >>= pPriv->shiftValue;
#ifdef SISMERGED
       }
       if((pSiS->MergedFB) && (overlay->DoSecond)) {
          sx2 = (pPriv->src_x + overlay->srcOffsetX2) & ~7;
          sy2 = (pPriv->src_y + overlay->srcOffsetY2) & ~1;
          overlay->PSY2 = pPriv->bufAddr[pPriv->currentBuf] + sx2 + sy2*srcPitch;
          overlay->PSV2 = pPriv->bufAddr[pPriv->currentBuf] + height*srcPitch*5/4 + ((sx2 + sy2*srcPitch/2) >> 1);
          overlay->PSU2 = pPriv->bufAddr[pPriv->currentBuf] + height*srcPitch + ((sx2 + sy2*srcPitch/2) >> 1);
          overlay->PSY2 += FBOFFSET;
          overlay->PSV2 += FBOFFSET;
          overlay->PSU2 += FBOFFSET;
          overlay->PSY2 >>= pPriv->shiftValue;
          overlay->PSV2 >>= pPriv->shiftValue;
          overlay->PSU2 >>= pPriv->shiftValue;
       }
#endif
       break;

     case PIXEL_FMT_NV12:
     case PIXEL_FMT_NV21:
       overlay->planar = 1;
       overlay->planar_shiftpitch = 0;
#ifdef SISMERGED
       if((!pSiS->MergedFB) || (overlay->DoFirst)) {
#endif
          sx = (pPriv->src_x + overlay->srcOffsetX) & ~7;
          sy = (pPriv->src_y + overlay->srcOffsetY) & ~1;
          overlay->PSY = pPriv->bufAddr[pPriv->currentBuf] + sx + sy*srcPitch;
          overlay->PSV =	pPriv->bufAddr[pPriv->currentBuf] + height*srcPitch + ((sx + sy*srcPitch/2) >> 1);
          overlay->PSY += FBOFFSET;
          overlay->PSV += FBOFFSET;
          overlay->PSY >>= pPriv->shiftValue;
          overlay->PSV >>= pPriv->shiftValue;
          overlay->PSU = overlay->PSV;
#ifdef SISMERGED
       }
       if((pSiS->MergedFB) && (overlay->DoSecond)) {
          sx2 = (pPriv->src_x + overlay->srcOffsetX2) & ~7;
          sy2 = (pPriv->src_y + overlay->srcOffsetY2) & ~1;
          overlay->PSY2 = pPriv->bufAddr[pPriv->currentBuf] + sx2 + sy2*srcPitch;
          overlay->PSV2 = pPriv->bufAddr[pPriv->currentBuf] + height*srcPitch + ((sx2 + sy2*srcPitch/2) >> 1);
          overlay->PSY2 += FBOFFSET;
          overlay->PSV2 += FBOFFSET;
          overlay->PSY2 >>= pPriv->shiftValue;
          overlay->PSV2 >>= pPriv->shiftValue;
          overlay->PSU2 = overlay->PSV2;
       }
#endif
       break;

     case PIXEL_FMT_YUY2:
     case PIXEL_FMT_UYVY:
     case PIXEL_FMT_YVYU:
     case PIXEL_FMT_RGB6:
     case PIXEL_FMT_RGB5:
     default:
       overlay->planar = 0;
#ifdef SISMERGED
       if((!pSiS->MergedFB) || (overlay->DoFirst)) {
#endif
          sx = (pPriv->src_x + overlay->srcOffsetX) & ~1;
          sy = (pPriv->src_y + overlay->srcOffsetY);
          overlay->PSY = (pPriv->bufAddr[pPriv->currentBuf] + sx*2 + sy*srcPitch);
          overlay->PSY += FBOFFSET;
          overlay->PSY >>= pPriv->shiftValue;
#ifdef SISMERGED
       }
       if((pSiS->MergedFB) && (overlay->DoSecond)) {
          sx2 = (pPriv->src_x + overlay->srcOffsetX2) & ~1;
          sy2 = (pPriv->src_y + overlay->srcOffsetY2);
          overlay->PSY2 = (pPriv->bufAddr[pPriv->currentBuf] + sx2*2 + sy2*srcPitch);
          overlay->PSY2 += FBOFFSET;
          overlay->PSY2 >>= pPriv->shiftValue;
       }
#endif
       break;
   }

   /* Some clipping checks */
#ifdef SISMERGED
   if((!pSiS->MergedFB) || (overlay->DoFirst)) {
#endif
      overlay->srcW = pPriv->src_w - (sx - pPriv->src_x);
      overlay->srcH = pPriv->src_h - (sy - pPriv->src_y);
      if( (pPriv->oldx1 != overlay->dstBox.x1) ||
	  (pPriv->oldx2 != overlay->dstBox.x2) ||
	  (pPriv->oldy1 != overlay->dstBox.y1) ||
	  (pPriv->oldy2 != overlay->dstBox.y2) ) {
	 pPriv->mustwait = 1;
	 pPriv->oldx1 = overlay->dstBox.x1; pPriv->oldx2 = overlay->dstBox.x2;
	 pPriv->oldy1 = overlay->dstBox.y1; pPriv->oldy2 = overlay->dstBox.y2;
      }
#ifdef SISMERGED
   }
   if((pSiS->MergedFB) && (overlay->DoSecond)) {
      overlay->srcW2 = pPriv->src_w - (sx2 - pPriv->src_x);
      overlay->srcH2 = pPriv->src_h - (sy2 - pPriv->src_y);
      if( (pPriv->oldx1_2 != overlay->dstBox2.x1) ||
	  (pPriv->oldx2_2 != overlay->dstBox2.x2) ||
	  (pPriv->oldy1_2 != overlay->dstBox2.y1) ||
	  (pPriv->oldy2_2 != overlay->dstBox2.y2) ) {
	 pPriv->mustwait = 1;
	 pPriv->oldx1_2 = overlay->dstBox2.x1; pPriv->oldx2_2 = overlay->dstBox2.x2;
	 pPriv->oldy1_2 = overlay->dstBox2.y1; pPriv->oldy2_2 = overlay->dstBox2.y2;
      }
   }
#endif

#ifdef SISMERGED
   /* Disable an overlay if it is not to be displayed (but enabled currently) */
   if((pSiS->MergedFB) && (pPriv->hasTwoOverlays)) {
      /* Disable overlay 0 only on 300 series; on 315 series ov 0
       * is used for CRT2. (If it's not, both overlays have been
       * already been disabled in CheckOverlay.)
       */
      if(!overlay->DoFirst && (pSiS->VGAEngine == SIS_300_VGA)) {
	 setvideoregmask(pSiS, Index_VI_Control_Misc2, 0x00, 0x05);
	 setvideoregmask(pSiS, Index_VI_Control_Misc1, 0x00, 0x01);
	 disableoverlay(pSiS, pPriv, 0);
      }
      /* Disable overlay 1 if it's unused. This is the case on 300
       * series if it's really unused, or on 315 series, if overlay
       * 0 takes over (because we only need one).
       */
      if(!overlay->DoSecond ||
	 ((pSiS->VGAEngine == SIS_315_VGA) && !overlay->DoFirst)) {
	 setvideoregmask(pSiS, Index_VI_Control_Misc2, 0x01, 0x01);
	 setvideoregmask(pSiS, Index_VI_Control_Misc1, 0x00, 0x01);
	 disableoverlay(pSiS, pPriv, 1);
      }
   }
#endif

   /* ********* Loop head ********* */
   /* Note: index can only be 1 for CRT2, ie overlay 1
    * is only used for CRT2.
    */
   if(pPriv->displayMode & DISPMODE_SINGLE2) {
      if(pPriv->hasTwoOverlays) {			/* We have 2 overlays: */
	 if(pPriv->dualHeadMode) {
	    /* Dual head: Use overlay 1 for CRT2 */
	    index = 1; iscrt2 = 1;
	 } else {
	    /* Single head: Use overlay 0 for CRT2 */
	    index = 0; iscrt2 = 1;
	 }
      } else {						/* We have 1 overlay */
	 /* Use that only overlay for CRT2 */
	 index = 0; iscrt2 = 1;
      }
      overlay->VBlankActiveFunc = vblank_active_CRT2;
#ifdef SISMERGED
      if(!pPriv->hasTwoOverlays) {
	 if((pSiS->MergedFB) && (!overlay->DoSecond)) {
	    index = 0; iscrt2 = 0;
	    overlay->VBlankActiveFunc = vblank_active_CRT1;
	    pPriv->displayMode = DISPMODE_SINGLE1;
	 }
      }
#endif
   } else {
      index = 0; iscrt2 = 0;
      overlay->VBlankActiveFunc = vblank_active_CRT1;
#ifdef SISMERGED
      if((pSiS->MergedFB) && (!overlay->DoFirst)) {
         /* 300 series all have 2 overlays, so we use ov 1 for CRT2.
          * On 315, we use ov 0 even on dual-overlay-chips for
          * better scaling if ov 0 is available.
          */
	 if(pSiS->VGAEngine == SIS_300_VGA) index = 1;
	 iscrt2 = 1;
	 overlay->VBlankActiveFunc = vblank_active_CRT2;
	 if(!pPriv->hasTwoOverlays) {
	    pPriv->displayMode = DISPMODE_SINGLE2;
	 }
      }
#endif
   }

   /* set display mode SR06,32 (CRT1, CRT2 or mirror) */
   set_disptype_regs(pScrn, pPriv,
#ifdef SISMERGED
		overlay->DoFirst, overlay->DoSecond
#else
		TRUE, TRUE
#endif
		);

   /* set (not only calc) merge line buffer */
#ifdef SISMERGED
   if(!pSiS->MergedFB) {
#endif
      merge_line_buf(pSiS, pPriv, (overlay->srcW > pPriv->linebufMergeLimit), overlay->srcW,
      		     pPriv->linebufMergeLimit);
#ifdef SISMERGED
   } else {
      Bool temp1 = FALSE, temp2 = FALSE;
      if(overlay->DoFirst) {
         if(overlay->srcW > pPriv->linebufMergeLimit)  temp1 = TRUE;
      }
      if(overlay->DoSecond) {
         if(overlay->srcW2 > pPriv->linebufMergeLimit) temp2 = TRUE;
      }
      merge_line_buf_mfb(pSiS, pPriv, temp1, temp2,
			 overlay->DoFirst, overlay->DoSecond,
			 overlay->srcW, overlay->srcW2,
			 pPriv->linebufMergeLimit);
   }
#endif

   /* calculate (not set!) line buffer length */
#ifdef SISMERGED
   if((!pSiS->MergedFB) || (overlay->DoFirst))
#endif
      calc_line_buf_size_1(overlay, pPriv);

#ifdef SISMERGED
   if((pSiS->MergedFB) && (overlay->DoSecond))
      calc_line_buf_size_2(overlay, pPriv);
#endif

   if(pPriv->dualHeadMode) {
#ifdef SISDUALHEAD
      if(!pSiS->SecondHead) {
         if(pPriv->updatetvxpos) {
            SiS_SetTVxposoffset(pScrn, pPriv->tvxpos);
            pPriv->updatetvxpos = FALSE;
         }
         if(pPriv->updatetvypos) {
            SiS_SetTVyposoffset(pScrn, pPriv->tvypos);
            pPriv->updatetvypos = FALSE;
         }
      }
#endif
   } else {
      if(pPriv->updatetvxpos) {
         SiS_SetTVxposoffset(pScrn, pPriv->tvxpos);
         pPriv->updatetvxpos = FALSE;
      }
      if(pPriv->updatetvypos) {
         SiS_SetTVyposoffset(pScrn, pPriv->tvypos);
         pPriv->updatetvypos = FALSE;
      }
   }

#if 0 /* Clearing this does not seem to be required */
      /* and might even be dangerous. */
   if(pSiS->VGAEngine == SIS_315_VGA) {
      watchdog = WATCHDOG_DELAY;
      while(overlay->VBlankActiveFunc(pSiS, pPriv) && --watchdog);
      setvideoregmask(pSiS, Index_VI_Control_Misc3, 0x00, 0x03);
   }
#endif
	if(pPriv->is670)		setvideoregmask(pSiS, Index_VI_Control_Misc3, 0x00, 0x01);
	else		setvideoregmask(pSiS, Index_VI_Control_Misc3, 0x03, 0x03);

   /* Do the following in a loop for CRT1 and CRT2 ----------------- */
MIRROR:

   /* calculate scale factor */
#ifdef SISMERGED
   if(pSiS->MergedFB && iscrt2)
      calc_scale_factor_2(overlay, pScrn, pPriv);
   else
#endif
      calc_scale_factor(overlay, pScrn, pPriv, iscrt2);

   /* Select overlay 0 (used for CRT1 or CRT2) or overlay 1 (used for CRT2 only) */
   setvideoregmask(pSiS, Index_VI_Control_Misc2, index, 0x01);

   /* set format (before color and chroma keys) */
   set_format(pSiS, overlay);

   /* set color key */
   set_colorkey(pSiS, pPriv->colorKey);

   if(pPriv->usechromakey) {
      /* Select chroma key format (300 series only) */
      if(pSiS->VGAEngine == SIS_300_VGA) {
	 setvideoregmask(pSiS, Index_VI_Control_Misc0,
	                 (pPriv->yuvchromakey ? 0x40 : 0x00), 0x40);
      }
      set_chromakey(pSiS, pPriv->chromamin, pPriv->chromamax);
   }

   /* set brightness, contrast, hue, saturation */
   set_brightness(pSiS, pPriv->brightness);
   set_contrast(pSiS, pPriv->contrast);
   if(pSiS->VGAEngine == SIS_315_VGA) {
      set_hue(pSiS, pPriv->hue);
      set_saturation(pSiS, pPriv->saturation);
   }

   /* enable/disable graphics display around overlay
    * (Since disabled overlays don't get treated in this
    * loop, we omit respective checks here)
    */
   if(!iscrt2) {
     set_disablegfx(pSiS, pPriv->disablegfx, overlay);
   } else if(!pSiS->hasTwoOverlays) {
     set_disablegfx(pSiS, FALSE, overlay);
   }
   set_disablegfxlr(pSiS, pPriv->disablegfxlr, overlay);


   /* set remaining overlay parameters */
   set_overlay(pSiS, overlay, pPriv, index, iscrt2);

   /* enable overlay */
   setvideoregmask(pSiS, Index_VI_Control_Misc0, 0x02, 0x02);

   /* loop foot */
   if((pPriv->displayMode & DISPMODE_MIRROR) &&
      (index == 0)			     &&
      pPriv->hasTwoOverlays) {
#ifdef SISMERGED
      if((!pSiS->MergedFB) || (overlay->DoSecond && overlay->DoFirst)) {
#endif
	 index = 1; iscrt2 = 1;
	 overlay->VBlankActiveFunc = vblank_active_CRT2;
	 goto MIRROR;
#ifdef SISMERGED
      }
#endif
   }

   /* Now for the trigger: This is a bad hack to work-around
    * an obvious hardware bug: Overlay 1 (which is ONLY used
    * for CRT2 in this driver) does not always update its
    * window position and some other stuff. Earlier, this was
    * solved be disabling the overlay, but this took forever
    * and was ugly on the screen.
    * Now: We write 0x03 to 0x74 from the beginning. This is
    * meant as a "lock" - the driver is supposed to write 0
    * to this register, bit 0 for overlay 0, bit 1 for over-
    * lay 1, then change buffer addresses, pitches, window
    * position, scaler registers, format, etc., then write
    * 1 to 0x74. The hardware then reads the registers into
    * its internal engine and clears these bits.
    * All this works for overlay 0, but not 1. Overlay 1
    * has assumingly the following restrictions:
    * - New data written to the registers is only read
    *   correctly by the engine if the registers are written
    *   when the current scanline is beyond the current
    *   overlay position and below the maximum visible
    *   scanline (vertical screen resolution)
    * - If a vertical retrace occures during writing the
    *   registers, the registers written BEFORE this re-
    *   trace happened, are not being read into the
    *   engine if the trigger is set after the retrace.
    * Therefore: We write the overlay registers above in
    * set_overlay only if the scanline matches, and save
    * the then current scanline. If this scanline is higher
    * than the now current scanline, we assume a retrace,
    * wait for the scanline to match the criteria above again,
    * and rewrite all relevant registers.
    * I have no idea if this is meant that way, but after
    * fiddling three entire days with this crap, I found this
    * to be the only solution.
    */
   if(pSiS->VGAEngine == SIS_315_VGA) {
      if((pPriv->mustwait) && index) {
	 watchdog = get_scanline_CRT2(pSiS, pPriv);
	 if(watchdog <= overlay->oldLine) {
	    int i, mytop = overlay->oldtop;
	    int screenHeight = overlay->SCREENheight;
#ifdef SISMERGED
	    if(pSiS->MergedFB) {
	       screenHeight = overlay->SCREENheight2;
	    }
#endif
	    if(mytop < screenHeight - 2) {
	       do {
	          watchdog = get_scanline_CRT2(pSiS, pPriv);
	       } while((watchdog <= mytop) || (watchdog >= screenHeight));
	    }
	    for(i=0x02; i<=0x12; i++) {
	       setvideoreg(pSiS, i, getvideoreg(pSiS, i));
	    }
	    for(i=0x18; i<=0x1c; i++) {
	       setvideoreg(pSiS, i, getvideoreg(pSiS, i));
	    }
	    for(i=0x2c; i<=0x2e; i++) {
	       setvideoreg(pSiS, i, getvideoreg(pSiS, i));
	    }
	    for(i=0x6b; i<=0x6f; i++) {
	       setvideoreg(pSiS, i, getvideoreg(pSiS, i));
	    }
	 }
      }
      /* Trigger register copy for 315/330 series */
	  if(pPriv->is670)		setvideoregmask(pSiS, Index_VI_Control_Misc3, 0x01, 0x01);
	  else	setvideoregmask(pSiS, Index_VI_Control_Misc3, 0x03, 0x03);
   }

   pPriv->mustwait = 0;
   pPriv->overlayStatus = TRUE;
}

/*********************************
 *        Memory management      *
 *********************************/

#ifdef SIS_USE_EXA
static void
SiSDestroyArea(ScreenPtr pScreen, ExaOffscreenArea *area)
{
   void **handle = (void *)area->privData;
   *handle = NULL;
}
#endif

unsigned int
SISAllocateFBMemory(
  ScrnInfoPtr pScrn,
  void **handle,
  int bytesize
  ){
   SISPtr pSiS = SISPTR(pScrn);
   ScreenPtr pScreen = screenInfo.screens[pScrn->scrnIndex];

#ifdef SIS_USE_XAA
   if(!pSiS->useEXA) {
      FBLinearPtr linear = (FBLinearPtr)(*handle);
      FBLinearPtr new_linear;
      int depth = pSiS->CurrentLayout.bitsPerPixel >> 3;
      int size = ((bytesize + depth - 1) / depth);

      if(linear) {
         if(linear->size >= size) {
	    return (unsigned int)(linear->offset * depth);
	 }

         if(xf86ResizeOffscreenLinear(linear, size)) {
	    return (unsigned int)(linear->offset * depth);
	 }

         xf86FreeOffscreenLinear(linear);
	 *handle = NULL;
      }

      new_linear = xf86AllocateOffscreenLinear(pScreen, size, 8, NULL, NULL, NULL);

      if(!new_linear) {
         int max_size;

         xf86QueryLargestOffscreenLinear(pScreen, &max_size, 8, PRIORITY_EXTREME);

         if(max_size < size) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	           "Failed to allocate %d pixels of linear video memory\n", size);
	    return 0;
	 }

         xf86PurgeUnlockedOffscreenAreas(pScreen);
         new_linear = xf86AllocateOffscreenLinear(pScreen, size, 8, NULL, NULL, NULL);
      }
      if(!new_linear) {
         xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	           "Failed to allocate %d pixels of linear video memory\n", size);
	 return 0;
      } else {
         *handle = (void *)new_linear;
	 return (unsigned int)(new_linear->offset * depth);
      }
   }
#endif
#ifdef SIS_USE_EXA
   if(pSiS->useEXA && !pSiS->NoAccel) {
      ExaOffscreenArea *area = (ExaOffscreenArea *)(*handle);

      if(area) {
	 if(area->size >= bytesize)
	    return (unsigned int)(area->offset);

	 exaOffscreenFree(pScreen, area);
	 *handle = NULL;
      }

      if(!(area = exaOffscreenAlloc(pScreen, bytesize, 8, TRUE, SiSDestroyArea, (pointer)handle))) {
	 xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	           "Failed to allocate %d bytes of video memory\n", bytesize);
	 return 0;
      } else {
	 *handle = (void *)area;
	 return (unsigned int)(area->offset);
      }
   }
#endif

   return 0;
}

void
SISFreeFBMemory(ScrnInfoPtr pScrn, void **handle)
{
    SISPtr pSiS = SISPTR(pScrn);
#ifdef SIS_USE_EXA
    ScreenPtr pScreen = screenInfo.screens[pScrn->scrnIndex];
#endif

#ifdef SIS_USE_XAA
    if(!pSiS->useEXA) {
       if(*handle) {
          xf86FreeOffscreenLinear((FBLinearPtr)(*handle));
       }
    }
#endif
#ifdef SIS_USE_EXA
   if(pSiS->useEXA && !pSiS->NoAccel) {
      if(*handle) {
         exaOffscreenFree(pScreen, (ExaOffscreenArea *)(*handle));
      }
   }
#endif
   *handle = NULL;
}

/*********************************
 *           StopVideo()         *
 *********************************/

static void
SISStopVideo(ScrnInfoPtr pScrn, pointer data, Bool shutdown)
{
  SISPortPrivPtr pPriv = (SISPortPrivPtr)data;
  SISPtr pSiS = SISPTR(pScrn);

#ifdef XVDEBUG
	xf86DrvMsg(0, X_INFO, "[Xv] StopVideo is called.\n");
#endif

  if(pPriv->grabbedByV4L) return;

  SISSetPortDefaults(pScrn, pPriv);

  REGION_EMPTY(pScrn->pScreen, &pPriv->clip);

  if(shutdown) {
     if(pPriv->videoStatus & CLIENT_VIDEO_ON) {
        close_overlay(pSiS, pPriv);
        pPriv->mustwait = 1;
     }
     SISFreeFBMemory(pScrn, &pPriv->handle);
     pPriv->videoStatus = 0;
  } else {
     if(pPriv->videoStatus & CLIENT_VIDEO_ON) {
        UpdateCurrentTime();
        pPriv->offTime = currentTime.milliseconds + OFF_DELAY;
        pPriv->videoStatus |= OFF_TIMER;
        pSiS->VideoTimerCallback = SISVideoTimerCallback;
     }
  }
}

/*********************************
 *        PutImage() etc         *
 *********************************/

static void
SiSHandleClipListColorkey(ScrnInfoPtr pScrn, SISPortPrivPtr pPriv, RegionPtr clipBoxes)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SIS_USE_XAA
   XAAInfoRecPtr pXAA = pSiS->AccelInfoPtr;
   int depth = pSiS->CurrentLayout.bitsPerPixel >> 3;
   int myreds[] = { 0x000000ff, 0x0000f800, 0, 0x00ff0000 };
#endif

   if( pPriv->forceColorkey ||
       (pPriv->autopaintColorKey &&
         ( pPriv->grabbedByV4L ||
#if XF86_VERSION_CURRENT < XF86_VERSION_NUMERIC(4,3,99,3,0)
           (!RegionsEqual(&pPriv->clip, clipBoxes)) ||
#else
           (!REGION_EQUAL(pScrn->pScreen, &pPriv->clip, clipBoxes)) ||
#endif
           (pPriv->PrevOverlay != pPriv->NoOverlay))) ) {

      /* We always paint the colorkey for V4L */
      if(!pPriv->grabbedByV4L) {
	 REGION_COPY(pScrn->pScreen, &pPriv->clip, clipBoxes);
      }

      /* draw these */
      pPriv->PrevOverlay = pPriv->NoOverlay;
      pPriv->forceColorkey = 0;
#ifdef SIS_USE_XAA
      if((pPriv->NoOverlay) && pXAA && pXAA->FillMono8x8PatternRects) {
         (*pXAA->FillMono8x8PatternRects)(pScrn, myreds[depth-1],
			0x000000, GXcopy, ~0,
			REGION_NUM_RECTS(clipBoxes),
			REGION_RECTS(clipBoxes),
			0x00422418, 0x18244200, 0, 0);
      } else {
#endif
         if(!pSiS->disablecolorkeycurrent) {
#if XF86_VERSION_CURRENT < XF86_VERSION_NUMERIC(4,1,99,1,0)
            (*pXAA->FillSolidRects)(pScrn, pPriv->colorKey, GXcopy, ~0,
                           REGION_NUM_RECTS(clipBoxes),
                           REGION_RECTS(clipBoxes));
#else
	    xf86XVFillKeyHelper(pScrn->pScreen, (pPriv->NoOverlay) ? 0x00ff0000 : pPriv->colorKey, clipBoxes);
#endif
	 }
#ifdef SIS_USE_XAA
      }
#endif

   }
}

static int
SISPutImage(
  ScrnInfoPtr pScrn,
  short src_x, short src_y,
  short drw_x, short drw_y,
  short src_w, short src_h,
  short drw_w, short drw_h,
  int id, UChar *buf,
  short width, short height,
  Bool sync,
  RegionPtr clipBoxes, pointer data, DrawablePtr pDraw
){
   SISPtr pSiS = SISPTR(pScrn);
   SISPortPrivPtr pPriv = (SISPortPrivPtr)data;
   SISOverlayRec overlay;
   int totalSize = 0, result;
   Bool dorest = TRUE;

   if(pPriv->grabbedByV4L) return Success;

   memset(&overlay, 0, sizeof(overlay));

   pPriv->drw_x = drw_x;
   pPriv->drw_y = drw_y;
   pPriv->drw_w = drw_w;
   pPriv->drw_h = drw_h;
   pPriv->src_x = src_x;
   pPriv->src_y = src_y;
   pPriv->src_w = src_w;
   pPriv->src_h = src_h;
   pPriv->id = id;
   pPriv->height = height;

   pPriv->isRGB = FALSE;

   pPriv->currentBuf = pPriv->nextBuf;


#ifdef XVDEBUG
	xf86DrvMsg(0, X_INFO, "[Xv] PutImage is called: id = %d\n", id);
#endif
 

   /* Pixel formats:
      1. YU12:  3 planes:       H    V
               Y sample period  1    1   (8 bit per pixel)
	       V sample period  2    2	 (8 bit per pixel, subsampled)
	       U sample period  2    2   (8 bit per pixel, subsampled)

	 Y plane is fully sampled (width*height), U and V planes
	 are sampled in 2x2 blocks, hence a group of 4 pixels requires
	 4 + 1 + 1 = 6 bytes. The data is planar, ie in single planes
	 for Y, U and V.
      2. UYVY: 3 planes:        H    V
	       Y sample period  1    1   (8 bit per pixel)
	       V sample period  2    1	 (8 bit per pixel, subsampled)
	       U sample period  2    1   (8 bit per pixel, subsampled)
	 Y plane is fully sampled (width*height), U and V planes
	 are sampled in 2x1 blocks, hence a group of 4 pixels requires
	 4 + 2 + 2 = 8 bytes. The data is bit packed, there are no separate
	 Y, U or V planes.
	 Bit order:  U0 Y0 V0 Y1  U2 Y2 V2 Y3 ...
      3. I420: Like YU12, but planes U and V are in reverse order.
      4. YUY2: Like UYVY, but order is
		     Y0 U0 Y1 V0  Y2 U2 Y3 V2 ...
      5. YVYU: Like YUY2, but order is
		     Y0 V0 Y1 U0  Y2 V2 Y3 U2 ...
      6. NV12, NV21: 2 planes   H    V
	       Y sample period  1    1   (8 bit per pixel)
	       V sample period  2    1	 (8 bit per pixel, subsampled)
	       U sample period  2    1   (8 bit per pixel, subsampled)
	 Y plane is fully samples (width*height), U and V planes are
	 interleaved in memory (one byte U, one byte V for NV12, NV21
	 other way round) and sampled in 2x1 blocks. Otherwise such
	 as all other planar formats.
   */

   switch(id) {
     case PIXEL_FMT_YV12:
     case PIXEL_FMT_I420:
     case PIXEL_FMT_NV12:
     case PIXEL_FMT_NV21:
       pPriv->srcPitch = (width + pPriv->PitchAlignmentMask) & ~(pPriv->PitchAlignmentMask);
       /* Size = width * height * 3 / 2 */
       totalSize = (pPriv->srcPitch * height * 3) >> 1; /* Verified */
       break;
     case PIXEL_FMT_RGB6:
     case PIXEL_FMT_RGB5:
       pPriv->isRGB = TRUE;
       /* fall through */
     case PIXEL_FMT_YUY2:
     case PIXEL_FMT_UYVY:
     case PIXEL_FMT_YVYU:
     default:
       pPriv->srcPitch = ((width << 1) + 15) & ~15;	/* Verified */
       /* Size = width * 2 * height */
       totalSize = pPriv->srcPitch * height;
   }

   result = SISCheckOverlay(pScrn, pPriv, &overlay);

   /* returns: 0 = overlay not available, 1 = ok,
    *          2 = overlay wouldn't be drawn (outside visible screen)
    */

   if(result != 1) {
      if(pPriv->overlayStatus) {
	 close_overlay(pSiS, pPriv);
      }
      pPriv->NoOverlay = TRUE;
      dorest = FALSE;
   }


   pSiS->nocolorkey = FALSE;

   if(dorest) {

      /* make it a multiple of 16 to simplify to copy loop */
      totalSize += 15;
      totalSize &= ~15; /* in bytes */

      /* allocate memory (we do doublebuffering) - size is in bytes */
      if(!(pPriv->bufAddr[0] = SISAllocateFBMemory(pScrn, &pPriv->handle, totalSize << 1)))
         return BadAlloc;

         pPriv->bufAddr[1] = pPriv->bufAddr[0] + totalSize;

         /* copy data */
         SiSMemCopyToVideoRam(pSiS, pSiS->FbBase + pPriv->bufAddr[pPriv->currentBuf], buf, totalSize);


      SISDisplayVideo(pScrn, pPriv, &overlay);

   }

   /* update cliplist */
   SiSHandleClipListColorkey(pScrn, pPriv, clipBoxes);

   if(dorest) pPriv->nextBuf ^= 1;

   pPriv->videoStatus = CLIENT_VIDEO_ON;

   pSiS->VideoTimerCallback = SISVideoTimerCallback;

   return Success;
}

static int
SISReputImage(
  ScrnInfoPtr pScrn,
  short src_x, short src_y,
  short drw_x, short drw_y,
  short src_w, short src_h,
  short drw_w, short drw_h,
  RegionPtr clipBoxes, pointer data, DrawablePtr pDraw
){
   SISPtr pSiS = SISPTR(pScrn);
   SISPortPrivPtr pPriv = (SISPortPrivPtr)data;
   SISOverlayRec overlay;
   int result;

   /* ? */
   if(pPriv->grabbedByV4L)
      return Success;

   if(!(pPriv->videoStatus & CLIENT_VIDEO_ON))
      return BadValue;

#ifdef XVDEBUG
	xf86DrvMsg(0, X_INFO, "[Xv] ReputImage is called\n");
#endif

   memset(&overlay, 0, sizeof(overlay));

   pPriv->drw_x = drw_x;
   pPriv->drw_y = drw_y;

   result = SISCheckOverlay(pScrn, pPriv, &overlay);

   /* returns: 0 = overlay not available, 1 = ok,
    *          2 = overlay wouldn't be drawn (outside visible screen)
    */

   if(result != 1) {

      if(pPriv->overlayStatus) {
	 close_overlay(pSiS, pPriv);
      }
      pPriv->NoOverlay = TRUE;

   } else {

      SISDisplayVideo(pScrn, pPriv, &overlay);

      /* update cliplist */
      SiSHandleClipListColorkey(pScrn, pPriv, clipBoxes);

   }

   return Success;
}

/*********************************
 *     QueryImageAttributes()    *
 *********************************/

static int
SISQueryImageAttributes(
  ScrnInfoPtr pScrn,
  int id,
  UShort *w, UShort *h,
  int *pitches, int *offsets
){
    SISPtr pSiS = SISPTR(pScrn);
    SISPortPrivPtr pPriv = GET_PORT_PRIVATE(pScrn);
    int    pitchY, pitchUV;
    int    size, sizeY, sizeUV;

    if(*w < IMAGE_MIN_WIDTH) *w = IMAGE_MIN_WIDTH;
    if(*h < IMAGE_MIN_HEIGHT) *h = IMAGE_MIN_HEIGHT;

    if(!pSiS->HaveBlitAdaptor) {
       if(*w > DummyEncoding.width) *w = DummyEncoding.width;
       if(*h > DummyEncoding.height) *h = DummyEncoding.height;
    }

    switch(id) {
    case PIXEL_FMT_IA44:/* subpicture */
	pitchY = (*w);
	pitches[0] = pitchY;
	size = pitchY * (*h);
	offsets[0] = 0;
	break;
    case PIXEL_FMT_YV12:
    case PIXEL_FMT_I420:
        *w = (*w + pPriv->PitchAlignmentMask) & ~(pPriv->PitchAlignmentMask);
        *h = (*h + 1) & ~1;
        pitchY = *w;
    	pitchUV = *w >> 1;
    	if(pitches) {
      	    pitches[0] = pitchY;
            pitches[1] = pitches[2] = pitchUV;
        }
    	sizeY = pitchY * (*h);
    	sizeUV = pitchUV * ((*h) >> 1);
    	if(offsets) {
          offsets[0] = 0;
          offsets[1] = sizeY;
          offsets[2] = sizeY + sizeUV;
        }
        size = sizeY + (sizeUV << 1);
    	break;
    case PIXEL_FMT_NV12:
    case PIXEL_FMT_NV21:
        *w = (*w + 7) & ~7;
        *h = (*h + 1) & ~1;
	pitchY = *w;
    	pitchUV = *w;
    	if(pitches) {
      	    pitches[0] = pitchY;
            pitches[1] = pitchUV;
        }
    	sizeY = pitchY * (*h);
    	sizeUV = pitchUV * ((*h) >> 1);
    	if(offsets) {
          offsets[0] = 0;
          offsets[1] = sizeY;
        }
        size = sizeY + (sizeUV << 1);
        break;
    case PIXEL_FMT_YUY2:
    case PIXEL_FMT_UYVY:
    case PIXEL_FMT_YVYU:
    case PIXEL_FMT_RGB6:
    case PIXEL_FMT_RGB5:
    default:
        *w = (*w + 15) & ~15;
        pitchY = (*w << 1);
    	if(pitches) pitches[0] = pitchY;
    	if(offsets) offsets[0] = 0;
    	size = pitchY * (*h);
    	break;
    }


#ifdef XVDEBUG
	xf86DrvMsg(0, X_INFO, "[Xv] QueryImageAttributes is called: w=%d, h=%d,  pitchY=%d,  pitchUV=%d\n",
				*w, *h, pitchY,pitchUV);
#endif


    return size;
}


/*********************************
 *      OFFSCREEN SURFACES       *
 *********************************/

static int
SISAllocSurface (
    ScrnInfoPtr pScrn,
    int id,
    UShort w,
    UShort h,
    XF86SurfacePtr surface
)
{
    SISPtr pSiS = SISPTR(pScrn);
    SISPortPrivPtr pPriv = GET_PORT_PRIVATE(pScrn);
    int size;

    if((w < IMAGE_MIN_WIDTH) || (h < IMAGE_MIN_HEIGHT))
       return BadValue;

    if((w > DummyEncoding.width) || (h > DummyEncoding.height))
       return BadValue;

    if(pPriv->grabbedByV4L)
       return BadAlloc;

    w = (w + 1) & ~1;
    pPriv->pitch = ((w << 1) + 63) & ~63; /* Only packed pixel modes supported */
    size = h * pPriv->pitch;
    if(!(pPriv->offset = SISAllocateFBMemory(pScrn, &pPriv->handle, size)))
       return BadAlloc;

    surface->width   = w;
    surface->height  = h;
    surface->pScrn   = pScrn;
    surface->id      = id;
    surface->pitches = &pPriv->pitch;
    surface->offsets = &pPriv->offset;
    surface->devPrivate.ptr = (pointer)pPriv;

    close_overlay(pSiS, pPriv);
    pPriv->videoStatus = 0;
    REGION_EMPTY(pScrn->pScreen, &pPriv->clip);
    pPriv->grabbedByV4L = TRUE;
    return Success;
}

static int
SISStopSurface(XF86SurfacePtr surface)
{
    SISPortPrivPtr pPriv = (SISPortPrivPtr)(surface->devPrivate.ptr);
    SISPtr pSiS = SISPTR(surface->pScrn);

    if(pPriv->grabbedByV4L && pPriv->videoStatus) {
       close_overlay(pSiS, pPriv);
       pPriv->mustwait = 1;
       pPriv->videoStatus = 0;
    }
    return Success;
}

static int
SISFreeSurface(XF86SurfacePtr surface)
{
    SISPortPrivPtr pPriv = (SISPortPrivPtr)(surface->devPrivate.ptr);

    if(pPriv->grabbedByV4L) {
       SISStopSurface(surface);
       SISFreeFBMemory(surface->pScrn, &pPriv->handle);
       pPriv->grabbedByV4L = FALSE;
    }
    return Success;
}

static int
SISGetSurfaceAttribute (
    ScrnInfoPtr pScrn,
    Atom attribute,
    INT32 *value
)
{
    SISPortPrivPtr pPriv = GET_PORT_PRIVATE(pScrn);

    return SISGetPortAttribute(pScrn, attribute, value, (pointer)pPriv);
}

static int
SISSetSurfaceAttribute (
    ScrnInfoPtr pScrn,
    Atom attribute,
    INT32 value
)
{
    SISPortPrivPtr pPriv = GET_PORT_PRIVATE(pScrn);;

    return SISSetPortAttribute(pScrn, attribute, value, (pointer)pPriv);
}

static int
SISDisplaySurface (
    XF86SurfacePtr surface,
    short src_x, short src_y,
    short drw_x, short drw_y,
    short src_w, short src_h,
    short drw_w, short drw_h,
    RegionPtr clipBoxes
)
{
   ScrnInfoPtr pScrn = surface->pScrn;
   SISPortPrivPtr pPriv = (SISPortPrivPtr)(surface->devPrivate.ptr);
   SISOverlayRec overlay;
   int result;
#ifdef SIS_USE_XAA
   SISPtr pSiS = SISPTR(pScrn);
   int myreds[] = { 0x000000ff, 0x0000f800, 0, 0x00ff0000 };
#endif

   if(!pPriv->grabbedByV4L)
      return Success;

   memset(&overlay, 0, sizeof(overlay));

   pPriv->drw_x = drw_x;
   pPriv->drw_y = drw_y;
   pPriv->drw_w = drw_w;
   pPriv->drw_h = drw_h;
   pPriv->src_x = src_x;
   pPriv->src_y = src_y;
   pPriv->src_w = src_w;
   pPriv->src_h = src_h;
   pPriv->id = surface->id;
   pPriv->height = surface->height;
   pPriv->bufAddr[0] = surface->offsets[0];
   pPriv->currentBuf = 0;
   pPriv->srcPitch = surface->pitches[0];

   result = SISCheckOverlay(pScrn, pPriv, &overlay);

   /* returns: 0 = overlay not available, 1 = ok,
    *          2 = overlay wouldn't be drawn (outside visible screen)
    */

   if(result == 1) {

      SISDisplayVideo(pScrn, pPriv, &overlay);

   }

   if(pPriv->autopaintColorKey) {
#ifdef SIS_USE_XAA
      XAAInfoRecPtr pXAA = pSiS->AccelInfoPtr;

      if((pPriv->NoOverlay) && pXAA && pXAA->FillMono8x8PatternRects) {
         (*pXAA->FillMono8x8PatternRects)(pScrn,
	  		myreds[(pSiS->CurrentLayout.bitsPerPixel >> 3) - 1],
	 		0x000000, GXcopy, ~0,
			REGION_NUM_RECTS(clipBoxes),
			REGION_RECTS(clipBoxes),
			0x00422418, 0x18244200, 0, 0);

      } else {
#endif
#if XF86_VERSION_CURRENT < XF86_VERSION_NUMERIC(4,1,99,1,0)
   	 (*pXAA->FillSolidRects)(pScrn, pPriv->colorKey, GXcopy, ~0,
                        REGION_NUM_RECTS(clipBoxes),
                        REGION_RECTS(clipBoxes));
#else
         xf86XVFillKeyHelper(pScrn->pScreen, (pPriv->NoOverlay) ? 0x00ff0000 : pPriv->colorKey, clipBoxes);
#endif
#ifdef SIS_USE_XAA
      }
#endif
   }

   pPriv->videoStatus = CLIENT_VIDEO_ON;

   return Success;
}

#define NUMOFFSCRIMAGES_300 4
#define NUMOFFSCRIMAGES_315 5


static XF86OffscreenImageRec SISOffscreenImages[NUMOFFSCRIMAGES_315] =
{
 {
   &SISImages[0],  	/* YUV2 */
   VIDEO_OVERLAID_IMAGES | VIDEO_CLIP_TO_VIEWPORT,
   SISAllocSurface,
   SISFreeSurface,
   SISDisplaySurface,
   SISStopSurface,
   SISGetSurfaceAttribute,
   SISSetSurfaceAttribute,
   0, 0,  			/* Rest will be filled in */
   0,
   NULL
 },
 {
   &SISImages[2],	/* UYVY */
   VIDEO_OVERLAID_IMAGES | VIDEO_CLIP_TO_VIEWPORT,
   SISAllocSurface,
   SISFreeSurface,
   SISDisplaySurface,
   SISStopSurface,
   SISGetSurfaceAttribute,
   SISSetSurfaceAttribute,
   0, 0,  			/* Rest will be filled in */
   0,
   NULL
 }
 ,
 {
   &SISImages[4],	/* RV15 */
   VIDEO_OVERLAID_IMAGES | VIDEO_CLIP_TO_VIEWPORT,
   SISAllocSurface,
   SISFreeSurface,
   SISDisplaySurface,
   SISStopSurface,
   SISGetSurfaceAttribute,
   SISSetSurfaceAttribute,
   0, 0,  			/* Rest will be filled in */
   0,
   NULL
 },
 {
   &SISImages[5],	/* RV16 */
   VIDEO_OVERLAID_IMAGES | VIDEO_CLIP_TO_VIEWPORT,
   SISAllocSurface,
   SISFreeSurface,
   SISDisplaySurface,
   SISStopSurface,
   SISGetSurfaceAttribute,
   SISSetSurfaceAttribute,
   0, 0,  			/* Rest will be filled in */
   0,
   NULL
 },
 {
   &SISImages[6],	/* YVYU */
   VIDEO_OVERLAID_IMAGES | VIDEO_CLIP_TO_VIEWPORT,
   SISAllocSurface,
   SISFreeSurface,
   SISDisplaySurface,
   SISStopSurface,
   SISGetSurfaceAttribute,
   SISSetSurfaceAttribute,
   0, 0,  			/* Rest will be filled in */
   0,
   NULL
 }
};

static void
SISInitOffscreenImages(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SISPtr pSiS = SISPTR(pScrn);
    int i, num;

    if(pSiS->VGAEngine == SIS_300_VGA)	num = NUMOFFSCRIMAGES_300;
    else 				num = NUMOFFSCRIMAGES_315;

    for(i = 0; i < num; i++) {
       SISOffscreenImages[i].max_width  = DummyEncoding.width;
       SISOffscreenImages[i].max_height = DummyEncoding.height;
       if(pSiS->VGAEngine == SIS_300_VGA) {
          SISOffscreenImages[i].attributes = &SISAttributes_300[0];
	  SISOffscreenImages[i].num_attributes = SiSCountAttributes(&SISAttributes_300[0]);
       } else {
          SISOffscreenImages[i].attributes = &SISAttributes_315[0];
	  SISOffscreenImages[i].num_attributes = SiSCountAttributes(&SISAttributes_315[0]);
	  if((pSiS->hasTwoOverlays) && (!(pSiS->SiS_SD2_Flags & SiS_SD2_SUPPORT760OO))) {
	     SISOffscreenImages[i].num_attributes--;
	  }
       }
    }
    xf86XVRegisterOffscreenImages(pScreen, SISOffscreenImages, num);
}

/*****************************************************************/
/*                         BLIT ADAPTORS                         */
/*****************************************************************/
#ifdef INCL_YUV_BLIT_ADAPTOR

static void
SISSetPortDefaultsBlit(ScrnInfoPtr pScrn, SISBPortPrivPtr pPriv)
{
    /* Default: Don't sync. */
    pPriv->vsync  = 0;
}

static void
SISResetVideoBlit(ScrnInfoPtr pScrn)
{
}

static XF86VideoAdaptorPtr
SISSetupBlitVideo(ScreenPtr pScreen)
{
   ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
   SISPtr pSiS = SISPTR(pScrn);
   XF86VideoAdaptorPtr adapt;
   SISBPortPrivPtr pPriv;
   int i;

#ifdef SIS_USE_XAA
   if(!pSiS->useEXA) {
      if(!pSiS->AccelInfoPtr) return NULL;
   }
#endif

   if(!(adapt = calloc(1, sizeof(XF86VideoAdaptorRec) +
    			   (sizeof(DevUnion) * NUM_BLIT_PORTS) +
                           sizeof(SISBPortPrivRec)))) {
      return NULL;
   }

   adapt->type = XvWindowMask | XvInputMask | XvImageMask;
   adapt->flags = 0;
   adapt->name = "SIS 315/330/340/350 series Video Blitter";
   adapt->nEncodings = 1;
   adapt->pEncodings = &DummyEncodingBlit;
   adapt->nFormats = NUM_FORMATS;
   adapt->pFormats = SISFormats;
   adapt->nImages = NUM_IMAGES_BLIT;
   adapt->pImages = SISImagesBlit;
   adapt->pAttributes = SISAttributes_Blit;
   adapt->nAttributes = NUM_ATTRIBUTES_BLIT;
   adapt->nPorts = NUM_BLIT_PORTS;
   adapt->pPortPrivates = (DevUnion*)(&adapt[1]);

   pSiS->blitPriv = (void *)(&adapt->pPortPrivates[NUM_BLIT_PORTS]);
   pPriv = (SISBPortPrivPtr)(pSiS->blitPriv);

   for(i = 0; i < NUM_BLIT_PORTS; i++) {
      adapt->pPortPrivates[i].uval = (ULong)(i);
#if defined(REGION_NULL)
      REGION_NULL(pScreen, &pPriv->blitClip[i]);
#else
      REGION_INIT(pScreen, &pPriv->blitClip[i], NullBox, 0);
#endif
      pPriv->videoStatus[i] = 0;
      pPriv->currentBuf[i]  = 0;
      pPriv->handle[i]      = NULL;
   }

   /* Scanline trigger not implemented by hardware! */
   pPriv->VBlankTriggerCRT1 = 0; /* SCANLINE_TRIGGER_ENABLE | SCANLINE_TR_CRT1; */
   pPriv->VBlankTriggerCRT2 = 0; /* SCANLINE_TRIGGER_ENABLE | SCANLINE_TR_CRT2; */
   if(pSiS->ChipType >= SIS_330) {
      pPriv->AccelCmd = YUVRGB_BLIT_330;
   } else {
      pPriv->AccelCmd = YUVRGB_BLIT_325;
   }

   adapt->PutVideo = NULL;
   adapt->PutStill = NULL;
   adapt->GetVideo = NULL;
   adapt->GetStill = NULL;
   adapt->StopVideo = (StopVideoFuncPtr)SISStopVideoBlit;
   adapt->SetPortAttribute = (SetPortAttributeFuncPtr)SISSetPortAttributeBlit;
   adapt->GetPortAttribute = (GetPortAttributeFuncPtr)SISGetPortAttributeBlit;
   adapt->QueryBestSize = (QueryBestSizeFuncPtr)SISQueryBestSizeBlit;
   /* because SIS671 has no sctretch engine, we use old bliter function */ 
   adapt->PutImage = (pSiS->ChipType == SIS_671) ? (PutImageFuncPtr)SISPutImageBlit_671 : 
   	(PutImageFuncPtr)SISPutImageBlit; 
   adapt->QueryImageAttributes = SISQueryImageAttributesBlit;

   pSiS->blitadaptor = adapt;

#if 0
   pSiS->xvVSync = MAKE_ATOM(sisxvvsync);
#endif
   pSiS->xvSetDefaults = MAKE_ATOM(sisxvsetdefaults);

   SISResetVideoBlit(pScrn);

   /* Reset the properties to their defaults */
   SISSetPortDefaultsBlit(pScrn, pPriv);

   return adapt;
}

static int
SISGetPortAttributeBlit(ScrnInfoPtr pScrn, Atom attribute,
  			INT32 *value, ULong index)
{
#if 0
   SISPtr pSiS = SISPTR(pScrn);
   SISBPortPrivPtr pPriv = (SISBPortPrivPtr)(pSiS->blitPriv);

   if(attribute == pSiS->xvVSync) {
      *value = pPriv->vsync;
      return Success;
   } else
#endif
      return BadMatch;
}

static int
SISSetPortAttributeBlit(ScrnInfoPtr pScrn, Atom attribute,
  		    	INT32 value, ULong index)
{
   SISPtr pSiS = SISPTR(pScrn);
   SISBPortPrivPtr pPriv = (SISBPortPrivPtr)(pSiS->blitPriv);

#if 0
   if(attribute == pSiS->xvVSync) {
      if((value < 0) || (value > 1)) return BadValue;
      pPriv->vsync = value;
   } else
#endif
          if(attribute == pSiS->xvSetDefaults) {
      SISSetPortDefaultsBlit(pScrn, pPriv);
   } else return BadMatch;
   return Success;
}

static void
SISStopVideoBlit(ScrnInfoPtr pScrn, ULong index, Bool shutdown)
{
   SISPtr pSiS = SISPTR(pScrn);
   SISBPortPrivPtr pPriv = (SISBPortPrivPtr)(pSiS->blitPriv);

   /* This shouldn't be called for blitter adaptors due to
    * adapt->flags but we provide it anyway.
    */

   if(index >= NUM_BLIT_PORTS) return;

   REGION_EMPTY(pScrn->pScreen, &pPriv->blitClip[index]);

   if(shutdown) {
      (*pSiS->SyncAccel)(pScrn);
      pPriv->videoStatus[index] = 0;
      SISFreeFBMemory(pScrn, &pPriv->handle[(int)index]);
      SISFreeFBMemory(pScrn, &pPriv->ScaleBufHandle[(int)index]);
   }
}


static int
SISPutImageBlit_671(
  ScrnInfoPtr pScrn,
  short src_x, short src_y,
  short drw_x, short drw_y,
  short src_w, short src_h,
  short drw_w, short drw_h,
  int id, UChar *buf,
  short width, short height,
  Bool sync,
  RegionPtr clipBoxes, ULong index
){
   SISPtr pSiS = SISPTR(pScrn);
   SISBPortPrivPtr pPriv = (SISBPortPrivPtr)(pSiS->blitPriv);
   BoxPtr pbox = REGION_RECTS(clipBoxes);
   int    nbox = REGION_NUM_RECTS(clipBoxes);
   CARD32 bufInFbOffs, dstbase = 0, offsety, offsetuv, temp;
   int    totalSize, yuvSize, bytesize=0, h, w, wb, srcPitch;
   int 	  left, right, top, bottom;
   UChar  *ybases, *ubases = NULL, *vbases = NULL, *myubases, *myvbases;
   UChar  *ybased, *uvbased, packed;
   CARD16 *myuvbased;
   SiS_Packet12_YUV MyPacket;
#if XF86_VERSION_CURRENT < XF86_VERSION_NUMERIC(4,1,99,1,0)
   XAAInfoRecPtr pXAA = pSiS->AccelInfoPtr;
#endif
   int	  xoffset = 0, yoffset = 0;
   Bool   first;

   if(index >= NUM_BLIT_PORTS)
      return BadMatch;

   if(!height || !width)
      return Success;

   if(width > IMAGE_MAX_WIDTH_BLIT || height > IMAGE_MAX_HEIGHT_BLIT)
      return BadMatch;

   switch(id) {
     case PIXEL_FMT_YV12:
     case PIXEL_FMT_I420:
     case PIXEL_FMT_NV12:
     case PIXEL_FMT_NV21:
       srcPitch = (width + 7) & ~7;		/* Should come this way anyway */
       bytesize = srcPitch * height;
       yuvSize = (bytesize * 3) >> 1;
       break;
     case PIXEL_FMT_YUY2:
     case PIXEL_FMT_UYVY:
     case PIXEL_FMT_YVYU:
       srcPitch = ((width << 1) + 3) & ~3;
       yuvSize = srcPitch * height;		/* Size = width * 2 * height */
       bytesize = 0;
       break;
     default:
       return BadMatch;
   }

   totalSize = yuvSize;

   /* allocate memory (we do doublebuffering) */
   if(!(pPriv->bufAddr[index][0] = SISAllocateFBMemory(pScrn, &pPriv->handle[index], totalSize << 1)))
      return BadAlloc;

   pPriv->bufAddr[index][1] = pPriv->bufAddr[index][0] + totalSize;

   if(drw_w > width) {
      xoffset = (drw_w - width) >> 1;
   }
   if(drw_h > (height & ~1)) {
      yoffset = (drw_h - height) >> 1;
   }

   if(xoffset || yoffset) {
#if XF86_VERSION_CURRENT < XF86_VERSION_NUMERIC(4,3,99,3,0)
      if(!RegionsEqual(&pPriv->blitClip[index], clipBoxes)) {
#else
      if(!REGION_EQUAL(pScrn->pScreen, &pPriv->blitClip[index], clipBoxes)) {
#endif
#if XF86_VERSION_CURRENT < XF86_VERSION_NUMERIC(4,1,99,1,0)
         (*pXAA->FillSolidRects)(pScrn, 0x00000000, GXcopy, ~0,
                              REGION_NUM_RECTS(clipBoxes),
                              REGION_RECTS(clipBoxes));
#else
         xf86XVFillKeyHelper(pScrn->pScreen, 0x00000000, clipBoxes);
#endif
         REGION_COPY(pScrn->pScreen, &pPriv->blitClip[index], clipBoxes);
      }
   }

   bufInFbOffs = pPriv->bufAddr[index][pPriv->currentBuf[index]];

   memset(&MyPacket, 0, sizeof(MyPacket));

   ybased =  pSiS->FbBase + bufInFbOffs;
   uvbased = ybased + bytesize;


   ybases = buf;
   packed = 0;

   switch(id) {
     case PIXEL_FMT_YV12:
	vbases = buf + bytesize;
	ubases = buf + bytesize*5/4;
	break;
     case PIXEL_FMT_I420:
	ubases = buf + bytesize;
	vbases = buf + bytesize*5/4;
	break;
     case PIXEL_FMT_NV12:
        MyPacket.P12_Command = YUV_FORMAT_NV12;
        break;
     case PIXEL_FMT_NV21:
        MyPacket.P12_Command = YUV_FORMAT_NV21;
        break;
     case PIXEL_FMT_YUY2:
        MyPacket.P12_Command = YUV_FORMAT_YUY2;
	packed = 1;
        break;
     case PIXEL_FMT_UYVY:
        MyPacket.P12_Command = YUV_FORMAT_UYVY;
	packed = 1;
        break;
     case PIXEL_FMT_YVYU:
        MyPacket.P12_Command = YUV_FORMAT_YVYU;
	packed = 1;
        break;
     default:
        return BadMatch;
   }

   switch(id) {
   case PIXEL_FMT_YV12:
   case PIXEL_FMT_I420:
      MyPacket.P12_Command = YUV_FORMAT_NV12;
      /* Copy y plane */
      SiSMemCopyToVideoRam(pSiS, ybased, ybases, bytesize);
      /* Copy u/v planes */
      wb = srcPitch >> 1;
      h = height >> 1;
      while(h--) {
         myuvbased = (CARD16 *)uvbased;
         myubases = ubases;
         myvbases = vbases;
	 w = wb;
	 while(w--) {
#if X_BYTE_ORDER == X_BIG_ENDIAN
 	    temp =  (*myubases++) << 8;
	    temp |= (*myvbases++);
#else
	    temp =  (*myvbases++) << 8;
	    temp |= (*myubases++);
#endif
	    *myuvbased++ = temp;
	 }
	 uvbased += srcPitch;
	 ubases += wb;
	 vbases += wb;
      }
      break;
   default:
      SiSMemCopyToVideoRam(pSiS, ybased, ybases, yuvSize);
   }

   MyPacket.P12_Header0 = SIS_PACKET12_HEADER0;
   MyPacket.P12_Header1 = SIS_PACKET12_HEADER1;
   MyPacket.P12_Null1 = SIS_NIL_CMD;
   MyPacket.P12_Null2 = SIS_NIL_CMD;

   MyPacket.P12_YPitch = MyPacket.P12_UVPitch = srcPitch;
   MyPacket.P12_DstHeight = 0x0fff;

   MyPacket.P12_DstAddr = dstbase + FBOFFSET;
   MyPacket.P12_DstPitch = pSiS->scrnOffset;

   MyPacket.P12_Command |= pPriv->AccelCmd		|
			   SRCVIDEO			|
			   PATFG			|
			   pSiS->SiS310_AccelDepth	|
			   YUV_CMD_YUV			|
			   DSTVIDEO;


   first = TRUE;
   while(nbox--) {
      left = pbox->x1;
      if(left >= drw_x + xoffset + width) goto mycont;

      right = pbox->x2;
      if(right <= drw_x + xoffset) goto mycont;

      top = pbox->y1;
      if(top >= drw_y + yoffset + height) goto mycont;

      bottom = pbox->y2;
      if(bottom <= drw_y + yoffset) goto mycont;

      if(left < (drw_x + xoffset)) left = drw_x + xoffset;
      if(right > (drw_x + xoffset + width)) right = drw_x + xoffset + width;
      if(top < (drw_y + yoffset)) top = drw_y + yoffset;
      if(bottom > (drw_y + yoffset + height)) bottom = drw_y + yoffset + height;

      MyPacket.P12_DstX = left;
      MyPacket.P12_DstY = top;
      MyPacket.P12_RectWidth = right - left;
      MyPacket.P12_RectHeight = bottom - top;


      offsety = offsetuv = 0;
      if(packed) {
         if(pbox->y1 > drw_y + yoffset) {
            offsetuv  = (pbox->y1 - drw_y - yoffset) * srcPitch;
         }
         if(pbox->x1 > drw_x + xoffset) {
            offsetuv += ((pbox->x1 - drw_x - xoffset) << 1);
	    if(offsetuv & 3) {
	       offsetuv = (offsetuv + 3) & ~3;
	       MyPacket.P12_DstX++;
	       MyPacket.P12_RectWidth--;
	    }
         }
      } else {
         if(pbox->y1 > drw_y + yoffset) {
            offsety  = (pbox->y1 - drw_y - yoffset) * srcPitch;
	    offsetuv = ((pbox->y1 - drw_y - yoffset) >> 1) * srcPitch;
         }
         if(pbox->x1 > drw_x + xoffset) {
            offsety += (pbox->x1 - drw_x - xoffset);
	    offsetuv += (pbox->x1 - drw_x - xoffset);
	    if(offsetuv & 1) {
	       offsety++;
	       offsetuv++;
	       MyPacket.P12_DstX++;
	       MyPacket.P12_RectWidth--;
	    }
         }
      }

      if(!MyPacket.P12_RectWidth) continue;

      MyPacket.P12_YSrcAddr  = pPriv->bufAddr[index][pPriv->currentBuf[index]] + offsety + FBOFFSET;
      MyPacket.P12_UVSrcAddr = pPriv->bufAddr[index][pPriv->currentBuf[index]] + bytesize + offsetuv + FBOFFSET;
      SISWriteBlitPacket(pSiS, (CARD32*)&MyPacket);

      first = FALSE;
mycont:
	 pbox++;
   }


   pPriv->currentBuf[index] ^= 1;

   UpdateCurrentTime();
   pPriv->freeTime[index] = currentTime.milliseconds + FREE_DELAY;
   pPriv->videoStatus[index] = FREE_TIMER;

   pSiS->VideoTimerCallback = SISVideoTimerCallback;

   return Success;
}


static int
SISPutImageBlit(
  ScrnInfoPtr pScrn,
  short src_x, short src_y,
  short drw_x, short drw_y,
  short src_w, short src_h,
  short drw_w, short drw_h,
  int id, UChar *buf,
  short width, short height,
  Bool sync,
  RegionPtr clipBoxes, ULong index
){
   SISPtr pSiS = SISPTR(pScrn);
   SISBPortPrivPtr pPriv = (SISBPortPrivPtr)(pSiS->blitPriv);
   BoxPtr pbox = REGION_RECTS(clipBoxes);
   int    nbox = REGION_NUM_RECTS(clipBoxes);
   CARD32 bufInFbOffs, dstbase = 0, offsety, offsetuv, temp;
   int    totalSize, yuvSize, bytesize=0, h, w, wb, srcPitch;
   int 	  left, right, top, bottom;
   UChar  *ybases, *ubases = NULL, *vbases = NULL, *myubases, *myvbases;
   UChar  *ybased, *uvbased, packed;
   CARD16 *myuvbased;
   SiS_Packet12_YUV MyPacket;
#if XF86_VERSION_CURRENT < XF86_VERSION_NUMERIC(4,1,99,1,0)
   XAAInfoRecPtr pXAA = pSiS->AccelInfoPtr;
#endif
   int	  xoffset = 0, yoffset = 0;
   Bool   first;

   /* for scaling */
   int ScaleBufSize;
   SiS_Packet12_Stretch ScalePacket;
   short MM_x, mm_x, stretch_x;
   short MM_y, mm_y, stretch_y;
   short diff_x, diff_y;

#ifdef XVDEBUG
	xf86DrvMsg(0, X_INFO, "[Xv] PutImageBlit is called: id = %d\n", id);
	xf86DrvMsg(0, X_INFO, "[Xv] src_x = %d, src_y = %d, src_w = %d, src_h = %d,\
		drw_x = %d, drw_y = %d, drw_w = %d, drw_h = %d, width = %d, height = %d\n", 
		src_x, src_y, src_w, src_h, drw_x, drw_y, drw_w, drw_h, width, height);
#endif

   if(index >= NUM_BLIT_PORTS)
      return BadMatch;

   if(!height || !width)
      return Success;

   if(width > IMAGE_MAX_WIDTH_BLIT || height > IMAGE_MAX_HEIGHT_BLIT)
      return BadMatch;

   switch(id) {
     case PIXEL_FMT_YV12:
     case PIXEL_FMT_I420:
     case PIXEL_FMT_NV12:
     case PIXEL_FMT_NV21:
       srcPitch = (width + 7) & ~7;		/* Should come this way anyway */
       bytesize = srcPitch * height;
       yuvSize = (bytesize * 3) >> 1;
       break;
     case PIXEL_FMT_YUY2:
     case PIXEL_FMT_UYVY:
     case PIXEL_FMT_YVYU:
       srcPitch = ((width << 1) + 3) & ~3;
       yuvSize = srcPitch * height;		/* Size = width * 2 * height */
       bytesize = 0;
       break;
     default:
       return BadMatch;
   }

   totalSize = yuvSize;
   ScaleBufSize =  srcPitch * height * 4;
   
   /* allocate memory (we do doublebuffering) */
   if(!(pPriv->bufAddr[index][0] = SISAllocateFBMemory(pScrn, &pPriv->handle[index], totalSize << 1)))
      return BadAlloc;
   
   if(!(pPriv->ScaleBufAddr[index][0] = SISAllocateFBMemory(pScrn, &pPriv->ScaleBufHandle[index], ScaleBufSize << 1)))
      return BadAlloc;

   pPriv->bufAddr[index][1] = pPriv->bufAddr[index][0] + totalSize;
   pPriv->ScaleBufAddr[index][1] = pPriv->ScaleBufAddr[index][0] + ScaleBufSize;


   bufInFbOffs = pPriv->bufAddr[index][pPriv->currentBuf[index]];

   memset(&MyPacket, 0, sizeof(MyPacket));

   ybased =  pSiS->FbBase + bufInFbOffs;
   uvbased = ybased + bytesize;


   ybases = buf;
   packed = 0;

   switch(id) {
     case PIXEL_FMT_YV12:
	vbases = buf + bytesize;
	ubases = buf + bytesize*5/4;
	break;
     case PIXEL_FMT_I420:
	ubases = buf + bytesize;
	vbases = buf + bytesize*5/4;
	break;
     case PIXEL_FMT_NV12:
        MyPacket.P12_Command = YUV_FORMAT_NV12;
        break;
     case PIXEL_FMT_NV21:
        MyPacket.P12_Command = YUV_FORMAT_NV21;
        break;
     case PIXEL_FMT_YUY2:
        MyPacket.P12_Command = YUV_FORMAT_YUY2;
	packed = 1;
        break;
     case PIXEL_FMT_UYVY:
        MyPacket.P12_Command = YUV_FORMAT_UYVY;
	packed = 1;
        break;
     case PIXEL_FMT_YVYU:
        MyPacket.P12_Command = YUV_FORMAT_YVYU;
	packed = 1;
        break;
     default:
        return BadMatch;
   }

   switch(id) {
   case PIXEL_FMT_YV12:
   case PIXEL_FMT_I420:
      MyPacket.P12_Command = YUV_FORMAT_NV12;
      /* Copy y plane */
      SiSMemCopyToVideoRam(pSiS, ybased, ybases, bytesize);
      /* Copy u/v planes */
      wb = srcPitch >> 1;
      h = height >> 1;
      while(h--) {
         myuvbased = (CARD16 *)uvbased;
         myubases = ubases;
         myvbases = vbases;
	 w = wb;
	 while(w--) {
#if X_BYTE_ORDER == X_BIG_ENDIAN
 	    temp =  (*myubases++) << 8;
	    temp |= (*myvbases++);
#else
	    temp =  (*myvbases++) << 8;
	    temp |= (*myubases++);
#endif
	    *myuvbased++ = temp;
	 }
	 uvbased += srcPitch;
	 ubases += wb;
	 vbases += wb;
      }
      break;
   default:
      SiSMemCopyToVideoRam(pSiS, ybased, ybases, yuvSize);
   }

   MyPacket.P12_Header0 = SIS_PACKET12_HEADER0;
   MyPacket.P12_Header1 = SIS_PACKET12_HEADER1;
   MyPacket.P12_Null1 = SIS_NIL_CMD;
   MyPacket.P12_Null2 = SIS_NIL_CMD;

   MyPacket.P12_YPitch = MyPacket.P12_UVPitch = srcPitch;
   MyPacket.P12_DstHeight = src_h;

   MyPacket.P12_DstAddr = pPriv->ScaleBufAddr[index][pPriv->currentBuf[index]] + FBOFFSET;/*dstbase + FBOFFSET;*/
   MyPacket.P12_DstPitch = srcPitch * 4;
   
   MyPacket.P12_Command |= pPriv->AccelCmd		|
			   SRCVIDEO			|
			   PATFG			|
			   pSiS->SiS310_AccelDepth	|
			   YUV_CMD_YUV			|
			   DSTVIDEO;

   MyPacket.P12_DstX = 0;
   MyPacket.P12_DstY = 0;
   MyPacket.P12_RectWidth = src_w;
   MyPacket.P12_RectHeight = src_h;

   MyPacket.P12_YSrcAddr  = pPriv->bufAddr[index][pPriv->currentBuf[index]] + FBOFFSET;
   MyPacket.P12_UVSrcAddr = pPriv->bufAddr[index][pPriv->currentBuf[index]] + bytesize + FBOFFSET;
   SISWriteBlitPacket(pSiS, (CARD32*)&MyPacket);


   /* setting stretch packet */
   memset(&ScalePacket, 0, sizeof(SiS_Packet12_Stretch));

   ScalePacket.P12_Header0 = SIS_PACKET12_HEADER0;
   ScalePacket.P12_Header1 = SIS_PACKET12_HEADER1;
   ScalePacket.P12_Null1 = SIS_NIL_CMD;
   ScalePacket.P12_Null2 = SIS_NIL_CMD;
   ScalePacket.P12_SrcPitch = srcPitch * 4;
   ScalePacket.P12_SrcX = src_x;  ScalePacket.P12_SrcY = src_y;
   ScalePacket.P12_SrcWidth= src_w;  ScalePacket.P12_SrcHeight= src_h;  
   ScalePacket.P12_SrcAddr = pPriv->ScaleBufAddr[index][pPriv->currentBuf[index]] + FBOFFSET; 
   
   
   ScalePacket.P12_DstHeight = 0x0fff;
   ScalePacket.P12_DstAddr = dstbase + FBOFFSET;
   ScalePacket.P12_DstPitch = pSiS->scrnOffset;
   

   ScalePacket.P12_DstX = drw_x;  
   ScalePacket.P12_DstY = drw_y;
   ScalePacket.P12_RectWidth = drw_w;  
   ScalePacket.P12_RectHeight= drw_h;
   
   
   if(drw_w > src_w){
      MM_x = drw_w;  mm_x = src_w;   stretch_x = 1;
   }else{
      MM_x = src_w;  mm_x = drw_w;    stretch_x = 0;
   }
   diff_x = 2 * (mm_x - MM_x);

   if(drw_h > src_h){
      MM_y = drw_h;  mm_y = src_h;   stretch_y = 1;
   }else{
      MM_y = src_h;  mm_y = drw_h;    stretch_y = 0;
   }
   diff_y = 2 * (mm_y - MM_y);

   ScalePacket.P12_InitErrorX = (stretch_x == 1) ? (src_w + diff_x) : src_w;  
   ScalePacket.P12_X_K1 = 2 * mm_x;
   ScalePacket.P12_X_K2 = diff_x;

   ScalePacket.P12_InitErrorY =  (stretch_y == 1) ? (src_h + diff_y) : src_h; 
   ScalePacket.P12_Y_K1 = 2 * mm_y;
   ScalePacket.P12_Y_K2 = diff_y;

   ScalePacket.P12_Command |= STRETCH_BITBLT		|
			   SRCVIDEO			|
			   0xcc00		|/* ROP: Src only */
			   STRETCH_SRC_X_INC	|	
			   STRETCH_SRC_Y_INC	|
			   STRETCH_DST_X_INC	|
			   STRETCH_DST_Y_INC	|
			   CLIPENABLE		|
			   pSiS->SiS310_AccelDepth	;


   first = TRUE;
   while(nbox--) {
      left = pbox->x1;
      if(left >= drw_x + drw_w) goto mycont;

      right = pbox->x2;
      if(right <= drw_x) goto mycont;

      top = pbox->y1;
      if(top >= drw_y + drw_h) goto mycont;

      bottom = pbox->y2;
      if(bottom <= drw_y) goto mycont;

      if(left < drw_x) left = drw_x;
      if(right > (drw_x + drw_w)) right = drw_x + drw_w;
      if(top < drw_y) top = drw_y;
      if(bottom > (drw_y + drw_h)) bottom = drw_y + drw_h;

      ScalePacket.P12_ClipLeft= left;
      ScalePacket.P12_ClipTop = top;
      ScalePacket.P12_ClipRight = right;
      ScalePacket.P12_ClipBottom = bottom;

      SISWriteBlitPacket(pSiS, (CARD32*)&ScalePacket);

      first = FALSE;
mycont:
	 pbox++;
   }

   pPriv->currentBuf[index] ^= 1;

   UpdateCurrentTime();
   pPriv->freeTime[index] = currentTime.milliseconds + FREE_DELAY;
   pPriv->videoStatus[index] = FREE_TIMER;

   pSiS->VideoTimerCallback = SISVideoTimerCallback;

   return Success;
}



static int
SISQueryImageAttributesBlit(
  ScrnInfoPtr pScrn,
  int id,
  UShort *w, UShort *h,
  int *pitches, int *offsets
){
    int    pitchY, pitchUV;
    int    size, sizeY, sizeUV;

    if(*w > DummyEncodingBlit.width) *w = DummyEncodingBlit.width;
    if(*h > DummyEncodingBlit.height) *h = DummyEncodingBlit.height;

    switch(id) {
    case PIXEL_FMT_YV12:
    case PIXEL_FMT_I420:
	*w = (*w + 7) & ~7;
	*h = (*h + 1) & ~1;
	pitchY = *w;
	pitchUV = *w >> 1;
	if(pitches) {
	   pitches[0] = pitchY;
	   pitches[1] = pitches[2] = pitchUV;
	}
	sizeY = pitchY * (*h);
	sizeUV = pitchUV * ((*h) >> 1);
	if(offsets) {
	   offsets[0] = 0;
	   offsets[1] = sizeY;
	   offsets[2] = sizeY + sizeUV;
	}
	size = sizeY + (sizeUV << 1);
	break;
    case PIXEL_FMT_NV12:
    case PIXEL_FMT_NV21:
	*w = (*w + 7) & ~7;
	pitchY = *w;
	pitchUV = *w;
	if(pitches) {
	   pitches[0] = pitchY;
	   pitches[1] = pitchUV;
	}
	sizeY = pitchY * (*h);
	sizeUV = pitchUV * ((*h) >> 1);
	if(offsets) {
	   offsets[0] = 0;
	   offsets[1] = sizeY;
	}
	size = sizeY + (sizeUV << 1);
	break;
    case PIXEL_FMT_YUY2:
    case PIXEL_FMT_UYVY:
    case PIXEL_FMT_YVYU:
    default:
	*w = (*w + 1) & ~1;
	pitchY = *w << 1;
	if(pitches) pitches[0] = pitchY;
	if(offsets) offsets[0] = 0;
	size = pitchY * (*h);
	break;
    }

    return size;
}

static void
SISQueryBestSizeBlit(
  ScrnInfoPtr pScrn,
  Bool motion,
  short vid_w, short vid_h,
  short drw_w, short drw_h,
  unsigned int *p_w, unsigned int *p_h,
  ULong index
){
  *p_w = drw_w;
  *p_h = drw_h;
}
#endif /* INCL_YUV */

/*****************************************/
/*            TIMER CALLBACK             */
/*****************************************/

static void
SISVideoTimerCallback(ScrnInfoPtr pScrn, Time now)
{
    SISPtr          pSiS = SISPTR(pScrn);
    SISPortPrivPtr  pPriv = NULL;
#ifdef INCL_YUV_BLIT_ADAPTOR
    SISBPortPrivPtr pPrivBlit = NULL;
#endif
    UChar           sridx, cridx;
    Bool	    setcallback = FALSE;

    if(!pScrn->vtSema) return;

    if(pSiS->adaptor) {
       pPriv = GET_PORT_PRIVATE(pScrn);
       if(!pPriv->videoStatus) pPriv = NULL;
    }

    if(pPriv && !pPriv->grabbedByV4L) {
       if(pPriv->videoStatus & OFF_TIMER) {
	  setcallback = TRUE;
	  if(pPriv->offTime < now) {
	     sridx = inSISREG(SISSR); cridx = inSISREG(SISCR);
             close_overlay(pSiS, pPriv);
	     outSISREG(SISSR, sridx); outSISREG(SISCR, cridx);
	     pPriv->mustwait = 1;
             pPriv->videoStatus = FREE_TIMER;
             pPriv->freeTime = now + FREE_DELAY;
	  }
       } else if(pPriv->videoStatus & FREE_TIMER) {
	  if(pPriv->freeTime < now) {
             SISFreeFBMemory(pScrn, &pPriv->handle);
	     pPriv->mustwait = 1;
             pPriv->videoStatus = 0;
          } else {
	     setcallback = TRUE;
	  }
       }
    }

#ifdef INCL_YUV_BLIT_ADAPTOR
    if(pSiS->blitadaptor) {
       int i;
       pPrivBlit = (SISBPortPrivPtr)(pSiS->blitPriv);
       for(i = 0; i < NUM_BLIT_PORTS; i++) {
          if(pPrivBlit->videoStatus[i] & FREE_TIMER) {
	     if(pPrivBlit->freeTime[i] < now) {
                SISFreeFBMemory(pScrn, &pPrivBlit->handle[i]);
                pPrivBlit->videoStatus[i] = 0;
	     } else {
	        setcallback = TRUE;
	     }
          }
       }
    }
#endif

    pSiS->VideoTimerCallback = (setcallback) ? SISVideoTimerCallback : NULL;
}

