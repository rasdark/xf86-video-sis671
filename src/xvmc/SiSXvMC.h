/***************************************************************************

Copyright 2007 SiS Corporation.  All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sub license, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice (including the
next paragraph) shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
IN NO EVENT SHALL INTEL, AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/

/***************************************************************************
 * SiSXvMC.h: MC Driver SiS includes
 *
 * Authors:
 *      Chaoyu Chen <chaoyu_chen@sis.com>
 *      Ming-Ru Li  <mark_li@sis.com>
 *
 *
 ***************************************************************************/

#define XVMC_DEBUG(x)

#include "xf86drm.h"
#include <X11/Xlibint.h>

#define CMD_BUFFER_MAX 8

typedef struct {
  drm_context_t drmcontext;
  drm_context_t AGPHandle;
  unsigned long AGPSize;
  drm_handle_t MMIOHandle;
  unsigned long MMIOSize;
  unsigned long FBhandle;
  unsigned long FBSize;
  unsigned long ChipID;
  unsigned long HDisplay;
  unsigned long VDisplay;
  char busIdString[10];
  char pad[2];
} SiSXvMCCreateContextRec;


typedef struct {
  unsigned int offsets[3];
  unsigned int MyNum;
} SiSXvMCCreateSurfaceRec;


/***************************************************************************
//  SiSOverlayRec: Structure that is used to reference the overlay
//  register memory. A SiSOverlayRecPtr is set to the address of the
//  allocated overlay registers.
***************************************************************************/
typedef struct _SiSOverlayRec {
  int x, x2, y,y2;
  uint window_width,window_height;
  unsigned long HDisplay, VDisplay; /* current  mode (resolution) */
  unsigned int startx, starty;
  CARD16 HUSF, VUSF;
  CARD8 IntBit, wHPre;
  void* privContext;
  float tap_scale, tap_scale_old;
  short srcx, srcy;
  unsigned short srcw, srch;

  /* values differing from chips nature*/
  int PitchAlignmentMask;
  int AdddressHighestBits;
  int AddressShiftNum;
  int  havetapscaler;
  int SubpShift;
  int SubPitchShiftMask;
} SiSOverlayRec, *SiSOverlayRecPtr;

/***************************************************************************
// SiSXvMCDrmMap: Holds the data about the DRM maps
***************************************************************************/
typedef struct _SiSXvMCDrmMap {
  drm_handle_t offset; /* handle */
  drmAddress address;
  unsigned long size;
} SiSXvMCDrmMap, *SiSXvMCDrmMapPtr;


/***************************************************************************
//  SiSXvMCContext: Private Context data referenced via the privData
//  pointer in the XvMCContext structure.
***************************************************************************/
typedef struct _SiSXvMCContext {
   int fd;   /* File descriptor for /dev/dri */
   int ref; /* reference number = surface num + subpicture num*/
   int lock; /* for drmlock */
   int ChipID;
   
   /* infomation from X-server */
   SiSXvMCDrmMap agp_map;
   SiSXvMCDrmMap mmio_map;
   SiSXvMCDrmMap fb_map;
   drm_context_t drmcontext;
   char busIdString[10]; /* PCI:0:1:0 or PCI:0:2:0 */

   /* attributions about MPEG */
   int DecodeMode; /* slice or mc only. */
   int CmdBufSize;
   int AGPHeap; /* AGP or video-ram */
   int SurfaceNum;
   unsigned long CmdBuf[CMD_BUFFER_MAX];
   int FrameBufShift;
   
   /* attrbutions about overlay(displaying) */
   SiSOverlayRec *pOverlay;
   Atom xv_colorkey;
   Atom xv_brightness;
   Atom xv_contrast;
   Atom xv_saturation;
   int brightness;
   int saturation;
   int contrast;
   int colorkey;

   /* flags */
   unsigned int last_render;
} SiSXvMCContext;


/***************************************************************************
//  SiSXvMCSurface: Private data structure for each XvMCSurface. This
//  structure is referenced by the privData pointer in the XvMCSurface
//  structure.
***************************************************************************/
typedef struct _SiSXvMCSurface {
   
   unsigned int MyNum;
   unsigned long MyBuff;/* AGP Cmd Buff */
   unsigned long MyOffset; /*AGP offset */
   unsigned short FrameType; /* I, P, or B Frame*/
   unsigned int offsets[3];/* Y, U, V offsets of AGP */ 
   unsigned long CurrentEntry;/* Cmd buffer pointer */
   unsigned short pict_struct;
   int forRefer, BackRefer;
   void* xvmcsurface; /* XvMCSurface* */

   /* other structure */
   SiSXvMCContext *privContext;
   void *Subp;
   
   /* flags */
   unsigned int last_render;
   unsigned short status;
   int DisplayingAskCounter; /* count the times showing displaying */
} SiSXvMCSurface;

/* Surface Status */
#define SurfaceIdle 	0
#define SurfaceRendering 	1
#define SurfaceDisplaying		2

/***************************************************************************
// i810XvMCSubpicture: Private data structure for each XvMCSubpicture. This
//  structure is referenced by the privData pointer in the XvMCSubpicture
//  structure.
***************************************************************************/
typedef struct _SiSXvMCSubpicture {
   /* infomation from X-server */
   drmAddress data;
   unsigned int pitch;
   unsigned long offset;


   
   CARD8 palette[16][2];
   XID formatID;
   int status;
   SiSXvMCContext *privContext;
} SiSXvMCSubpicture;


/***************************************************************************
// drm_i810_mc_t: Structure used by mc dispatch ioctl.
// NOTE: If you change this structure you will have to change the equiv.
//  structure in the kernel.
***************************************************************************/
typedef struct _drm_sis_mc {
  int idx;		/* buffer index */
  int used;		/* nr bytes in use */
  int num_blocks;         /* number of GFXBlocks */
  int *length;	        /* List of lengths for GFXBlocks */
  unsigned int last_render; /* Last render request */
} drm_sis_mc_t;


/* decode mode */
#define     HWMODE_MCONLY               0x001l
#define     HWMODE_ADVANCED             0x002l
#define     HWMODE_H264               0x004l


/* Subpicture fourcc */
#define FOURCC_IA44 0x34344149


/* SR */
#define Index_SR_Module_Enable		0x1e


/* CR */
/*select MPEG data source From AGP or FLB */
#define  Index_CR_MPEG_Data_Source		0x46


/************************************************/
/*					MMIO  						*/
/************************************************/

/* mmio registers for video */
#define REG_PRIM_CRT_COUNTER    0x8514



/* Clcok Static and Dynamic Control and Fire Command enable */
#define    REG_CLK_CMD    			0x8704	

/* starting address od the system buffer in 128-bit unit*/
#define    REG_BUFFER_CMD		0x8708

/* motion compensation enable/disable flag */
#define    REG_VIDEO_FLIP_CMD	0x870C

/* mode selection and module off/on flag */
#define    REG_MODE_SELECT     	0x8710

/* Frame Buffer Indicator Set Register */
#define	REG_BUF_DEC_MASK		0x8714

/* Frame Buffer Indicator Register */
#define	REG_BUF_DEC_FLAG		0x8718

/* Video Playback Status checking On/Off flag */
#define	REG_VID_CHKC_FLAG		0x871c

/* line pitch of luminance and chrominance buffer */
#define     REG_MPEG_PITCH      	0x8720

/* starting address of each frame buffer in 128-bit unit */
#define     REG_BUFFER_Y0       0x8724
#define     REG_BUFFER_CB0      0x8728
#define     REG_BUFFER_CR0      0x872C
#define     REG_BUFFER_Y1       0x8730
#define     REG_BUFFER_CB1      0x8734
#define     REG_BUFFER_CR1      0x8738
#define     REG_BUFFER_Y2       0x873C
#define     REG_BUFFER_CB2      0x8740
#define     REG_BUFFER_CR2      0x8744
#define     REG_BUFFER_Y3       0x8748
#define     REG_BUFFER_CB3      0x874C
#define     REG_BUFFER_CR3      0x8750
#define     REG_BUFFER_Y4       0x877c
#define     REG_BUFFER_CB4      0x8780
#define     REG_BUFFER_CR4      0x8784
#define     REG_BUFFER_Y5       0x8788
#define     REG_BUFFER_CB5      0x878c
#define     REG_BUFFER_CR5      0x8790

/* MPEG decode status register (read only) */
#define     REG_MPEG_STATUS     0x8754

/* Picture Layer Parameter (read only) */
#define     REG_MPEG_PLP0       0x8758
#define     REG_MPEG_PLP1       0x875C
#define     REG_MPEG_PLP2       0x8760

/* Macroblock Header (read only) */
#define     REG_MPEG_MBT        	0x8764
#define     REG_MPEG_FORXY      	0x8768
#define     REG_MPEG_BACKXY     	0x876C
#define     REG_MPEG_FORBXY     	0x8770
#define     REG_MPEG_BACKBXY    	0x8774

/* system buffer status (read only) */
#define     REG_MPEG_CMDBUFFG   	0x8778



/* Video registers (300/315/330/340 series only)  --------------- */
#define  Index_VI_Passwd                        0x00

/* Video overlay horizontal start/end, unit=screen pixels */
#define  Index_VI_Win_Hor_Disp_Start_Low        0x01
#define  Index_VI_Win_Hor_Disp_End_Low          0x02
#define  Index_VI_Win_Hor_Over                  0x03 /* Overflow */

/* Video overlay vertical start/end, unit=screen pixels */
#define  Index_VI_Win_Ver_Disp_Start_Low        0x04
#define  Index_VI_Win_Ver_Disp_End_Low          0x05
#define  Index_VI_Win_Ver_Over                  0x06 /* Overflow */

/* Y Plane (4:2:0) or YUV (4:2:2) buffer start address, unit=word */
#define  Index_VI_Disp_Y_Buf_Start_Low          0x07
#define  Index_VI_Disp_Y_Buf_Start_Middle       0x08
#define  Index_VI_Disp_Y_Buf_Start_High         0x09

/* U Plane (4:2:0) buffer start address, unit=word */
#define  Index_VI_U_Buf_Start_Low               0x0A
#define  Index_VI_U_Buf_Start_Middle            0x0B
#define  Index_VI_U_Buf_Start_High              0x0C

/* V Plane (4:2:0) buffer start address, unit=word */
#define  Index_VI_V_Buf_Start_Low               0x0D
#define  Index_VI_V_Buf_Start_Middle            0x0E
#define  Index_VI_V_Buf_Start_High              0x0F

/* Pitch for Y, UV Planes, unit=word */
#define  Index_VI_Disp_Y_Buf_Pitch_Low          0x10
#define  Index_VI_Disp_UV_Buf_Pitch_Low         0x11
#define  Index_VI_Disp_Y_UV_Buf_Pitch_Middle    0x12

/* What is this ? */
#define  Index_VI_Disp_Y_Buf_Preset_Low         0x13
#define  Index_VI_Disp_Y_Buf_Preset_Middle      0x14

#define  Index_VI_UV_Buf_Preset_Low             0x15
#define  Index_VI_UV_Buf_Preset_Middle          0x16
#define  Index_VI_Disp_Y_UV_Buf_Preset_High     0x17

/* Scaling control registers */
#define  Index_VI_Hor_Post_Up_Scale_Low         0x18
#define  Index_VI_Hor_Post_Up_Scale_High        0x19
#define  Index_VI_Ver_Up_Scale_Low              0x1A
#define  Index_VI_Ver_Up_Scale_High             0x1B
#define  Index_VI_Scale_Control                 0x1C

/* Playback line buffer control */
#define  Index_VI_Play_Threshold_Low            0x1D
#define  Index_VI_Play_Threshold_High           0x1E
#define  Index_VI_Line_Buffer_Size              0x1F

/* Destination color key */
#define  Index_VI_Overlay_ColorKey_Red_Min      0x20
#define  Index_VI_Overlay_ColorKey_Green_Min    0x21
#define  Index_VI_Overlay_ColorKey_Blue_Min     0x22
#define  Index_VI_Overlay_ColorKey_Red_Max      0x23
#define  Index_VI_Overlay_ColorKey_Green_Max    0x24
#define  Index_VI_Overlay_ColorKey_Blue_Max     0x25

/* Source color key, YUV color space */
#define  Index_VI_Overlay_ChromaKey_Red_Y_Min   0x26
#define  Index_VI_Overlay_ChromaKey_Green_U_Min 0x27
#define  Index_VI_Overlay_ChromaKey_Blue_V_Min  0x28
#define  Index_VI_Overlay_ChromaKey_Red_Y_Max   0x29
#define  Index_VI_Overlay_ChromaKey_Green_U_Max 0x2A
#define  Index_VI_Overlay_ChromaKey_Blue_V_Max  0x2B

/* Contrast enhancement and brightness control */
#define  Index_VI_Contrast_Factor               0x2C
#define  Index_VI_Brightness                    0x2D
#define  Index_VI_Contrast_Enh_Ctrl             0x2E

#define  Index_VI_Key_Overlay_OP                0x2F

#define  Index_VI_Control_Misc0                 0x30
#define  Index_VI_Control_Misc1                 0x31
#define  Index_VI_Control_Misc2                 0x32

/* Subpicture registers */
#define  Index_VI_SubPict_Buf_Start_Low		0x33
#define  Index_VI_SubPict_Buf_Start_Middle	0x34
#define  Index_VI_SubPict_Buf_Start_High	0x35

/* What is this ? */
#define  Index_VI_SubPict_Buf_Preset_Low	0x36
#define  Index_VI_SubPict_Buf_Preset_Middle	0x37

/* Subpicture pitch, unit=16 bytes */
#define  Index_VI_SubPict_Buf_Pitch		0x38

/* Subpicture scaling control */
#define  Index_VI_SubPict_Hor_Scale_Low		0x39
#define  Index_VI_SubPict_Hor_Scale_High		0x3A
#define  Index_VI_SubPict_Vert_Scale_Low		0x3B
#define  Index_VI_SubPict_Vert_Scale_High	0x3C

#define  Index_VI_SubPict_Scale_Control		0x3D
/* (0x40 = enable/disable subpicture) */

/* Subpicture line buffer control */
#define  Index_VI_SubPict_Threshold		0x3E

/* What is this? */
#define  Index_VI_FIFO_Max			0x3F

/* Subpicture palette; 16 colors, total 32 bytes address space */
#define  Index_VI_SubPict_Pal_Base_Low		0x40
#define  Index_VI_SubPict_Pal_Base_High		0x41

/* I wish I knew how to use these ... */
#define  Index_MPEG_Read_Ctrl0                  0x60	/* MPEG auto flip */
#define  Index_MPEG_Read_Ctrl1                  0x61	/* MPEG auto flip */
#define  Index_MPEG_Read_Ctrl2                  0x62	/* MPEG auto flip */
#define  Index_MPEG_Read_Ctrl3                  0x63	/* MPEG auto flip */

/* MPEG AutoFlip scale */
#define  Index_MPEG_Ver_Up_Scale_Low            0x64
#define  Index_MPEG_Ver_Up_Scale_High           0x65

#define  Index_MPEG_Y_Buf_Preset_Low		0x66
#define  Index_MPEG_Y_Buf_Preset_Middle		0x67
#define  Index_MPEG_UV_Buf_Preset_Low		0x68
#define  Index_MPEG_UV_Buf_Preset_Middle	0x69
#define  Index_MPEG_Y_UV_Buf_Preset_High	0x6A

/* The following registers only exist on the 315/330/340 series */

/* Bit 16:24 of Y_U_V buf start address */
#define  Index_VI_Y_Buf_Start_Over		0x6B
#define  Index_VI_U_Buf_Start_Over		0x6C
#define  Index_VI_V_Buf_Start_Over		0x6D

#define  Index_VI_Disp_Y_Buf_Pitch_High		0x6E
#define  Index_VI_Disp_UV_Buf_Pitch_High	0x6F

/* Hue and saturation */
#define	 Index_VI_Hue				0x70
#define  Index_VI_Saturation			0x71

#define  Index_VI_SubPict_Start_Over		0x72
#define  Index_VI_SubPict_Buf_Pitch_High	0x73

#define  Index_VI_Control_Misc3			0x74

/* 340 and later: */
/* DDA registers 0x75 - 0xb4 */
#define  Horizontal_6Tap_DDA_WeightingMatrix_Index     0x75
#define  Horizontal_6Tap_DDA_WeightingMatrix_Value      0x76
#define  Vertical_4Tap_DDA_WeightingMatrix_Index          0x77
#define  Vertical_4Tap_DDA_WeightingMatrix_Value          0x78
#define  Index_VI_Control_Misc4                 0x79

#define  Index_VI_SubPict_Start_Pitch_Ext		0x7a
#define  Index_VI_SubPict_Buf_Preset_Ext 	0x7b

/*MCE misc regist*/
#define  Index_VI_MCE_Control_Misc1                 0x8d

/* threshold high 0xb5, 0xb6 */
#define  Index_VI_Threshold_Ext_Low			0xb5
#define  Index_VI_Threshold_Ext_High			0xb6

#define  Index_VI_Line_Buffer_Size_High		0xb7
#define Index_VI_SubPict_Threshold_Ext		0xb8

#define Source_VertLine_Number_Low			0xb9
#define Source_VertLine_Number_High			0xba

#define Index_VI_SubPict_Format			0xbc


/* Locking Macros lightweight lock used to prevent relocking */
#define SIS_LOCK(c,f)                     \
  if(!c->lock) {                           \
    drmGetLock(c->fd, c->drmcontext, f);   \
  }                                        \
  c->lock++;

#define SIS_UNLOCK(c)                     \
  c->lock--;                               \
  if(!c->lock) {                           \
    drmUnlock(c->fd, c->drmcontext);       \
  }
  

/*
  Definitions for temporary wire protocol hooks to be replaced
  when a HW independent libXvMC is created.
*/
extern Status _xvmc_create_context(Display *dpy, XvMCContext *context,
				   int *priv_count, uint **priv_data);

extern Status _xvmc_destroy_context(Display *dpy, XvMCContext *context);

extern Status _xvmc_create_surface(Display *dpy, XvMCContext *context,
				   XvMCSurface *surface, int *priv_count,
				   uint **priv_data);

extern Status _xvmc_destroy_surface(Display *dpy, XvMCSurface *surface);

extern Status  _xvmc_create_subpicture(Display *dpy, XvMCContext *context,
				       XvMCSubpicture *subpicture,
				       int *priv_count, uint **priv_data);

extern Status   _xvmc_destroy_subpicture(Display *dpy,
					 XvMCSubpicture *subpicture);


/*
  Prototypes
*/
void sis_free_privContext(SiSXvMCContext *pSiSXvMC);

#define OverlayMapSiZe 4096


/* IO port seeting */
#define	VIDEOOFFSET     0x02
#define	SROFFSET     0x44
#define	CROFFSET     0x54
#define	outSISREG32(base,val)		outl(val,base)
#define	inSISREG32(base)		inl(base)
#define 	outSISVidReg(base, idx, val)	do { \
			outb(idx,base); outb(val,(base)+1); \
			} while (0)

#define setreg(base,idx,data)		   	\
		    do {				   	\
		      outb(idx,base);			\
		      outb(data, (base)+1);		\
		    } while(0)
		    
#define setregmask(base,idx,data,mask)		   	\
		    do {				   	\
		      unsigned char __Temp;		   		\
		      outb(idx,base);			\
		      __Temp = (inb((base)+1)) & (~(mask));\
		      __Temp |= ((data) & (mask));	   	\
		      outb( __Temp, (base)+1);		\
		    } while(0)
		    
#define getreg(base,idx,var)   		\
		    do { 			\
                    outb(idx, base); 	\
		      var = inb((base)+1);	\
                    } while (0)
		    
#define outMMIO32(base,addr,val)  \
	 *(volatile CARD32 *)(base + (addr))= (val)

#define inMMIO32(base,addr)\
         *(volatile CARD32 *)(base + (addr))


#if 0
typedef struct {
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

    //BoxRec  dstBox;

    CARD32  PSY;
    CARD32  PSV;
    CARD32  PSU;

    CARD16  SCREENheight;

    CARD16  lineBufSize;

    //DisplayModePtr  currentmode;

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
#endif

typedef struct {
	int context;
	unsigned long offset;
	unsigned long size;
	void *free;
} drm_sis_mem_t;

#define DRM_SIS_FB_ALLOC	0x04
#define DRM_SIS_FB_FREE		0x05
#define DRM_SIS_FLIP		0x08
#define DRM_SIS_FLIP_INIT	0x09
#define DRM_SIS_FLIP_FINAL	0x10
#define DRM_SIS_AGP_INIT	0x13
#define DRM_SIS_AGP_ALLOC	0x14
#define DRM_SIS_AGP_FREE	0x15
#define DRM_SIS_FB_INIT		0x16


/*******************************************************************************/
/*                                         Motion Compensation data structure                                          */
/*******************************************************************************/
typedef  unsigned char   BYTE, *LPBYTE;
typedef  unsigned short  WORD, *LPWORD;
/*
 * MC Only Mode structure
 */
/*
// structure for picture layer parameter
*/
typedef struct
{
    BYTE    pict_type;          /* D[2:0]   picture coding type */
    BYTE    pict_struct;        /* D[1:0]   picture structure */
    union
    {
        BYTE    fix_buf;        /* D0       the reference frame of the second field in P-frame 
        					when field picture */
        BYTE    secondfield;    /* D0       second field flag, for slice layer mode */
    };
    BYTE    decframe;           /* D[2:0]   current decoding buffer */
    BYTE    forframe;           /* D[2:0]   forward reference buffer */
    BYTE    backframe;        /* D[2:0]   backward reference buffer */
    BYTE    mb_width;           /* D[6:0]   mb_width - 1 */
    union
    {
        BYTE    mb_height;      /* D[6:0]   mb_height - 1 */
        BYTE    hreserved;      /* D[7:0]   reserved, for slice layer mode */
    };

    BYTE    pfframe;            /* D[2:0]   page flip buffer */
    BYTE    showframe;          /* D0       show frame or field */
    BYTE    topfirst;           /* D0       top first or not */
    BYTE    top_no;             /* D[3:0]   frame/top field display number */
    BYTE    bottom_no;          /* D[2:0]   bottom field display number */
    BYTE    pic_no;             /* D[3:0]   picture serial number in a GOP */
    BYTE    Reserved;      		/* D[7:0]   Reserved byte in 315 */
    BYTE    mode;               /* D0       page flipping only flag */
                                         /* D1 decoding only flag */
                                         /* D2 check page flip buffer selection before decoding */
} PICTUREPARA, *LPPICTUREPARA;

/*
// structure for macroblocks in I_TYPE picture
*/
typedef struct _IMBHEADER
{
    WORD    cbp;                /* D[5:0]   coded block pattern */
    WORD    mc_flag;            /* D0       intra macroblock */
                                /* D3       forward MC */
                                /* D2       backward MC */
    BYTE    mcmode;             /* D[1:0]   prediction type */
    BYTE    dcttype;            /* D0       frame/field DCT type */
    WORD    reserved;
    WORD    filflga;            /* D[3:0]   subpixel flag of the 1st MV */
    WORD    filflgb;            /* D[3:0]   subpixel flag of the 2nd MV */
    WORD    filflgc;            /* D[3:0]   subpixel flag of the 3rd MV */
    WORD    filflgd;            /* D[3:0]   subpixel flag of the 4th MV */
} IMBHEADER, *LPIMBHEADER;



/*
// structure for macroblocks in PB_TYPE picture
*/
typedef struct _MBHEADER
{
    WORD    cbp;           /* D[5:0]   coded block pattern */
    WORD    mc_flag;    /* D0       intra macroblock */
                                   /* D3       forward MC */
                                   /* D2       backward MC */
    BYTE    mcmode;     /* D[1:0]   prediction type */
    BYTE    dcttype;       /* D0       frame/field DCT type */
    WORD    reserved;
    WORD    filflga;            /* D[3:0]   subpixel flag of the 1st MV */
    WORD    filflgb;            /* D[3:0]   subpixel flag of the 2nd MV */
    WORD    filflgc;            /* D[3:0]   subpixel flag of the 3rd MV */
    WORD    filflgd;            /* D[3:0]   subpixel flag of the 4th MV */
    /* the 1st motion vector */
    WORD    forvx;     /* D[10:0]  x-direction vector of the 1st MV */
                                /* D[12:11] buffer selection of the 1st MV */
                                /* D15      x-direction chrominance vector 
                                /*          add one flag of the 1st MV */
    WORD    forvy;     /* D[10:0]  y-direction vector of the 1st MV */
                                /* D15      y-direction chronminance vector 
                                                add one flag of the 1st MV */
    /* the 2nd motion vector */
    WORD    backvx;  /* D[10:0]  x-direction vector of the 2nd MV */
                                /* D[12:11] buffer selection of the 2nd MV */
                                /* D15      x-direction chrominance vector 
                                /*          add one flag of the 2nd MV */
    WORD    backvy;  /* D[10:0]  y-direction vector of the 2nd MV */
                                /* D15      y-direction chronminance vector
                                                add one flag of the 2nd MV */
    /* the 3rd motion vector */
    WORD    forbvx;   /* D[10:0]  x-direction vector of the 3rd MV */
                                /* D[12:11] buffer selection of the 3rd MV */
                                /* D15      x-direction chrominance vector 
                                /*          add one flag of the 3rd MV */
    WORD    forbvy;   /* D[10:0]  y-direction vector of the 3rd MV */
                                /* D15      y-direction chronminance vector
                                                add one flag of the 3rd MV */
    /* the 4th motion vector */
    WORD    backbvx;/* D[10:0]  x-direction vector of the 4th MV */
                                /* D[12:11] buffer selection of the 4th MV */
                                /* D15      x-direction chrominance vector 
                                /*          add one flag of the 4th MV */
    WORD    backbvy;/* D[10:0]  y-direction vector of the 4th MV */
                                /* D15      y-direction chronminance vector
                                                add one flag of the 4th MV */
} MBHEADER, *LPMBHEADER;



/*****************************************************************************\
* MPEG-releated definition                                                    *
\*****************************************************************************/
/* picture coding type */
#define     I_TYPE          0x1
#define     P_TYPE          0x2
#define     B_TYPE          0x3


/* picture structure */
#define     TOP_FIELD       0x1
#define     BOTTOM_FIELD    0x2
#define     FRAME_PICTURE   0x3


/* macroblock type */
#define     MB_INTRA        1
#define     MB_PATTERN      2
#define     MB_BACKWARD     4
#define     MB_FORWARD      8
#define     MB_QUANT        16
#define     MB_WEIGHT       32
#define     MB_CLASS4       64


/* motion_type */
#define     MC_FIELD        1
#define     MC_FRAME        2
#define     MC_16X8         2
#define     MC_DMV          3

#define FrameBufferNum 6
#define I_BlockSize 64  /* 64 bytes */
#define BlockSize 64*2 /* 64 shorts = 128 bytes */

/*****************************************************************************\
* SiS HW-releated definition                                                  *
\*****************************************************************************/
#define     ADD_ONE_FLAG                0x8000
#define     REFERENCE_TOP_FIELD         0x0000
#define     REFERENCE_BOTTOM_FIELD      0x0001
