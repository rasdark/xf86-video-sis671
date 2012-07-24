/***************************************************************************

Copyright 2000 Intel Corporation.  All Rights Reserved.

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

/*
 * SiS_hwmc.c: SiS HWMC Driver
 *
 * Authors:
 *      Chaoyu Chen <chaoyu_chen@sis.com>
 *
 *
 */



#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "xf86.h"
#include "xf86_OSproc.h"
#ifndef GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 6
#include "xf86Resources.h"
#endif
#include "compiler.h"
#include "sis_pci.h"
#include "xf86Pci.h"
#include "xf86fbman.h"
#include "regionstr.h"

#include "sis.h"
#ifdef SISDRI
#include "sis_dri.h"
#endif

#include "xf86xv.h"
#include "xf86xvmc.h"
#include <X11/extensions/Xv.h>
#include <X11/extensions/XvMC.h>
#include "xaa.h"
#include "xaalocal.h"
#include "dixstruct.h"
#include "fourcc.h"
#include "sis_common.h"

#ifdef ENABLEXvMC

#define MCDEBUG


int SiSXvMCCreateContext (ScrnInfoPtr pScrn, XvMCContextPtr pContext,
                           int *num_priv, long **priv );
void SiSXvMCDestroyContext (ScrnInfoPtr pScrn, XvMCContextPtr pContext);

int SiSXvMCCreateSurface (ScrnInfoPtr pScrn, XvMCSurfacePtr pSurf,
                           int *num_priv, long **priv );
void SiSXvMCDestroySurface (ScrnInfoPtr pScrn, XvMCSurfacePtr pSurf);

int SiSXvMCCreateSubpicture (ScrnInfoPtr pScrn, XvMCSubpicturePtr pSurf,
                               int *num_priv, long **priv );
void SiSXvMCDestroySubpicture (ScrnInfoPtr pScrn, XvMCSubpicturePtr pSurf);

extern unsigned int SISAllocateFBMemory(ScrnInfoPtr pScrn,  void **handle, int bytesize); 
extern void SISFreeFBMemory(ScrnInfoPtr pScrn, void **handle);
extern void SISXvMCResetVideo(ScrnInfoPtr pScrn);
extern void SISXvMCCloseOverlay(ScrnInfoPtr pScrn);

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


static int yv12_subpicture_index_list[1] = 
{
  /*FOURCC_IA44,*/
  FOURCC_AI44
};

static XF86MCImageIDList yv12_subpicture_list =
{
  1,
  yv12_subpicture_index_list
};
 
static XF86MCSurfaceInfoRec SiS_YV12_mpg2_surface =
{
    FOURCC_YV12,  
    XVMC_CHROMA_FORMAT_420,
    0,
    720,
    576,
    720,
    576,
    XVMC_MPEG_2,
    XVMC_OVERLAID_SURFACE | XVMC_SUBPICTURE_INDEPENDENT_SCALING |
    XVMC_INTRA_UNSIGNED | XVMC_BACKEND_SUBPICTURE,
    &yv12_subpicture_list
};

static XF86MCSurfaceInfoRec SiS_YV12_mpg1_surface =
{
    FOURCC_YV12,  
    XVMC_CHROMA_FORMAT_420,
    0,
    720,
    576,
    720,
    576,
    XVMC_MPEG_1,
    XVMC_OVERLAID_SURFACE | XVMC_SUBPICTURE_INDEPENDENT_SCALING |
    XVMC_INTRA_UNSIGNED | XVMC_BACKEND_SUBPICTURE,
    &yv12_subpicture_list
};

static XF86MCSurfaceInfoPtr ppSI[2] = 
{
    (XF86MCSurfaceInfoPtr)&SiS_YV12_mpg2_surface,
    (XF86MCSurfaceInfoPtr)&SiS_YV12_mpg1_surface
};

/* List of subpicture types that we support */
static XF86ImageRec ia44_subpicture = XVIMAGE_IA44;
static XF86ImageRec ai44_subpicture = XVIMAGE_AI44;

static XF86ImagePtr SiS_subpicture_list[1] =
{
/*  (XF86ImagePtr)&ia44_subpicture,*/
  (XF86ImagePtr)&ai44_subpicture
};

/* Fill in the device dependent adaptor record. 
 * This is named "SiS Video Overlay" because this code falls under the
 * XV extenstion, the name must match or it won't be used.
 *
 * Surface and Subpicture - see above
 * Function pointers to functions below
 */
static XF86MCAdaptorRec pAdapt = 
{
  "SIS 300/315/330 series Video Overlay",		/* name */
  2,				/* num_surfaces */
  ppSI,				/* surfaces */
  1,				/* num_subpictures */
  SiS_subpicture_list,		/* subpictures */
  (xf86XvMCCreateContextProcPtr)SiSXvMCCreateContext,
  (xf86XvMCDestroyContextProcPtr)SiSXvMCDestroyContext,
  (xf86XvMCCreateSurfaceProcPtr)SiSXvMCCreateSurface,
  (xf86XvMCDestroySurfaceProcPtr)SiSXvMCDestroySurface,
  (xf86XvMCCreateSubpictureProcPtr)SiSXvMCCreateSubpicture,
  (xf86XvMCDestroySubpictureProcPtr)SiSXvMCDestroySubpicture
};

static XF86MCAdaptorPtr ppAdapt[1] = 
{
	(XF86MCAdaptorPtr)&pAdapt
};

/**************************************************************************
 *
 *  SiSInitMC
 *
 *  Initialize the hardware motion compenstation extention for this 
 *  hardware. The initialization routines want the address of the pointers
 *  to the structures, not the address of the structures. This means we
 *  allocate (or create static?) the pointer memory and pass that 
 *  address. This seems a little convoluted.
 *
 *  We need to allocate memory for the device depended adaptor record. 
 *  This is what holds the pointers to all our device functions.
 *
 *  We need to map the overlay registers into the drm.
 *
 *  We need to map the surfaces into the drm.
 *
 *  Inputs:
 *    Screen pointer
 *
 *  Outputs:
 *    None, this calls the device independent screen initialization 
 *    function.
 *
 *  Revisions:
 *  
 **************************************************************************/
void SiSInitMC(ScreenPtr pScreen)
{
  ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
  SISPtr pSIS = SISPTR(pScrn);
  int i;

  switch(pSIS->ChipType){
  case SIS_741:
  case SIS_662:
  case SIS_671:
     break;
  default:
     xf86DrvMsg(pScreen->myNum, X_INFO, "[MC] %s: This chip does not support XvMC.\n", __FUNCTION__);
     return;
  }

  /* Clear the Surface Allocation */
  for(i=0; i<SIS_MAX_SURFACES; i++) {
	pSIS->surfaceAllocation[i] = 0;
  } 

  
  /* Clear the Subpicture Allocation */
  for(i=0; i<SIS_MAX_SUBPICTURES; i++) {
	pSIS->subpictureAllocation[i] = 0;
  } 
  pSIS->SubpictBuffOffset = 0;

  if(!pSIS->MC_AgpAlloc.DRM_Success) /* Karma@080304 Check DRM enbale to avoid */
  {
     xf86DrvMsg(pScreen->myNum, X_ERROR, "[MC] AddMap (AGP) failed\n");
     return;
  }
  
  if(drmAddMap(pSIS->drmSubFD, (drm_handle_t)pSIS->MC_AgpAlloc.Start,
                pSIS->MC_AgpAlloc.Size, DRM_AGP, 0, (drmAddress) &pSIS->mc_agp_handle) < 0) {
    		xf86DrvMsg(pScreen->myNum, X_ERROR, "[MC] AddMap (AGP) failed\n");
    		return;
  }
  
  xf86XvMCScreenInit(pScreen, 1, ppAdapt);
  xf86DrvMsg(pScreen->myNum, X_INFO, "[MC] XvMC adaptor is initialized succfully.\n");
}


/**************************************************************************
 *
 *  SiSXvMCCreateContext
 *
 *  Some info about the private data:
 *
 *  Set *num_priv to the number of 32bit words that make up the size of
 *  of the data that priv will point to.
 *
 *  *priv = (long *) calloc (elements, sizeof(element))
 *  *num_priv = (elements * sizeof(element)) >> 2;
 *
 **************************************************************************/

int SiSXvMCCreateContext (ScrnInfoPtr pScrn, XvMCContextPtr pContext,
                            int *num_priv, long **priv )
{


	
  SISPtr pSiS = SISPTR(pScrn);
  DRIInfoPtr pDRIInfo = pSiS->pDRIInfo;
  SISDRIPtr pSISDRI = (SISDRIPtr)pDRIInfo->devPrivate;
  SiSXvMCCreateContextRec *contextRec;
  short src_pitch;
  int PitchAlignmentMask;

#ifdef MCDEBUG
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[MC] %s() in %s is called.\n",
				__FUNCTION__, __FILE__);
#endif

  if(!pSiS->directRenderingEnabled) {
    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
        "[MC] SiSXvMCCreateContext: Cannot use XvMC without DRI!\n");
    return BadAlloc;
  }

  /* Context Already in use! */
  if(pSiS->xvmcContext) {
    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
        "[MC] SiSXvMCCreateContext: 2 XvMC Contexts Attempted, not supported.\n");
    return BadAlloc;
  }

  *priv = calloc(1,sizeof(SiSXvMCCreateContextRec));
  contextRec = (SiSXvMCCreateContextRec *)*priv;

  if(!*priv) {
    *num_priv = 0;
    return BadAlloc;
  }

  *num_priv = sizeof(SiSXvMCCreateContextRec);
  if(drmCreateContext(pSiS->drmSubFD, &(contextRec->drmcontext) ) < 0) {
    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
        "[MC] SiSXvMCCreateContext: Unable to create DRMContext!\n");
    free(*priv);
    return BadAlloc;
  }

  drmAuthMagic(pSiS->drmSubFD, pContext->flags);

    

  drmSize FBSize = pScrn->videoRam * 1024;
  pSiS->fb_handle = 0;
  if(drmAddMap(pSiS->drmSubFD, pSiS->FbAddress ,FBSize, 
  	DRM_FRAME_BUFFER, 0, &pSiS->fb_handle) < 0){
  	
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,"[MC] Frame buffer AddMap  failed!\n");
		free(*priv);
		*num_priv = 0;
		return BadAlloc;
  }

  /* identify chip type */
  switch(pSiS->ChipType){
  case SIS_741:
     contextRec->ChipID = 741;
     PitchAlignmentMask = 7;
     break;
  case SIS_662:
     contextRec->ChipID = 662;
     PitchAlignmentMask = 63;
     break;
  case SIS_671:
     contextRec->ChipID = 671;
     PitchAlignmentMask = 63;
     break;
  default:
      xf86DrvMsg(pScrn->scrnIndex, X_ERROR," [MC] XvMC is not supposted on this chip! Stop.\n");
      free(*priv);
      *num_priv = 0;
      return BadValue;
  }


  /* calculate the size of Frame buffer per surface*/
  src_pitch = (pContext->width + PitchAlignmentMask) & ~PitchAlignmentMask;
  pSiS->FBBuffSize = (pContext->height * src_pitch * 3) >> 1;
  pSiS->FBBuffSize = (pSiS->FBBuffSize  + 15) & ~15;
  if(!(pSiS->FBBuffOffset = SISAllocateFBMemory(pScrn, &pSiS->FBBufferHandle,
  	                                                                          pSiS->FBBuffSize * pSiS->numSurfaces))){
  	                                                                          
      xf86DrvMsg(pScrn->scrnIndex, X_ERROR,"[MC] Frame buffer allocation failed!\n");
      xf86DrvMsg(pScrn->scrnIndex, X_ERROR,"[MC] Enlarging the video ram may solve this problem\n");
      return BadAlloc;
   }

  pSiS->xvmcContext = contextRec->drmcontext;
  contextRec->AGPHandle= pSiS->mc_agp_handle;
  contextRec->AGPSize = pSiS->MC_AgpAlloc.Size;
  contextRec->MMIOHandle = pSISDRI->regs.handle;
  contextRec->MMIOSize = pSISDRI->regs.size;
  contextRec->FBhandle = pSiS->fb_handle;
  contextRec->FBSize = FBSize;  
  contextRec->HDisplay = (unsigned long)pSiS->CurrentLayout.mode->HDisplay;
  contextRec->VDisplay = (unsigned long)pSiS->CurrentLayout.mode->VDisplay;
  
  
  strncpy (contextRec->busIdString, pDRIInfo->busIdString, 9);

  SISXvMCResetVideo(pScrn);

  return Success;
}


int SiSXvMCCreateSurface (ScrnInfoPtr pScrn, XvMCSurfacePtr pSurf,
                           int *num_priv, long **priv ){
                           
  SISPtr pSiS = SISPTR(pScrn);
  int i;
  SiSXvMCCreateSurfaceRec* surfaceRec;
 
  
#ifdef MCDEBUG
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[MC] %s() in %s is called.\n",
				__FUNCTION__, __FILE__);
#endif


  *priv = calloc(1,sizeof(SiSXvMCCreateSurfaceRec));
  

  if(!*priv) {
    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
        "[MC] SiSXvMCCreateSurface: Unable to allocate memory!\n");
    *num_priv = 0;
    return BadAlloc;
  }
  *num_priv = sizeof(SiSXvMCCreateSurfaceRec);
  surfaceRec = (SiSXvMCCreateSurfaceRec *)*priv;

  /* Surface Arrangement is different based on 6 Surfaces */
  if(pSiS->numSurfaces == 6) {
     for(i=0; i<pSiS->numSurfaces; i++) {
       if(!pSiS->surfaceAllocation[i]) {
         pSiS->surfaceAllocation[i] = pSurf->surface_id;
         surfaceRec->offsets[0] = pSiS->FBBuffOffset + i * pSiS->FBBuffSize;
         surfaceRec->offsets[1] = pSiS->FBBuffOffset + i * pSiS->FBBuffSize + (pSiS->FBBuffSize * 2)/3;
         surfaceRec->offsets[2] = pSiS->FBBuffOffset + i * pSiS->FBBuffSize + (pSiS->FBBuffSize * 5)/6;
         surfaceRec->MyNum = (unsigned int)i;		 
         return Success;
       }
     }
  }

  
  free(*priv);
  return BadAlloc;

}

int SiSXvMCCreateSubpicture (ScrnInfoPtr pScrn, XvMCSubpicturePtr pSubp,
                              int *num_priv, long **priv )
{

	
   SISPtr pSIS = SISPTR(pScrn);
   int i;
    int PitchShift;

#ifdef MCDEBUG
   xf86DrvMsg(pScrn->scrnIndex, X_INFO,"[MC] %s() in %s is called.\n",
				  __FUNCTION__, __FILE__);
#endif

   *priv = (long *)calloc(1,sizeof(long));

   if(!*priv) {
      xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "SiSXvMCCreateSubpicture: Unable to allocate memory!\n");
      *num_priv = 0;
      return BadAlloc;
   }
   *num_priv = 1;


   /* Allocate the buffer of Subpicture */
   switch(pSIS->ChipType){
   case SIS_741:
      PitchShift = 15;
      break;
   case SIS_662:
   case SIS_671:
   	PitchShift = 63;
      break;
   }
   pSIS->SubpictBuffSize = pSubp->width * ((pSubp->height + PitchShift) & ~PitchShift);
   if(pSIS->SubpictBuffOffset == 0 &&
   	(!(pSIS->SubpictBuffOffset = SISAllocateFBMemory(pScrn, &pSIS->SubpictBuffHandle,
  	                                                                                pSIS->SubpictBuffSize * SIS_MAX_SUBPICTURES)))){	                                                                          
      xf86DrvMsg(pScrn->scrnIndex, X_ERROR,"[MC] Subpicture allocation failed!\n");
      xf86DrvMsg(pScrn->scrnIndex, X_ERROR,"[MC] Enlarging the video ram may solve this problem\n");
      return BadAlloc;
   }

   for(i=0; i<SIS_MAX_SUBPICTURES; i++) {
      if(!pSIS->subpictureAllocation[i]) {
        pSIS->subpictureAllocation[i] = pSubp->subpicture_id; 
        (*priv)[0] = pSIS->SubpictBuffOffset + i*pSIS->SubpictBuffSize;  	 
        return Success;
      }
    }


   (*priv)[0] = 0;
   return BadAlloc;
  
}

void SiSXvMCDestroyContext (ScrnInfoPtr pScrn, XvMCContextPtr pContext)
{

   SISPtr pSiS = SISPTR(pScrn);
   int errorno;
#ifdef MCDEBUG
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[MC] %s() in %s is called.\n",
				__FUNCTION__, __FILE__);
#endif

   /* according to the comment of libdrm, Removing map is not necessary */
   /*
   if(pSiS->fb_handle){
      errorno = drmRmMap(pSiS->drmSubFD, pSiS->fb_handle);
      if(errorno != 0)
         xf86DrvMsg(pScrn->scrnIndex, X_ERROR, 
            "[MC] Removing frame buffer Map (from drm) failed. (ErrorNo: %d)\n", errorno);
   }
   */
   
   if(pSiS->xvmcContext){
      errorno = drmDestroyContext(pSiS->drmSubFD,pSiS->xvmcContext);
      if(errorno != 0)
         xf86DrvMsg(pScrn->scrnIndex, X_ERROR, 
            "[MC] Destroying Context (from drm) failed. (ErrorNo: %d)\n", errorno);
   }
   pSiS->xvmcContext = 0;
   if(pSiS->FBBufferHandle != NULL)  SISFreeFBMemory(pScrn, &pSiS->FBBufferHandle);
   pSiS->FBBufferHandle = NULL;
   
   if(! pSiS->SubpictBuffOffset){
      SISFreeFBMemory(pScrn, &pSiS->SubpictBuffHandle);
      pSiS->SubpictBuffOffset = 0;
   }

   SISXvMCCloseOverlay(pScrn);

}

void SiSXvMCDestroySurface (ScrnInfoPtr pScrn, XvMCSurfacePtr pSurf)
{
	
   SISPtr pSiS = SISPTR(pScrn);
   int i;

#ifdef MCDEBUG
   xf86DrvMsg(pScrn->scrnIndex, X_INFO,"[MC] %s() in %s is called.\n",
   		      __FUNCTION__, __FILE__);
#endif

   for(i=0; i<SIS_MAX_SURFACES; i++) {
      if(pSiS->surfaceAllocation[i] == pSurf->surface_id) {
         pSiS->surfaceAllocation[i] = 0;
         return;
      }
   }
   return;
}

void SiSXvMCDestroySubpicture (ScrnInfoPtr pScrn, XvMCSubpicturePtr pSubp)
{


   SISPtr pSiS = SISPTR(pScrn);
   int i;

#ifdef MCDEBUG
   xf86DrvMsg(pScrn->scrnIndex, X_INFO,"[MC] %s() in %s is called.\n",
				__FUNCTION__, __FILE__);
#endif

   for(i = 0; i < SIS_MAX_SUBPICTURES; i++) {
      if(pSiS->subpictureAllocation[i] == pSubp->subpicture_id) {
         pSiS->subpictureAllocation[i] = 0;
         return;
      }
   }
   return;
}

#endif

