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

/*************************************************************************
** File SiSXvMC.c
**
** Authors:
**      Chaoyu Chen <chaoyu_chen@sis.com>
**      Ming-Ru Li  <mark_li@sis.com>
**
**
***************************************************************************/
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>

#include <sys/ioctl.h>
#include <X11/Xlibint.h>
#include <fourcc.h>
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>
#include <X11/extensions/XvMC.h>
#include <X11/extensions/XvMClib.h>
#include <asm/io.h>
#include "SiSXvMC.h"


#define XVMCDEBUG
#define SUBPDEBUG

/* Hardware data dump */
/*
#define XvMCHWDATADUMP  
*/

static int error_base;
static int event_base;
static drmAddress mmioAddress;
static drmAddress agpAddress;
static drmAddress fbAddress;
SiSXvMCSurface* SurfaceList[FrameBufferNum];

#ifdef XvMCHWDATADUMP
FILE* dump;
#endif

/***************************************************************************
// Function: free_privContext
// Description: Free's the private context structure if the reference
//  count is 0.
***************************************************************************/

void sis_free_privContext(SiSXvMCContext *pSiSXvMC) {

   int errorno;

   SIS_LOCK(pSiSXvMC, DRM_LOCK_QUIESCENT);  
   if(!pSiSXvMC->ref--){
      errorno = drmUnmap(pSiSXvMC->mmio_map.address, pSiSXvMC->mmio_map.size);
      if(errorno != 0)	printf("[XvMC] Ummap mmio failed! (ErrorNo: %d)\n", errorno);
#ifdef XVMCDEBUG
      else		printf("[XvMC] Ummap mmio Successfully!\n");
#endif

      errorno = drmUnmap(pSiSXvMC->agp_map.address, pSiSXvMC->agp_map.size);
      if(errorno != 0)	printf("[XvMC] Ummap agp failed! (ErrorNo: %d)\n", errorno);
#ifdef XVMCDEBUG
      else		printf("[XvMC] Ummap agp Successfully!\n");
#endif

      errorno = drmUnmap(pSiSXvMC->fb_map.address, pSiSXvMC->fb_map.size);
      if(errorno != 0)	printf("[XvMC] Ummap frame buffer failed! (ErrorNo: %d)\n", errorno);
#ifdef XVMCDEBUG
      else		printf("[XvMC] Ummap frame buffer Successfully!\n");
#endif


      drmClose(pSiSXvMC->fd);
      free(pSiSXvMC);
   }
   SIS_UNLOCK(pSiSXvMC);
}

int ChipDiffering(SiSXvMCContext* pSiSXvMC){

   SiSOverlayRec* pOverlay = pSiSXvMC->pOverlay;
	
   switch(pSiSXvMC->ChipID){
   case 741:
      pSiSXvMC->FrameBufShift = 4;
      pOverlay->PitchAlignmentMask = 7;
      pOverlay->AdddressHighestBits = 3;
      pOverlay->AddressShiftNum = 1;
      pOverlay->SubpShift = 4;
      pOverlay->SubPitchShiftMask = 15; /* 128 alignment */
      pOverlay->havetapscaler = FALSE;
      break;
   case 662:
      pSiSXvMC->FrameBufShift = 4;
      pOverlay->PitchAlignmentMask = 63;
      pOverlay->AdddressHighestBits = 7;
      pOverlay->AddressShiftNum = 1;
      pOverlay->SubpShift = 4;
      pOverlay->SubPitchShiftMask = 63; /* 256 alignment */
      pOverlay->havetapscaler = TRUE;
      break;
   case 671:
      pSiSXvMC->FrameBufShift = 4;
      pOverlay->PitchAlignmentMask = 63;
      pOverlay->AdddressHighestBits = 7;
      pOverlay->AddressShiftNum = 5;
      pOverlay->SubpShift = 5;
      pOverlay->SubPitchShiftMask = 63; /* 256 alignment */
      pOverlay->havetapscaler = TRUE;
      break;
   default:
      printf("[XvMC] Chip Error!This chip does not support XvMC. Stop. \n");
      return -1;
  }

   return 1;

}


/*//////////////////////////////////////////////////////////////////////////////
// IsMPEGEngineIdle
//      check if the MPEG engine is busy or not
//////////////////////////////////////////////////////////////////////////////*/
int
IsMPEGEngineIdle(SiSXvMCContext* pSiSXvMC){

    CARD32 MpegStatus = inMMIO32(mmioAddress, REG_MPEG_STATUS);
    CARD32 CmdBuffer = inMMIO32(mmioAddress, REG_MPEG_CMDBUFFG);

   if(pSiSXvMC->ChipID == 671){
      outMMIO32(mmioAddress, REG_CLK_CMD, 0x2a1);
      while((MpegStatus & 0x200)  != 0x200)
         MpegStatus = inMMIO32(mmioAddress, REG_MPEG_STATUS);
   }
   
#ifdef XVMCDEBUG
    printf("MpegStatus = 0x%x, Cmdbuffer = 0x%x,\n",MpegStatus,CmdBuffer);
#endif

   switch(pSiSXvMC->ChipID){
   case 671:
      if ( ((MpegStatus & 0x3f )  != 0x3f ) ||   /* MCIDLE */
        ((MpegStatus & 0x200)  != 0x200) ||  /* Mpeg queue empty */
        ((CmdBuffer & 0xff) != 0x0  ) )    /* AGP buffer IDLE */
         return FALSE;
      break;
   
   case 741:
   case 662:
      if ( ((MpegStatus & 0x20 )  != 0x20 ) ||   /* MCIDLE */
        ((MpegStatus & 0x200)  != 0x200) ||   /* Mpeg queue empty */
        ((CmdBuffer & 0xff) != 0x0 ) )    /* AGP buffer IDLE */
         return FALSE;
      break;
	  
   default:
      printf("[XvMC] %s: Decode mode or chip type ERROR! Please check.\n", __FUNCTION__);
      return FALSE;
   
   }

    return TRUE;
}


/*//////////////////////////////////////////////////////////////////////////////
// EnableHWMPEG
//      - turn on/off MPEG engine
//  arguments:
//      bFlag - flag to enablg or disable MPEG hardware
//              TRUE: enable hardware
//              FALSE: disable hardware
//////////////////////////////////////////////////////////////////////////////*/
int
EnableHWMPEG (XvMCContext *context, BOOL bFlag)
{
    static int bEnable = FALSE;
    int ulAlignment;
    unsigned short iobase, sr_port, cr_port;
    unsigned short pitch;
    SiSXvMCContext *pSiSXvMC = (SiSXvMCContext *)context->privData;
    int PitchAlignmentMask = pSiSXvMC->pOverlay->PitchAlignmentMask;
    CARD32 HWFunctSel;
    
    /* set user as root */
    if(setuid(0) < 0){
    	printf("[XvMC] %s: Can't set the user root.\n\t Please check the Application mode.\n",
    		__FUNCTION__);
    	return -1;
    }
    if(iopl(3) < 0){
    	printf("[XvMC] %s: Can't set ports available.\n", __FUNCTION__);
    	return -1;
    }

    /* Get Relocated io */ 
    outSISREG32(0xcf8, 0x80010018);
    iobase = inSISREG32(0xcfc) & 0xFFFC;
    sr_port = iobase + SROFFSET;
    cr_port = iobase + CROFFSET;


    if (bFlag){ /* enable MPEG engine */

    if(pSiSXvMC->AGPHeap)  /*use AGP */
		setregmask(cr_port, Index_CR_MPEG_Data_Source, 0x00, 0x01);
    else /*use video-ram */
		setregmask(cr_port, Index_CR_MPEG_Data_Source, 0x01, 0x01);

    /* enable hardware MPEG and all MMIO registers */
    setregmask(sr_port, Index_SR_Module_Enable, 0x01, 0x01);
#if 0
        // turn on MC engine, No CSS, AutoFlip in AGP,
        // We should modify the setting here if slice layer enable

        //330,660,661,662,760,761,761gx,741 <-->  lpMI->chipID=SIS_330
        // 315,325,640,740,650,650M <-->  lpMI->chipID=SIS_325
        // 340,670,770,771 <-->  lpMI->chipID=SIS_340
        switch ( lpMI->chipID )
        {
        case SIS_GlamourII:
            w_REG_MODE_SELECT(0x0814);
            break;

        case SIS_325:
            w_REG_MODE_SELECT(0x10);            
            break;

        case SIS_330:
            w_REG_MODE_SELECT(0x30);
            break;

        case SIS_340: //770~
            //[Super] 3-3-2005
            //if ( lpMI->McOnly)
            if ( ppdev->pEngShareData->ulDecodeMethod == 1)
            {
                MPEGSelect.mcoff = 0;
                MPEGSelect.idctoff = 0;  //IDCT off
                MPEGSelect.modelsel = 1;  //MC mode
                MPEGSelect.encss = 0;
                MPEGSelect.FifoSel = 0;
                MPEGSelect.VLDIDCT3T = 0;

                //[Kevin] 08-26-2005
                if (ppdev->DD.dwEnableNV12)
                    MPEGSelect.NV12Enable= 1;
                else
                    MPEGSelect.NV12Enable= 0;
                //~[Kevin] 08-26-2005

                //MPEGSelect.Reserverd1 = 0;

                MPEGSelect.ReqThr = 8;
                //lichun@2006/08/31, For HW to dynamically change MPEG AGP request threshold in MMIO8710[8:12]
                if (ppdev->DD.dwReqThreshold)
                    MPEGSelect.ReqThr = (ppdev->DD.dwReqThreshold & 0x1F);

                MPEGSelect.Reserverd2 = 0;
                w_REG_MODE_SELECT(MPEGSelect.MPREG_8710H);					
            }

            else
            {
                MPEGSelect.mcoff = 0;
                MPEGSelect.idctoff = 0;  //IDCT on
                MPEGSelect.modelsel = 0;  //Slice Layer mode
                MPEGSelect.encss = 0;
                MPEGSelect.FifoSel = 0;
                MPEGSelect.VLDIDCT3T = 0;

                //[Kevin] 08-26-2005
                if (ppdev->DD.dwEnableNV12)
                    MPEGSelect.NV12Enable= 1;
                else
                    MPEGSelect.NV12Enable= 0;
                //~[Kevin] 08-26-2005

                //MPEGSelect.Reserverd1 = 0;

                MPEGSelect.ReqThr = 8;
                //lichun@2006/08/31, For HW to dynamically change MPEG AGP request threshold in MMIO8710[8:12]
                if (ppdev->DD.dwReqThreshold)
                    MPEGSelect.ReqThr = (ppdev->DD.dwReqThreshold & 0x1F);

                MPEGSelect.Reserverd2 = 0;
                w_REG_MODE_SELECT(MPEGSelect.MPREG_8710H);
            }

            break;
            //~[Super] 3-3-2005

        }//switch ( lpMI->chipID )
#endif
        /* Mode & flag selection */
        switch(pSiSXvMC->ChipID){
        case 671:

            HWFunctSel = inMMIO32(mmioAddress, REG_MODE_SELECT);

            /* set mc type: mc-only or  */
            switch(pSiSXvMC->DecodeMode){
            case HWMODE_ADVANCED:
                HWFunctSel &= ~0x4; /* slice layer */ 
                break;
            case HWMODE_MCONLY:
            default:
                HWFunctSel |= 0x4; /* MC Only */ 
                break;
            }
			
           /* ToDo: support NV12 video format */
		   
            outMMIO32(mmioAddress, REG_MODE_SELECT,  HWFunctSel);
		   
            break;
        case 741:
        case 662:
        default:
            outMMIO32(mmioAddress, REG_MODE_SELECT,  0x30);
            break;
        }
        
        /* Turn off video playback status On/Off check bits*/
        outMMIO32(mmioAddress, REG_VID_CHKC_FLAG,  0x01);
    
        /* Setting pitch */
        switch(context->surface_type_id){
        case FOURCC_YV12:
        default:
    		pitch = (context->width + PitchAlignmentMask) & ~PitchAlignmentMask;
    		break;
        }
        outMMIO32(mmioAddress, REG_MPEG_PITCH,  ((pitch<<12 & 0xffff0000)|(pitch>>3)));

        bEnable = TRUE;
    }
    else{ /* disable MPEG engine */
		
        if (!bEnable)     /* not enabled yet!! */
            return 0;

        /* disable hardware MPEG and all MMIO registers */
        setregmask(sr_port, Index_SR_Module_Enable, 0x00, 0x01);

        bEnable = FALSE;
    }
}


/***************************************************************************
// Function: XvMCCreateContext
// Description: Create a XvMC context for the given surface parameters.
// Arguments:
//   display - Connection to the X server.
//   port - XvPortID to use as avertised by the X connection.
//   surface_type_id - Unique identifier for the Surface type.
//   width - Width of the surfaces.
//   height - Height of the surfaces.
//   flags - one or more of the following
//      XVMC_DIRECT - A direct rendered context is requested.
//
// Notes: surface_type_id and width/height parameters must match those
//        returned by XvMCListSurfaceTypes.
// Returns: Status
***************************************************************************/
Status XvMCCreateContext(Display *display, XvPortID port,
			 int surface_type_id, int width, int height, int flags,
			 XvMCContext *context) {

 
  int i;
  SiSXvMCContext *pSiSXvMC;
  char busIdString[10];
  int priv_count;
  uint *priv_data;
  uint magic;
  Status ret;
  int major, minor;
  SiSXvMCCreateContextRec* pContextRec; 

#ifdef XVMCDEBUG
  printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif

  /* Verify Obvious things first */
  if(context == NULL) {
    return XvMCBadContext;
  }

  if(!(flags & XVMC_DIRECT)) {
    /* Indirect */
    printf("[XvMC] Indirect Rendering not supported!\nUsing Direct.");
  }

  /* FIXME: Check $DISPLAY for legal values here */

  context->surface_type_id = surface_type_id;
  context->width = (unsigned short)width;
  context->height = (unsigned short)height;
  context->flags = flags;
  context->port = port;
  /* 
     Width, Height, and flags are checked against surface_type_id
     and port for validity inside the X server, no need to check
     here.
  */


  /* Allocate private Context data */
  context->privData = (void *)malloc(sizeof(SiSXvMCContext));
  if(!context->privData) {
    printf("[XvMC] Unable to allocate resources for XvMC context.\n");
    return BadAlloc;
  }
  pSiSXvMC = (SiSXvMCContext *)context->privData;
  memset((void*)pSiSXvMC, 0x00, sizeof(SiSXvMCContext));


  /* Verify the XvMC extension exists */
  if(! XvMCQueryExtension(display, &event_base,
			  &error_base)) {
    printf("[XvMC] XvMC Extension is not available!\n");
    return BadAlloc;
  }
  /* Verify XvMC version */
  ret = XvMCQueryVersion(display, &major, &minor);
  if(ret) {
    printf("[XvMC] XvMCQuery Version Failed, unable to determine protocol version\n");
  }
  /* FIXME: Check Major and Minor here */

  /* Check for drm */
  if(! drmAvailable()) {
    printf("[XvMC] Direct Rendering is not avilable on this system!\n");
    return BadAlloc;
  }


  /* initalize the overlay record */
  pSiSXvMC->pOverlay = (SiSOverlayRec*)malloc(sizeof(SiSOverlayRec));
  if(!pSiSXvMC->pOverlay){
  printf("[XvMC] Unable to allocate overlay Record!.\n");
  return BadAlloc;
  }
  memset((void*)pSiSXvMC->pOverlay, 0x00, sizeof(SiSOverlayRec));
  pSiSXvMC->pOverlay->privContext = (void*)pSiSXvMC;

  /* 
     Build the Attribute Atoms, and Initialize the ones that exist
     in Xv.
  */
  pSiSXvMC->xv_colorkey = XInternAtom(display,"XV_COLORKEY",0);
  if(!pSiSXvMC->xv_colorkey) {
    return XvBadPort;
  }
  ret = XvGetPortAttribute(display, port, pSiSXvMC->xv_colorkey,
			   &pSiSXvMC->colorkey);
  if(ret) {
    printf("[XvMC] GetPortAttribute() failed !\n");
    return ret;
  }
  pSiSXvMC->xv_brightness = XInternAtom(display,"XV_BRIGHTNESS",0);
  pSiSXvMC->xv_saturation = XInternAtom(display,"XV_SATURATION",0);
  pSiSXvMC->xv_contrast = XInternAtom(display,"XV_CONTRAST",0);
  pSiSXvMC->brightness = 0;
  pSiSXvMC->saturation = 0x80;  /* 1.0 in 3.7 format */
  pSiSXvMC->contrast = 0x40; /* 1.0 in 3.6 format */

 
  /* Open DRI Device */
  if((pSiSXvMC->fd = drmOpen("sis315",NULL)) < 0) {
    printf("[XvMC] DRM Device for SiS could not be opened.\n");
    free(pSiSXvMC);
    return BadAccess;
  } /* !pSiSXvMC->fd */

  /* Get magic number and put it in privData for passing */
  drmGetMagic(pSiSXvMC->fd,&magic);
  context->flags = (unsigned long)magic;

  /*
    Pass control to the X server to create a drm_context_t for us and
    validate the with/height and flags.
  */
  if((ret = _xvmc_create_context(display, context, &priv_count, &priv_data))) {
    printf("[XvMC] Unable to create XvMC Context.\n");
    return ret;
  }

  /* 
     X server returns a structure like this:
     drm_context_t
     AGPOffset
     AGPSize
     MCOffset
     MCSize
     MMIOHandle
     MMIOSize
     FBOffset
     FBSize
     ChipID
     HDisplay
     VDisplay
     busIdString = 9 char + 1
  */
  if(priv_count != sizeof(SiSXvMCCreateContextRec)) {
    printf("[XvMC] _xvmc_create_context() returned incorrect data size!\n");
    printf("\tExpected %d, got %d\n", sizeof(SiSXvMCCreateContextRec), priv_count);
    _xvmc_destroy_context(display, context);
    free(pSiSXvMC);
    return BadAlloc;
  }

  pContextRec = (SiSXvMCCreateContextRec*)priv_data;
  pSiSXvMC->drmcontext = pContextRec->drmcontext;
  pSiSXvMC->agp_map.offset = pContextRec->AGPHandle;
  pSiSXvMC->agp_map.size = pContextRec->AGPSize;
  pSiSXvMC->mmio_map.offset = pContextRec->MMIOHandle;
  pSiSXvMC->mmio_map.size = pContextRec->MMIOSize;
  pSiSXvMC->fb_map.offset = pContextRec->FBhandle;
  pSiSXvMC->fb_map.size = pContextRec->FBSize;
  pSiSXvMC->ChipID = (int)pContextRec->ChipID;
  pSiSXvMC->pOverlay->HDisplay = pContextRec->HDisplay;
  pSiSXvMC->pOverlay->VDisplay = pContextRec->VDisplay;
  strncpy(pSiSXvMC->busIdString, (char *)pContextRec->busIdString,9);
  pSiSXvMC->busIdString[10] = '\0';

#ifdef XVMCDEBUG
  printf("%s:\n",__FUNCTION__);
  printf("drmcontext = 0x%x,\n",pSiSXvMC->drmcontext);
  printf("agp offset = 0x%x, agp size = 0x%x,\n",pSiSXvMC->agp_map.offset, pSiSXvMC->agp_map.size);
  printf("mmio offset = 0x%x, mmio size = 0x%x,\n",pSiSXvMC->mmio_map.offset, pSiSXvMC->mmio_map.size);
  printf("fb offset = 0x%x, fb size = 0x%x,\n",pSiSXvMC->fb_map.offset, pSiSXvMC->fb_map.size);
#endif

  
  /* Must free the private data we were passed from X */
  free(priv_data);

  
  /* ToDo: Initialize private context values */
  

  /* Map AGP memory */
  if(drmMap(pSiSXvMC->fd, pSiSXvMC->agp_map.offset,
	    pSiSXvMC->agp_map.size, &(pSiSXvMC->agp_map.address)) < 0) {
		printf("[XvMC] Unable to map AGP at offset 0x%x and size 0x%x\n",
		   (unsigned int)pSiSXvMC->agp_map.offset,pSiSXvMC->agp_map.size);
		_xvmc_destroy_context(display, context);
		/*free(pSiSXvMC->dmabufs->list);*/
		free(pSiSXvMC);
		return BadAlloc;
  } /* drmMap() < 0 */

  agpAddress = pSiSXvMC->agp_map.address;


  /* Map MMIO registers */
  if(drmMap(pSiSXvMC->fd, pSiSXvMC->mmio_map.offset,
	    pSiSXvMC->mmio_map.size, &(pSiSXvMC->mmio_map.address))<0){
		printf("[XvMC] Unable to map MMIO at offset 0x%x and size 0x%x\n",
		   (unsigned int)pSiSXvMC->mmio_map.offset,pSiSXvMC->mmio_map.size);
		_xvmc_destroy_context(display, context);
		/*free(pSiSXvMC->dmabufs->list);*/
		free(pSiSXvMC);
		return BadAlloc;
  }
  
  mmioAddress = pSiSXvMC->mmio_map.address;

  /* Map Frame Buffer */
  if(drmMap(pSiSXvMC->fd, pSiSXvMC->fb_map.offset,
	    pSiSXvMC->fb_map.size, &(pSiSXvMC->fb_map.address))<0){
		printf("[XvMC] Unable to map frame buffer at offset 0x%x and size 0x%x\n",
		   (unsigned int)pSiSXvMC->fb_map.offset,pSiSXvMC->fb_map.size);
		_xvmc_destroy_context(display, context);
		/*free(pSiSXvMC->dmabufs->list);*/
		free(pSiSXvMC);
		return BadAlloc;
  }
  fbAddress = pSiSXvMC->fb_map.address;


  /* setting some values differing from various chips */
  if(ChipDiffering(pSiSXvMC) <0 )
    return BadValue;


  pSiSXvMC->DecodeMode = HWMODE_MCONLY; /* default: MC only*/
  pSiSXvMC->CmdBufSize = 0;

  /* calculate the size of the necessary AGP buffer */
  switch(pSiSXvMC->DecodeMode){
  case HWMODE_ADVANCED:
  	 /* uncompressed frame max size + additional header(ex. PICP, Quantization matirx, ...). */
        pSiSXvMC->CmdBufSize = ((width * height * 3 / 2) + 4095 ) / 4096;
        pSiSXvMC->CmdBufSize = (pSiSXvMC->CmdBufSize + 1) * 4096;
	break;

   case HWMODE_MCONLY:
   default:
   	{
   	 int dwMBCount = 0;
        dwMBCount = (width * height) / 256;
        pSiSXvMC->CmdBufSize = 16 + 800 * dwMBCount;
        pSiSXvMC->CmdBufSize = (pSiSXvMC->CmdBufSize + 63) & ~63;
	 break;
	 }
   }


   pSiSXvMC->AGPHeap = TRUE; /* Use AGP */
   pSiSXvMC->SurfaceNum = 6; 
   /* Creaet the necessary command buffers */
   if (pSiSXvMC->AGPHeap){ /* allocate cmd buffer on AGP */

	for(i = 0; i<CMD_BUFFER_MAX; i++){
 		pSiSXvMC->CmdBuf[i] = (unsigned long)(pSiSXvMC->agp_map.address + i*pSiSXvMC->CmdBufSize);
 	} 
   }
   else{ /* allocate cmd buffer on Video memory */
 
 	for(i = 0; i<pSiSXvMC->SurfaceNum ; i++){
 		pSiSXvMC->CmdBuf[i] = (unsigned long)pSiSXvMC->fb_map.address + 0x309000 + i*pSiSXvMC->CmdBufSize;
 	}  		
   }  
 
   /*  reset MPEG & video hardware */
   if(EnableHWMPEG(context, FALSE) < 0){ 
   	printf("[XvMC] Failed to Reset MPEG Engine.\n");  return BadAlloc;}
   if(EnableHWMPEG(context, TRUE) < 0){ 
   	printf("[XvMC] Failed to Enable MPEG Engine.\n");  return BadAlloc;}

   /* check if MPEG Engine is Idle */
   if(!IsMPEGEngineIdle(pSiSXvMC)){
   	printf("[XvMC] MPEG Engine is busy.\n");  return BadAlloc;}

   /* Clean Surface list */
   for(i = 0; i<FrameBufferNum; i++)
   	SurfaceList[i] = NULL;

   pSiSXvMC->ref = 0;
   pSiSXvMC->lock = 0;

#ifdef XvMCHWDATADUMP
   dump = fopen("dump.dat", "wb");
#endif
   
   /* drmUnlock(pSiSXvMC->fd, pSiSXvMC->drmcontext);*/

   return Success;
}

/***************************************************************************
// Function: XvMCDestroyContext
// Description: Destorys the specified context.
//
// Arguments:
//   display - Specifies the connection to the server.
//   context - The context to be destroyed.
//
// Returns: Status
***************************************************************************/
Status 
XvMCDestroyContext(Display *display, XvMCContext *context) {
  
   SiSXvMCContext *pSiSXvMC;
   
#ifdef XVMCDEBUG
   printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif
   
   if(context == NULL) {
      return (error_base + XvMCBadContext);
   }
   if(context->privData == NULL) {
      return (error_base + XvMCBadContext);
   }
   pSiSXvMC = (SiSXvMCContext *)context->privData;
   
   /* Pass Control to the X server to destroy the drm_context_t */
   _xvmc_destroy_context(display, context);

   if(pSiSXvMC->pOverlay)
   	free(pSiSXvMC->pOverlay);
 
   sis_free_privContext(pSiSXvMC);
   context->privData = NULL;

#ifdef XvMCHWDATADUMP
   fflush(dump);
   fclose(dump);
#endif

   return Success;
}


/***************************************************************************
// Function: XvMCCreateSurface
***************************************************************************/
Status XvMCCreateSurface( Display *display, XvMCContext *context,
			  XvMCSurface *surface) {

   SiSXvMCContext *pSiSXvMC;
   SiSXvMCSurface *pSiSSurface;
   int priv_count;
   uint *priv_data;
   Status ret;
   SiSXvMCCreateSurfaceRec* pSurfaceRec;;

#ifdef XVMCDEBUG
   printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif

   if((surface == NULL) || (context == NULL) || (display == NULL)){
      return BadValue;
   }
  
   pSiSXvMC = (SiSXvMCContext *)context->privData;
   if(pSiSXvMC == NULL) {
      return (error_base + XvMCBadContext);
   }

   surface->privData = (SiSXvMCSurface *)malloc(sizeof(SiSXvMCSurface));
   if(!surface->privData) {
      return BadAlloc;
   }
   pSiSSurface = (SiSXvMCSurface *)surface->privData;
   pSiSSurface->xvmcsurface = (void*)surface;

   /* Initialize private values */
   pSiSSurface->privContext = pSiSXvMC;
   pSiSSurface->last_render = 0;
   
 
   if((ret = _xvmc_create_surface(display, context, surface,
				       &priv_count, &priv_data))) {
      free(pSiSSurface);
#ifdef XVMCDEBUG
      printf("[XvMC] Unable to create XvMCSurface.\n");
#endif
      return ret;
   }


   /*
      _xvmc_create_subface returns 4 uints with the offset into
      the DRM map for the Y surface and UV surface.
   */
   if(priv_count != sizeof(SiSXvMCCreateSurfaceRec)) {
      printf("[XvMC] _xvmc_create_surface() return incorrect data size.\n");
      printf("Expected %d, got %d\n",sizeof(SiSXvMCCreateSurfaceRec), priv_count);
      free(priv_data);
      free(pSiSSurface);
      return BadAlloc;
   }

   /*
      offsets[0,1,2] == Offsets from either data or offset for the Y
      U and V surfaces.
   */
   pSurfaceRec = (SiSXvMCCreateSurfaceRec*)priv_data;
   pSiSSurface->offsets[0] = pSurfaceRec->offsets[0];
   if((pSiSSurface->offsets[0]) & 0xf) {
      printf("[XvMC] XvMCCreateSurface: Surface offset 0 is not 15 aligned\n");
   }

   /* Planar surface */
   pSiSSurface->offsets[1] = pSurfaceRec->offsets[1];
   if((pSiSSurface->offsets[1]) & 0xf) {
      printf("[XvMC] XvMCCreateSurface: Surface offset 1 is not 15 aligned\n");
   }
   
   pSiSSurface->offsets[2] = pSurfaceRec->offsets[2];
   if((pSiSSurface->offsets[2]) & 0xf) {
      printf("[XvMC] XvMCCreateSurface: Surface offset 2 is not 15 aligned\n");
   }
      
   pSiSSurface->MyNum = pSurfaceRec->MyNum;
   SurfaceList[pSiSSurface->MyNum] = pSiSSurface;
   pSiSSurface->MyBuff = pSiSSurface->CurrentEntry = pSiSXvMC->CmdBuf[pSiSSurface->MyNum];
   pSiSSurface->MyOffset = pSiSSurface->MyNum * pSiSXvMC->CmdBufSize;
   pSiSSurface->status = SurfaceIdle;

   
   /* Free data returned from xvmc_create_surface */
   free(priv_data);
   
   pSiSXvMC->ref++;
   pSiSSurface->Subp = NULL;
   pSiSSurface->DisplayingAskCounter = 0;
   
   return Success;
}


/***************************************************************************
// Function: XvMCDestroySurface
***************************************************************************/
Status XvMCDestroySurface(Display *display, XvMCSurface *surface) {

  
   SiSXvMCSurface *pSiSSurface;
   SiSXvMCContext *pSiSXvMC;

#ifdef XVMCDEBUG
   printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif

   if((display == NULL) || (surface == NULL)) {
      return BadValue;
   }
   if(surface->privData == NULL) {
      return (error_base + XvMCBadSurface);
   }

    pSiSSurface = (SiSXvMCSurface *)surface->privData;
    pSiSXvMC = (SiSXvMCContext *) pSiSSurface->privContext;

    _xvmc_destroy_surface(display,surface);

    sis_free_privContext(pSiSXvMC);

    free(pSiSSurface);
    surface->privData = NULL;
    return Success;

}

/***************************************************************************
// Function: XvMCCreateBlocks
***************************************************************************/
Status XvMCCreateBlocks(Display *display, XvMCContext *context,
			unsigned int num_blocks,
			XvMCBlockArray *block) {

#ifdef XVMCDEBUG
   printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif

   if((display == NULL) || (context == NULL) || (num_blocks == 0)) {
      return BadValue;
   }

   block->blocks = (short *)malloc(num_blocks<<6 * sizeof(short));
   if(block->blocks == NULL) {
      return BadAlloc;
   }

   block->num_blocks = num_blocks;
   block->context_id = context->context_id;
   
   block->privData = NULL;
   
   return Success;

}

/***************************************************************************
// Function: XvMCDestroyBlocks
***************************************************************************/
Status XvMCDestroyBlocks(Display *display, XvMCBlockArray *block) {

#ifdef XVMCDEBUG
   printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif
  
   if(display == NULL) {
      return BadValue;
   }
   
   free(block->blocks);
   block->num_blocks = 0;
   block->context_id = 0;
   block->privData = NULL;
   return Success;

}

/***************************************************************************
// Function: XvMCCreateMacroBlocks
***************************************************************************/
Status XvMCCreateMacroBlocks(Display *display, XvMCContext *context,
			     unsigned int num_blocks,
			     XvMCMacroBlockArray *blocks) {

#ifdef XVMCDEBUG
   printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif
 
   if((display == NULL) || (context == NULL) || (blocks == NULL) ||
      (num_blocks == 0)) {
     return BadValue;
   }
   memset(blocks,0,sizeof(XvMCMacroBlockArray));
  
   blocks->context_id = context->context_id;
   blocks->privData = NULL;
   
   blocks->macro_blocks = (XvMCMacroBlock *)
      malloc(num_blocks * sizeof(XvMCMacroBlock));
   if(blocks->macro_blocks == NULL) {
      return BadAlloc;
   }
   blocks->num_blocks = num_blocks;
 
   return Success;

}

/***************************************************************************
// Function: XvMCDestroyMacroBlocks
***************************************************************************/
Status XvMCDestroyMacroBlocks(Display *display, XvMCMacroBlockArray *block) {

#ifdef XVMCDEBUG
   printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif
 

  if((display == NULL) || (block == NULL)) {
     return BadValue;
  }
  if(block->macro_blocks) {
     free(block->macro_blocks);
  }
   block->context_id = 0;
   block->num_blocks = 0;
   block->privData = NULL;

   return Success;

}


/***************************************************************************
// Function: XvMCRenderSurface
// Description: This function does the actual HWMC. Given a list of
//  macroblock structures it dispatched the hardware commands to execute
//  them. DMA buffer containing Y data are dispatched as they fill up
//  U and V DMA buffers are queued until all Y's are done. This minimizes
//  the context flipping and flushing required when switching between Y
//  U and V surfaces.
***************************************************************************/
Status XvMCRenderSurface(Display *display, XvMCContext *context,
			 unsigned int picture_structure,
			 XvMCSurface *target_surface,
			 XvMCSurface *past_surface,
			 XvMCSurface *future_surface,
			 unsigned int flags,
			 unsigned int num_macroblocks,
			 unsigned int first_macroblock,
			 XvMCMacroBlockArray *macroblock_array,
			 XvMCBlockArray *blocks) {
  
   int i;
   SiSXvMCSurface *pSiSXvMCSurface;
   SiSXvMCContext *pSiSXvMCContext;
   XvMCMacroBlock *mb;
   PICTUREPARA *PictPara;
   IMBHEADER *I_MBHeader;
   int cbp, BlockCount;
   unsigned long Blocks;
   unsigned long pshort;
   int bytecount;

   /* for P- & B-frames */
   SiSXvMCSurface *privPast ;
   SiSXvMCSurface *privFuture;
   MBHEADER *lpMBHeader;
   int gbx, gby;
   WORD forvx, forvy, forbvx, forbvy;
   WORD backvx, backvy, backbvx, backbvy;
   WORD filflga, filflgb, filflgc, filflgd;
   short vectorX, vectorY;
   
   
#ifdef XVMCDEBUG
   printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif

#if 0
   /* Temp path */
   if(future_surface != NULL){/* B-Frame */
       return Success;
   }
#endif 

   pSiSXvMCSurface = (SiSXvMCSurface*)target_surface->privData;
   pSiSXvMCContext = (SiSXvMCContext*)context->privData;
   
  /********    STEP1: editing a  pciture layer parameter when first MB/slice of a frame comes  ********/

  if(pSiSXvMCSurface->status == SurfaceIdle ||/* first MB/slice */
      (((past_surface != NULL) || (pSiSXvMCSurface->FrameType != I_TYPE))  &&
        ((future_surface == NULL) || (pSiSXvMCSurface->FrameType != B_TYPE)) &&
        ((past_surface == NULL) || (future_surface != NULL) || (pSiSXvMCSurface->FrameType != P_TYPE)))
        /* fault frame type : ReRander! */
     ){ 

      pSiSXvMCSurface->status = SurfaceRendering;
	  
      PictPara = (PICTUREPARA*)pSiSXvMCSurface->MyBuff;
      pSiSXvMCSurface->CurrentEntry = pSiSXvMCSurface->MyBuff + sizeof(PICTUREPARA);
      /* initialize the header */
      memset(PictPara, 0x00, sizeof(PICTUREPARA));
      
      /*picture code type */
      if(past_surface == NULL)/* I-frame */
         PictPara->pict_type = pSiSXvMCSurface->FrameType = I_TYPE;
      else if(future_surface == NULL)/* P-frame */
         PictPara->pict_type = pSiSXvMCSurface->FrameType = P_TYPE; 
      else /* B-frame */
         PictPara->pict_type = pSiSXvMCSurface->FrameType = B_TYPE;
      
      
      /* picture struct */
      switch(picture_structure){
      case XVMC_TOP_FIELD:
         PictPara->pict_struct = pSiSXvMCSurface->pict_struct = TOP_FIELD;
         break;
      case XVMC_BOTTOM_FIELD:
         PictPara->pict_struct = pSiSXvMCSurface->pict_struct = BOTTOM_FIELD;
         break;
      case XVMC_FRAME_PICTURE:
         PictPara->pict_struct = pSiSXvMCSurface->pict_struct = FRAME_PICTURE;
         break;
      default:
         printf("[XvMC]%s: Parameter picture_structure is worng. Plesae check.\n",__FUNCTION__);
         return BadValue;
      }
      
      
      /* fix reference buffer selection (ToDo!) */
      /*if(PictPara->pict_type != P_TYPE)*/
         PictPara->fix_buf = 0x0;
   
      /* frame buffer selection */
      pSiSXvMCSurface->forRefer = pSiSXvMCSurface->BackRefer = -1;
      switch(PictPara->pict_type){
      case I_TYPE:
         PictPara->decframe = PictPara->forframe = PictPara->backframe = 
		 	(BYTE)pSiSXvMCSurface->MyNum;
         break;
      case P_TYPE:
         privPast = (SiSXvMCSurface *)past_surface->privData; 	
         PictPara->decframe = (BYTE)pSiSXvMCSurface->MyNum;
         PictPara->forframe = PictPara->backframe = pSiSXvMCSurface->forRefer = 
		 	(BYTE)privPast->MyNum;
	  break;
      case B_TYPE:
         privPast = (SiSXvMCSurface *)past_surface->privData;
         privFuture = (SiSXvMCSurface *)future_surface->privData;
         PictPara->decframe = (BYTE)pSiSXvMCSurface->MyNum; 	
         PictPara->forframe = pSiSXvMCSurface->forRefer = (BYTE)privPast->MyNum;		
         PictPara->backframe = pSiSXvMCSurface->BackRefer = (BYTE)privFuture->MyNum;
	  break;
      default:
         break;
      }
   
      /* Num of horizontal & vertical MBs */
      PictPara->mb_height = (((context->height + 15) & ~15) >> 4) - 1; 
      PictPara->mb_width = (((context->width + 15) & ~15) >> 4) - 1;
   
       /* skip the flip */
      PictPara->mode = 0x0;
   }
   


   /**************       STEP2: editing the Macro blocks       **************/   
   mb = macroblock_array->macro_blocks;
   for(i = first_macroblock; i<first_macroblock + num_macroblocks; i++){
      switch(pSiSXvMCSurface->FrameType){
      case I_TYPE:
         
         I_MBHeader = (IMBHEADER*)pSiSXvMCSurface->CurrentEntry;	
         /* initialize the header */
         memset(I_MBHeader, 0x00, sizeof(IMBHEADER));
         
         /* coded_block_pattern */
         if(mb->coded_block_pattern & ~0x3f){
            printf("[XvMC] Error: This chip only support YUV 420 format.\n");
            return BadValue;
         }
         I_MBHeader->cbp = cbp = mb->coded_block_pattern & 0x3f;
         
         /* enable intra-macroblock flag */
         if(mb->macroblock_type & XVMC_MB_TYPE_INTRA)
            I_MBHeader->mc_flag = 0x1;
         if(mb->macroblock_type & XVMC_MB_TYPE_PATTERN){
            /* ? */
         }
         
         
         /* No MC */
         I_MBHeader->mcmode= mb->motion_type;
         
         /* DCT  encoding type: Frame */
         I_MBHeader->dcttype = mb->dct_type;
         
         /* Copy the payload */
         BlockCount = 0;
         Blocks = (unsigned long)I_MBHeader + sizeof(IMBHEADER);
         pshort = (unsigned long)blocks->blocks + ((mb->index)*BlockSize);
         while(cbp){
            pshort = (unsigned long)blocks->blocks + ((mb->index + BlockCount)*BlockSize);
            if(cbp & 0x01){ /* the block exists */

               if(pSiSXvMCContext->ChipID == 671){/* In 671, the residuel is 16 bpp */ 
                  memcpy((void*)Blocks, (void*)pshort, BlockSize);
                  Blocks += BlockSize;
               }

               else{/* 662 & 741: 8 bpp */
                  for(bytecount = 0; bytecount < I_BlockSize ; bytecount++){
                     memcpy((void*)Blocks, (void*)pshort, 1);
                     Blocks += 1; 
                     pshort += 2;
                  }
               }
            }
            BlockCount++;            
            cbp >>= 1;
         }

         if(pSiSXvMCContext->ChipID == 671)/* In 671, the residuel is 16 bpp */ 
            pSiSXvMCSurface->CurrentEntry = 
               (unsigned long)pSiSXvMCSurface->CurrentEntry + sizeof(IMBHEADER) + BlockCount*BlockSize;
         else /* 662 & 741: 8 bpp */
            pSiSXvMCSurface->CurrentEntry = 
               (unsigned long)pSiSXvMCSurface->CurrentEntry + sizeof(IMBHEADER) + BlockCount*I_BlockSize;
         break;


      case P_TYPE:
         privPast =(SiSXvMCSurface *)past_surface->privData; 	

         lpMBHeader = (MBHEADER*)pSiSXvMCSurface->CurrentEntry;
         
         /* initialize the header */
         memset(lpMBHeader, 0x00, sizeof(MBHEADER));

          /* coded_block_pattern */
         if(mb->coded_block_pattern & ~0x3f){
            printf("[XvMC] Error: This chip only support YUV 420 format.\n");
            return BadValue;
         }
         lpMBHeader->cbp = cbp = mb->coded_block_pattern & 0x3f;

         /* enable macroblock flag */	 
         switch(mb->macroblock_type){
         case XVMC_MB_TYPE_INTRA:
            lpMBHeader->mc_flag = 0x1;
            break;
         case XVMC_MB_TYPE_MOTION_FORWARD :
            lpMBHeader->mc_flag = 0x8;
            break;
         case  XVMC_MB_TYPE_MOTION_FORWARD | XVMC_MB_TYPE_PATTERN:
            lpMBHeader->mc_flag = 0x8;
            break;
         default:
            printf("[XvMC] macroblock type error\n");
	     return BadValue;
         }
		 
         /* MC type */
         lpMBHeader->mcmode = mb->motion_type;
		 
         /* DCT  encoding type */
         lpMBHeader->dcttype = mb->dct_type;
         

         /* decode motion vector */
         gbx=(mb->x * 16);
         gby=(mb->y * 16);

	  /* initialize vector */
         forvx = forvy = filflga = 0;
         

         /* forward && MC_Frame */	
         if(mb->motion_type == XVMC_PREDICTION_FRAME){/* for frame */
            vectorX = mb->PMV[0][0][0];
            vectorY = mb->PMV[0][0][1];
            forvx = gbx + (vectorX >> 1);
            forvy = gby + (vectorY >> 1);
            /* for m0 and n0 but I don't understand why?  @@ */
            forvx |= (vectorX%(-4) == -1) ? ADD_ONE_FLAG : 0;
            forvy |= (vectorY%(-4) == -1) ? ADD_ONE_FLAG : 0;
            filflga = (vectorX & 1)
                        | ((vectorY & 1) << 1)
                        | (((vectorX/2) & 1) << 2)
                        | (((vectorY/2) & 1) << 3);        	
            lpMBHeader->filflga = filflga;
            lpMBHeader->forvx = forvx | (privPast->MyNum<< 11);
            lpMBHeader->forvy = forvy;
         }
   	  else if(mb->motion_type == XVMC_PREDICTION_FIELD){/* For field */
            vectorX = mb->PMV[0][0][0];
            vectorY = mb->PMV[0][0][1] >> 1;
            
            if (mb->motion_vertical_field_select & 0x01){   
               forvx = gbx + (vectorX >> 1);
               forvy = gby + vectorY + !(vectorY & 1);
            }
            else{ /* REFERENCE_TOP_FIELD */
               forvx = gbx + (vectorX >> 1);
               forvy = gby + vectorY - (vectorY & 1);
            }
            forvx |= (vectorX%(-4) == -1) ? ADD_ONE_FLAG : 0;
            forvy |= (vectorY%(-4) == -1) ? ADD_ONE_FLAG : 0;
            
            filflga = (vectorX & 1)
                    | ((vectorY & 1) << 1)
                    | (((vectorX/2) & 1) << 2)
                    | (((vectorY/2) & 1) << 3);
            
            /* bottom field prediction */
            vectorX = mb->PMV[1][0][0];
            vectorY = mb->PMV[1][0][1] >> 1;
   
            if (mb->motion_vertical_field_select & 0x04 ){
                forbvx = gbx + (vectorX >> 1);
                forbvy = gby + vectorY + !(vectorY & 1);
            }    
            else{   /* REFERENCE_TOP_FIELD */
                forbvx = gbx + (vectorX >> 1);
                forbvy = gby + vectorY - (vectorY & 1);
            }
            forbvx |= (vectorX%(-4) == -1) ? ADD_ONE_FLAG : 0;
            forbvy |= (vectorY%(-4) == -1) ? ADD_ONE_FLAG : 0;
            
            filflgc = (vectorX & 1)
                      | ((vectorY & 1) << 1)
                      | (((vectorX/2) & 1) << 2)
                      | (((vectorY/2) & 1) << 3);
            lpMBHeader->filflga = filflga;
            lpMBHeader->forvx = forvx|(privPast->MyNum<< 11);
            lpMBHeader->forvy = forvy;
            lpMBHeader->filflgc = filflgc;
            lpMBHeader->forbvx = forbvx|(privPast->MyNum<< 11);
            lpMBHeader->forbvy = forbvy;
            }
         else if ( mb->motion_type == XVMC_PREDICTION_DUAL_PRIME){/* for MDV */
                
          /* predict top field from top field */
          vectorX = mb->PMV[0][0][0];
          vectorY = mb->PMV[0][0][1] >> 1;
          
          forvx = gbx + (vectorX >> 1);
          forvy = gby + vectorY - (vectorY & 1);
          forvx |= (vectorX%(-4) == -1) ? ADD_ONE_FLAG : 0;
          forvy |= (vectorY%(-4) == -1) ? ADD_ONE_FLAG : 0;
          
          filflga = (vectorX & 1)
                    | ((vectorY & 1) << 1)
                    | (((vectorX/2) & 1) << 2)
                    | (((vectorY/2) & 1) << 3);
          
          /* add difference to top field from bottom field
          // MVector[1] is (vector'[2] << 1), therefore we need to shift it back */
          vectorX = mb->PMV[0][1][0];
          vectorY = mb->PMV[0][1][1] >> 1;
          
          backvx = gbx + (vectorX >> 1);
          backvy = gby + vectorY + !(vectorY & 1);
          backvx |= (vectorX%(-4) == -1) ? ADD_ONE_FLAG : 0;
          backvy |= (vectorY%(-4) == -1) ? ADD_ONE_FLAG : 0;
          
          filflgb = (vectorX & 1)
                    | ((vectorY & 1) << 1)
                    | (((vectorX/2) & 1) << 2)
                    | (((vectorY/2) & 1) << 3);
          
          /* predict bottom field from bottom field */
          vectorX = mb->PMV[1][0][0];
          vectorY = mb->PMV[1][0][1] >> 1;
          
          forbvx = gbx + (vectorX >> 1);
          forbvy = gby + vectorY + !(vectorY & 1);
          forbvx |= (vectorX%(-4) == -1) ? ADD_ONE_FLAG : 0;
          forbvy |= (vectorY%(-4) == -1) ? ADD_ONE_FLAG : 0;
          
          filflgc = (vectorX & 1)
                    | ((vectorY & 1) << 1)
                    | (((vectorX/2) & 1) << 2)
                    | (((vectorY/2) & 1) << 3);
          
          /* add differences to bottom field from top field
          // MVector[3] is (vector'[3] << 1), therefore we need to shift it back */
          vectorX = mb->PMV[1][1][0];
          vectorY = mb->PMV[1][1][1]>>1;
          
          backbvx = gbx + (vectorX >> 1);
          backbvy = gby + vectorY - (vectorY & 1);
          backbvx |= (vectorX%(-4) == -1) ? ADD_ONE_FLAG : 0;
          backbvy |= (vectorY%(-4) == -1) ? ADD_ONE_FLAG : 0;
          
          filflgd = (vectorX & 1)
                    | ((vectorY & 1) << 1)
                    | (((vectorX/2) & 1) << 2)
                    | (((vectorY/2) & 1) << 3);
          
          lpMBHeader->filflga = filflga;
          lpMBHeader->filflgb = filflgb;
          lpMBHeader->filflgc = filflgc;
          lpMBHeader->filflgd = filflgd;
          /*shift 11bit to write buffer selection flag*/
          lpMBHeader->forvx = forvx|(privPast->MyNum<< 11);
          lpMBHeader->forvy = forvy;
          lpMBHeader->backvx = backvx|(privPast->MyNum<< 11);;
          lpMBHeader->backvy = backvy;
          lpMBHeader->forbvx = forbvx|(privPast->MyNum<< 11);
          lpMBHeader->forbvy = forbvy;
          lpMBHeader->backbvx = backbvx|(privPast->MyNum<< 11);;
          lpMBHeader->backbvy = backbvy;
          }
      	
         /* Copy the payload */ 
         BlockCount= 0;
         while(cbp){            
            if(cbp & 0x01) 		
               BlockCount++;
               cbp >>= 1;
         }
         	 
         if(BlockCount >0){		 
            Blocks = (unsigned long)lpMBHeader + sizeof(MBHEADER);
            memcpy((void*)Blocks, (void*)((unsigned long)blocks->blocks + (mb->index *BlockSize)), BlockCount*BlockSize);
         }
		 
         pSiSXvMCSurface->CurrentEntry = 
               (unsigned long)pSiSXvMCSurface->CurrentEntry + sizeof(MBHEADER) + BlockCount*BlockSize;	
         break;


		 
      case B_TYPE:
         
         privFuture=(SiSXvMCSurface *)future_surface->privData;
         privPast =(SiSXvMCSurface *)past_surface->privData;
         lpMBHeader = (MBHEADER*)pSiSXvMCSurface->CurrentEntry;
		 
         /* initialize the header */
         memset(lpMBHeader, 0x00, sizeof(MBHEADER));

          /* coded_block_pattern */
         if(mb->coded_block_pattern & ~0x3f){
            printf("[XvMC] Error: This chip only support YUV 420 format.\n");
            return BadValue;
         }
         lpMBHeader->cbp = cbp = mb->coded_block_pattern & 0x3f;

		 
         /* enable macroblock flag */	 
         switch(mb->macroblock_type){
         case XVMC_MB_TYPE_INTRA:
            lpMBHeader->mc_flag = 0x1;
            break;
         case XVMC_MB_TYPE_MOTION_FORWARD :
            lpMBHeader->mc_flag = 0x8;
            break;
         case XVMC_MB_TYPE_MOTION_BACKWARD :
            lpMBHeader->mc_flag = 0x4;
            break;
         case  XVMC_MB_TYPE_MOTION_FORWARD | XVMC_MB_TYPE_PATTERN:
            lpMBHeader->mc_flag = 0x8;
            break;
         case  XVMC_MB_TYPE_MOTION_BACKWARD | XVMC_MB_TYPE_PATTERN:
            lpMBHeader->mc_flag = 0x4;
            break;
         case  XVMC_MB_TYPE_MOTION_FORWARD | XVMC_MB_TYPE_MOTION_BACKWARD:
            lpMBHeader->mc_flag = 0xc;
            break;
         case  XVMC_MB_TYPE_MOTION_FORWARD | XVMC_MB_TYPE_MOTION_BACKWARD |XVMC_MB_TYPE_PATTERN:
            lpMBHeader->mc_flag = 0xc;
            break;
         default:
            printf("[XvMC] macroblock type error\n");
	     return BadValue;	
         }
	 
         /* MC type */
         lpMBHeader->mcmode = mb->motion_type;
         /* DCT  encoding type */
         lpMBHeader->dcttype = mb->dct_type;

         /* decode motion vector */
         gbx=(mb->x * 16);
         gby=(mb->y * 16);

         /* forward && MC_Frame */	
         if(mb->macroblock_type & XVMC_MB_TYPE_MOTION_FORWARD){
            /* forward && MC_Frame */	
            if(mb->motion_type == XVMC_PREDICTION_FRAME){  
               vectorX = mb->PMV[0][0][0];
               vectorY = mb->PMV[0][0][1];
               forvx = gbx + (vectorX >> 1);
               forvy = gby + (vectorY >> 1);
               /* for m0 and n0 but I don't understand why?  @@ */
               forvx |= (vectorX%(-4) == -1) ? ADD_ONE_FLAG : 0;
               forvy |= (vectorY%(-4) == -1) ? ADD_ONE_FLAG : 0;
               filflga = (vectorX & 1)
                       | ((vectorY & 1) << 1)
                       | (((vectorX/2) & 1) << 2)
                       | (((vectorY/2) & 1) << 3);        	
               lpMBHeader->filflga = filflga;
               lpMBHeader->forvx = forvx | (privPast->MyNum<< 11);
               lpMBHeader->forvy = forvy;
			   
            }else if(mb->motion_type == XVMC_PREDICTION_FIELD){
               /**********************/
               /*     top field prediction   */
               /**********************/
               vectorX = mb->PMV[0][0][0];
               vectorY = mb->PMV[0][0][1] >> 1;
               
               if (mb->motion_vertical_field_select & XVMC_SELECT_FIRST_FORWARD){   
                  forvx = gbx + (vectorX >> 1);
                  forvy = gby + vectorY + !(vectorY & 1);
               }
               else{  /* REFERENCE_TOP_FIELD */
                  forvx = gbx + (vectorX >> 1);
                  forvy = gby + vectorY - (vectorY & 1);
               }
               forvx |= (vectorX%(-4) == -1) ? ADD_ONE_FLAG : 0;
               forvy |= (vectorY%(-4) == -1) ? ADD_ONE_FLAG : 0;
               
               filflga = (vectorX & 1)
                         | ((vectorY & 1) << 1)
                         | (((vectorX/2) & 1) << 2)
                         | (((vectorY/2) & 1) << 3);
               
               /**********************/
               /*  bottom field prediction */
               /**********************/
               vectorX = mb->PMV[1][0][0];
               vectorY = mb->PMV[1][0][1] >> 1;
               
               if (mb->motion_vertical_field_select & XVMC_SELECT_SECOND_FORWARD){
                  forbvx = gbx + (vectorX >> 1);
                  forbvy = gby + vectorY + !(vectorY & 1);
               }    
               else{ /* REFERENCE_TOP_FIELD  */ 
                  forbvx = gbx + (vectorX >> 1);
                  forbvy = gby + vectorY - (vectorY & 1);
               }
               forbvx |= (vectorX%(-4) == -1) ? ADD_ONE_FLAG : 0;
               forbvy |= (vectorY%(-4) == -1) ? ADD_ONE_FLAG : 0;
               
               filflgc = (vectorX & 1)
                        | ((vectorY & 1) << 1)
                        | (((vectorX/2) & 1) << 2)
                        | (((vectorY/2) & 1) << 3);
               lpMBHeader->filflga = filflga;
               lpMBHeader->forvx = forvx|(privPast->MyNum<< 11);
               lpMBHeader->forvy = forvy;
               lpMBHeader->filflgc = filflgc;
               lpMBHeader->forbvx = forbvx|(privPast->MyNum<< 11);
               lpMBHeader->forbvy = forbvy;
            }
         }        	
           	 
         /* Backward && MC_Frame */
         if(mb->macroblock_type & XVMC_MB_TYPE_MOTION_BACKWARD){
		 	
            if(mb->motion_type == XVMC_PREDICTION_FRAME){
            vectorX = mb->PMV[0][1][0];
            vectorY = mb->PMV[0][1][1];
            backvx = gbx + (vectorX >> 1);
            backvy = gby + (vectorY >> 1);
            /* for m1 and n1 but I don't understand why?  @@ */
            backvx |= (vectorX%(-4) == -1) ? ADD_ONE_FLAG : 0;
            backvy |= (vectorY%(-4) == -1) ? ADD_ONE_FLAG : 0;
            filflgb = (vectorX & 1)
                     | ((vectorY & 1) << 1)
                     | (((vectorX/2) & 1) << 2)
                     | (((vectorY/2) & 1) << 3);
            lpMBHeader->filflgb = filflgb;
            lpMBHeader->backvx = backvx |((privFuture->MyNum)<<11);
            lpMBHeader->backvy = backvy;
			
            }else if(mb->motion_type == XVMC_PREDICTION_FIELD){
               /**********************/
               /*     top field prediction   */
               /**********************/
               vectorX = mb->PMV[0][1][0];
               vectorY = mb->PMV[0][1][1]>> 1;
               
               if (mb->motion_vertical_field_select & XVMC_SELECT_FIRST_BACKWARD){  
                  backvx = gbx + (vectorX >> 1);
                  backvy = gby + vectorY + !(vectorY & 1);
               }
               else{/* REFERENCE_TOP_FIELD */
                  backvx = gbx + (vectorX >> 1);
                  backvy = gby + vectorY - (vectorY & 1);
               }
               backvx |= (vectorX%(-4) == -1) ? ADD_ONE_FLAG : 0;
               backvy |= (vectorY%(-4) == -1) ? ADD_ONE_FLAG : 0;
               
               filflgb = (vectorX & 1)
                        | ((vectorY & 1) << 1)
                        | (((vectorX/2) & 1) << 2)
                        | (((vectorY/2) & 1) << 3);
			   
        
               /**********************/
               /*  bottom field prediction */
               /**********************/
               vectorX = mb->PMV[1][1][0];
               vectorY = mb->PMV[1][1][1] >> 1;
               
               if ( mb->motion_vertical_field_select & XVMC_SELECT_SECOND_BACKWARD ){
                  backbvx = gbx + (vectorX >> 1);
                  backbvy = gby + vectorY + !(vectorY & 1);
               }
               else{  /* REFERENCE_TOP_FIELD */
                  backbvx = gbx + (vectorX >> 1);
                  backbvy = gby + vectorY - (vectorY & 1);
               }
               backbvx |= (vectorX%(-4) == -1) ? ADD_ONE_FLAG : 0;
               backbvy |= (vectorY%(-4) == -1) ? ADD_ONE_FLAG : 0;
        
               filflgd = (vectorX & 1)
                        | ((vectorY & 1) << 1)
                        | (((vectorX/2) & 1) << 2)
                        | (((vectorY/2) & 1) << 3);	
               lpMBHeader->filflgb = filflgb;
               lpMBHeader->backvx = backvx | (privFuture->MyNum<< 11);
               lpMBHeader->backvy = backvy;	
               lpMBHeader->filflgd = filflgd;
               lpMBHeader->backbvx = backbvx | (privFuture->MyNum<< 11);
               lpMBHeader->backbvy = backbvy;		  
            }
         }
         
		 
          
         /* Copy the payload */ 
         BlockCount= 0;
         while(cbp){            
            if(cbp & 0x01) 		
               BlockCount++;
               cbp >>= 1;
         }
         	 
         if(BlockCount > 0){		 
            Blocks = (unsigned long)lpMBHeader + sizeof(MBHEADER);
            memcpy((void*)Blocks, (void*)((unsigned long)blocks->blocks + (mb->index *BlockSize)), BlockCount*BlockSize);
         }
		 
         pSiSXvMCSurface->CurrentEntry = 
               (unsigned long)pSiSXvMCSurface->CurrentEntry + sizeof(MBHEADER) + BlockCount*BlockSize;	
         break;
		 
      default:
         printf("[XvMC] macroblock type error.\n");
         return BadValue;
      }
	  
      mb++;
   }
    
    return Success;
}



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


static void 
set_dda_regs_6tap(/*SISPtr pSiS,*/ float scale)
{

       int uid;
       unsigned short iobase, video_port, sr_port;
	float WW, W[6], tempW[6];
	int i, j, w, k, m, *temp[6], *wtemp[6], WeightMat[16][6], idxw;
	int *wm1, *wm2, *wm3, *wm4, *wm5, *wm6, *tempadd;
	long top, bot;
	unsigned long dwScanLine, dwTopLine, dwBottomLine;
   
       /* set user as root */
       uid = getuid();
       if(setuid(0) < 0){
       	printf("[XvMC] %s: Can't set the user root.\n\t Please check the Application mode.\n",
       		__FUNCTION__);
          return;
       }
       if(iopl(3) < 0){
       	printf("[XvMC] %s: Can't set ports available.\n", __FUNCTION__);
          return;
       }
       
       /* Get Relocated io */ 
       outSISREG32(0xcf8, 0x80010018);
       iobase = inSISREG32(0xcfc) & 0xFFFC;
       
       video_port = iobase + VIDEOOFFSET;
       sr_port = iobase + SROFFSET;

	

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
	for(i = 0; i < 16; i++)
		for(j = 0; j < 6; j++)
		{
			setregmask(video_port, Horizontal_6Tap_DDA_WeightingMatrix_Index, (6*i + j), 0x7F);
			setregmask(video_port, Horizontal_6Tap_DDA_WeightingMatrix_Value, WeightMat[i][j], 0x3F);
		}	
}



int
set_dda_regs(SiSXvMCContext* pSiSXvMC, float scale)
{

    int uid;
    unsigned short iobase, video_port, sr_port;
    float W[4], WS, myadd;
    int   *temp[4], *wm1, *wm2, *wm3, *wm4;
    int   i, j, w, tidx, weightmatrix[16][4];
	
    /* set user as root */
    uid = getuid();
    if(setuid(0) < 0){
    	printf("[XvMC] %s: Can't set the user root.\n\t Please check the Application mode.\n",
    		__FUNCTION__);
       return -1;
    }
    if(iopl(3) < 0){
    	printf("[XvMC] %s: Can't set ports available.\n", __FUNCTION__);
       return -1;
    }
    
    /* Get Relocated io */ 
    outSISREG32(0xcf8, 0x80010018);
    iobase = inSISREG32(0xcfc) & 0xFFFC;
    
    video_port = iobase + VIDEOOFFSET;
    sr_port = iobase + SROFFSET;


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

	if(pSiSXvMC->ChipID == 671){
		
		 for(i = 0; i < 16; i++){
		 	for(j = 0; j < 4; j++){
				setregmask(video_port, Vertical_4Tap_DDA_WeightingMatrix_Index, (4*i + j), 0x3F);
                		setregmask(video_port, Vertical_4Tap_DDA_WeightingMatrix_Value, weightmatrix[i][j], 0x3F);
            	}}
		set_dda_regs_6tap(scale);

	}

	else{ /* 662 & 741 */
		/* Set 4-tap scaler video regs 0x75-0xb4 */
		w = 0x75;
		for(i = 0; i < 16; i++){
			for(j = 0; j < 4; j++, w++){
				setregmask(video_port, w, weightmatrix[i][j], 0x3f);
	       }}
       }
}



#define OVERLAY_MIN_WIDTH       32  	/* Minimum overlay sizes */
#define OVERLAY_MIN_HEIGHT      24
/*********************************
 *       subfunction: Calc scaling factor    *
 *********************************/

static void
calc_scale_factor(unsigned short srcW, unsigned short srcH,
                                  unsigned short dstW, unsigned short dstH, 
                                  SiSOverlayRec* pOverlay, short srcPitch){
                                  
   CARD32 I = 0, mult = 0;
   
#if 0
   SISPtr pSiS = SISPTR(pScrn);
   
   int flag = 0, flag2 = 0;


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
#endif

   pOverlay->tap_scale = 1.0;
   
   if(dstW < OVERLAY_MIN_WIDTH) dstW = OVERLAY_MIN_WIDTH;

   if(dstW == srcW) {
   
      pOverlay->HUSF   = 0x00;
      pOverlay->IntBit = 0x05;
      pOverlay->wHPre  = 0;
      
   } else if(dstW > srcW) {

      pOverlay->IntBit = 0x04;
      pOverlay->wHPre  = 0;
 

       if(pOverlay->havetapscaler) {
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

#if 0
     /* It seems, the hardware can't scale below factor .125 (=1/8) if the
        pitch isn't a multiple of 256.
	TODO: Test this on the 315 series!
      */
     if((srcPitch % 256) || (srcPitch < 256)) {
        if(((dstW * 1000) / srcW) < 125) dstW = tmpW = ((srcW * 125) / 1000) + 1;
     }
#endif
         I = 0;
         pOverlay->IntBit = 0x01;
         while(srcW >= tmpW) {
            tmpW <<= 1;
            I++;
         }
         pOverlay->wHPre = (CARD8)(I - 1);
         dstW <<= (I - 1);
         
         pOverlay->tap_scale = (float)srcW / (float)dstW;
         if(pOverlay->tap_scale < 1.0) 		pOverlay->tap_scale = 1.0;
         
         if((srcW % dstW))
            pOverlay->HUSF = ((srcW - dstW) << 16) / dstW;
         else
            pOverlay->HUSF = 0;
   }

   if(dstH < OVERLAY_MIN_HEIGHT) dstH = OVERLAY_MIN_HEIGHT;

   if(dstH == srcH) {

      pOverlay->VUSF   = 0x00;
      pOverlay->IntBit |= 0x0A;

   }else if(dstH > srcH) {

      dstH += 2;
      pOverlay->IntBit |= 0x08;


      if(pOverlay->havetapscaler) {
	 	
         if((dstH > 2) && (srcH > 2)) {
            pOverlay->VUSF = (((srcH - 2) << 16) - 32768 + dstH - 3) / (dstH - 2);
         } else {
            pOverlay->VUSF = ((srcH << 16) + dstH - 1) / dstH;
         }

      } else {
         pOverlay->VUSF = (srcH << 16) / dstH;
      }

   }else {

      I = srcH / dstH;
      pOverlay->IntBit |= 0x02;

      if(I < 2) {
         pOverlay->VUSF = ((srcH - dstH) << 16) / dstH;
#if 0
	/* Needed for LCD-scaling modes */
	if((flag) && (mult = (srcH / origdstH)) >= 2) {
	   pOverlay->pitch /= mult;
	}
#endif
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
	/*pOverlay->pitch = (CARD16)(srcPitch * I);*/
      }
   }
}



/*********************************
 *     Calc line buffer size     *
 *********************************/

static CARD16
calc_line_buf_size(CARD32 srcW, CARD8 wHPre, /*CARD8 planar,*/ SiSXvMCContext *pSiSXvMC)
{
    CARD32 I, mask = 0xffffffff;
    CARD32 shift = (pSiSXvMC->ChipID == 662 || pSiSXvMC->ChipID == 671) ? 1 : 0;

    /* FIXME: Need to calc line buffer length not according
     * to total source width but width of source actually
     * visible on screen. Fixes flicker bug if overlay 1
     * (much) bigger than screen and being moved outside
     * screen.
     */

   /* ToDo: So far we only support plannar format (YV12) */
#if 0   
   if(planar) {
#endif
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
		   shift += 11;
		   mask <<= shift;
		   I = srcW >> shift;
		   if((mask & srcW) != srcW) I++;
		   I <<= 8;
		   break;
	    default:
		shift += 7;
		mask <<= shift;
		I = srcW >> shift;
		if((mask & srcW) != srcW) I++;
		I <<= 4;
		break;
	}
	
 /* ToDo: So far we only support plannar format (YV12) */
#if 0   
    } else { /* packed */

	shift += 3;
	mask <<= shift;
	I = srcW >> shift;
	if((mask & srcW) != srcW) I++;

    }
#endif

    if(I <= 3) I = 4;

    return((CARD16)(I - 1));
}



void CalculateOverlayRectangle(SiSOverlayRec *pOverlay){

   SiSXvMCContext* pSiSXvMC;
   float ratio;

   pSiSXvMC = (SiSXvMCContext*)pOverlay->privContext;
   
   pOverlay->startx = pOverlay->starty = 0;

   /* left */	
   if(pOverlay->x < 0){
      ratio = (-1.0 * (pOverlay->x)) / (float)(pOverlay->window_width);
      pOverlay->startx = (unsigned int)(ratio * pOverlay->srcw) & ~pOverlay->PitchAlignmentMask;
      pOverlay->x = 0;
   }

   /* right */
   if(pOverlay->x2 >= pOverlay->HDisplay){      
      pOverlay->x2 = pOverlay->HDisplay;
   }
   

   /* up */
   if(pOverlay->y < 0){
      ratio = (-1.0 * (pOverlay->y)) / (float)(pOverlay->window_height);
      pOverlay->starty = (unsigned int)(ratio * pOverlay->srch);
      pOverlay->y = 0;
   }

   /* down */
   if(pOverlay->y2 >= pOverlay->VDisplay){
      
      if(pSiSXvMC->ChipID == 662)
	  /* hardware bug: the bottom can not be equal to the VDisplay */	
         pOverlay->y2 = pOverlay->VDisplay - 1; 
      else
         pOverlay->y2 = pOverlay->VDisplay;
	  
   }
}


/***************************************************************************
// Function: XvMCPutSurface
// Description:
// Arguments:
//  display: Connection to X server
//  surface: Surface to be displayed
//  draw: X Drawable on which to display the surface
//  srcx: X coordinate of the top left corner of the region to be
//          displayed within the surface.
//  srcy: Y coordinate of the top left corner of the region to be
//          displayed within the surface.
//  srcw: Width of the region to be displayed.
//  srch: Height of the region to be displayed.
//  destx: X cordinate of the top left corner of the destination region
//         in the drawable coordinates.
//  desty: Y cordinate of the top left corner of the destination region
//         in the drawable coordinates.
//  destw: Width of the destination region.
//  desth: Height of the destination region.
//  flags: One or more of the following.
//     XVMC_TOP_FIELD - Display only the Top field of the surface.
//     XVMC_BOTTOM_FIELD - Display only the Bottom Field of the surface.
//     XVMC_FRAME_PICTURE - Display both fields or frame.
//
//   This function is organized so that we wait as long as possible before
//   touching the overlay registers. Since we don't know that the last
//   flip has happened yet we want to give the overlay as long as
//   possible to catch up before we have to check on its progress. This
//   makes it unlikely that we have to wait on the last flip.
***************************************************************************/
Status XvMCPutSurface(Display *display,XvMCSurface *surface,
		      Drawable draw, short srcx, short srcy,
		      unsigned short srcw, unsigned short srch,
		      short destx, short desty,
		      unsigned short destw, unsigned short desth,
		      int flags) {

  int i,j;
  int uid;
  unsigned short iobase, video_port, sr_port;
  unsigned char index, value;
  SiSXvMCContext *pSiSXvMC;
  SiSXvMCSurface *pSiSSurface;
  SiSXvMCSubpicture *pSiSSubp;
  SiSOverlayRec *overlay;
  /*SISOverlayPtr overlay;*/
  CARD8 LineBufferSize;
  short srcPitch; 
  int PitchAlignmentMask;
  int AdddressHighestBits;
  Window win,root,parent,*pChilds;
  uint d;
  uint nChilds;
  CARD8 fmt;
  CARD32 colorkey;
  int			XvChromaMin, XvChromaMax;
  CARD32 PSY,PSU,PSV;
  CARD32 pitch;
  CARD32 PSS;/* subpicture */
  

#ifdef XVMCDEBUG
  printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif

  pSiSSurface = (SiSXvMCSurface *)surface->privData;
  pSiSXvMC = (SiSXvMCContext *)pSiSSurface->privContext;
  overlay = pSiSXvMC->pOverlay;
  PitchAlignmentMask = overlay->PitchAlignmentMask;
  AdddressHighestBits = overlay->AdddressHighestBits;
  overlay->srcx = srcx; overlay->srcy = srcy;
  overlay->srcw = srcw; overlay->srch = srch;


#if 0
  /* Temp Path */
  if(pSiSSurface->FrameType == B_TYPE || pSiSSurface->status != SurfaceDisplaying){
  	pSiSSurface->status = SurfaceIdle;
   	return Success;
  }
#endif
  
  /* set user as root */
  uid = getuid();
  if(setuid(0) < 0){
  	printf("[XvMC] %s: Can't set the user root.\n\t Please check the Application mode.\n",
  		__FUNCTION__);
  return BadAlloc;
  }
  if(iopl(3) < 0){
  	printf("[XvMC] %s: Can't set ports available.\n", __FUNCTION__);
  return BadAlloc;
  }

  /* Get Relocated io */ 
  outSISREG32(0xcf8, 0x80010018);
  iobase = inSISREG32(0xcfc) & 0xFFFC;
  
  video_port = iobase + VIDEOOFFSET;
  sr_port = iobase + SROFFSET;
  
  /* set source pitch */
  switch(surface->surface_type_id){
  case FOURCC_YV12:
  	srcPitch = (srcw + PitchAlignmentMask) & ~(PitchAlignmentMask);
  	/* overlay->planar = 1;
	overlay->planar_shiftpitch = 1; */
	
	break;
   default:
   	printf("[XvMC] %s: This image format (id: 0x%x) is not supported\n",
		surface->surface_id);
	break;		
  }

  /* calculate the scale factor & line buffer size (not set!)*/
  calc_scale_factor(srcw, srch, destw, desth, overlay, srcPitch); 
  LineBufferSize = calc_line_buf_size((CARD32)srcw, overlay->wHPre, pSiSXvMC);
  

  /* use the first overlay */ 
  setregmask(video_port, Index_VI_Control_Misc2, 0x00, 0x01);
  setregmask(video_port, Index_VI_Control_Misc1, 0x00, 0x01);


  /* 
      ToDo:
      The overlay choosing is moved to the initalization of context,
      but actully we should do that here. This is for the cases of mergedFB,
      the overlay may be moved from one CRT to another. We should 
      update the overlay dynamically.
    */
#if 0  
  /* only overlay -> CRT1 */
  setregmask(sr_port, 0x06, 0x00, 0xc0); 
  setregmask(sr_port, 0x32, 0x00, 0xc0);
#endif
 
  /* merge buffer ..........................*/
  setregmask(video_port, Index_VI_Control_Misc2, 0x10, 0xff);
  /*
  setregmask(video_port, Index_VI_Control_Misc1, 0x04, 0x04);
  setregmask(video_port, Index_VI_Control_Misc2, 0x01, 0xb1);
  setregmask(video_port, Index_VI_Control_Misc1, 0x04, 0x04);
  */


  /* set format */
  switch (surface->surface_type_id){
    case FOURCC_YV12:
	fmt = 0x0c;
	break;
    default:
   	printf("[XvMC] %s: This image format (id: 0x%x) is not supported\n",
		surface->surface_type_id);
	return BadValue;	
  }
  setregmask(video_port, Index_VI_Control_Misc0, fmt, 0xfc);


  /* set color key */
  colorkey = 0x000101fe;
  {
    CARD8 r, g, b;    
    b = (CARD8)(colorkey & 0xFF);
    g = (CARD8)((colorkey >> 8) & 0xFF);
    r = (CARD8)((colorkey >> 16) & 0xFF);
    
    setregmask(video_port, Index_VI_Overlay_ColorKey_Blue_Min  ,(CARD8)b, 0xff);
    setregmask(video_port, Index_VI_Overlay_ColorKey_Green_Min ,(CARD8)g, 0xff);
    setregmask(video_port, Index_VI_Overlay_ColorKey_Red_Min   ,(CARD8)r, 0xff);
    
    setregmask(video_port, Index_VI_Overlay_ColorKey_Blue_Max  ,(CARD8)b, 0xff);
    setregmask(video_port, Index_VI_Overlay_ColorKey_Green_Max ,(CARD8)g, 0xff);
    setregmask(video_port, Index_VI_Overlay_ColorKey_Red_Max   ,(CARD8)r, 0xff);
  }


  /* set chroma key format */
  XvChromaMin = 0x000101fe;
  XvChromaMax = 0x000101ff;
  {
    CARD8 r1, g1, b1;
    CARD8 r2, g2, b2;

    b1 = (CARD8)(XvChromaMin & 0xFF);
    g1 = (CARD8)((XvChromaMin >> 8) & 0xFF);
    r1 = (CARD8)((XvChromaMin >> 16) & 0xFF);
    b2 = (CARD8)(XvChromaMax & 0xFF);
    g2 = (CARD8)((XvChromaMax >> 8) & 0xFF);
    r2 = (CARD8)((XvChromaMax >> 16) & 0xFF);

    setregmask(video_port, Index_VI_Overlay_ChromaKey_Blue_V_Min  ,(CARD8)b1, 0xff);
    setregmask(video_port, Index_VI_Overlay_ChromaKey_Green_U_Min ,(CARD8)g1, 0xff);
    setregmask(video_port, Index_VI_Overlay_ChromaKey_Red_Y_Min   ,(CARD8)r1, 0xff);

    setregmask(video_port, Index_VI_Overlay_ChromaKey_Blue_V_Max  ,(CARD8)b2, 0xff);
    setregmask(video_port, Index_VI_Overlay_ChromaKey_Green_U_Max ,(CARD8)g2, 0xff);
    setregmask(video_port, Index_VI_Overlay_ChromaKey_Red_Y_Max   ,(CARD8)r2, 0xff);
  }


  /* brightness, contrast, hue, and saturation */  
  setregmask(video_port, Index_VI_Brightness, 10, 0xff);
  setregmask(video_port, Index_VI_Contrast_Enh_Ctrl, 2,0x07);
  setregmask(video_port, Index_VI_Hue, 0 ,0xff);
  setregmask(video_port, Index_VI_Saturation, 0, 0xff);


  /* set line buffer size */
  setreg(video_port, Index_VI_Line_Buffer_Size, LineBufferSize);
  if(pSiSXvMC->ChipID == 741)
     setregmask(video_port, Index_VI_Key_Overlay_OP, (0x5f >> 1) & 0x80, 0x80);
  
  /* set color key mode */
  setregmask(video_port, Index_VI_Key_Overlay_OP, 0x3, 0x0f);


  /* Unlock address registers */
  setregmask(video_port, Index_VI_Control_Misc1, 0x20, 0x20);
  

  /* calculate destination window position & size */
  win = draw;
  XQueryTree(display,win,&root,&parent,&pChilds,&nChilds);
  if(nChilds) XFree(pChilds);
  XGetGeometry(display,win, &root, &overlay->x, &overlay->y, 
  	      &overlay->window_width, &overlay->window_height, &d, &d);
  win = parent;
  do {
    XQueryTree(display,win,&root,&parent,&pChilds,&nChilds);
    if(nChilds) XFree(pChilds);
    XGetGeometry(display,win, &root, &overlay->x2, &overlay->y2, &d, &d, &d, &d);
    overlay->x += overlay->x2;
    overlay->y += overlay->y2;
    win = parent;
  }while(win != root);
  overlay->x += destx;
  overlay->y += desty;
  overlay->x2 = overlay->x + destw;
  overlay->y2 = overlay->y + desth;
  CalculateOverlayRectangle(overlay);

#if 0   
   /* wait for the scanline */
   CARD16 currtop, currbottom; 
   int watchdog;
   CARD8 regvalue;
   currtop = currbottom = 0x0000;
   getreg(video_port, Index_VI_Win_Ver_Disp_Start_Low, regvalue);  
   currtop |= regvalue;
   getreg(video_port, Index_VI_Win_Ver_Disp_End_Low, regvalue);
   currbottom |= regvalue;
   getreg(video_port, Index_VI_Win_Ver_Over, regvalue);
   currtop |= (regvalue & 0x0f) << 8;
   currbottom |= (regvalue & 0xf0) << 4;
   //pOverlay->oldtop = mytop;
   watchdog = 0xffff;
   //if(mytop < screenY - 2) {
   do {
      /* CRT1 scanline */		
      outMMIO32(mmioAddress, REG_PRIM_CRT_COUNTER, 0x00000001);
      watchdog = inMMIO32(mmioAddress, REG_PRIM_CRT_COUNTER);
      watchdog = ((CARD16)((watchdog >> 16) & 0x07FF));
   } while((watchdog <= currtop) || (watchdog >= currbottom));
   //}
#endif 

  /* Set destination window position & size*/
  setreg(video_port, Index_VI_Win_Hor_Disp_Start_Low, (CARD8)(overlay->x));
  setreg(video_port, Index_VI_Win_Hor_Disp_End_Low,   (CARD8)(overlay->x2));
  setreg(video_port, Index_VI_Win_Hor_Over,  (CARD8)((overlay->x2>>8)<<4 | (overlay->x>> 8)));
  setreg(video_port, Index_VI_Win_Ver_Disp_Start_Low, (CARD8)(overlay->y));
  setreg(video_port, Index_VI_Win_Ver_Disp_End_Low,   (CARD8)(overlay->y2));
  setreg(video_port, Index_VI_Win_Ver_Over, (CARD8)((overlay->y2>>8)<<4 | (overlay->y>> 8)));


   /* Contrast factor */
   setregmask(video_port, Index_VI_Contrast_Enh_Ctrl,  (CARD8)0x55, 0xc0);
   setregmask(video_port, Index_VI_Contrast_Factor,        0x3ab, 0xff);


   /* set YUV starting address */
   PSY = overlay->startx + overlay->starty * pitch +pSiSSurface->offsets[0]; 
   PSU = (overlay->startx >> 1) + (overlay->starty >> 1) * pitch + pSiSSurface->offsets[1];
   PSV = (overlay->startx >> 1) + (overlay->starty >> 1) * pitch + pSiSSurface->offsets[2];
   PSY >>= overlay->AddressShiftNum;
   PSU >>= overlay->AddressShiftNum;
   PSV >>= overlay->AddressShiftNum;
   pitch = srcPitch >> overlay->AddressShiftNum;
   
   

  /* Y */
  setregmask(video_port, Index_VI_Disp_Y_Buf_Start_Low,    (CARD8)(PSY), 0xff);
  setregmask(video_port, Index_VI_Disp_Y_Buf_Start_Middle, (CARD8)(PSY >> 8), 0xff);
  setregmask(video_port, Index_VI_Disp_Y_Buf_Start_High,   (CARD8)(PSY >> 16), 0xff);
  setregmask(video_port, Index_VI_Disp_Y_Buf_Pitch_Low, (CARD8)(pitch), 0xff);
  setregmask(video_port, Index_VI_Disp_Y_Buf_Pitch_High, (CARD8)(pitch >> 12), 0xff);
  setregmask(video_port, Index_VI_Y_Buf_Start_Over, ((CARD8)(PSY >> 24) & AdddressHighestBits), AdddressHighestBits);
  setregmask(video_port, Index_VI_Disp_Y_UV_Buf_Pitch_Middle, (CARD8)(pitch >> 8), 0x0f);


  pitch >>=1;
  /* Set U/V pitch */
  setregmask(video_port, Index_VI_Disp_UV_Buf_Pitch_Low, (CARD8)pitch, 0xff);
  setregmask(video_port, Index_VI_Disp_Y_UV_Buf_Pitch_Middle, (CARD8)(pitch >> 4), 0xf0);
  
 
  /* U/V */
  setregmask(video_port, Index_VI_U_Buf_Start_Low,   (CARD8)PSU, 0xff);
  setregmask(video_port, Index_VI_U_Buf_Start_Middle,(CARD8)(PSU >> 8), 0xff);
  setregmask(video_port, Index_VI_U_Buf_Start_High,  (CARD8)(PSU >> 16), 0xff);
  
  setregmask(video_port, Index_VI_V_Buf_Start_Low,   (CARD8)PSV, 0xff);
  setregmask(video_port, Index_VI_V_Buf_Start_Middle,(CARD8)(PSV >> 8), 0xff);
  setregmask(video_port, Index_VI_V_Buf_Start_High,  (CARD8)(PSV >> 16), 0xff);


  setregmask(video_port, Index_VI_Disp_UV_Buf_Pitch_High, (CARD8)(pitch >> 12),0xff);
  setregmask(video_port, Index_VI_U_Buf_Start_Over, ((CARD8)(PSU >> 24) & AdddressHighestBits), AdddressHighestBits);
  setregmask(video_port, Index_VI_V_Buf_Start_Over, ((CARD8)(PSV >> 24) & AdddressHighestBits), AdddressHighestBits);


 /* lock address registers */
  setregmask(video_port, Index_VI_Control_Misc1, 0x00, 0x20);


  /* Set scale factor */
  if(overlay->havetapscaler && overlay->tap_scale != overlay->tap_scale_old){
      if(set_dda_regs(pSiSXvMC, overlay->tap_scale) < 0){
        printf("[XvMC] DDA registers setting Error! Stop.\n");
        return BadValue;
      }
      overlay->tap_scale_old = overlay->tap_scale;
  }
  setreg(video_port, Index_VI_Hor_Post_Up_Scale_Low, (CARD8)(overlay->HUSF));
  setreg(video_port, Index_VI_Hor_Post_Up_Scale_High,(CARD8)((overlay->HUSF) >> 8));
  setreg(video_port, Index_VI_Ver_Up_Scale_Low, (CARD8)(overlay->VUSF));
  setreg(video_port, Index_VI_Ver_Up_Scale_High, (CARD8)((overlay->VUSF)>>8));
  
  setregmask(video_port, Index_VI_Scale_Control,     (overlay->IntBit << 3) |
                                                         (overlay->wHPre), 0x7f);

   /* set line number  & set down scale bit ---- 662 & 671*/
  if(pSiSXvMC->ChipID == 662 || pSiSXvMC->ChipID == 671){
    setreg(video_port, Source_VertLine_Number_Low, (CARD8)srch);
    setregmask(video_port, Source_VertLine_Number_High, srch >> 8, 0x07);
    if(overlay->IntBit & 0x2)		setregmask(video_port, Index_VI_Control_Misc1, 0x40, 0x40);
    else		setregmask(video_port, Index_VI_Control_Misc1, 0x00, 0x40);
   }


  setregmask(video_port, Index_VI_Control_Misc0, 0x02, 0x02);
  setregmask(video_port, Index_VI_Control_Misc3, 0x01, 0x01);

  pSiSSurface->status = SurfaceIdle;


  

   /******************************************************************/
   /* Section to Draw Subpicture */  
   /******************************************************************/

   CARD8 *palette;
   pSiSSubp = (SiSXvMCSubpicture *)pSiSSurface->Subp;
   if(pSiSSubp == NULL){
       setregmask(video_port, Index_VI_SubPict_Scale_Control, 0x00, 0x40);
   	return Success;
   }
  
   setregmask(video_port, Index_VI_Control_Misc3, 0x04, 0x04);
    
   
   /* set format */
   switch(pSiSSubp->formatID){
   case FOURCC_IA44:
   	setregmask(video_port, Index_VI_SubPict_Format, 0x00, 0x03);
#ifdef SUBPDEBUG
	printf("[XvMC] subpicture format: IA44\n");
#endif
	break;
   case FOURCC_AI44:
   	setregmask(video_port, Index_VI_SubPict_Format, 0x00, 0x03);
#ifdef SUBPDEBUG
	printf("[XvMC] subpicture format: AI44\n");
#endif
	break;
   default:
   	printf("[XvMC] %s: This format of subpicture (id: 0x%x) is not support!\n",
		__FUNCTION__, pSiSSubp->formatID);
	return BadValue;
   }
   
   /* set the palette */
   palette = &pSiSSubp->palette[0][0];
   for(i = 0x40; i <= 0x5f; i++){
   	setreg(video_port, i, *palette);
	palette++;
   }

   /* Set subpicture starting address */
   PSS = (CARD32)(overlay->startx + pSiSSubp->offset);
   PSS >>= overlay->SubpShift;
   setreg(video_port, Index_VI_SubPict_Buf_Start_Low, (CARD8)PSS);
   setreg(video_port, Index_VI_SubPict_Buf_Start_Middle, (CARD8)(PSS>>8));
   setregmask(video_port, Index_VI_SubPict_Buf_Start_High, (CARD8)(PSS>>16), 0x3f);
   setregmask(video_port, Index_VI_SubPict_Start_Over, (CARD8)(PSS>>22), 0x03);
   if(pSiSXvMC->ChipID == 671)
      setregmask(video_port, Index_VI_SubPict_Start_Pitch_Ext, (CARD8)(PSS>>24), 0x07);
   
   /* set pitch */
   /* printf("pitch = %d(0x%x)",pSiSSubp->pitch,pSiSSubp->pitch); */
   setreg(video_port, Index_VI_SubPict_Buf_Pitch, (CARD8)(pSiSSubp->pitch >> overlay->SubpShift));
   setregmask(video_port, Index_VI_SubPict_Buf_Pitch_High, (CARD8)(pSiSSubp->pitch >> (overlay->SubpShift + 8)), 0x1f);
   if(pSiSXvMC->ChipID == 671)
      setregmask(video_port, Index_VI_SubPict_Start_Pitch_Ext, (CARD8)(pSiSSubp->pitch >> (overlay->SubpShift + 9)), 0x70);
   setreg(video_port,Index_VI_FIFO_Max, (CARD8)(pSiSSubp->pitch >> overlay->SubpShift));

  /* set scaling factor */
  setreg(video_port, Index_VI_SubPict_Hor_Scale_Low, (CARD8)(overlay->HUSF));
  setreg(video_port, Index_VI_SubPict_Hor_Scale_High, (CARD8)((overlay->HUSF) >> 8));
  setreg(video_port, Index_VI_SubPict_Vert_Scale_Low, (CARD8)(overlay->VUSF));
  setreg(video_port, Index_VI_SubPict_Vert_Scale_High, (CARD8)((overlay->VUSF)>>8));
  
  setregmask(video_port, Index_VI_SubPict_Scale_Control,     ((overlay->IntBit & ~0x01)<< 3) | 
  	               (overlay->IntBit & 0x01), 0x3f);

   /* clear preset registers */
   setregmask(video_port, Index_VI_SubPict_Buf_Start_High, 0x00, 0x40);
   setreg(video_port, Index_VI_SubPict_Buf_Preset_Low, 0x00);
   setreg(video_port, Index_VI_SubPict_Buf_Preset_Middle, 0x00);
   if(pSiSXvMC->ChipID == 671)
      setregmask(video_port, Index_VI_SubPict_Buf_Preset_Ext, 0x00, 0x07);
   

   /* set Threshold */
   setreg(video_port,Index_VI_SubPict_Threshold, 0x03);


  /* Enable Subpicture */
  setregmask(video_port, Index_VI_SubPict_Scale_Control, 0x40, 0x40);

  setregmask(video_port, Index_VI_Control_Misc3, 0x04, 0x04);
  pSiSSubp->status = SurfaceIdle;
  pSiSSurface->Subp = NULL;
   
   /* setuid(uid); */
   return Success;
}


/***************************************************************************
// Function: XvMCSyncSurface
// Arguments:
//   display - Connection to the X server
//   surface - The surface to synchronize
// Info:
// Returns: Status
***************************************************************************/
Status XvMCSyncSurface(Display *display,XvMCSurface *surface) {


   SiSXvMCSurface *pSiSXvMCSurface = surface->privData;
   SiSXvMCContext *pSiSXvMC = pSiSXvMCSurface->privContext;
   CARD32 CmdBufferBusy;
   unsigned short CmdBuff;
   CARD32 FrameBuffIndicator, DecodeFinished;
   int shiftvalue;
   CARD32 FrameBufferStatus;
   int backref;
#ifdef XVMCDEBUG
  printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif


#if 0
   /* Temp Path */
   if(pSiSXvMCSurface->FrameType == B_TYPE){
   	return Success;
   }
#endif

   /* finding a available command/data buffer */
   CmdBufferBusy = inMMIO32(mmioAddress, REG_MPEG_CMDBUFFG);
   for(CmdBuff = 0; CmdBuff<CMD_BUFFER_MAX; CmdBuff++){
      if(CmdBufferBusy%2){
         CmdBufferBusy >>= 1;
         continue;
      	}
       else 
	   break;
   }

   shiftvalue = pSiSXvMC->FrameBufShift;

   /* Setting the Frame buffer address */
   switch(pSiSXvMCSurface->MyNum){
   case 0:
      outMMIO32(mmioAddress, REG_BUFFER_Y0, pSiSXvMCSurface->offsets[0] >> shiftvalue);
      outMMIO32(mmioAddress, REG_BUFFER_CB0, pSiSXvMCSurface->offsets[1]  >> shiftvalue);
      outMMIO32(mmioAddress, REG_BUFFER_CR0, pSiSXvMCSurface->offsets[2]  >> shiftvalue);
      break;
   case 1:
      outMMIO32(mmioAddress, REG_BUFFER_Y1, pSiSXvMCSurface->offsets[0] >> shiftvalue);
      outMMIO32(mmioAddress, REG_BUFFER_CB1, pSiSXvMCSurface->offsets[1] >> shiftvalue);
      outMMIO32(mmioAddress, REG_BUFFER_CR1, pSiSXvMCSurface->offsets[2] >> shiftvalue);
      break;
   case 2:
      outMMIO32(mmioAddress, REG_BUFFER_Y2, pSiSXvMCSurface->offsets[0] >> shiftvalue);
      outMMIO32(mmioAddress, REG_BUFFER_CB2, pSiSXvMCSurface->offsets[1] >> shiftvalue);
      outMMIO32(mmioAddress, REG_BUFFER_CR2, pSiSXvMCSurface->offsets[2] >> shiftvalue);
      break;
   case 3:
      outMMIO32(mmioAddress, REG_BUFFER_Y3, pSiSXvMCSurface->offsets[0] >> shiftvalue);
      outMMIO32(mmioAddress, REG_BUFFER_CB3, pSiSXvMCSurface->offsets[1] >> shiftvalue);
      outMMIO32(mmioAddress, REG_BUFFER_CR3, pSiSXvMCSurface->offsets[2] >> shiftvalue);
      break;
   case 4:
      outMMIO32(mmioAddress, REG_BUFFER_Y4, pSiSXvMCSurface->offsets[0] >> shiftvalue);
      outMMIO32(mmioAddress, REG_BUFFER_CB4, pSiSXvMCSurface->offsets[1] >> shiftvalue);
      outMMIO32(mmioAddress, REG_BUFFER_CR4, pSiSXvMCSurface->offsets[2] >> shiftvalue);
      break;
   case 5:
      outMMIO32(mmioAddress, REG_BUFFER_Y5, pSiSXvMCSurface->offsets[0] >> shiftvalue);
      outMMIO32(mmioAddress, REG_BUFFER_CB5, pSiSXvMCSurface->offsets[1] >> shiftvalue);
      outMMIO32(mmioAddress, REG_BUFFER_CR5, pSiSXvMCSurface->offsets[2] >> shiftvalue);
      break;
   default:
     printf("[XvMC] %s: Num of Frame Buffer Error! Stop.\n",__FUNCTION__);
     return BadValue;
   }



   
   /* setting the frame buffer: ToDo for P- & B-Frame !*/
   switch(pSiSXvMCSurface->pict_struct){
   case TOP_FIELD:
   case FRAME_PICTURE:
	  FrameBuffIndicator = 1 << (8*(pSiSXvMCSurface->MyNum/4) + (pSiSXvMCSurface->MyNum%4));
	  break;
   case BOTTOM_FIELD:
         FrameBuffIndicator = 1 << 4 + (6*(pSiSXvMCSurface->MyNum/4) + (pSiSXvMCSurface->MyNum%4));
	  break;
   }   
   outMMIO32(mmioAddress, REG_BUF_DEC_MASK, FrameBuffIndicator);
   

   /* check if the reference surfaces are ready. If not, let it ready.*/ 
   backref = pSiSXvMCSurface->BackRefer;
   if(backref >= 0 && SurfaceList[backref]->status == SurfaceRendering)
      XvMCSyncSurface(display, SurfaceList[backref]->xvmcsurface);

#ifdef XvMCHWDATADUMP
   /* before firing, we dump data. */
   if(dump == NULL)	
      printf("[XvMC] Hw dump: File open error!\n");
   if(fwrite((void*)pSiSXvMCSurface->MyBuff, 1, pSiSXvMC->CmdBufSize, dump) != 
                pSiSXvMC->CmdBufSize)
      printf("[XvMC] Hw dump: File write error!\n");
#endif

   /* FIRE!*/
   if(pSiSXvMC->ChipID == 671)  outMMIO32(mmioAddress, REG_CLK_CMD, 0x03f1);
   outMMIO32(mmioAddress, REG_BUFFER_CMD, 
   	(CARD32)pSiSXvMCSurface->MyOffset  | CmdBuff);
   if(pSiSXvMC->ChipID == 671)  outMMIO32(mmioAddress, REG_CLK_CMD, 0x03f0);

   DecodeFinished = inMMIO32(mmioAddress,REG_BUF_DEC_FLAG);
   /* waiting until decoded completed */  
   while(DecodeFinished & FrameBuffIndicator){

#ifdef XVMCDEBUG
      CARD32 mmiovalue;     
      FrameBufferStatus = inMMIO32(mmioAddress,REG_MPEG_STATUS);
      printf("[XvMC] Decoder Status (0x8754): 0x%x, Buffer state (0x8718)= 0x%x\n",FrameBufferStatus, DecodeFinished);
      mmiovalue = inMMIO32(mmioAddress,REG_MPEG_PLP0);
      printf("[XvMC] PLP0 (0x8758): 0x%x, ", mmiovalue);
      mmiovalue = inMMIO32(mmioAddress,REG_MPEG_PLP1);
      printf("PLP1 (0x875c): 0x%x, ", mmiovalue);
      if(pSiSXvMC->ChipID == 671){
         mmiovalue = inMMIO32(mmioAddress,REG_MPEG_PLP2);
         printf("PLP2 (0x8760): 0x%x\n[XvMC] ", mmiovalue);
      }
      mmiovalue = inMMIO32(mmioAddress,REG_MPEG_MBT);
      printf("MBT (0x8764): 0x%x\n", mmiovalue);
#endif
#if 0
      FrameBufferStatus = inMMIO32(mmioAddress,REG_MPEG_STATUS);
      if(FrameBufferStatus == 0x220)	break;
#endif
      DecodeFinished = inMMIO32(mmioAddress,REG_BUF_DEC_FLAG);
   }

   pSiSXvMCSurface->status = SurfaceDisplaying;

   return Success;
}


/***************************************************************************
// Function: XvMCFlushSurface
// Description:
//   This function commits pending rendering requests to ensure that they
//   wll be completed in a finite amount of time.
// Arguments:
//   display - Connection to X server
//   surface - Surface to flush
// Info:
//   This command is a noop becuase we always dispatch buffers in
//   render. There is little gain to be had with 4k buffers.
// Returns: Status
***************************************************************************/
Status XvMCFlushSurface(Display * display, XvMCSurface *surface) {
#ifdef XVMCDEBUG
  printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif
  return Success;
}

/***************************************************************************
// Function: XvMCGetSurfaceStatus
// Description:
// Arguments:
//  display: connection to X server
//  surface: The surface to query
//  stat: One of the Following
//    XVMC_RENDERING - The last XvMCRenderSurface command has not
//                     completed.
//    XVMC_DISPLAYING - The surface is currently being displayed or a
//                     display is pending.
***************************************************************************/
Status XvMCGetSurfaceStatus(Display *display, XvMCSurface *surface,
			    int *stat) {
			    
   SiSXvMCSurface *privSurface;
   SiSXvMCContext *pSiSXvMC;
   int temp;
  
#ifdef XVMCDEBUG
   printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif

   if((display == NULL) || (surface == NULL) || (stat == NULL)) {
      return BadValue;
   }
   if(surface->privData == NULL) {
      return BadValue;
   }
   *stat = 0;
   privSurface = surface->privData;
   
   pSiSXvMC = privSurface->privContext;
   if(pSiSXvMC == NULL) {
      return (error_base + XvMCBadSurface);
   }

   SIS_LOCK(pSiSXvMC,0);
   
   switch(privSurface->status){
   case SurfaceIdle:
      *stat = 0;
      privSurface->DisplayingAskCounter = 0;
      break;
   case SurfaceRendering:
      *stat |= XVMC_RENDERING;
      privSurface->DisplayingAskCounter = 0;
      break;
   case SurfaceDisplaying:
      if(privSurface->DisplayingAskCounter++ < 2)
         *stat |= XVMC_DISPLAYING;
      else{/* too many times:  that means we should cancel this frame */
         *stat = 0;
         privSurface->DisplayingAskCounter = 0;
         privSurface->status = SurfaceIdle;
      }
      break;
   default:
      printf("[XvMC] Surface status Error!! Stop.\n");
      SIS_UNLOCK(pSiSXvMC);
      return XvMCBadSurface;
   }
   SIS_UNLOCK(pSiSXvMC);

   return Success;

}

/***************************************************************************
// 
//  Surface manipulation functions
//
***************************************************************************/

/***************************************************************************
// Function: XvMCHideSurface
// Description: Stops the display of a surface.
// Arguments:
//   display - Connection to the X server.
//   surface - surface to be hidden.
//
// Returns: Status
***************************************************************************/
Status XvMCHideSurface(Display *display, XvMCSurface *surface) {


#ifdef XVMCDEBUG
  printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif

  SiSXvMCSurface *pSiSSurface;

  /* Did we get a good display and surface passed into us? */
  if(display == NULL) {
    return BadValue;
  }

  if(surface == NULL) {
    return (error_base + XvMCBadSurface);
  }
  
  /* Get surface private data pointer */
  if(surface->privData == NULL) {
    return (error_base + XvMCBadSurface);
  }
  pSiSSurface = (SiSXvMCSurface *)surface->privData;

  pSiSSurface->status = SurfaceIdle;

  return Success;
}




/***************************************************************************
//
// Functions that deal with subpictures
//
***************************************************************************/



/***************************************************************************
// Function: XvMCCreateSubpicture
// Description: This creates a subpicture by filling out the XvMCSubpicture
//              structure passed to it and returning Success.
// Arguments:
//   display - Connection to the X server.
//   context - The context to create the subpicture for.
//   subpicture - Pre-allocated XvMCSubpicture structure to be filled in.
//   width - of subpicture
//   height - of subpicture
//   xvimage_id - The id describing the XvImage format.
//
// Returns: Status
***************************************************************************/
Status XvMCCreateSubpicture(Display *display, XvMCContext *context,
                          XvMCSubpicture *subpicture,
                          unsigned short width, unsigned short height,
                          int xvimage_id) {
                          
   SiSXvMCContext *pSiSXvMC;
   SiSXvMCSubpicture *pSiSSubpicture;
   SiSOverlayRec *pSiSOverlay;
   int priv_count;
   uint *priv_data;
   Status ret;

#ifdef XVMCDEBUG
   printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif

   if((subpicture == NULL) || (context == NULL) || (display == NULL)){ 
      return BadValue;
   }
  
   pSiSXvMC = (SiSXvMCContext *)context->privData;
   if(pSiSXvMC == NULL) {
      return (error_base + XvMCBadContext);
   }
   pSiSOverlay = pSiSXvMC->pOverlay;

   
   subpicture->context_id = context->context_id;
   subpicture->xvimage_id = xvimage_id;
  
   /* These need to be checked to make sure they are not too big! */
   subpicture->width = width;
   subpicture->height = height;

   subpicture->privData = (SiSXvMCSubpicture *)malloc(sizeof(SiSXvMCSubpicture));

    if(!subpicture->privData) {
       return BadAlloc;
    }
    pSiSSubpicture = (SiSXvMCSubpicture *)subpicture->privData;


   if((ret = _xvmc_create_subpicture(display, context, subpicture,
   			    &priv_count, &priv_data))) {
#ifdef SUBPDEBUG
      printf("[XvMC] Unable to create XvMCSubpicture.\n");
#endif
      return ret;
   }
   
   if(priv_count != 1) {
      printf("[XvMC] _xvmc_create_subpicture() returned incorrect data size.\n");
      printf("\tExpected 1 got %d\n",priv_count);
      free(priv_data);
      return BadAlloc;
   }
   pSiSSubpicture->privContext = pSiSXvMC;
   pSiSXvMC->ref++;

   switch(subpicture->xvimage_id) {
   case FOURCC_IA44:
   case FOURCC_AI44:   
      pSiSSubpicture->formatID = xvimage_id;
      break;
   default:
      printf("[XvMC] Subpicture format is not supported\n");
      free(subpicture->privData);
      return BadMatch;
   }
   pSiSSubpicture->data = (drmAddress)(priv_data[0] + fbAddress);
   pSiSSubpicture->offset = (unsigned long)priv_data[0];
   pSiSSubpicture->pitch = (width + pSiSOverlay->SubPitchShiftMask) & ~pSiSOverlay->SubPitchShiftMask;
   pSiSSubpicture->status = SurfaceIdle;
   

  return Success;
}



/***************************************************************************
// Function: XvMCClearSubpicture
// Description: Clear the area of the given subpicture to "color".
//              structure passed to it and returning Success.
// Arguments:
//   display - Connection to the X server.
//   subpicture - Subpicture to clear.
//   x, y, width, height - rectangle in the subpicture to clear.
//   color - The data to file the rectangle with.
//
// Returns: Status
***************************************************************************/
Status XvMCClearSubpicture(Display *display, XvMCSubpicture *subpicture,
                          short x, short y,
                          unsigned short width, unsigned short height,
                          unsigned int color) {

   SiSXvMCContext *pSiSXvMC;
   SiSXvMCSubpicture *pSiSSubpicture;
   int i,j;
  
#ifdef XVMCDEBUG
   printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif
   
   if((subpicture == NULL) || (display == NULL)){
      return BadValue;
   }

   if(!subpicture->privData) {
      return (error_base + XvMCBadSubpicture);
   }
  pSiSSubpicture = (SiSXvMCSubpicture *)subpicture->privData;

   pSiSXvMC = (SiSXvMCContext *)pSiSSubpicture->privContext;
   if(pSiSXvMC == NULL) {
      return (error_base + XvMCBadSubpicture);
   }

   if((x < 0) || (x + width > subpicture->width)) {
      return BadValue;
   }

   if((y < 0) || (y + height > subpicture->height)) {
      return BadValue;
   }

#ifdef SUBPDEBUG
   printf("%s: w = %d, h = %d, color = 0x%x\n", __FUNCTION__, width, height, color);
#endif
  
   for(i=y; i<y + height; i++) {
      unsigned long addr = (unsigned long)(pSiSSubpicture->data) + (pSiSSubpicture->pitch*i)+x;
      memset((void *)addr, (char)color, width);
    }
   return Success;
}

/***************************************************************************
// Function: XvMCCompositeSubpicture
// Description: Composite the XvImae on the subpicture. This composit uses
//              non-premultiplied alpha. Destination alpha is utilized
//              except for with indexed subpictures. Indexed subpictures
//              use a simple "replace".
// Arguments:
//   display - Connection to the X server.
//   subpicture - Subpicture to clear.
//   image - the XvImage to be used as the source of the composite.
//   srcx, srcy, width, height - The rectangle from the image to be used.
//   dstx, dsty - location in the subpicture to composite the source.
//
// Returns: Status
***************************************************************************/
Status XvMCCompositeSubpicture(Display *display, XvMCSubpicture *subpicture,
                               XvImage *image,
                               short srcx, short srcy,
                               unsigned short width, unsigned short height,
                               short dstx, short dsty) {

   SiSXvMCContext *pSiSXvMC;
   SiSXvMCSubpicture *pSiSSubpicture;
   int i,j;
   unsigned long dst_addr, src_addr; 
   
#ifdef XVMCDEBUG
   printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif
   
   if((subpicture == NULL) || (display == NULL)){
      return BadValue;
   }

   if(!subpicture->privData) {
      return (error_base + XvMCBadSubpicture);
   }
   pSiSSubpicture = (SiSXvMCSubpicture *)subpicture->privData;

   pSiSXvMC = (SiSXvMCContext *)pSiSSubpicture->privContext;
   if(pSiSXvMC == NULL) {
      return (error_base + XvMCBadSubpicture);
   }

   if((srcx < 0) || (srcx + width > image->width)) {
      return BadValue;
   }

   if((dstx < 0) || (dstx + width > subpicture->width)) {
      return BadValue;
   }
 
   if((srcy < 0) || (srcy + height > image->height)) {
      return BadValue;
   }
 
   if((dsty < 0) || (dsty + height > subpicture->height)) {
     return BadValue;
   }
   
#ifdef SUBPDEBUG
     printf("%s: srcx=%d, srcy=%d, dstx=%d, dsty=%d, w = %d, h = %d\n ",
             __FUNCTION__,srcx,srcy,dstx,dsty,width,height);
#endif


   for(i=0; i<height; i++) {
     dst_addr = (unsigned long)(pSiSSubpicture->data + (pSiSSubpicture->pitch) * (i + dsty) + dstx);
     src_addr = (unsigned long)(image->data + image->pitches[0] * (i + srcy) + srcx);
     memcpy((void *)dst_addr, (void *)src_addr, width);
     memset((void *)src_addr, 0x00, width);/* clear the source memory */
   }

   pSiSSubpicture->status = SurfaceDisplaying;

   return Success;

}


/***************************************************************************
// Function: XvMCDestroySubpicture
// Description: Destroys the specified subpicture.
// Arguments:
//   display - Connection to the X server.
//   subpicture - Subpicture to be destroyed.
//
// Returns: Status
***************************************************************************/
Status XvMCDestroySubpicture(Display *display, XvMCSubpicture *subpicture) {

   SiSXvMCSubpicture *pSiSSubpicture;
   SiSXvMCContext *pSiSXvMC;

#ifdef XVMCDEBUG
   printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif

  
   if((display == NULL) || (subpicture == NULL)) {
      return BadValue;
   }
   if(!subpicture->privData) {
      return (error_base + XvMCBadSubpicture);
   }
   pSiSSubpicture = (SiSXvMCSubpicture *)subpicture->privData;
   pSiSXvMC = (SiSXvMCContext *)pSiSSubpicture->privContext;
   if(!pSiSXvMC) {
     return (error_base + XvMCBadSubpicture);
   }

   _xvmc_destroy_subpicture(display,subpicture);
 
   sis_free_privContext(pSiSXvMC);
 
   free(pSiSSubpicture);
   subpicture->privData = NULL;
   return Success;
}


/***************************************************************************
// Function: XvMCSetSubpicturePalette
// Description: Set the subpictures palette
// Arguments:
//   display - Connection to the X server.
//   subpicture - Subpiture to set palette for.
//   palette - A pointer to an array holding the palette data. The array
//     is num_palette_entries * entry_bytes in size.
// Returns: Status
***************************************************************************/

Status XvMCSetSubpicturePalette(Display *display, XvMCSubpicture *subpicture,
				unsigned char *palette) {
   SiSXvMCSubpicture *privSubpicture;
   int i,j;

#ifdef XVMCDEBUG
   printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif

   if((display == NULL) || (subpicture == NULL)) {
      return BadValue;
   }
   if(subpicture->privData == NULL) {
      return (error_base + XvMCBadSubpicture);
   }
   privSubpicture = (SiSXvMCSubpicture *)subpicture->privData;

   j = 0;
   for(i = 0; i<16; i++) {
      privSubpicture->palette[i][0] = palette[j++];
      privSubpicture->palette[i][1] = palette[j++];
   }
   
   privSubpicture->status = SurfaceRendering;
   
   return Success;
}

/***************************************************************************
// Function: XvMCBlendSubpicture
// Description: 
//    The behavior of this function is different depending on whether
//    or not the XVMC_BACKEND_SUBPICTURE flag is set in the XvMCSurfaceInfo.
//    we only support backend behavior.
//  
//    XVMC_BACKEND_SUBPICTURE not set ("frontend" behavior):
//   
//    XvMCBlendSubpicture is a no-op in this case.
//   
// Arguments:
//   display - Connection to the X server.
//   subpicture - The subpicture to be blended into the video.
//   target_surface - The surface to be displayed with the blended subpic.
//   source_surface - Source surface prior to blending.
//   subx, suby, subw, subh - The rectangle from the subpicture to use.
//   surfx, surfy, surfw, surfh - The rectangle in the surface to blend
//      blend the subpicture rectangle into. Scaling can ocure if 
//      XVMC_SUBPICTURE_INDEPENDENT_SCALING is set.
//
// Returns: Status
***************************************************************************/
Status XvMCBlendSubpicture(Display *display, XvMCSurface *target_surface,
                         XvMCSubpicture *subpicture,
                         short subx, short suby,
                         unsigned short subw, unsigned short subh,
                         short surfx, short surfy,
                         unsigned short surfw, unsigned short surfh) {

   SiSXvMCSurface* pSiSSurface;
   SiSXvMCSubpicture* pSiSSubpicture;

#ifdef XVMCDEBUG
   printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif

   if(target_surface->privData == NULL) {
      return (error_base + XvMCBadSurface);
   }
   pSiSSurface = (SiSXvMCSurface *)target_surface->privData;
   
   if(subpicture->privData == NULL) {
      return (error_base + XvMCBadSubpicture);
   }
   pSiSSubpicture = (SiSXvMCSubpicture *)subpicture->privData;
   
   
   pSiSSurface->Subp = (void*)pSiSSubpicture;
   
   return Success;
}



/***************************************************************************
// Function: XvMCBlendSubpicture2
// Description: 
//    The behavior of this function is different depending on whether
//    or not the XVMC_BACKEND_SUBPICTURE flag is set in the XvMCSurfaceInfo.
//    we only supports backend blending.
//  
//    XVMC_BACKEND_SUBPICTURE not set ("frontend" behavior):
//   
//    XvMCBlendSubpicture2 blends the source_surface and subpicture and
//    puts it in the target_surface.  This does not effect the status of
//    the source surface but will cause the target_surface to query
//    XVMC_RENDERING until the blend is completed.
//   
// Arguments:
//   display - Connection to the X server.
//   subpicture - The subpicture to be blended into the video.
//   target_surface - The surface to be displayed with the blended subpic.
//   source_surface - Source surface prior to blending.
//   subx, suby, subw, subh - The rectangle from the subpicture to use.
//   surfx, surfy, surfw, surfh - The rectangle in the surface to blend
//      blend the subpicture rectangle into. Scaling can ocure if 
//      XVMC_SUBPICTURE_INDEPENDENT_SCALING is set.
//
// Returns: Status
***************************************************************************/
Status XvMCBlendSubpicture2(Display *display, 
                          XvMCSurface *source_surface,
                          XvMCSurface *target_surface,
                          XvMCSubpicture *subpicture,
                          short subx, short suby,
                          unsigned short subw, unsigned short subh,
                          short surfx, short surfy,
                          unsigned short surfw, unsigned short surfh) {

#ifdef XVMCDEBUG
  printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif
  return Success;
}



/***************************************************************************
// Function: XvMCSyncSubpicture
// Description: This function blocks until all composite/clear requests on
//              the subpicture have been complete.
// Arguments:
//   display - Connection to the X server.
//   subpicture - The subpicture to synchronize
//
// Returns: Status
***************************************************************************/
Status XvMCSyncSubpicture(Display *display, XvMCSubpicture *subpicture) {

#ifdef XVMCDEBUG
   printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif
   return Success;
}



/***************************************************************************
// Function: XvMCFlushSubpicture
// Description: This function commits pending composite/clear requests to
//              ensure that they will be completed in a finite amount of
//              time.
// Arguments:
//   display - Connection to the X server.
//   subpicture - The subpicture whos compsiting should be flushed
//
// Returns: Status
// NOTES: i810 always dispatches commands so flush is a no-op
***************************************************************************/
Status XvMCFlushSubpicture(Display *display, XvMCSubpicture *subpicture) {

#ifdef XVMCDEBUG
  printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif

  if(display == NULL) {
    return BadValue;
  }
  if(subpicture == NULL) {
    return (error_base + XvMCBadSubpicture);
  }

  return Success;
}


/***************************************************************************
// Function: XvMCGetSubpictureStatus
// Description: This function gets the current status of a subpicture
//
// Arguments:
//   display - Connection to the X server.
//   subpicture - The subpicture whos status is being queried
//   stat - The status of the subpicture. It can be any of the following
//          OR'd together:
//          XVMC_RENDERING  - Last composite or clear request not completed
//          XVMC_DISPLAYING - Suppicture currently being displayed.
//
// Returns: Status
***************************************************************************/
Status XvMCGetSubpictureStatus(Display *display, XvMCSubpicture *subpicture,
                             int *stat) {

   SiSXvMCSubpicture *privSubpicture;
   SiSXvMCContext *pSiSXvMC;
   
#ifdef XVMCDEBUG
   printf("[XvMC] %s() in %s is called.\n",__FUNCTION__, __FILE__);
#endif

   if((display == NULL) || (stat == NULL)) {
      return BadValue;
   }
   if((subpicture == NULL) || (subpicture->privData == NULL)) {
      return (error_base + XvMCBadSubpicture);
   }

   
   *stat = 0;
   privSubpicture = (SiSXvMCSubpicture *)subpicture->privData;

   pSiSXvMC = (SiSXvMCContext *)privSubpicture->privContext;
   if(pSiSXvMC == NULL) {
      return (error_base + XvMCBadSubpicture);
   }

   SIS_LOCK(pSiSXvMC,0);

   switch(privSubpicture->status){
   case SurfaceIdle:
      *stat = 0;
      break;
   case SurfaceRendering:
      *stat |= XVMC_RENDERING;
      break;
   case SurfaceDisplaying:
      *stat |= XVMC_DISPLAYING;
      break;
   default:
      printf("[XvMC] Subpicture status Error!! Stop.\n");
      SIS_UNLOCK(pSiSXvMC);
      return XvMCBadSubpicture;
   }
   
   SIS_UNLOCK(pSiSXvMC);

   return Success;
}


#define NUM_XVMC_ATTRIBUTES 4
static XvAttribute SIS_XVMC_ATTRIBUTES[] = {
  {XvGettable | XvSettable, 0, 0xffffff, "XV_COLORKEY"},
  {XvGettable | XvSettable, -127, +127, "XV_BRIGHTNESS"},
  {XvGettable | XvSettable, 0, 0x1ff, "XV_CONTRAST"},
  {XvGettable | XvSettable, 0, 0x3ff, "XV_SATURATION"}
};

/***************************************************************************
// Function: XvMCQueryAttributes
// Description: An array of XvAttributes of size "number" is returned by
//   this function. If there are no attributes, NULL is returned and number
//   is set to 0. The array may be freed with xfree().
//
// Arguments:
//   display - Connection to the X server.
//   context - The context whos attributes we are querying.
//   number - The number of returned atoms.
//
// Returns:
//  An array of XvAttributes.
// Notes:
//    we support these Attributes:
//    XV_COLORKEY: The colorkey value, initialized from the Xv value at
//                 context creation time.
//    XV_BRIGHTNESS
//    XV_CONTRAST
//    XV_SATURATION
***************************************************************************/
XvAttribute *XvMCQueryAttributes(Display *display, XvMCContext *context,
				 int *number) {
  SiSXvMCContext *pSiSXvMC;
  XvAttribute *attributes;

  if(number == NULL) {
    return NULL;
  }
  if(display == NULL) {
    *number = 0;
    return NULL;
  }
  if(context == NULL) {
    *number = 0;
    return NULL;
  }
  pSiSXvMC = context->privData;
  if(pSiSXvMC == NULL) {
    *number = 0;
    return NULL;
  }

  attributes = (XvAttribute *)malloc(NUM_XVMC_ATTRIBUTES *
				     sizeof(XvAttribute));
  if(attributes == NULL) {
    *number = 0;
    return NULL;
  }

  memcpy(attributes,SIS_XVMC_ATTRIBUTES,(NUM_XVMC_ATTRIBUTES *
					  sizeof(XvAttribute)));

  *number = NUM_XVMC_ATTRIBUTES;
  return attributes;
}



/***************************************************************************
// Function: XvMCSetAttribute
// Description: This function sets a context-specific attribute.
//
// Arguments:
//   display - Connection to the X server.
//   context - The context whos attributes we are querying.
//   attribute - The X atom of the attribute to be changed.
//   value - The new value for the attribute.
//
// Returns:
//  Status
// Notes:
//    we support these Attributes:
//    XV_COLORKEY: The colorkey value, initialized from the Xv value at
//                 context creation time.
//    XV_BRIGHTNESS
//    XV_CONTRAST
//    XV_SATURATION
***************************************************************************/
Status XvMCSetAttribute(Display *display, XvMCContext *context,
			Atom attribute, int value) {
  SiSXvMCContext *pSiSXvMC;

  if(display == NULL) {
    return BadValue;
  }
  if(context == NULL) {
    return (error_base + XvMCBadContext);
  }
  pSiSXvMC = context->privData;
  if(pSiSXvMC == NULL) {
    return (error_base + XvMCBadContext);
  }

  if(attribute == pSiSXvMC->xv_colorkey) {
    if((value < SIS_XVMC_ATTRIBUTES[0].min_value) ||
       (value > SIS_XVMC_ATTRIBUTES[0].max_value)) {
      return BadValue;
    }
    pSiSXvMC->colorkey = value;
    return Success;
  }
  if(attribute == pSiSXvMC->xv_brightness) {
    if((value < SIS_XVMC_ATTRIBUTES[1].min_value) ||
       (value > SIS_XVMC_ATTRIBUTES[1].max_value)) {
      return BadValue;
    }
    pSiSXvMC->brightness = value;
    return Success;
  }
  if(attribute == pSiSXvMC->xv_saturation) {
    if((value < SIS_XVMC_ATTRIBUTES[2].min_value) ||
       (value > SIS_XVMC_ATTRIBUTES[2].max_value)) {
      return BadValue;
    }
    pSiSXvMC->saturation = value;
    return Success;
  }
  if(attribute == pSiSXvMC->xv_contrast) {
    if((value < SIS_XVMC_ATTRIBUTES[3].min_value) ||
       (value > SIS_XVMC_ATTRIBUTES[3].max_value)) {
      return BadValue;
    }
    pSiSXvMC->contrast = value;
    return Success;
  }
  return BadValue;
}

/***************************************************************************
// Function: XvMCGetAttribute
// Description: This function queries a context-specific attribute and
//   returns the value.
//
// Arguments:
//   display - Connection to the X server.
//   context - The context whos attributes we are querying.
//   attribute - The X atom of the attribute to be queried
//   value - The returned attribute value
//
// Returns:
//  Status
// Notes:
//    we support these Attributes:
//    XV_COLORKEY: The colorkey value, initialized from the Xv value at
//                 context creation time.
//    XV_BRIGHTNESS
//    XV_CONTRAST
//    XV_SATURATION
***************************************************************************/
Status XvMCGetAttribute(Display *display, XvMCContext *context,
			Atom attribute, int *value) {
  SiSXvMCContext *pSiSXvMC;

  if(display == NULL) {
    return BadValue;
  }
  if(context == NULL) {
    return (error_base + XvMCBadContext);
  }
  pSiSXvMC = context->privData;
  if(pSiSXvMC == NULL) {
    return (error_base + XvMCBadContext);
  }
  if(value == NULL) {
    return BadValue;
  }

  if(attribute == pSiSXvMC->xv_colorkey) {
    *value = pSiSXvMC->colorkey;
    return Success;
  }
  if(attribute == pSiSXvMC->xv_brightness) {
    *value = pSiSXvMC->brightness;
    return Success;
  }
  if(attribute == pSiSXvMC->xv_saturation) {
    *value = pSiSXvMC->saturation;
    return Success;
  }
  if(attribute == pSiSXvMC->xv_contrast) {
    *value = pSiSXvMC->contrast;
    return Success;
  }
  return BadValue;
}




