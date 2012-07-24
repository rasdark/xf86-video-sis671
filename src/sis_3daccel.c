/* $XFree86$ */
/* $XdotOrg$ */
/*
 * 2D Acceleration for SiS 671 chip
 * Copyright (c) 2007 SiS Corp. All Rights Reserved.
 * Copyright (c) 2007 Chaoyu Chen. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE ABOVE LISTED COPYRIGHT HOLDER(S) BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name(s) of the above copyright
 * holders shall not be used in advertising or otherwise to promote the
 * sale, use or other dealings in this Software without prior written
 * authorization.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "sis.h"
#define SIS_NEED_MYMMIO
#define SIS_NEED_ACCELBUF
#include "sis_regs.h"
#include "sis310_accel.h"
#include "sis_3daccel.h"

/*
#define ACCELDEBUG_3D
*/

#define FBOFFSET 	(pSiS->dhmOffset)

#define DEV_HEIGHT	0xfff	/* "Device height of destination bitmap" */

#undef SIS_NEED_ARRAY

/* For XAA */

#ifdef SIS_USE_XAA

#define INCL_RENDER	/* Use/Don't use RENDER extension acceleration */

#ifdef INCL_RENDER
# ifdef RENDER
#  include "mipict.h"
#  include "dixstruct.h"
#  define SIS_NEED_ARRAY
#  undef SISNEWRENDER
#  ifdef XORG_VERSION_CURRENT
//#   if XORG_VERSION_CURRENT > XORG_VERSION_NUMERIC(6,7,0,0,0)
#    define SISNEWRENDER
//#   endif
#  endif
# endif
#endif

#endif /* XAA */

/* For EXA */

#ifdef SIS_USE_EXA
#if 0
#define SIS_HAVE_COMPOSITE		/* Have our own EXA composite */
#endif
#ifdef SIS_HAVE_COMPOSITE
#if 0
#ifndef SIS_NEED_ARRAY
#define SIS_NEED_ARRAY
#endif
#endif
#endif
#endif


#ifdef SIS_USE_EXA		/* EXA */
void SiSScratchSave(ScreenPtr pScreen, ExaOffscreenArea *area);
Bool SiSUploadToScreen(PixmapPtr pDst, int x, int y, int w, int h, char *src, int src_pitch);
Bool SiSUploadToScratch(PixmapPtr pSrc, PixmapPtr pDst);
Bool SiSDownloadFromScreen(PixmapPtr pSrc, int x, int y, int w, int h, char *dst, int dst_pitch);
#endif /* EXA */



#define _SHT_PS_SrcNum	        6
#define _SHT_PS_DstNum	        18
#define _SHT_PS_DstL   14
#define _SHT_PS_SrcL   14


#define MSK_PS_SrcR            0x08
#define MSK_PS_SrcG            0x04
#define MSK_PS_SrcB            0x02
#define MSK_PS_SrcA            0x01
#define MSK_PS_SrcSwizzleXYZW   0x00
#define MSK_PS_DstoCn           0x100
#define MSK_PS_SrcNULL         0x07C0
#define MSK_PS_ModifierSat      0x80000000
#define MSK_PS_TEXLD            (0x04<<23)
#define MSK_PS_DstR             (0x08 << _SHT_PS_DstL)
#define MSK_PS_DstG             (0x04 << _SHT_PS_DstL)
#define MSK_PS_DstB             (0x02 << _SHT_PS_DstL)
#define MSK_PS_DstA             (0x01 << _SHT_PS_DstL)

#define MSK_PS_MOV              (0x11<<23)
#define MSK_PS_SrcRGB          (MSK_PS_SrcR | MSK_PS_SrcG | MSK_PS_SrcB) 
#define MSK_PS_SrcRGBA         (MSK_PS_SrcRGB | MSK_PS_SrcA)
#define MSK_PS_DstALL           (MSK_PS_DstR | MSK_PS_DstG | MSK_PS_DstB | MSK_PS_DstA)
#define MSK_PS_DstRGBA          (MSK_PS_DstALL)
#define MSK_PS_SrcNoSwizzle     (MSK_PS_SrcSwizzleXYZW | MSK_PS_SrcRGBA)



#define PS_DstTemp(R,Param)     ((R) <<_SHT_PS_DstNum | Param)
#define PS_DstR(R,Param)        PS_DstTemp(R, Param)
#define PS_SrcS(S)			    (0x0700 | (S))
#define PS_SrcR(R, Param)		((R) << _SHT_PS_SrcNum |  (Param)) 
#define PS_DstColor(C)          ((MSK_PS_DstoCn | (C)) << _SHT_PS_DstL)

float HwIdentityMatrix_340[40] = {
    /* T1*/
    1.0f,        /* M_00, M_10, M_20, M_30*/
    0.0f,
    0.0f,
    0.0f,
    0.0f,        /* M_01, M_11, M_21, M_31*/
    1.0f,
    0.0f,
    0.0f,
    0.0f,        /* M_02, M_12, M_22, M_32*/
    0.0f,
    1.0f,
    0.0f,
    0.0f,        /* M_03, M_13, M_23, M_33*/
    0.0f,
    0.0f,
    1.0f,

    /*WV*/
    1.0f,        /* M_00, M_10, M_20, M_30*/
    0.0f,
    0.0f,
    0.0f,
    0.0f,        /* M_01, M_11, M_21, M_31*/
    1.0f,
    0.0f,
    0.0f,
    0.0f,        /* M_02, M_12, M_22, M_32*/
    0.0f,
    1.0f,
    0.0f,

    /*IWV*/
    1.0f,        /* M_00, M_10, M_20, Reserved_30*/
    0.0f,
    0.0f,
    0.0f,
    0.0f,        /* M_01, M_11, M_21, Reserved_31*/
    1.0f,
    0.0f,
    0.0f,
    0.0f,        /* M_02, M_12, M_22, Reserved_32*/
    0.0f,
    1.0f,
    0.0f,
};

typedef struct _CoordVectData{
	unsigned long sx, sy, sz;
	unsigned long tu, tv;
	float u, v, m, n;
}CoordVectData;

unsigned long MRTCWMask[5][2] =
{
    {0x00000000, 0x00000000},
    {0x0000FFFF, 0x00000000},
    {0xFFFFFFFF, 0x00000000},
    {0xFFFFFFFF, 0x0000FFFF},
    {0xFFFFFFFF, 0xFFFFFFFF}
};

unsigned long FVF_TexCoord_771[33][6] =
{
    {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000},
    {0x40250000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000},
    {0x40250000, 0x00004026, 0x00000000, 0x00000000, 0x00000000, 0x00000000},
    {0x40250000, 0x40274026, 0x00000000, 0x00000000, 0x00000000, 0x00000000},
    {0x40250000, 0x40274026, 0x00004028, 0x00000000, 0x00000000, 0x00000000},
    {0x40250000, 0x40274026, 0x40294028, 0x00000000, 0x00000000, 0x00000000},
    {0x40250000, 0x40274026, 0x40294028, 0x0000402A, 0x00000000, 0x00000000},
    {0x40250000, 0x40274026, 0x40294028, 0x402B402A, 0x00000000, 0x00000000},
    {0x40250000, 0x40274026, 0x40294028, 0x402B402A, 0x0000402C, 0x00000000},
    {0xC0650000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}, /*Ext from t0, for diffuse*/
    {0xC0650000, 0x0000C066, 0x00000000, 0x00000000, 0x00000000, 0x00000000}, /*Ext from t0, for diffuse, specular*/
    {0xC0650000, 0x4027C066, 0x00000000, 0x00000000, 0x00000000, 0x00000000}, /*Ext from t0, for diffuse, specular, fog*/
    {0x40250000, 0x0000C066, 0x00000000, 0x00000000, 0x00000000, 0x00000000}, /*Ext from t1, for diffuse*/
    {0x40250000, 0xC067C066, 0x00000000, 0x00000000, 0x00000000, 0x00000000}, /*Ext from t1, for diffuse, specular*/
    {0x40250000, 0xC067C066, 0x00004028, 0x00000000, 0x00000000, 0x00000000}, /*Ext from t1, for diffuse, specular, fog*/
    {0x40250000, 0xC0674026, 0x00000000, 0x00000000, 0x00000000, 0x00000000}, /*Ext from t2, for diffuse	// tyhuang modified, fix the typo of the copytype after diffuse, [2006/10/12]*/
    {0x40250000, 0xC0674026, 0x0000C068, 0x00000000, 0x00000000, 0x00000000}, /*Ext from t2, for diffuse, specular	// tyhuang modified, fix the typo of the copytype after diffuse, [2006/10/12]*/
    {0x40250000, 0xC0674026, 0x4029C068, 0x00000000, 0x00000000, 0x00000000}, /*Ext from t2, for diffuse, specular, fog	// tyhuang modified, fix the typo of the copytype after diffuse, [2006/10/12]*/
    {0x40250000, 0x40274026, 0x0000C068, 0x00000000, 0x00000000, 0x00000000}, /*Ext from t3, for diffuse*/
    {0x40250000, 0x40274026, 0xC069C068, 0x00000000, 0x00000000, 0x00000000}, /*Ext from t3, for diffuse, specular*/
    {0x40250000, 0x40274026, 0xC069C068, 0x0000402A, 0x00000000, 0x00000000}, /*Ext from t3, for diffuse, specular, fog*/
    {0x40250000, 0x40274026, 0xC0694028, 0x00000000, 0x00000000, 0x00000000}, /*Ext from t4, for diffuse*/
    {0x40250000, 0x40274026, 0xC0694028, 0x0000C06A, 0x00000000, 0x00000000}, /*Ext from t4, for diffuse, specular*/
    {0x40250000, 0x40274026, 0xC0694028, 0x402BC06A, 0x00000000, 0x00000000}, /*Ext from t4, for diffuse, specular, fog*/
    {0x40250000, 0x40274026, 0x40294028, 0x0000C06A, 0x00000000, 0x00000000}, /*Ext from t5, for diffuse*/
    {0x40250000, 0x40274026, 0x40294028, 0xC06BC06A, 0x00000000, 0x00000000}, /*Ext from t5, for diffuse, specular*/
    {0x40250000, 0x40274026, 0x40294028, 0xC06BC06A, 0x0000402B, 0x00000000}, /*Ext from t5, for diffuse, specular, fog*/
    {0x40250000, 0x40274026, 0x40294028, 0xC06B402A, 0x00000000, 0x00000000}, /*Ext from t6, for diffuse*/
    {0x40250000, 0x40274026, 0x40294028, 0xC06B402A, 0x0000C06C, 0x00000000}, /*Ext from t6, for diffuse, specular*/
    {0x40250000, 0x40274026, 0x40294028, 0xC06B402A, 0x402DC06C, 0x00000000}, /*Ext from t6, for diffuse, specular, fog*/
    {0x40250000, 0x40274026, 0x40294028, 0x402B402A, 0xC06D402C, 0x00000000}, /*Ext from t7, for diffuse*/
    {0x40250000, 0x40274026, 0x40294028, 0x402B402A, 0xC06D402C, 0x0000C06E}, /*Ext from t7, for diffuse, specular*/
    {0x40250000, 0x40274026, 0x40294028, 0x402B402A, 0xC06D402C, 0x402FC06E}, /*Ext from t7, for diffuse, specular, fog*/
};

unsigned long VertexActBitsL_771[9] = {0x00000007, 0x0000001F, 0x0000007F, 0x000001FF,
                          0x000007FF, 0x00001FFF, 0x00007FFF, 0x0001FFFF,
                          0x0007FFFF};
unsigned long VertexActBitsLEx_771[4] = {0x00000000, 0x0000000F, 0x000000FF, 0x000003FF};

unsigned long dwStamp=0L;


extern Bool SiSAllocateLinear(ScrnInfoPtr pScrn, int sizeNeeded);


#define PS_Packing(index, wOP, wDst, wSrc1, wSrc2, wSrc3)\
{\
	dwInst[index++] = ((wSrc2 << _SHT_PS_SrcL) | (wSrc3));\
	dwInst[index++] = (wOP) | (wDst) | (wSrc1);\
}

#define MAX(a,b)  (((a) > (b)) ? (a) : (b))
#define MIN(a,b)  (((a) < (b)) ? (a) : (b))

#define FBOFFSET	(pSiS->dhmOffset)

unsigned long Float2FixedS7(unsigned long dwValue)
{
    unsigned long dwMantissa;
    int nTemp;

    if(dwValue == 0) return 0;
    *(float*)&dwValue = MAX(-1.0f, MIN(1.0f,*(float*)&dwValue));
    nTemp = (int) (dwValue & 0x7F800000) >> 23;
    nTemp = nTemp - 127 + 7 - 23;
    dwMantissa = (dwValue & 0x007FFFFF) | 0x00800000;   /* Mantissa*/


    if(nTemp > 0){
        dwMantissa <<= nTemp;
    }
    else{
        dwMantissa >>= -nTemp;
    }
    if(dwValue & 0x80000000)        /* negative*/
    {
        dwMantissa = ~dwMantissa + 1;
        dwMantissa &= 0x0000007F;
        dwMantissa |= 0x80;
        if (dwMantissa == 0x80)
        {
            if ( *(float*)&dwValue < -0.9f )
                dwMantissa = 0x81; /* overflow when dwValue close to -1 (0x80) ==> (0x81)*/
            else
                dwMantissa = 0xFF; /* overflow when dwValue close to -0 (0x80) ==> (0xFF)*/
        }
    }
    else if (dwMantissa == 0x80)    /* overflow, 1(0x80) ==> 0.9999..(7F)*/
    {
        dwMantissa = 0x7F;
    }
    return dwMantissa;
}



unsigned long Float2Fixed(unsigned long dwValue, int nInterger, int nFraction)
{
    unsigned long dwMantissa, dwMantissa_771;
    int nTemp;

    if(dwValue == 0) return 0;
    nTemp = (int) (dwValue & 0x7F800000) >> 23;
    nTemp = nTemp - 127 + nFraction - 23;
    dwMantissa_771=dwMantissa = (dwValue & 0x007FFFFF) | 0x00800000;   /*  Mantissa */

    /* if(nTemp < -25) return 0; */
    if(nTemp > 0)
   {
	 if(nTemp >= 32)
	 {
            dwMantissa = (0x1FFFFFFF >> (32-nInterger-nFraction));
	 }
	 else 
              dwMantissa <<= nTemp;
    }	 
    else
   {
	if(nTemp <= -32)
	   dwMantissa = 0;	
	else
          dwMantissa >>= -nTemp;
	
           if(-nTemp > 4)
              dwMantissa_771 = dwMantissa_771 >>= ((-nTemp)-4);
	   /* if(d3d.Registry.PatchFloat2Fix == 1)
	    { */          
              /* if((dwMantissa_771 & d3d.Registry.ManMsk) >= d3d.Registry.ManVal)  .ManMsk = 0xf, ManVal = 0xc */
                   dwMantissa = dwMantissa +1;
	   /* } */
    }
    if(dwValue & 0x80000000)    /*  negative number */
    {
        int nShift = 32-(nInterger+nFraction+1);
        dwMantissa = ~dwMantissa + 1;
        dwMantissa <<= nShift;
        dwMantissa >>= nShift;
        if(/*d3d.Registry.FixSign31 &&*/ (nInterger == 13) && (nFraction == 4))
        {
            if (dwMantissa)  /* sis3412, 2006-3-22, if assign bit when it's zero, it will become a very large negative number */
                dwMantissa = (dwMantissa & 0x0001FFFF) | 0x80000000;
        }
    }
    return dwMantissa;
}



Bool IsPower2(unsigned long dw)
{
    unsigned long i;

    if (!dw) return(0);

    for(i=0x80000000; i>0; i>>=1){
        if(dw & i) break;
    }

    return(dw & (i-1) ? FALSE : TRUE);
}


unsigned long GetTexturePitch(unsigned long dwPitch)
{
    unsigned long i = 0;
    if(dwPitch == 0)
        return 0;

    while( ((dwPitch & 1) == 0) && (i < 15) )
    {
        dwPitch >>= 1;
        i++;
    }

    return dwPitch | (i<<9);
}



Bool
SiSSetupForCPUToScreenAlphaTexture3D(ScrnInfoPtr pScrn,
			int op, CARD16 red, CARD16 green,
			CARD16 blue, CARD16 alpha,
#ifdef SISNEWRENDER
			CARD32 alphaType, CARD32 dstType,
#else
			int alphaType,
#endif
			CARD8 *alphaPtr,
			int alphaPitch, int width,
			int height, int	flags)
{
	



	SISPtr pSiS = SISPTR(pScrn);
	static unsigned char *renderaccelarray;
	CARD32 *dstPtr;
	int    sbpp = pSiS->CurrentLayout.bytesPerPixel;
	int    sbppshift = sbpp >> 1;	/* 8->0, 16->1, 32->2 */
	int    pitch, sizeNeeded;
	CARD8  myalpha;
	Bool   docopy = TRUE;


	int i,x;
	unsigned long dwEnable0, dwEnable1, dwEnable2;
	unsigned long dwTexCoorNum, dwTexCoorExtNum, dwFrontEnable2;
	unsigned long dwSet, dwFormat, dwPitch;
	unsigned long dwTexSet;
	unsigned long dwAlphaBlend, dwBlendMode, dwBlendCst;
	unsigned long dwInstNum, dwDstNum, dwPSSet;
	unsigned long dwCount, dwInst[NUM_MAX_PS_INST340*PS_INST_DWSIZE];
        int InstIndex;	
	unsigned long dwTexCoorSet;
	unsigned long dwTexPitch;
	unsigned long dwTexCoordDim;
	unsigned long TexCrdIdx, dwExt;
	unsigned long dwExtDWNum;

	/*For Init_Read*/
	unsigned long PSSet1;
	unsigned long PSInitRead[8];
	
#ifdef ACCELDEBUG_3D
	xf86DrvMsg(0, X_INFO, "AT3D(1): op %d t %x ARGB %x %x %x %x, w %d h %d pch %d\n",
		op, alphaType, alpha, red, green, blue, width, height, alphaPitch);

#endif 



	if((width > 2048) || (height > 2048)) return FALSE;

	if(!((renderaccelarray = pSiS->RenderAccelArray)))
	   return FALSE;

	pitch = (width+15)& ~15;
	sizeNeeded = (pitch << 2) * height;

	if(!SiSAllocateLinear(pScrn,(sizeNeeded + sbpp - 1) >> sbppshift))	
		return FALSE;

	
	dwEnable0 = MSK_En_Blend_340/* | 0x00400000 | 0x00008000*/;
	dwEnable1 = MSK_En_TexCache_340 |MSK_En_TexL2Cache_340 |/* MSK_En_AGPRqBurst_340 |*/\
				MSK_En_DRAM128b_342 |MSK_En_PixelShader_340 | MSK_En_TexMap_340;
	dwEnable2 = VAL_SolidFill_340 |VAL_SolidCCWFill_340 |MSK_En_ArbPreCharge_340; 

	

	dwTexCoorNum = dwTexCoorExtNum = 1;
	dwFrontEnable2 = 0xffffffff >> (32 - dwTexCoorNum); /*PtSprite*/
	dwFrontEnable2 |= (1L << SHT_TnDiffuse_771 | 1L<<SHT_TnSpecular_771);
	dwTexCoorSet = dwTexCoordDim =0;
	for(i=0; i < dwTexCoorExtNum; i++)
	{
                if(i < 2)
                {
                	dwTexCoorSet |= 2 << ((i+ dwTexCoorNum)*2);
			dwTexCoordDim |= 2 << ((i+ dwTexCoorNum)*2);
			dwFrontEnable2 |= 1 << (dwTexCoorNum + i);
                }
	}

	
	if (sbppshift == 2)		dwFormat = VAL_Dst_A8R8G8B8_340; /*32 bpp*/
	else 	dwFormat = VAL_Dst_R5G6B5_340;

	dwPitch = pSiS->scrnOffset;  /*the screen is the dest*/
	dwSet = 0x0C000000 |dwFormat |dwPitch;


	dwTexSet = ((dwTexCoorNum + dwTexCoorExtNum) << SHT_FrontTXNUM_340) |
				((dwTexCoorNum + dwTexCoorExtNum) << SHT_TXNUM_340);


	
	dwAlphaBlend = (VAL_CBLSRC_One_340 << 4) | VAL_CBLDST_InvSrc_Alpha_340;
	dwBlendMode = INI_BlendMode_340 |dwAlphaBlend;
	float fAlpha = ((float)alpha)/255.0f;
	float fRed = ((float)red)/255.0f;
	float fGreen = ((float)green)/255.0f;
	float fBlue = ((float)blue)/255.0f;
	dwBlendCst = ((Float2FixedS7(*(unsigned long*)(&fAlpha)) << 24) |  /* A */
                                  (Float2FixedS7(*(unsigned long*)(&fRed)) << 16)  |   /* R */
                                  (Float2FixedS7(*(unsigned long*)(&fGreen)) << 8) |   /* G */
                                  (Float2FixedS7(*(unsigned long*)(&fBlue))));          /* B */



	dwInstNum = 2;
	dwDstNum = 1;
	dwPSSet = VAL_PSReName_NormalMode | MSK_PS_ElementGamma_340 | VAL_PS_PixelNum64_340 | (dwInstNum -1);


        InstIndex = 0;	
	dwCount = dwInstNum * PS_INST_DWSIZE;
	PS_Packing(InstIndex, MSK_PS_TEXLD, PS_DstR(2, MSK_PS_DstRGBA), PS_SrcS(0), PS_SrcR(0,MSK_PS_SrcNoSwizzle),MSK_PS_SrcNULL)
  	PS_Packing(InstIndex, MSK_PS_MOV | MSK_PS_ModifierSat, PS_DstColor(0), PS_SrcR(2,MSK_PS_SrcNoSwizzle), MSK_PS_SrcNULL, MSK_PS_SrcNULL)


	dwTexPitch = pitch << 2;
	
	
	
	dwExt = (dwTexCoorExtNum) ? 1 : 0;
	TexCrdIdx = (dwExt) ? (dwTexCoorNum*3 + 8 + dwTexCoorExtNum) : dwTexCoorNum;


	dwExtDWNum = (dwTexCoorExtNum == 3) ? 10 : (dwTexCoorExtNum*4);


	/* For Init_Read */
	PSSet1 = 0x0 | MSK_EGenTXInitR;
	PSInitRead[0] = 0x0 | (0x0<<24) | (0x0<<16) | 0x2f;
	for(i=1; i<8 ; i++)	PSInitRead[i]=0x0;	


	/*Copy the Source*/
	/* Don't need source for clear and dest */
	if(!docopy) return TRUE;

	dstPtr = (CARD32*)(pSiS->FbBase + (pSiS->AccelLinearScratch->offset << sbppshift));

	if(pSiS->alphaBlitBusy) {
	   pSiS->alphaBlitBusy = FALSE;
	   SiSIdle
	}

	if(alpha == 0xffff) {

	   while(height--) {
	      for(x = 0; x < width; x++) {
	         myalpha = alphaPtr[x];
	         dstPtr[x] = (renderaccelarray[(red & 0xff00)+myalpha] << 16)  |
			     (renderaccelarray[(green & 0xff00) + myalpha] << 8) |
			     renderaccelarray[(blue & 0xff00) + myalpha]         |
			     myalpha << 24;
	      }
	      dstPtr += pitch;
	      alphaPtr += alphaPitch;
	   }

	} else {

	   alpha &= 0xff00;

	   while(height--) {
	      for(x = 0; x < width; x++) {
	         myalpha = alphaPtr[x];
	         dstPtr[x] = (renderaccelarray[alpha + myalpha] << 24) |
			     (renderaccelarray[(red & 0xff00) + myalpha] << 16)   |
			     (renderaccelarray[(green & 0xff00) + myalpha] << 8)  |
			     renderaccelarray[(blue & 0xff00) + myalpha];
	      }
	      dstPtr += pitch;
	      alphaPtr += alphaPitch;
	   }

	}


	
	SiS3DClearTexCache
	SiS3DEnableSet(dwEnable0, dwEnable1, dwEnable2)
	SiS3DFrontEnableSet(dwFrontEnable2)
	SiS3DSetupDestination0Set(dwSet, 0x00ffffff)
	SiS3DSetupIdentityT1WVInvWV
	SiS3DSetupIdentityT2
	SiS3DSetupTextureSet(dwTexSet)
	SiS3DSetupAlphaBlend(dwBlendMode, dwBlendCst)
	SiS3DSetupShaderMRT(dwPSSet, dwDstNum)
	SiS3DSetupInitRead0(PSSet1,PSInitRead[0])
	SiS3DSetupPixelInst(dwCount, dwInst)
	SiS3DSetupBackCoordinate(dwTexCoorSet)
	SiS3DSetupTexture0Pitch(GetTexturePitch(dwTexPitch))
	SiS3DSetupDimLight(dwTexCoordDim)
	SiS3DSetupVertexVector(TexCrdIdx)
	SiS3DPrimitiveSet
	SiS3DSetupStream(dwTexCoorNum, dwTexCoorExtNum, dwExtDWNum)
	SiS3DSetupSwapColor



	return TRUE;
}




void
SiSSubsequentCPUToScreenTexture3D(ScrnInfoPtr pScrn,
			int dst_x, int dst_y,
			int src_x, int src_y,
			int width, int height)
{


	SISPtr pSiS = SISPTR(pScrn);
	int i;
	unsigned long dwDstAddr;
	unsigned long dwClipTB, dwClipLR;
	unsigned long dwTexSet0, dwTexSet1, dwTexDepth, dwTexFormat, dwTexSize, dwTexAddr;
	float x1, y1, x2, y2, u1, u2, v1, v2, fzero, fone;
	CoordVectData vect[4];
	CARD16 EngineId;

	/*
	width = 1000;
	height = 700;
	dst_x = dst_y = 10;
	*/
	/*src_x = src_y = 0;*/
	
	dwTexAddr = (unsigned long)pSiS->AccelLinearScratch->offset<< 1;
	if(pScrn->bitsPerPixel == 32) dwTexAddr <<= 1;

#ifdef ACCELDEBUG_3D
	xf86DrvMsg(0, X_INFO, "FIRE: srcbase %x sx %d sy %d dx %d dy %d w %d h %d\n",\
		dwTexAddr, src_x, src_y, dst_x, dst_y, width, height);
#endif

	dwDstAddr = 0;
	if((dst_y >= pScrn->virtualY) || (dst_y >= 2048)) {
	   dwDstAddr = pSiS->scrnOffset * dst_y;
	   dst_y = 0;
	}
	dwTexAddr += FBOFFSET;
	dwDstAddr += FBOFFSET;

	dwClipTB = (dst_y<<13)|(dst_y + height);
	dwClipLR = (dst_x<<13)|(dst_x + width);

	Bool bPower2 = IsPower2((unsigned long)width) && IsPower2((unsigned long)height);
	dwTexSet0 = bPower2 ? 0 : MSK_TXNonPwdTwo_340;
	dwTexSet1 = VAL_Tex_MinNearest_340|VAL_Tex_MagNearest_340/* |VAL_TxL0InSys_340*/;
	dwTexDepth = dwTexFormat = VAL_Tex_A8R8G8B8_340;
	dwTexSize = (width<<SHT_TXW_340)|height;



	x1 = (float)dst_x/*-0.5f*/;
	y1 = (float)dst_y - 0.5f;
	x2 = (float)(dst_x + width)/*-0.5f*/;
	y2 = (float)(dst_y + height) - 0.5f;
	u1 = ((float)src_x) / (float)width;
	v1 = ((float)src_y) / (float)height;
	u2 = 1.0f; 
	v2 = 1.0f;
	fzero = 0.0f;
	fone = 1.0f;


	vect[0].sx = vect[1].sx = Float2Fixed(*(unsigned long*)&x1,13,4);
	vect[2].sx = vect[3].sx = Float2Fixed(*(unsigned long*)&x2,13,4);
	vect[0].sy = vect[2].sy = Float2Fixed(*(unsigned long*)&y2,13,4);
	vect[1].sy = vect[3].sy = Float2Fixed(*(unsigned long*)&y1,13,4);
	vect[0].sz = vect[1].sz = vect[2].sz = vect[3].sz = *(unsigned long*)&fzero;

	vect[0].tu = vect[1].tu = *(unsigned long*)&u1; 
	vect[2].tu = vect[3].tu = *(unsigned long*)&u2; 
	vect[0].tv = vect[2].tv = *(unsigned long*)&v2; 
	vect[1].tv = vect[3].tv = *(unsigned long*)&v1;

	vect[0].u = vect[1].u = vect[2].u = vect[3].u = vect[0].v = vect[1].v = vect[2].v= vect[3].v =
	vect[0].m = vect[1].m = vect[2].m = vect[3].m = vect[0].n = vect[1].n= vect[2].n= vect[3].n=*(unsigned long*)&fone;

	
	EngineId = REG_3D_EngineId_671;
	

	SiS3DSetupDestination0Addr(dwDstAddr)
	SiS3DSetupClippingRange(dwClipTB, dwClipLR)
	SiS3DTexture0Setting(dwTexSet0, dwTexSet1, dwTexDepth, dwTexSize, dwTexAddr)
	SiS3DSetupVertexData(vect)
	SiS3DListEnd(EngineId, dwStamp++)

}
