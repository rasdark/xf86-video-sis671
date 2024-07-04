/* $XFree86$ */
/* $XdotOrg$ */
/*
 * SiS driver main code
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
 * Author: Thomas Winischhofer <thomas@winischhofer.net>
 *
 * This notice covers the entire driver code unless indicated otherwise.
 *
 * Formerly based on code which was
 * 	     Copyright (C) 1998, 1999 by Alan Hourihane, Wigan, England.
 * 	     Written by:
 *           Alan Hourihane <alanh@fairlite.demon.co.uk>,
 *           Mike Chapman <mike@paranoia.com>,
 *           Juanjo Santamarta <santamarta@ctv.es>,
 *           Mitani Hiroshi <hmitani@drl.mei.co.jp>,
 *           David Thomas <davtom@dream.org.uk>.
 *
 * TW thoughts:
 * - start by default in MergedFB mode, even if only one
 *   device is detected. Check what mode would be used
 *   if MergedFB mode wasn't enabled, see if a clone mode
 *   of this mode is defined (or create one if not) and
 *   use this (meta)mode as the start mode. This allows
 *   "switching" to mergedfb mode upon output device
 *   reconfiguration.
 *   Needs consideration in RebuildModelists: We need to
 *   eventually create one such clone mode for each
 *   CRT1 and CRT2.
 *   Disadvantages: -) high memory consumption because
 *   the virtual screen size must include eventual
 *   "reserves" for later; -) ReserveLargeVirtual is
 *   not implemented for MergedFB mode atm. Would need
 *   that to fully make sense.
 * - user should be able to disable one output device
 *   in any MetaMode. Switch to clone mode automatically?
 *   (very likely sisctrl-material, not driver material.
 * - If metamodes statement is missing and we create MetaModes
 *   out of tallest/widest mode, also add "reverse" modes if
 *   CRT2Position is not set. (Assume these "reversed" modes
 *   being "specialxinerama" modes to avoid bad calculation
 *   of Xinerama info. Problem: These modes (the normal one
 *   and the reversed one) are of same size, so a RandR
 *   event will not lead to the window manager updating
 *   its into. Idea: Perhaps SiSCtrl can switch to a different
 *   size in the middle when it detects two identically
 *   sized metamodes? - Ugly...)
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "sis.h"

#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 6
#include "xf86RAC.h"
#endif
#include "dixstruct.h"
#include "shadowfb.h"
#include "fb.h"
#include "micmap.h"
#include "mipointer.h"
#include "edid.h"

#define SIS_NEED_inSISREG
#define SIS_NEED_inSISIDXREG
#define SIS_NEED_outSISIDXREG
#define SIS_NEED_orSISIDXREG
#define SIS_NEED_andSISIDXREG
#define SIS_NEED_setSISIDXREG
#define SIS_NEED_outSISREG
#define SIS_NEED_MYMMIO
#define SIS_NEED_sisclearvram
#include "sis_regs.h"
#include "sis_dac.h"

#include "sis_driver.h"

#include <X11/extensions/xf86dgaproto.h>

#include "globals.h"
#include <fcntl.h>

#ifdef HAVE_XEXTPROTO_71
#include <X11/extensions/dpmsconst.h>
#else
#define DPMS_SERVER
#include <X11/extensions/dpms.h>
#endif


#ifdef SISDRI
#include "dri.h"
#endif

/* Globals (yes, these ARE really required to be global) */

#ifdef SISUSEDEVPORT
int		sisdevport = 0;
#endif

#ifdef SISDUALHEAD
static int	SISEntityIndex = -1;
#endif

/*
 * This is intentionally screen-independent. It indicates the binding
 * choice made in the first PreInit.
 */
static int pix24bpp = 0;

/*
 * This contains the functions needed by the server after loading the driver
 * module. It must be supplied, and gets passed back by the SetupProc
 * function in the dynamic case. In the static case, a reference to this
 * is compiled in, and this requires that the name of this DriverRec be
 * an upper-case version of the driver name.
 */

#ifdef XSERVER_LIBPCIACCESS
#define SIS_DEVICE_MATCH(d, i)\
    {PCI_VENDOR_SIS, (d), PCI_MATCH_ANY, PCI_MATCH_ANY, 0, 0, (i) }

static const struct pci_id_match SIS_device_match[] = {
	SIS_DEVICE_MATCH (PCI_CHIP_SIS670, 0),
	SIS_DEVICE_MATCH (PCI_CHIP_SIS671, 0),
	{0, 0, 0 },
	};
#endif

#ifdef _X_EXPORT
_X_EXPORT
#endif
DriverRec SIS = {
    SIS_CURRENT_VERSION,
    SIS_DRIVER_NAME,
    SISIdentify,
#ifndef XSERVER_LIBPCIACCESS
    SISProbe,
#else
    NULL,
#endif
    SISAvailableOptions,
    NULL,
    0
#ifdef SIS_HAVE_DRIVER_FUNC
     ,
    SISDriverFunc
#endif
#ifdef XSERVER_LIBPCIACCESS
    ,
    SIS_device_match,
    SIS_pci_probe
#endif
};

static SymTabRec SISChipsets[] = {
    { PCI_CHIP_SIS5597,     "SIS5597/5598" },
    { PCI_CHIP_SIS530,      "SIS530/620" },
    { PCI_CHIP_SIS6326,     "SIS6326/AGP/DVD" },
    { PCI_CHIP_SIS300,      "SIS300/305" },
    { PCI_CHIP_SIS630,      "SIS630/730" },
    { PCI_CHIP_SIS540,      "SIS540" },
    { PCI_CHIP_SIS315,      "SIS315" },
    { PCI_CHIP_SIS315H,     "SIS315H" },
    { PCI_CHIP_SIS315PRO,   "SIS315PRO/E" },
    { PCI_CHIP_SIS550,	    "SIS550" },
    { PCI_CHIP_SIS650,      "SIS650/M650/651/740" },
    { PCI_CHIP_SIS330,      "SIS330(Xabre)" },
    { PCI_CHIP_SIS660,      "SIS[M]661[F|M]X/[M]741[GX]/[M]760[GX]/[M]761[GX]/662" },
    { PCI_CHIP_SIS340,      "SIS340" },
    { PCI_CHIP_SIS670,      "[M]670/[M]770[GX]" },   
    { PCI_CHIP_SIS671,      "[M]671/[M]771[GX]" },
    { -1,                   NULL }
};

static PciChipsets SISPciChipsets[] = {
    { PCI_CHIP_SIS5597,     PCI_CHIP_SIS5597,   RES_SHARED_VGA },
    { PCI_CHIP_SIS530,      PCI_CHIP_SIS530,    RES_SHARED_VGA },
    { PCI_CHIP_SIS6326,     PCI_CHIP_SIS6326,   RES_SHARED_VGA },
    { PCI_CHIP_SIS300,      PCI_CHIP_SIS300,    RES_SHARED_VGA },
    { PCI_CHIP_SIS630,      PCI_CHIP_SIS630,    RES_SHARED_VGA },
    { PCI_CHIP_SIS540,      PCI_CHIP_SIS540,    RES_SHARED_VGA },
    { PCI_CHIP_SIS550,      PCI_CHIP_SIS550,    RES_SHARED_VGA },
    { PCI_CHIP_SIS315,      PCI_CHIP_SIS315,    RES_SHARED_VGA },
    { PCI_CHIP_SIS315H,     PCI_CHIP_SIS315H,   RES_SHARED_VGA },
    { PCI_CHIP_SIS315PRO,   PCI_CHIP_SIS315PRO, RES_SHARED_VGA },
    { PCI_CHIP_SIS650,      PCI_CHIP_SIS650,    RES_SHARED_VGA },
    { PCI_CHIP_SIS330,      PCI_CHIP_SIS330,    RES_SHARED_VGA },
    { PCI_CHIP_SIS660,      PCI_CHIP_SIS660,    RES_SHARED_VGA },
    { PCI_CHIP_SIS340,      PCI_CHIP_SIS340,    RES_SHARED_VGA },
    { PCI_CHIP_SIS670,      PCI_CHIP_SIS670,    RES_SHARED_VGA },
    { PCI_CHIP_SIS671,      PCI_CHIP_SIS671,    RES_SHARED_VGA },
    { -1,                   -1,                 RES_UNDEFINED }
};

static SymTabRec XGIChipsets[] = {
    { PCI_CHIP_XGIXG20,     "Volari Z7 (XG20)" },
    { PCI_CHIP_XGIXG40,     "Volari V3XT/V5/V8/Duo (XG40/XG42)" },
    { -1,                   NULL }
};

static PciChipsets XGIPciChipsets[] = {
    { PCI_CHIP_XGIXG20,     PCI_CHIP_XGIXG20,   RES_SHARED_VGA },
    { PCI_CHIP_XGIXG40,     PCI_CHIP_XGIXG40,   RES_SHARED_VGA },
    { -1,                   -1,                 RES_UNDEFINED }
};

#ifdef XFree86LOADER

static MODULESETUPPROTO(sisSetup);

static XF86ModuleVersionInfo sisVersRec =
{
    SIS_DRIVER_NAME,
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
#ifdef XORG_VERSION_CURRENT
    XORG_VERSION_CURRENT,
#else
    XF86_VERSION_CURRENT,
#endif
    SIS_MAJOR_VERSION, SIS_MINOR_VERSION, SIS_PATCHLEVEL,
    ABI_CLASS_VIDEODRV,         /* This is a video driver */
    ABI_VIDEODRV_VERSION,
    MOD_CLASS_VIDEODRV,
    {0,0,0,0}
};

#ifdef _X_EXPORT
_X_EXPORT
#endif
XF86ModuleData sis671ModuleData = { &sisVersRec, sisSetup, NULL };

pointer
sisSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
    static Bool setupDone = FALSE;

    if(!setupDone) {
       setupDone = TRUE;
       xf86AddDriver(&SIS, module, SIS_HaveDriverFuncs);
       return (pointer)TRUE;
    }

    if(errmaj) *errmaj = LDR_ONCEONLY;
    return NULL;
}

#endif /* XFree86LOADER */

/* Mandatory */
static void
SISIdentify(int flags)
{
    xf86PrintChipsets(SIS_NAME, "driver for SiS chipsets", SISChipsets);
    xf86PrintChipsets(SIS_NAME, "driver for XGI chipsets", XGIChipsets);
}

/****************************************************/
/*          DriverFunc (formerly "RRFunc")          */
/****************************************************/

#ifdef SIS_HAVE_RR_FUNC

#ifdef SIS_HAVE_DRIVER_FUNC
#define SISDRIVERFUNCOPTYPE xorgDriverFuncOp
#define SISDRIVERFUNCPTRTYPE pointer
#else
#define SISDRIVERFUNCOPTYPE xorgRRFuncFlags
#define SISDRIVERFUNCPTRTYPE xorgRRRotationPtr
#endif

static Bool
SISDriverFunc(ScrnInfoPtr pScrn, SISDRIVERFUNCOPTYPE op, SISDRIVERFUNCPTRTYPE ptr)
{
#ifdef SIS_HAVE_DRIVER_FUNC
    xorgHWFlags *flag;
#endif
    xorgRRRotation *rot;
#ifdef SIS_HAVE_RR_GET_MODE_MM
    xorgRRModeMM *modemm;
#endif

    switch(op) {
    case RR_GET_INFO:
	rot = (xorgRRRotation *)ptr;
	rot->RRRotations = RR_Rotate_0;
	break;
    case RR_SET_CONFIG:
	return TRUE;
#ifdef SIS_HAVE_RR_GET_MODE_MM
    case RR_GET_MODE_MM:
        modemm = (xorgRRModeMM *)ptr;
        return SiS_GetModeMM(pScrn, modemm->mode, modemm->virtX, modemm->virtY,
					&modemm->mmWidth, &modemm->mmHeight);
	break;
#endif
#ifdef SIS_HAVE_DRIVER_FUNC
    case GET_REQUIRED_HW_INTERFACES:
	flag = (xorgHWFlags *)ptr;
	(*flag) = HW_IO | HW_MMIO;
	return TRUE;
	break;
#endif
    default:
	return FALSE;
    }
    return TRUE;
}
#undef SISDRIVERFUNCOPTYPE
#undef SISDRIVERFUNCPTRTYPE
#endif


/****************************************************/
/*                     Probe()                      */
/****************************************************/
static Bool SIS_pci_probe (DriverPtr driver, int entity_num, struct pci_device *device, intptr_t match_data)
{
    ScrnInfoPtr pScrn;
#ifdef SISDUALHEAD
    EntityInfoPtr pEnt;
    Bool    foundScreen = FALSE;
#endif
xf86DrvMsg(0, X_INFO, "SIS_pci_probe - begin, entity_num=%d\n", entity_num);
xf86DrvMsg(0, X_INFO, "                       vendor_id=0x%x\n", device->vendor_id);
xf86DrvMsg(0, X_INFO, "                       device_id=0x%x\n", device->device_id);
xf86DrvMsg(0, X_INFO, "                       bus=%d\n", device->bus);
xf86DrvMsg(0, X_INFO, "                       dev=%d\n", device->dev);
xf86DrvMsg(0, X_INFO, "                       func=%d\n", device->func);
    pScrn = NULL;
    if((pScrn = xf86ConfigPciEntity(pScrn, 0,
			entity_num,
			SISPciChipsets,
			NULL, NULL, NULL, NULL, NULL))) {
	    xf86DrvMsg(0, X_INFO, "SIS_pci_probe - ConfigPciEntity found\n");
	    /* Fill in what we can of the ScrnInfoRec */
	    pScrn->driverVersion    = SIS_CURRENT_VERSION;
	    pScrn->driverName       = SIS_DRIVER_NAME;
	    pScrn->name             = SIS_NAME;
	    pScrn->Probe            = NULL;//SISProbe;
	    pScrn->PreInit          = SISPreInit;
	    pScrn->ScreenInit       = SISScreenInit;
	    pScrn->SwitchMode       = SISSwitchMode;
	    pScrn->AdjustFrame      = SISAdjustFrame;
	    pScrn->EnterVT          = SISEnterVT;
	    pScrn->LeaveVT          = SISLeaveVT;
	    pScrn->FreeScreen       = SISFreeScreen;
	    pScrn->ValidMode        = SISValidMode;
	    pScrn->PMEvent          = SISPMEvent; /*add PM function for ACPI hotkey,Ivans*/
	    foundScreen = TRUE;
	}
    #ifdef SISDUALHEAD
	pEnt = xf86GetEntityInfo(entity_num);
xf86DrvMsg(0, X_INFO, "SIS_pci_probe - GetEntityInfo chipset is 0x%x\n",pEnt->chipset);
	switch(pEnt->chipset) {
	case PCI_CHIP_SIS300:
	case PCI_CHIP_SIS540:
	case PCI_CHIP_SIS630:
	case PCI_CHIP_SIS550:
	case PCI_CHIP_SIS315:
	case PCI_CHIP_SIS315H:
	case PCI_CHIP_SIS315PRO:
	case PCI_CHIP_SIS650:
	case PCI_CHIP_SIS330:
	case PCI_CHIP_SIS660:
	case PCI_CHIP_SIS340:
	case PCI_CHIP_SIS670:
	case PCI_CHIP_SIS671:
	case PCI_CHIP_XGIXG40:
	    {
	       SISEntPtr pSiSEnt = NULL;
	       DevUnion  *pPriv;

	       xf86SetEntitySharable(entity_num);
	       if(SISEntityIndex < 0) {
		  SISEntityIndex = xf86AllocateEntityPrivateIndex();
	       }
	       pPriv = xf86GetEntityPrivate(pScrn->entityList[0], SISEntityIndex);
	       if(!pPriv->ptr) {
		  pPriv->ptr = xnfcalloc(sizeof(SISEntRec), 1);
		  pSiSEnt = pPriv->ptr;
		  memset(pSiSEnt, 0, sizeof(SISEntRec));
		  pSiSEnt->lastInstance = -1;
	       } else {
		  pSiSEnt = pPriv->ptr;
	       }
	       pSiSEnt->lastInstance++;
	       xf86SetEntityInstanceForScreen(pScrn, pScrn->entityList[0],
						pSiSEnt->lastInstance);
	    }
	    break;

	default:
	    break;
	}
#endif /* DUALHEAD */
xf86DrvMsg(0, X_INFO, "SIS_pci_probe - end\n");
    return foundScreen;
}

static Bool
SISProbe(DriverPtr drv, int flags)
{
    int     i;
    GDevPtr *devSections;
    int     *usedChipsSiS, *usedChipsXGI;
    int     numDevSections;
    int     numUsed, numUsedSiS, numUsedXGI;
    Bool    foundScreen = FALSE;
xf86DrvMsg(0, X_INFO, "SISPRobe() begin, flags=%d\n", flags);
    /*
     * The aim here is to find all cards that this driver can handle,
     * and for the ones not already claimed by another driver, claim
     * the slot, and allocate a ScrnInfoRec.
     *
     * This should be a minimal probe, and it should under no circumstances
     * change the state of the hardware.  Because a device is found, don't
     * assume that it will be used.  Don't do any initialisations other than
     * the required ScrnInfoRec initialisations.  Don't allocate any new
     * data structures.
     *
     */

    /*
     * Next we check, if there has been a chipset override in the config file.
     * For this we must find out if there is an active device section which
     * is relevant, i.e., which has no driver specified or has THIS driver
     * specified.
     */

    if((numDevSections = xf86MatchDevice(SIS_DRIVER_NAME, &devSections)) <= 0) {
       /*
        * There's no matching device section in the config file, so quit
        * now.
        */
       xf86DrvMsg(0, X_INFO, "SISProbe - MatchDevice fail\n");
       return FALSE;
    }

    /*
     * We need to probe the hardware first.  We then need to see how this
     * fits in with what is given in the config file, and allow the config
     * file info to override any contradictions.
     */

    /*
     * All of the cards this driver supports are PCI, so the "probing" just
     * amounts to checking the PCI data that the server has already collected.
     */
#ifndef XSERVER_LIBPCIACCESS
    if(xf86GetPciVideoInfo() == NULL) {
       /*
        * We won't let anything in the config file override finding no
        * PCI video cards at all.
        */
       return FALSE;
    }
#endif


    numUsedSiS = xf86MatchPciInstances(SIS_NAME, PCI_VENDOR_SIS,
			SISChipsets, SISPciChipsets, devSections,
			numDevSections, drv, &usedChipsSiS);

    numUsedXGI = xf86MatchPciInstances(SIS_NAME, PCI_VENDOR_XGI,
			XGIChipsets, XGIPciChipsets, devSections,
			numDevSections, drv, &usedChipsXGI);

    /* Free it since we don't need that list after this */
    free(devSections);

    numUsed = numUsedSiS + numUsedXGI;
xf86DrvMsg(0, X_INFO, "SISPRobe - test1\n");
    if(numUsed <= 0) {
       xf86DrvMsg(0, X_INFO, "SISProbe - MatchPciInstances fail\n");
       return FALSE;
    }

    if(flags & PROBE_DETECT) {

	foundScreen = TRUE;
	xf86DrvMsg(0, X_INFO, "SISProbe - flags already probe");
    } else for(i = 0; i < numUsed; i++) {

	ScrnInfoPtr pScrn;
#ifdef SISDUALHEAD
	EntityInfoPtr pEnt;
#endif

	/* Allocate a ScrnInfoRec and claim the slot */
	pScrn = NULL;

	if((pScrn = xf86ConfigPciEntity(pScrn, 0,
			(i < numUsedSiS) ? usedChipsSiS[i] : usedChipsXGI[i-numUsedSiS],
			(i < numUsedSiS) ? SISPciChipsets  : XGIPciChipsets,
			NULL, NULL, NULL, NULL, NULL))) {
	    xf86DrvMsg(0, X_INFO, "SISProbe - ConfigPciEntity found\n");
	    /* Fill in what we can of the ScrnInfoRec */
	    pScrn->driverVersion    = SIS_CURRENT_VERSION;
	    pScrn->driverName       = SIS_DRIVER_NAME;
	    pScrn->name             = SIS_NAME;
	    pScrn->Probe            = SISProbe;
	    pScrn->PreInit          = SISPreInit;
	    pScrn->ScreenInit       = SISScreenInit;
	    pScrn->SwitchMode       = SISSwitchMode;
	    pScrn->AdjustFrame      = SISAdjustFrame;
	    pScrn->EnterVT          = SISEnterVT;
	    pScrn->LeaveVT          = SISLeaveVT;
	    pScrn->FreeScreen       = SISFreeScreen;
	    pScrn->ValidMode        = SISValidMode;
	    pScrn->PMEvent          = SISPMEvent; /*add PM function for ACPI hotkey,Ivans*/
#ifdef X_XF86MiscPassMessage
   if(xf86GetVersion() >= XF86_VERSION_NUMERIC(4,3,99,2,0)) {
//	       pScrn->HandleMessage = SISHandleMessage;
	    }
#endif
	    foundScreen = TRUE;
	}
xf86DrvMsg(0, X_INFO, "SISProbe - test2\n");
#ifdef SISDUALHEAD
	pEnt = xf86GetEntityInfo((i < numUsedSiS) ? usedChipsSiS[i] : usedChipsXGI[i-numUsedSiS]);
	xf86DrvMsg(0, X_INFO, "SISProbe - GetEntityInfo done\n");
	switch(pEnt->chipset) {
	case PCI_CHIP_SIS300:
	case PCI_CHIP_SIS540:
	case PCI_CHIP_SIS630:
	case PCI_CHIP_SIS550:
	case PCI_CHIP_SIS315:
	case PCI_CHIP_SIS315H:
	case PCI_CHIP_SIS315PRO:
	case PCI_CHIP_SIS650:
	case PCI_CHIP_SIS330:
	case PCI_CHIP_SIS660:
	case PCI_CHIP_SIS340:
	case PCI_CHIP_SIS670:
	case PCI_CHIP_SIS671:
	case PCI_CHIP_XGIXG40:
	    {
	       SISEntPtr pSiSEnt = NULL;
	       DevUnion  *pPriv;

	       xf86SetEntitySharable((i < numUsedSiS) ? usedChipsSiS[i] : usedChipsXGI[i-numUsedSiS]);
	       if(SISEntityIndex < 0) {
		  SISEntityIndex = xf86AllocateEntityPrivateIndex();
	       }
	       pPriv = xf86GetEntityPrivate(pScrn->entityList[0], SISEntityIndex);
	       if(!pPriv->ptr) {
		  pPriv->ptr = xnfcalloc(sizeof(SISEntRec), 1);
		  pSiSEnt = pPriv->ptr;
		  memset(pSiSEnt, 0, sizeof(SISEntRec));
		  pSiSEnt->lastInstance = -1;
	       } else {
		  pSiSEnt = pPriv->ptr;
	       }
	       pSiSEnt->lastInstance++;
	       xf86SetEntityInstanceForScreen(pScrn, pScrn->entityList[0],
						pSiSEnt->lastInstance);
	    }
	    break;

	default:
	    break;
	}
#endif /* DUALHEAD */

    }

    free(usedChipsSiS);
    free(usedChipsXGI);
xf86DrvMsg(0, X_INFO, "SISProbe end\n");
    return foundScreen;
}

/*****************************************************/
/*                 PreInit() helpers                 */
/*****************************************************/

/* Allocate/Free pSiS */

static Bool
SISGetRec(ScrnInfoPtr pScrn)
{
    /* Allocate an SISRec, and hook it into pScrn->driverPrivate.
     * pScrn->driverPrivate is initialised to NULL, so we can check if
     * the allocation has already been done.
     */
    if(pScrn->driverPrivate != NULL)
       return TRUE;

    pScrn->driverPrivate = xnfcalloc(sizeof(SISRec), 1);

    /* Initialise it to 0 */
    memset(pScrn->driverPrivate, 0, sizeof(SISRec));

    return TRUE;
}

static void
SISFreeRec(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
    SISEntPtr pSiSEnt = NULL;
#endif

    /* Just to make sure... */
    if(!pSiS)
       return;

#ifdef SISDUALHEAD
    pSiSEnt = pSiS->entityPrivate;
#endif

    if(pSiS->pstate) {
       free(pSiS->pstate);
       pSiS->pstate = NULL;
    }

    if(pSiS->fonts) {
       free(pSiS->fonts);
       pSiS->fonts = NULL;
    }

#ifdef SISDUALHEAD
    if(pSiSEnt) {
       if(!pSiS->SecondHead) {
	  /* Free memory only if we are first head; in case of an error
	   * during init of the second head, the server will continue -
	   * and we need the BIOS image and SiS_Private for the first
	   * head.
	   */
	  free(pSiSEnt->BIOS);
	  pSiSEnt->BIOS = pSiS->BIOS = NULL;

      free(pSiSEnt->SiS_Pr);
	  pSiSEnt->SiS_Pr = pSiS->SiS_Pr = NULL;

      free(pSiSEnt->RenderAccelArray);
	  pSiSEnt->RenderAccelArray = pSiS->RenderAccelArray = NULL;

	  pSiSEnt->pScrn_1 = NULL;
       } else {
	  pSiS->BIOS = NULL;
	  pSiS->SiS_Pr = NULL;
	  pSiS->RenderAccelArray = NULL;
	  pSiSEnt->pScrn_2 = NULL;
       }
    } else {
#endif
       if(pSiS->BIOS) {
          free(pSiS->BIOS);
          pSiS->BIOS = NULL;
       }

       if(pSiS->SiS_Pr) {
          free(pSiS->SiS_Pr);
          pSiS->SiS_Pr = NULL;
       }

       if(pSiS->RenderAccelArray) {
          free(pSiS->RenderAccelArray);
          pSiS->RenderAccelArray = NULL;
       }
#ifdef SISDUALHEAD
    }
#endif
#ifdef SISMERGED
    if(pSiS->CRT2HSync) {
       free(pSiS->CRT2HSync);
       pSiS->CRT2HSync = NULL;
    }

    if(pSiS->CRT2VRefresh) {
       free(pSiS->CRT2VRefresh);
       pSiS->CRT2VRefresh = NULL;
    }

    if(pSiS->MetaModes) {
       free(pSiS->MetaModes);
       pSiS->MetaModes = NULL;
    }

    if(pSiS->CRT2pScrn) {
       while(pSiS->CRT2pScrn->modes) {
	  xf86DeleteMode(&pSiS->CRT2pScrn->modes, pSiS->CRT2pScrn->modes);
       }
       if(pSiS->CRT2pScrn->monitor) {
	  while(pSiS->CRT2pScrn->monitor->Modes) {
	     xf86DeleteMode(&pSiS->CRT2pScrn->monitor->Modes, pSiS->CRT2pScrn->monitor->Modes);
	  }
	  free(pSiS->CRT2pScrn->monitor);
       }
       free(pSiS->CRT2pScrn);
       pSiS->CRT2pScrn = NULL;
    }

    if(pSiS->CRT1Modes) {
       if(pSiS->CRT1Modes != pScrn->modes) {
          /* Free metamodes */
	  if(pScrn->modes) {
	     pScrn->currentMode = pScrn->modes;
	     do {
	        DisplayModePtr p = pScrn->currentMode->next;
	 	   free(pScrn->currentMode->Private);
	 	   free(pScrn->currentMode->name);
	        free(pScrn->currentMode);
	        pScrn->currentMode = p;
	     } while(pScrn->currentMode != pScrn->modes);
	  }
	  pScrn->currentMode = pSiS->CRT1CurrentMode;
	  pScrn->modes = pSiS->CRT1Modes;
	  pSiS->CRT1CurrentMode = NULL;
	  pSiS->CRT1Modes = NULL;
       }
    }
#endif

    /* Just clear pointer; it only points to
     * one of the currcrtXXXXedid areas below
     */
    if(pScrn->monitor) {
       pScrn->monitor->DDC = NULL;
    }

    if(pSiS->currcrt1analogedid) {
       free(pSiS->currcrt1analogedid);
       pSiS->currcrt1analogedid = NULL;
    }

    if(pSiS->currcrt1digitaledid) {
       free(pSiS->currcrt1digitaledid);
       pSiS->currcrt1digitaledid = NULL;
    }

    if(pSiS->currcrt2analogedid) {
       free(pSiS->currcrt2analogedid);
       pSiS->currcrt2analogedid = NULL;
    }

    if(pSiS->currcrt2digitaledid) {
       free(pSiS->currcrt2digitaledid);
       pSiS->currcrt2digitaledid = NULL;
    }

    if(pSiS->UserModes) {
       while(pSiS->UserModes)
	  xf86DeleteMode(&pSiS->UserModes, pSiS->UserModes);
    }

    while(pSiS->SISVESAModeList) {
       sisModeInfoPtr mp = pSiS->SISVESAModeList->next;
       free(pSiS->SISVESAModeList);
       pSiS->SISVESAModeList = mp;
    }

    if(pSiS->pVbe) {
       vbeFree(pSiS->pVbe);
       pSiS->pVbe = NULL;
    }

#ifdef SISUSEDEVPORT
    if(pSiS->sisdevportopen)
       close(sisdevport);
#endif

    if(pScrn->driverPrivate == NULL)
        return;

    free(pScrn->driverPrivate);
    pScrn->driverPrivate = NULL;
}

/* Log error */

void
SISErrorLog(ScrnInfoPtr pScrn, const char *format, ...)
{
    va_list ap;
    static const char *str = "**************************************************\n";

    va_start(ap, format);
    xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "%s", str);
    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
	"                      ERROR:\n");
    xf86VDrvMsgVerb(pScrn->scrnIndex, X_ERROR, 1, format, ap);
    va_end(ap);
    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
	"                  END OF MESSAGE\n");
    xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "%s", str);
}

static void
SiSPrintLogHeader(ScrnInfoPtr pScrn)
{

    /* Due to the liberal license terms this is needed for
     * keeping the copyright notice readable and intact in
     * binary distributions. Removing this is a copyright
     * and license infringement. Please read the license
     * terms above.
     */

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	"SiS driver (%d/%02d/%02d-%d, compiled for " SISMYSERVERNAME " %d.%d.%d.%d)\n",
	SISDRIVERVERSIONYEAR + 2000, SISDRIVERVERSIONMONTH,
	SISDRIVERVERSIONDAY, SISDRIVERREVISION,
#ifdef XORG_VERSION_CURRENT
	XORG_VERSION_MAJOR, XORG_VERSION_MINOR,
	XORG_VERSION_PATCH, XORG_VERSION_SNAP
#else
	XF86_VERSION_MAJOR, XF86_VERSION_MINOR,
	XF86_VERSION_PATCH, XF86_VERSION_SNAP
#endif
	);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	"Copyright (C) 2001-2005 Thomas Winischhofer <thomas@winischhofer.net> and others\n");

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	"*** See http://www.winischhofer.at/linuxsisvga.shtml\n");
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	"*** for documentation, updates and a Premium Version.\n");




#ifdef XORG_VERSION_CURRENT
#ifdef SISISXORG6899900
/*	In some distributions, xorgGetVersion() has been removed.
	for compatibility, we mark this section.*/
/*
    if(xorgGetVersion() != XORG_VERSION_CURRENT) {
       xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
         "This driver binary is not compiled for this version of " SISMYSERVERNAME "\n");
    }
*/
#endif
#else
#if XF86_VERSION_CURRENT >= XF86_VERSION_NUMERIC(4,2,99,0,0)
	
	if(xf86GetVersion() != XF86_VERSION_CURRENT) {
       xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
         "This driver binary is not compiled for this version of " SISMYSERVERNAME "\n");
	}
#endif
#endif


    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	"RandR rotation support not available in this version.\n");
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	"Dynamic modelist support not available in this version.\n");
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	"Screen growing support not available in this version.\n");
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	"Advanced Xv video blitter not available in this version.\n");
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	"Advanced MergedFB support not available in this version.\n");
}

/* Map standard VGA memory area */

#ifdef SIS_PC_PLATFORM
static void
SiS_MapVGAMem(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);

    /* Map 64k VGA window for saving/restoring CGA fonts */
    pSiS->VGAMapSize = 0x10000;
    pSiS->VGAMapPhys = 0;	/* Default */
    if((!pSiS->Primary) || (!pSiS->VGADecodingEnabled)) {
       /* If card is secondary or if a0000-address decoding
        * is disabled, set Phys to beginning of our video RAM.
	*/
       pSiS->VGAMapPhys = PCI_REGION_BASE( pSiS->PciInfo, 0, REGION_MEM);
    }
    if(!SiSVGAMapMem(pScrn)) {
       xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
	  "Failed to map VGA memory (0x%lx), can't save/restore console fonts\n",
	  pSiS->VGAMapPhys);
    }
}
#endif

/* Load and initialize VBE module */

static void
SiS_LoadInitVBE(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);

    /* Don't load the VBE module for secondary
     * cards which sisfb POSTed. We don't want
     * int10 to overwrite our set up (such as
     * disabled a0000 memory address decoding).
     * We don't need the VBE anyway because
     * the card will never be in text mode,
     * and we can restore graphics modes just
     * perfectly.
     */
    if(!pSiS->Primary && pSiS->sisfbcardposted)
       return;

    if(pSiS->pVbe)
       return;

    if(xf86LoadSubModule(pScrn, "vbe")) {
#if XF86_VERSION_CURRENT < XF86_VERSION_NUMERIC(4,2,99,0,0)
       pSiS->pVbe = VBEInit(pSiS->pInt, pSiS->pEnt->index);
#else
       pSiS->pVbe = VBEExtendedInit(pSiS->pInt, pSiS->pEnt->index,
	                SET_BIOS_SCRATCH | RESTORE_BIOS_SCRATCH);
#endif
    }

    if(!pSiS->pVbe) {
       xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
	   "Failed to load/initialize vbe module\n");
    }
}

static Bool
SiSLoadInitDDCModule(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);

    if(pSiS->haveDDC)
       return TRUE;

    if(xf86LoadSubModule(pScrn, "ddc")) {
       pSiS->haveDDC = TRUE;
       return TRUE;
    }

    return FALSE;
}

/* Look for and eventually communicate with sisfb */

static void
SiS_CheckKernelFB(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
    int        fd, i;
    CARD32     sisfbinfosize = 0, sisfbversion;
    sisfb_info *mysisfbinfo;
    char       name[16];

    pSiS->donttrustpdc = FALSE;
    pSiS->sisfbpdc = 0xff;
    pSiS->sisfbpdca = 0xff;
    pSiS->sisfblcda = 0xff;
    pSiS->sisfbscalelcd = -1;
    pSiS->sisfbspecialtiming = CUT_NONE;
    pSiS->sisfb_haveemi = FALSE;
    pSiS->sisfbfound = FALSE;
    pSiS->sisfb_tvposvalid = FALSE;
    pSiS->sisfbdevname[0] = 0;
    pSiS->sisfb_havelock = FALSE;
    pSiS->sisfbHaveNewHeapDef = FALSE;
    pSiS->sisfbHeapSize = 0;
    pSiS->sisfbVideoOffset = 0;
    pSiS->sisfbxSTN = FALSE;
    pSiS->sisfbcanpost = FALSE;   /* (Old) sisfb can't POST card */
    pSiS->sisfbcardposted = TRUE; /* If (old) sisfb is running, card must have been POSTed */
    pSiS->sisfbprimary = FALSE;   /* (Old) sisfb doesn't know */

    if(pSiS->VGAEngine == SIS_300_VGA || pSiS->VGAEngine == SIS_315_VGA) {

       i = 0;
       do {

	  if(i <= 7) {
             sprintf(name, "/dev/fb%1d", i);
	  } else {
	     sprintf(name, "/dev/fb/%1d", (i - 8));
	  }

          if((fd = open(name, O_RDONLY)) != -1) {

	     Bool gotit = FALSE;

 	     if(!ioctl(fd, SISFB_GET_INFO_SIZE, &sisfbinfosize)) {
 		if((mysisfbinfo = malloc(sisfbinfosize))) {
 		   if(!ioctl(fd, (SISFB_GET_INFO | (sisfbinfosize << 16)), mysisfbinfo)) {
 		      gotit = TRUE;
 		   } else {
 		      free(mysisfbinfo);
 		      mysisfbinfo = NULL;
 		   }
 		}
 	     } else {
 		if((mysisfbinfo = malloc(sizeof(*mysisfbinfo) + 16))) {
 		   if(!ioctl(fd, SISFB_GET_INFO_OLD, mysisfbinfo)) {
 		      gotit = TRUE;
		      xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
				"Possibly old version of sisfb detected. Please update.\n");
		   } else {
		      free(mysisfbinfo);
		      mysisfbinfo = NULL;
		   }
		}
	     }

	     if(gotit) {

		if(mysisfbinfo->sisfb_id == SISFB_ID) {

		   sisfbversion = (mysisfbinfo->sisfb_version << 16) |
				  (mysisfbinfo->sisfb_revision << 8) |
				  (mysisfbinfo->sisfb_patchlevel);

	           if(sisfbversion >= SISFB_VERSION(1, 5, 8)) {
		      /* Added PCI bus/slot/func into in sisfb Version 1.5.08.
		       * Check this to make sure we run on the same card as sisfb
		       */
		      if((mysisfbinfo->sisfb_pcibus  == pSiS->PciBus)    &&
			 (mysisfbinfo->sisfb_pcislot == pSiS->PciDevice) &&
			 (mysisfbinfo->sisfb_pcifunc == pSiS->PciFunc)) {
			 pSiS->sisfbfound = TRUE;
		      }
		   } else pSiS->sisfbfound = TRUE;

		   if(pSiS->sisfbfound) {
		      xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
			     "%s: SiS kernel fb driver (sisfb) %d.%d.%d detected (PCI:%02d:%02d.%d)\n",
				&name[5],
				mysisfbinfo->sisfb_version,
				mysisfbinfo->sisfb_revision,
				mysisfbinfo->sisfb_patchlevel,
				pSiS->PciBus,
				pSiS->PciDevice,
				pSiS->PciFunc);

		      /* Added version/rev/pl in sisfb 1.4.0 */
		      if(mysisfbinfo->sisfb_version == 0) {
			 xf86DrvMsg(pScrn->scrnIndex, X_INFO,
				"Old version of sisfb found. Please update.\n");
		      }
		      /* Basically, we can't trust the pdc register if sisfb is loaded */
		      pSiS->donttrustpdc = TRUE;
		      pSiS->sisfbHeapStart = mysisfbinfo->heapstart;

		      if(sisfbversion >= SISFB_VERSION(1, 7, 20)) {
			 pSiS->sisfbHeapSize = mysisfbinfo->sisfb_heapsize;
			 pSiS->sisfbVideoOffset = mysisfbinfo->sisfb_videooffset;
			 pSiS->sisfbHaveNewHeapDef = TRUE;
			 pSiS->sisfbFSTN = mysisfbinfo->sisfb_curfstn;
			 pSiS->sisfbDSTN = mysisfbinfo->sisfb_curdstn;
			 pSiS->sisfbxSTN = TRUE;
			 xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
				"sisfb: memory heap at %dKB, size %dKB, viewport at %dKB\n",
				(int)pSiS->sisfbHeapStart, (int)pSiS->sisfbHeapSize,
				(int)pSiS->sisfbVideoOffset/1024);
		      } else {
			 xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
				"sisfb: memory heap at %dKB\n", (int)pSiS->sisfbHeapStart);
		      }
		      xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
				"sisfb: using video mode 0x%02x\n", mysisfbinfo->fbvidmode);
		      pSiS->OldMode = mysisfbinfo->fbvidmode;
		      if(sisfbversion >= SISFB_VERSION(1, 5, 6)) {
			 xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
				"sisfb: using %s, reserved %dK\n",
				(mysisfbinfo->sisfb_caps & 0x40) ? "SiS300 series Turboqueue" :
				   (mysisfbinfo->sisfb_caps & 0x20) ? "AGP command queue" :
				      (mysisfbinfo->sisfb_caps & 0x10) ? "VRAM command queue" :
					(mysisfbinfo->sisfb_caps & 0x08) ? "MMIO mode" :
					   "no command queue",
				(int)mysisfbinfo->sisfb_tqlen);
		      }
		      if(sisfbversion >= SISFB_VERSION(1, 5, 10)) {
			 /* We can trust the pdc value if sisfb is of recent version */
			 if(pSiS->VGAEngine == SIS_300_VGA) pSiS->donttrustpdc = FALSE;
		      }
		      if(sisfbversion >= SISFB_VERSION(1, 5, 11)) {
			 if(pSiS->VGAEngine == SIS_300_VGA) {
			    /* As of 1.5.11, sisfb saved the register for us (300 series) */
			    pSiS->sisfbpdc = mysisfbinfo->sisfb_lcdpdc;
			    if(!pSiS->sisfbpdc) pSiS->sisfbpdc = 0xff;
			 }
		      }
		      if(sisfbversion >= SISFB_VERSION(1, 5, 14)) {
			 if(pSiS->VGAEngine == SIS_315_VGA) {
			    pSiS->sisfblcda = mysisfbinfo->sisfb_lcda;
			 }
		      }
		      if(sisfbversion >= SISFB_VERSION(1, 6, 13)) {
			 pSiS->sisfbscalelcd = mysisfbinfo->sisfb_scalelcd;
			 pSiS->sisfbspecialtiming = mysisfbinfo->sisfb_specialtiming;
		      }
		      if(sisfbversion >= SISFB_VERSION(1, 6, 16)) {
			 if(pSiS->VGAEngine == SIS_315_VGA) {
			    pSiS->donttrustpdc = FALSE;
			    pSiS->sisfbpdc = mysisfbinfo->sisfb_lcdpdc;
			    if(sisfbversion >= SISFB_VERSION(1, 6, 24)) {
			       pSiS->sisfb_haveemi = mysisfbinfo->sisfb_haveemi ? TRUE : FALSE;
			       pSiS->sisfb_haveemilcd = TRUE;  /* will match most cases */
			       pSiS->sisfb_emi30 = mysisfbinfo->sisfb_emi30;
			       pSiS->sisfb_emi31 = mysisfbinfo->sisfb_emi31;
			       pSiS->sisfb_emi32 = mysisfbinfo->sisfb_emi32;
			       pSiS->sisfb_emi33 = mysisfbinfo->sisfb_emi33;
			    }
			    if(sisfbversion >= SISFB_VERSION(1, 6, 25)) {
			       pSiS->sisfb_haveemilcd = mysisfbinfo->sisfb_haveemilcd ? TRUE : FALSE;
			    }
			    if(sisfbversion >= SISFB_VERSION(1, 6, 31)) {
			       pSiS->sisfbpdca = mysisfbinfo->sisfb_lcdpdca;
			    } else {
			       if(pSiS->sisfbpdc) {
				  pSiS->sisfbpdca = (pSiS->sisfbpdc & 0xf0) >> 3;
				  pSiS->sisfbpdc  = (pSiS->sisfbpdc & 0x0f) << 1;
			       } else {
				  pSiS->sisfbpdca = pSiS->sisfbpdc = 0xff;
			       }
			    }
			 }
		      }
		      if(sisfbversion >= SISFB_VERSION(1, 7, 0)) {
		         pSiS->sisfb_havelock = TRUE;
			 if(sisfbversion >= SISFB_VERSION(1, 7, 1)) {
			    pSiS->sisfb_tvxpos = mysisfbinfo->sisfb_tvxpos;
			    pSiS->sisfb_tvypos = mysisfbinfo->sisfb_tvypos;
			    pSiS->sisfb_tvposvalid = TRUE;
			 }
		      }
		      if(sisfbversion >= SISFB_VERSION(1, 8, 7)) {
			 pSiS->sisfbcanpost = (mysisfbinfo->sisfb_can_post) ? TRUE : FALSE;
			 pSiS->sisfbcardposted = (mysisfbinfo->sisfb_card_posted) ? TRUE : FALSE;
			 pSiS->sisfbprimary = (mysisfbinfo->sisfb_was_boot_device) ? TRUE : FALSE;
			 /* Validity check */
			 if(!pSiS->sisfbcardposted) {
			    pSiS->sisfbprimary = FALSE;
			 }
		      }
		   }
	        }
		free(mysisfbinfo);
		mysisfbinfo = NULL;
	     }
	     close (fd);
          }
	  i++;
       } while((i <= 15) && (!pSiS->sisfbfound));

       if(pSiS->sisfbfound) {
          strncpy(pSiS->sisfbdevname, name, 15);
       } else {
          xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "sisfb not found\n");
       }
    }

    if(!pSiS->sisfbfound) {
       pSiS->sisfbcardposted = FALSE;
    }
}

/* ROM handling helpers */

static Bool
SISCheckBIOS(SISPtr pSiS, UShort mypciid, UShort mypcivendor, int biossize)
{
    UShort romptr, pciid;

    if(!pSiS->BIOS)
       return FALSE;

    if((pSiS->BIOS[0] != 0x55) || (pSiS->BIOS[1] != 0xaa))
       return FALSE;

    romptr = pSiS->BIOS[0x18] | (pSiS->BIOS[0x19] << 8);

    if(romptr > (biossize - 8))
       return FALSE;

    if((pSiS->BIOS[romptr]   != 'P') || (pSiS->BIOS[romptr+1] != 'C') ||
       (pSiS->BIOS[romptr+2] != 'I') || (pSiS->BIOS[romptr+3] != 'R'))
       return FALSE;

    pciid = pSiS->BIOS[romptr+4] | (pSiS->BIOS[romptr+5] << 8);
    if(pciid != mypcivendor)
       return FALSE;

    pciid = pSiS->BIOS[romptr+6] | (pSiS->BIOS[romptr+7] << 8);
    if(pciid != mypciid)
       return FALSE;
    return TRUE;
}

static void
SiSReadROM(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
    SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

    pSiS->SiS_Pr->VirtualRomBase = NULL;
    pSiS->BIOS = NULL;
    pSiS->ROMPCIENew = FALSE;
    pSiS->SiS_Pr->UseROM = FALSE;
    pSiS->ROM661New = FALSE;
    pSiS->HaveXGIBIOS = FALSE;

    if((pSiS->VGAEngine == SIS_300_VGA) || (pSiS->VGAEngine == SIS_315_VGA)) {
#ifdef SISDUALHEAD
       if(pSiSEnt) {
	  if(pSiSEnt->BIOS) {
	     pSiS->BIOS = pSiSEnt->BIOS;
	     pSiS->SiS_Pr->VirtualRomBase = pSiS->BIOS;
	     pSiS->ROM661New = pSiSEnt->ROM661New;
	     pSiS->HaveXGIBIOS = pSiSEnt->HaveXGIBIOS;
	  }
       }
#endif
       if(!pSiS->BIOS) {
	  if(!(pSiS->BIOS = calloc(1, BIOS_SIZE))) {
	     xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		"Could not allocate memory for video BIOS image\n");
	  } else {
	     UShort mypciid = pSiS->Chipset;
	     UShort mypcivendor = (pSiS->ChipFlags & SiSCF_IsXGI) ? PCI_VENDOR_XGI : PCI_VENDOR_SIS;
	     Bool   found = FALSE, readpci = FALSE;
	     int    biossize = BIOS_SIZE;

	     switch(pSiS->ChipType) {
	     case SIS_315:    mypciid = PCI_CHIP_SIS315;
			      readpci = TRUE;
			      break;
	     case SIS_315PRO: mypciid = PCI_CHIP_SIS315PRO;
			      readpci = TRUE;
			      break;
	     case SIS_300:
	     case SIS_315H:
	     case SIS_330:
	     case SIS_340:
	     case SIS_341:
	     case SIS_342:
	     case XGI_40:     readpci = TRUE;
			      break;
	     case XGI_20:     readpci = TRUE;
			      biossize = 0x8000;
			      break;
	     }

	     if(readpci) {
	       pSiS->PciInfo->rom_size = biossize;
#ifndef XSERVER_LIBPCIACCESS
               xf86ReadPciBIOS(0, pSiS->PciTag, 0, pSiS->BIOS, biossize);
#else
	       pci_device_read_rom(pSiS->PciInfo, pSiS->BIOS);
#endif
	       if(SISCheckBIOS(pSiS, mypciid, mypcivendor, biossize)) {
                 found = TRUE;
               }
             }

	     if(!found) {
		ULong segstart;
		for(segstart = BIOS_BASE; segstart < 0x000f0000; segstart += 0x00001000) {

#if XF86_VERSION_CURRENT < XF86_VERSION_NUMERIC(4,2,99,0,0)
		   if(xf86ReadBIOS(segstart, 0, pSiS->BIOS, biossize) != biossize) continue;
#else
#ifndef XSERVER_LIBPCIACCESS
		   if(xf86ReadDomainMemory(pSiS->PciTag, segstart, biossize, pSiS->BIOS) != biossize) continue;
#else
		   if(pci_device_read_rom(pSiS->PciInfo, pSiS->BIOS) != biossize) continue;
#endif
#endif

		   if(!SISCheckBIOS(pSiS, mypciid, mypcivendor, biossize)) continue;

		   found = TRUE;
		   break;
		}
             }

	     if(found) {
		UShort romptr = pSiS->BIOS[0x16] | (pSiS->BIOS[0x17] << 8);
		pSiS->SiS_Pr->VirtualRomBase = pSiS->BIOS;
		if(pSiS->ChipFlags & SiSCF_IsXGI) {
		   pSiS->HaveXGIBIOS = pSiS->SiS_Pr->SiS_XGIROM = TRUE;
		   pSiS->SiS_Pr->UseROM = FALSE;
		   if(pSiS->ChipFlags & SiSCF_IsXGIV3) {
		      if(!(pSiS->BIOS[0x1d1] & 0x01)) {
			 pSiS->SiS_Pr->DDCPortMixup = TRUE;
		      }
	           }
	        } else {
		   pSiS->ROM661New = SiSDetermineROMLayout661(pSiS->SiS_Pr);
		   if(pSiS->ROM661New){
                       /* The version number begin with 2 support AGP interface, and the version number begin with 3 support PCIE interface. */
                       if(pSiS->BIOS[romptr]=='3'){
                           pSiS->ROMPCIENew = TRUE; 
                           pSiS->BIOSVersion = atoi((char *)(&pSiS->BIOS[romptr+2]));
                       }
		   }
		}
		xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
			"Video BIOS version \"%7s\" found (%s data layout)\n",
			&pSiS->BIOS[romptr], pSiS->ROM661New ? "new SiS" :
				(pSiS->HaveXGIBIOS ? "XGI" : "old SiS"));
		if(pSiS->SiS_Pr->DDCPortMixup) {
		   xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			"*** Buggy XGI V3XT card detected: If VGA and DVI are connected at the\n");
		   xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			"*** same time, BIOS and driver will be unable to detect DVI connection.\n");
		}
#ifdef SISDUALHEAD
		if(pSiSEnt) {
		   pSiSEnt->BIOS = pSiS->BIOS;
		   pSiSEnt->ROM661New = pSiS->ROM661New;
		   pSiSEnt->HaveXGIBIOS = pSiS->HaveXGIBIOS;
		}
#endif
	     } else {
	        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			 "Could not find/read video BIOS\n");
		free(pSiS->BIOS);
		pSiS->BIOS = NULL;
	     }
          }
       }

       if(!(pSiS->ChipFlags & SiSCF_IsXGI)) {
          pSiS->SiS_Pr->UseROM = (pSiS->BIOS) ? TRUE : FALSE;
          if(pSiS->SiS_Pr->UseROM == TRUE) pSiS->SiS_Pr->BIOSVersion = pSiS->BIOSVersion;
       }
    }
}

/* Copy from and to SiS entity */

#ifdef SISDUALHEAD
static void
SiSCopyFromToEntity(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
    SISEntPtr pSiSEnt = pSiS->entityPrivate;

    if(pSiS->DualHeadMode) {
       if(!pSiS->SecondHead) {
	  /* Copy some option settings to entity private */
	  pSiSEnt->HWCursor = pSiS->HWCursor;
	  pSiSEnt->NoAccel = pSiS->NoAccel;
	  pSiSEnt->useEXA = pSiS->useEXA;
	  pSiSEnt->restorebyset = pSiS->restorebyset;
	  pSiSEnt->OptROMUsage = pSiS->OptROMUsage;
	  pSiSEnt->OptUseOEM = pSiS->OptUseOEM;
	  pSiSEnt->TurboQueue = pSiS->TurboQueue;
	  pSiSEnt->forceCRT1 = pSiS->forceCRT1;
	  pSiSEnt->ForceCRT1Type = pSiS->ForceCRT1Type;
	  pSiSEnt->CRT1TypeForced = pSiS->CRT1TypeForced;
	  pSiSEnt->ForceCRT2Type = pSiS->ForceCRT2Type;
	  pSiSEnt->ForceTVType = pSiS->ForceTVType;
	  pSiSEnt->ForceYPbPrType = pSiS->ForceYPbPrType;
	  pSiSEnt->ForceYPbPrAR = pSiS->ForceYPbPrAR;
	  pSiSEnt->UsePanelScaler = pSiS->UsePanelScaler;
	  pSiSEnt->CenterLCD = pSiS->CenterLCD;
	  pSiSEnt->DSTN = pSiS->DSTN;
	  pSiSEnt->FSTN = pSiS->FSTN;
	  pSiSEnt->OptTVStand = pSiS->OptTVStand;
	  pSiSEnt->NonDefaultPAL = pSiS->NonDefaultPAL;
	  pSiSEnt->NonDefaultNTSC = pSiS->NonDefaultNTSC;
	  pSiSEnt->chtvtype = pSiS->chtvtype;
	  pSiSEnt->OptTVOver = pSiS->OptTVOver;
	  pSiSEnt->OptTVSOver = pSiS->OptTVSOver;
	  pSiSEnt->chtvlumabandwidthcvbs = pSiS->chtvlumabandwidthcvbs;
	  pSiSEnt->chtvlumabandwidthsvideo = pSiS->chtvlumabandwidthsvideo;
	  pSiSEnt->chtvlumaflickerfilter = pSiS->chtvlumaflickerfilter;
	  pSiSEnt->chtvchromabandwidth = pSiS->chtvchromabandwidth;
	  pSiSEnt->chtvchromaflickerfilter = pSiS->chtvchromaflickerfilter;
	  pSiSEnt->chtvtextenhance = pSiS->chtvtextenhance;
	  pSiSEnt->chtvcontrast = pSiS->chtvcontrast;
	  pSiSEnt->chtvcvbscolor = pSiS->chtvcvbscolor;
	  pSiSEnt->sistvedgeenhance = pSiS->sistvedgeenhance;
	  pSiSEnt->sistvantiflicker = pSiS->sistvantiflicker;
	  pSiSEnt->sistvsaturation = pSiS->sistvsaturation;
	  pSiSEnt->sistvcfilter = pSiS->sistvcfilter;
	  pSiSEnt->sistvyfilter = pSiS->sistvyfilter;
	  pSiSEnt->sistvcolcalibc = pSiS->sistvcolcalibc;
	  pSiSEnt->sistvcolcalibf = pSiS->sistvcolcalibf;
	  pSiSEnt->tvxpos = pSiS->tvxpos;
	  pSiSEnt->tvypos = pSiS->tvypos;
	  pSiSEnt->tvxscale = pSiS->tvxscale;
	  pSiSEnt->tvyscale = pSiS->tvyscale;
	  pSiSEnt->siscrt1satgain = pSiS->siscrt1satgain;
	  pSiSEnt->crt1satgaingiven = pSiS->crt1satgaingiven;
	  pSiSEnt->CRT1gamma = pSiS->CRT1gamma;
	  pSiSEnt->CRT1gammaGiven = pSiS->CRT1gammaGiven;
	  pSiSEnt->XvGammaRed = pSiS->XvGammaRed;
	  pSiSEnt->XvGammaGreen = pSiS->XvGammaGreen;
	  pSiSEnt->XvGammaBlue = pSiS->XvGammaBlue;
	  pSiSEnt->XvGamma = pSiS->XvGamma;
	  pSiSEnt->XvGammaGiven = pSiS->XvGammaGiven;
	  pSiSEnt->CRT2gamma = pSiS->CRT2gamma;
	  pSiSEnt->XvOnCRT2 = pSiS->XvOnCRT2;
	  pSiSEnt->AllowHotkey = pSiS->AllowHotkey;
	  pSiSEnt->enablesisctrl = pSiS->enablesisctrl;
	  pSiSEnt->SenseYPbPr = pSiS->SenseYPbPr;
	  pSiSEnt->BenchMemCpy = pSiS->BenchMemCpy;
       } else {
	  /* We always use same cursor type on both screens */
	  pSiS->HWCursor = pSiSEnt->HWCursor;
	  /* We need identical NoAccel setting */
	  pSiS->NoAccel = pSiSEnt->NoAccel;
	  pSiS->useEXA = pSiSEnt->useEXA;
	  pSiS->TurboQueue = pSiSEnt->TurboQueue;
	  pSiS->restorebyset = pSiSEnt->restorebyset;
	  pSiS->AllowHotkey = pSiS->AllowHotkey;
	  pSiS->OptROMUsage = pSiSEnt->OptROMUsage;
	  pSiS->OptUseOEM = pSiSEnt->OptUseOEM;
	  pSiS->forceCRT1 = pSiSEnt->forceCRT1;
	  pSiS->nocrt2ddcdetection = FALSE;
	  pSiS->forceLCDcrt1 = FALSE;
	  pSiS->forcecrt2redetection = FALSE;
	  pSiS->ForceCRT1Type = pSiSEnt->ForceCRT1Type;
	  pSiS->ForceCRT2Type = pSiSEnt->ForceCRT2Type;
	  pSiS->CRT1TypeForced = pSiSEnt->CRT1TypeForced;
	  pSiS->UsePanelScaler = pSiSEnt->UsePanelScaler;
	  pSiS->CenterLCD = pSiSEnt->CenterLCD;
	  pSiS->DSTN = pSiSEnt->DSTN;
	  pSiS->FSTN = pSiSEnt->FSTN;
	  pSiS->OptTVStand = pSiSEnt->OptTVStand;
	  pSiS->NonDefaultPAL = pSiSEnt->NonDefaultPAL;
	  pSiS->NonDefaultNTSC = pSiSEnt->NonDefaultNTSC;
	  pSiS->chtvtype = pSiSEnt->chtvtype;
	  pSiS->ForceTVType = pSiSEnt->ForceTVType;
	  pSiS->ForceYPbPrType = pSiSEnt->ForceYPbPrType;
	  pSiS->ForceYPbPrAR = pSiSEnt->ForceYPbPrAR;
	  pSiS->OptTVOver = pSiSEnt->OptTVOver;
	  pSiS->OptTVSOver = pSiSEnt->OptTVSOver;
	  pSiS->chtvlumabandwidthcvbs = pSiSEnt->chtvlumabandwidthcvbs;
	  pSiS->chtvlumabandwidthsvideo = pSiSEnt->chtvlumabandwidthsvideo;
	  pSiS->chtvlumaflickerfilter = pSiSEnt->chtvlumaflickerfilter;
	  pSiS->chtvchromabandwidth = pSiSEnt->chtvchromabandwidth;
	  pSiS->chtvchromaflickerfilter = pSiSEnt->chtvchromaflickerfilter;
	  pSiS->chtvcvbscolor = pSiSEnt->chtvcvbscolor;
	  pSiS->chtvtextenhance = pSiSEnt->chtvtextenhance;
	  pSiS->chtvcontrast = pSiSEnt->chtvcontrast;
	  pSiS->sistvedgeenhance = pSiSEnt->sistvedgeenhance;
	  pSiS->sistvantiflicker = pSiSEnt->sistvantiflicker;
	  pSiS->sistvsaturation = pSiSEnt->sistvsaturation;
	  pSiS->sistvcfilter = pSiSEnt->sistvcfilter;
	  pSiS->sistvyfilter = pSiSEnt->sistvyfilter;
	  pSiS->sistvcolcalibc = pSiSEnt->sistvcolcalibc;
	  pSiS->sistvcolcalibf = pSiSEnt->sistvcolcalibf;
	  pSiS->tvxpos = pSiSEnt->tvxpos;
	  pSiS->tvypos = pSiSEnt->tvypos;
	  pSiS->tvxscale = pSiSEnt->tvxscale;
	  pSiS->tvyscale = pSiSEnt->tvyscale;
	  pSiS->SenseYPbPr = pSiSEnt->SenseYPbPr;
	  if(!pSiS->CRT1gammaGiven) {
	     if(pSiSEnt->CRT1gammaGiven)
	        pSiS->CRT1gamma = pSiSEnt->CRT1gamma;
	  }
	  pSiS->CRT2gamma = pSiSEnt->CRT2gamma;
	  if(!pSiS->XvGammaGiven) {
	     if(pSiSEnt->XvGammaGiven) {
		pSiS->XvGamma = pSiSEnt->XvGamma;
		pSiS->XvGammaRed = pSiS->XvGammaRedDef = pSiSEnt->XvGammaRed;
		pSiS->XvGammaGreen = pSiS->XvGammaGreenDef = pSiSEnt->XvGammaGreen;
		pSiS->XvGammaBlue = pSiS->XvGammaBlueDef = pSiSEnt->XvGammaBlue;
	     }
	  }
	  if(!pSiS->crt1satgaingiven) {
	     if(pSiSEnt->crt1satgaingiven)
	        pSiS->siscrt1satgain = pSiSEnt->siscrt1satgain;
	  }
	  pSiS->XvOnCRT2 = pSiSEnt->XvOnCRT2;
	  pSiS->enablesisctrl = pSiSEnt->enablesisctrl;
	  pSiS->BenchMemCpy = pSiSEnt->BenchMemCpy;
	  /* Copy gamma brightness to Ent (sic!) for Xinerama */
	  pSiSEnt->GammaBriR = pSiS->GammaBriR;
	  pSiSEnt->GammaBriG = pSiS->GammaBriG;
	  pSiSEnt->GammaBriB = pSiS->GammaBriB;
	  pSiSEnt->NewGammaBriR = pSiS->NewGammaBriR;
	  pSiSEnt->NewGammaBriG = pSiS->NewGammaBriG;
	  pSiSEnt->NewGammaBriB = pSiS->NewGammaBriB;
	  pSiSEnt->NewGammaConR = pSiS->NewGammaConR;
	  pSiSEnt->NewGammaConG = pSiS->NewGammaConG;
	  pSiSEnt->NewGammaConB = pSiS->NewGammaConB;
       }
    }
}
#endif

/* Handle Chrontel GPIO */

static void
SiSDetermineChrontelGPIO(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);

    /* There are some machines out there which require a special
     * setup of the GPIO registers in order to make the Chrontel
     * work. Try to find out if we're running on such a machine.
     */

    pSiS->SiS_Pr->SiS_ChSW = FALSE;
    if(pSiS->Chipset == PCI_CHIP_SIS630) {
       int i = 0;
       do {
	  if(mychswtable[i].subsysVendor == PCI_SUB_VENDOR_ID( pSiS->PciInfo) &&
	     mychswtable[i].subsysCard == PCI_SUB_DEVICE_ID(pSiS->PciInfo)) {
	     xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	         "PCI subsystem ID found in list for Chrontel/GPIO setup:\n");
	     xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		 "\tVendor/Card: %s %s (ID %04x)\n",
		  mychswtable[i].vendorName,
		  mychswtable[i].cardName,
		  PCI_SUB_DEVICE_ID(pSiS->PciInfo));
	     pSiS->SiS_Pr->SiS_ChSW = TRUE;
	     break;
          }
          i++;
       } while(mychswtable[i].subsysVendor != 0);
    }
}

/* Handle custom timing */

static void
SiSDetermineCustomTiming(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
    int    i = 0, j;
    UShort bversptr = 0;
    Bool   footprint;
    CARD32 chksum = 0;

    if(pSiS->SiS_Pr->SiS_CustomT == CUT_NONE) {

       if(pSiS->SiS_Pr->UseROM) {
          bversptr = pSiS->BIOS[0x16] | (pSiS->BIOS[0x17] << 8);
          for(i = 0; i < 32768; i++) chksum += pSiS->BIOS[i];
       }

       i = 0;
       do {
	  if( (SiS_customttable[i].chipID == pSiS->ChipType)                            &&
	      ((!strlen(SiS_customttable[i].biosversion)) ||
	       (pSiS->SiS_Pr->UseROM &&
	       (!strncmp(SiS_customttable[i].biosversion, (char *)&pSiS->BIOS[bversptr],
	                strlen(SiS_customttable[i].biosversion)))))                     &&
	      ((!strlen(SiS_customttable[i].biosdate)) ||
	       (pSiS->SiS_Pr->UseROM &&
	       (!strncmp(SiS_customttable[i].biosdate, (char *)&pSiS->BIOS[0x2c],
	                strlen(SiS_customttable[i].biosdate)))))			&&
	      ((!SiS_customttable[i].bioschksum) ||
	       (pSiS->SiS_Pr->UseROM &&
	       (SiS_customttable[i].bioschksum == chksum)))				&&
	      (SiS_customttable[i].pcisubsysvendor == PCI_SUB_VENDOR_ID( pSiS->PciInfo ))	&&
	      (SiS_customttable[i].pcisubsyscard == PCI_SUB_DEVICE_ID( pSiS->PciInfo )) ) {
	     footprint = TRUE;
	     for(j = 0; j < 5; j++) {
	        if(SiS_customttable[i].biosFootprintAddr[j]) {
		   if(pSiS->SiS_Pr->UseROM) {
		      if(pSiS->BIOS[SiS_customttable[i].biosFootprintAddr[j]] !=
						SiS_customttable[i].biosFootprintData[j])
		         footprint = FALSE;
		   } else footprint = FALSE;
	        }
	     }
	     if(footprint) {
	        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	           "Identified %s %s, special timing applies\n",
		   SiS_customttable[i].vendorName, SiS_customttable[i].cardName);
	        pSiS->SiS_Pr->SiS_CustomT = SiS_customttable[i].SpecialID;
	        break;
	     }
          }
          i++;
       } while(SiS_customttable[i].chipID);
    }

    if((pSiS->SiS_Pr->SiS_CustomT == CUT_ICOP550) ||
       (pSiS->SiS_Pr->SiS_CustomT == CUT_ICOP550_2)) {
       xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"This driver version doesn't entirely support this customized\n");
       xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"hardware. A special version is available from the author.\n");
       pSiS->SiS_Pr->SiS_CustomT = CUT_NONE;
    }

}

/* Handle PDC and EMI */

static void
SiSHandlePDCEMI(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
    const char *unable = "Unable to detect LCD PanelDelayCompensation, %s\n";
    const char *oldsisfb = "please update sisfb";
    const char *nolcd = "LCD is not active";
    const char *usingpdc = "Using LCD PanelDelayCompensation 0x%02x%s\n";
    const char *detected = "Detected LCD PanelDelayCompensation 0x%02x%s\n";
    const char *forcrt1 = " (for LCD=CRT1)";
    const char *forcrt2 = " (for LCD=CRT2)";
    const char *biosuses = "BIOS uses OEM LCD Panel Delay Compensation 0x%02x\n";

#ifdef SISDUALHEAD
    if((!pSiS->DualHeadMode) || (!pSiS->SecondHead)) {
#endif
       if(pSiS->VGAEngine == SIS_300_VGA) {

          if(pSiS->VBFlags2 & (VB2_LVDS | VB2_30xBDH)) {

	     /* Save the current PDC if the panel is used at the moment.
	      * This seems by far the safest way to find out about it.
	      * If the system is using an old version of sisfb, we can't
	      * trust the pdc register value. If sisfb saved the pdc for
	      * us, use it.
	      */
	     if(pSiS->sisfbpdc != 0xff) {
	        pSiS->SiS_Pr->PDC = pSiS->sisfbpdc;
	     } else {
	        if(!(pSiS->donttrustpdc)) {
	           UChar tmp;
	           inSISIDXREG(SISCR, 0x30, tmp);
	           if(tmp & 0x20) {
	              inSISIDXREG(SISPART1, 0x13, pSiS->SiS_Pr->PDC);
                   } else {
	             xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		          unable, nolcd);
	           }
	        } else {
	           xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		        unable, oldsisfb);
	        }
	     }
	     if(pSiS->SiS_Pr->PDC != -1) {
	        pSiS->SiS_Pr->PDC &= 0x3c;
	        xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		     detected, pSiS->SiS_Pr->PDC, "");
	     }

	     /* If we haven't been able to find out, use our other methods */
	     if(pSiS->SiS_Pr->PDC == -1) {
		int i=0;
		do {
		   if(mypdctable[i].subsysVendor == PCI_SUB_VENDOR_ID(pSiS->PciInfo) &&
		      mypdctable[i].subsysCard == PCI_SUB_DEVICE_ID( pSiS->PciInfo )) {
			 xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			    "PCI card/vendor identified for non-default PanelDelayCompensation\n");
			 xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
			     "Vendor: %s, card: %s (ID %04x), PanelDelayCompensation: 0x%02x\n",
			     mypdctable[i].vendorName, mypdctable[i].cardName,
			     PCI_SUB_DEVICE_ID( pSiS->PciInfo ), mypdctable[i].pdc);
			 if(pSiS->PDC == -1) {
			    pSiS->PDC = mypdctable[i].pdc;
			 } else {
			    xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
				"PanelDelayCompensation overruled by option\n");
			 }
			 break;
		   }
		   i++;
		} while(mypdctable[i].subsysVendor != 0);
	     }

	     if(pSiS->PDC != -1) {
		if(pSiS->BIOS) {
		   if(pSiS->VBFlags2 & VB2_LVDS) {
		      if(pSiS->BIOS[0x220] & 0x80) {
			 xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			     biosuses, pSiS->BIOS[0x220] & 0x3c);
			 pSiS->BIOS[0x220] &= 0x7f;
		      }
		   }
		   if(pSiS->VBFlags2 & (VB2_301B | VB2_302B)) {
		      if(pSiS->BIOS[0x220] & 0x80) {
			 xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			     biosuses,
			       (  (pSiS->VBLCDFlags & VB_LCD_1280x1024) ?
			                 pSiS->BIOS[0x223] : pSiS->BIOS[0x224]  ) & 0x3c);
			 pSiS->BIOS[0x220] &= 0x7f;
		      }
		   }
		}
		pSiS->SiS_Pr->PDC = (pSiS->PDC & 0x3c);
		xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		      usingpdc, pSiS->SiS_Pr->PDC, "");
	     }
	  }

       }  /* SIS_300_VGA */

       if(pSiS->VGAEngine == SIS_315_VGA) {

	  UChar tmp, tmp2;
	  inSISIDXREG(SISCR, 0x30, tmp);

	  /* Save the current PDC if the panel is used at the moment. */
	  if(pSiS->VBFlags2 & VB2_SISLVDSBRIDGE) {

	     if(pSiS->sisfbpdc != 0xff) {
	        pSiS->SiS_Pr->PDC = pSiS->sisfbpdc;
	     }
	     if(pSiS->sisfbpdca != 0xff) {
	        pSiS->SiS_Pr->PDCA = pSiS->sisfbpdca;
	     }

	     if(!pSiS->donttrustpdc) {
	        if((pSiS->sisfbpdc == 0xff) && (pSiS->sisfbpdca == 0xff)) {
		   CARD16 tempa, tempb;
		   inSISIDXREG(SISPART1,0x2d,tmp2);
		   tempa = (tmp2 & 0xf0) >> 3;
		   tempb = (tmp2 & 0x0f) << 1;
		   inSISIDXREG(SISPART1,0x20,tmp2);
		   tempa |= ((tmp2 & 0x40) >> 6);
		   inSISIDXREG(SISPART1,0x35,tmp2);
		   tempb |= ((tmp2 & 0x80) >> 7);
		   inSISIDXREG(SISPART1,0x13,tmp2);
		   if(!pSiS->ROM661New) {
		      if((tmp2 & 0x04) || (tmp & 0x20)) {
		         pSiS->SiS_Pr->PDCA = tempa;
		         pSiS->SiS_Pr->PDC  = tempb;
		      } else {
			 xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			     unable, nolcd);
		      }
		   } else {
		      if(tmp2 & 0x04) {
		         pSiS->SiS_Pr->PDCA = tempa;
		      } else if(tmp & 0x20) {
		         pSiS->SiS_Pr->PDC  = tempb;
		      } else {
			 xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			     unable, nolcd);
		      }
		   }
		}
	     } else {
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		    unable, oldsisfb);
	     }
	     if(pSiS->SiS_Pr->PDC != -1) {
		xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		     detected, pSiS->SiS_Pr->PDC, forcrt2);
	     }
	     if(pSiS->SiS_Pr->PDCA != -1) {
		xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		     detected, pSiS->SiS_Pr->PDCA, forcrt1);
	     }
	  }

	  /* Let user override (for all bridges) */
	  if(pSiS->VBFlags2 & VB2_30xBLV) {
	     if(pSiS->PDC != -1) {
	        pSiS->SiS_Pr->PDC = pSiS->PDC & 0x1f;
		xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		     usingpdc, pSiS->SiS_Pr->PDC, forcrt2);
	     }
	     if(pSiS->PDCA != -1) {
		pSiS->SiS_Pr->PDCA = pSiS->PDCA & 0x1f;
		xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		     usingpdc, pSiS->SiS_Pr->PDCA, forcrt1);
	     }
          }

 	  /* Read the current EMI (if not overruled) */
	  if(pSiS->VBFlags2 & VB2_SISEMIBRIDGE) {
	     MessageType from = X_PROBED;
	     if(pSiS->EMI != -1) {
		pSiS->SiS_Pr->EMI_30 = (pSiS->EMI >> 24) & 0x60;
		pSiS->SiS_Pr->EMI_31 = (pSiS->EMI >> 16) & 0xff;
		pSiS->SiS_Pr->EMI_32 = (pSiS->EMI >> 8)  & 0xff;
		pSiS->SiS_Pr->EMI_33 = pSiS->EMI & 0xff;
		pSiS->SiS_Pr->HaveEMI = pSiS->SiS_Pr->HaveEMILCD = TRUE;
		pSiS->SiS_Pr->OverruleEMI = TRUE;
		from = X_CONFIG;
	     } else if((pSiS->sisfbfound) && (pSiS->sisfb_haveemi)) {
		pSiS->SiS_Pr->EMI_30 = pSiS->sisfb_emi30;
		pSiS->SiS_Pr->EMI_31 = pSiS->sisfb_emi31;
		pSiS->SiS_Pr->EMI_32 = pSiS->sisfb_emi32;
		pSiS->SiS_Pr->EMI_33 = pSiS->sisfb_emi33;
		pSiS->SiS_Pr->HaveEMI = TRUE;
		if(pSiS->sisfb_haveemilcd) pSiS->SiS_Pr->HaveEMILCD = TRUE;
		pSiS->SiS_Pr->OverruleEMI = FALSE;
	     } else {
		inSISIDXREG(SISPART4, 0x30, pSiS->SiS_Pr->EMI_30);
		inSISIDXREG(SISPART4, 0x31, pSiS->SiS_Pr->EMI_31);
		inSISIDXREG(SISPART4, 0x32, pSiS->SiS_Pr->EMI_32);
		inSISIDXREG(SISPART4, 0x33, pSiS->SiS_Pr->EMI_33);
		pSiS->SiS_Pr->HaveEMI = TRUE;
		if(tmp & 0x20) pSiS->SiS_Pr->HaveEMILCD = TRUE;
		pSiS->SiS_Pr->OverruleEMI = FALSE;
	     }
	     xf86DrvMsg(pScrn->scrnIndex, from,
		   "302LV/302ELV: Using EMI 0x%02x%02x%02x%02x%s\n",
		   pSiS->SiS_Pr->EMI_30,pSiS->SiS_Pr->EMI_31,
		   pSiS->SiS_Pr->EMI_32,pSiS->SiS_Pr->EMI_33,
		   pSiS->SiS_Pr->HaveEMILCD ? " (LCD)" : "");
	  }

       } /* SIS_315_VGA */
#ifdef SISDUALHEAD
    }
#endif
}

/* DDC helpers */

/* We keep EDIDs for CRT1 and CRT2 (each analog and digital)
 * in memory for our dynamic modelist feature. The following
 * two routines are used to either set a new EDID, or free
 * one. The Free-routine also checks that none of our official
 * monitor->DDC pointer point to the to-be-freed EDID block.
 */

xf86MonPtr
SiSSetEDIDPtr(xf86MonPtr *ptr, xf86MonPtr pMonitor)
{
   if((*ptr)) {
      memcpy((*ptr), pMonitor, sizeof(xf86Monitor));
      free(pMonitor);
   } else {
      (*ptr) = pMonitor;
   }

   return (*ptr);
}

void
SiSFreeEDID(ScrnInfoPtr pScrn, xf86MonPtr *ptr)
{
   SISPtr pSiS = SISPTR(pScrn);

   if((*ptr)) {

      if(pScrn->monitor) {
         if(pScrn->monitor->DDC == (*ptr)) {
            pScrn->monitor->DDC = NULL;
         }
      }

#ifdef SISMERGED
      if(pSiS->MergedFB) {
         if(pSiS->CRT2pScrn && pSiS->CRT2pScrn->monitor) {
            if(pSiS->CRT2pScrn->monitor->DDC == (*ptr)) {
               pSiS->CRT2pScrn->monitor->DDC = NULL;
            }
         }
      }
#endif

      free((*ptr));
      *ptr = NULL;

   }
}

xf86MonPtr
SiSInternalDDC(ScrnInfoPtr pScrn, int crtno)
{
   SISPtr     pSiS = SISPTR(pScrn);
   xf86MonPtr pMonitor = NULL;
   UShort     temp = 0xffff, temp1, i, realcrtno = crtno;
   UChar      buffer[256];

   /* If CRT1 is off, skip DDC */
   if((pSiS->CRT1off) && (!crtno))
      return NULL;

   if(crtno) {
      if(pSiS->VBFlags & CRT2_LCD)      realcrtno = 1;
      else if(pSiS->VBFlags & CRT2_VGA) realcrtno = 2;
      else				return NULL;
      if(pSiS->SiS_Pr->DDCPortMixup)    realcrtno = 0;
   } else {
      if(!(pSiS->SiS_SD3_Flags & SiS_SD3_SUPPORTDUALDVI)) {
         /* If CRT1 is LCDA, skip DDC (except 301C: DDC allowed, but uses CRT2 port!) */
         if(pSiS->VBFlags & CRT1_LCDA) {
            if(pSiS->VBFlags2 & VB2_SISTMDSLCDABRIDGE) realcrtno = 1;
            else return NULL;
         }
      } else {
         realcrtno = 0;
      }
   }

   i = 3; /* Number of retrys */
   do {
      temp1 = SiS_HandleDDC(pSiS->SiS_Pr, pSiS->VBFlags, pSiS->VGAEngine,
			realcrtno, 0, &buffer[0], pSiS->VBFlags2);
      if((temp1) && (temp1 != 0xffff)) temp = temp1;
   } while((temp == 0xffff) && i--);

   if(temp != 0xffff) {

      xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "CRT%d DDC supported\n", crtno + 1);

      xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "CRT%d DDC level: %s%s%s%s\n",
	     crtno + 1,
	     (temp & 0x1a) ? "" : "[none of the supported]",
	     (temp & 0x02) ? "2 " : "",
	     (temp & 0x08) ? "D&P" : "",
             (temp & 0x10) ? "FPDI-2" : "");

      if(temp & 0x02) {

	 i = 5;  /* Number of retrys */
	 do {
	    temp = SiS_HandleDDC(pSiS->SiS_Pr, pSiS->VBFlags, pSiS->VGAEngine,
				realcrtno, 1, &buffer[0], pSiS->VBFlags2);
	 } while((temp) && i--);

         if(!temp) {

	    if((pMonitor = xf86InterpretEDID(pScrn->scrnIndex, &buffer[0]))) {

	       int tempvgagamma = 0, templcdgamma = 0;
		/* X will access rawData, so we should set it null */
	       /*pMonitor->rawData = NULL; *//* Toss pointer to raw data */

	       if(buffer[0x14] & 0x80) {
	          if((crtno == 0) && (pSiS->SiS_SD3_Flags & SiS_SD3_SUPPORTDUALDVI)) {
	             pMonitor = SiSSetEDIDPtr(&pSiS->currcrt1digitaledid, pMonitor);
	          } else {
	             pMonitor = SiSSetEDIDPtr(&pSiS->currcrt2digitaledid, pMonitor);
	          }
	       } else {
	          if(crtno == 0) {
	             pMonitor = SiSSetEDIDPtr(&pSiS->currcrt1analogedid, pMonitor);
	          } else {
	             pMonitor = SiSSetEDIDPtr(&pSiS->currcrt2analogedid, pMonitor);
	          }
	       }

	       if(buffer[0x14] & 0x80) {
	          templcdgamma = (buffer[0x17] + 100) * 10;
	       } else {
	          tempvgagamma = (buffer[0x17] + 100) * 10;
	       }
	       if(crtno == 0) {
	          pSiS->CRT1LCDMonitorGamma = 0;
		  if(tempvgagamma) pSiS->CRT1VGAMonitorGamma = tempvgagamma;
		  if(pSiS->SiS_SD3_Flags & SiS_SD3_SUPPORTDUALDVI) {
		     if(templcdgamma) pSiS->CRT1LCDMonitorGamma = templcdgamma;
		  }
	       } else {
	          if(tempvgagamma) pSiS->CRT2VGAMonitorGamma = tempvgagamma;
	          if(templcdgamma) pSiS->CRT2LCDMonitorGamma = templcdgamma;
	       }

	       return pMonitor;

	    } else {

	       xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
	           "CRT%d DDC EDID corrupt\n", crtno + 1);

	    }

	 } else if(temp == 0xFFFE) {

	    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		"CRT%d DDC data is from wrong device type (%s)\n",
			crtno + 1,
			(realcrtno == 1) ? "analog instead of digital" :
					   "digital instead of analog");

	 } else {

            xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		"CRT%d DDC reading failed\n", crtno + 1);

	 }

      } else if(temp & 0x18) {

         xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	      "DDC for VESA D&P and FPDI-2 not supported yet.\n");

      }

   } else {

      xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                "CRT%d DDC probing failed\n", crtno + 1);

   }

   return NULL;
}

static xf86MonPtr
SiSDoPrivateDDC(ScrnInfoPtr pScrn, int *crtnum)
{
    SISPtr pSiS = SISPTR(pScrn);

#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
       if(pSiS->SecondHead) {
          *crtnum = 1;
	  return SiSInternalDDC(pScrn, 0);
       } else {
          *crtnum = 2;
	  return SiSInternalDDC(pScrn, 1);
       }
    } else
#endif
    if( (pSiS->CRT1off)			||
        ( (!pSiS->CRT1Detected) &&
          (!((pSiS->VBFlags2 & VB2_SISTMDSLCDABRIDGE) && (pSiS->VBFlags & CRT1_LCDA))) ) ) {
       *crtnum = 2;
       return SiSInternalDDC(pScrn, 1);
    } else {
       *crtnum = 1;
       return SiSInternalDDC(pScrn, 0);
    }
}

void
SiSFindAspect(ScrnInfoPtr pScrn, xf86MonPtr pMonitor, int crtnum, Bool quiet)
{
    SISPtr pSiS = SISPTR(pScrn);
    int UseWide = 0;
    int aspect = 0;
    Bool fromdim = FALSE;

    if(pMonitor &&
       (pSiS->VGAEngine == SIS_315_VGA) &&
       (!DIGITAL(pMonitor->features.input_type))) {

       if(pMonitor->features.hsize && pMonitor->features.vsize) {
	  aspect = (pMonitor->features.hsize * 1000) / pMonitor->features.vsize;
	  if(aspect >= 1400) UseWide = 1;
	  fromdim = TRUE;
       } else if((PREFERRED_TIMING_MODE(pMonitor->features.msc)) &&
		 (pMonitor->det_mon[0].type == DT)) {
	  aspect = (pMonitor->det_mon[0].section.d_timings.h_active * 1000) /
			pMonitor->det_mon[0].section.d_timings.v_active;
	  if(aspect >= 1400) UseWide = 1;
       }

       if(!quiet) {
	  if(aspect) {
	     xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		"According to %s, CRT%d aspect ratio is %.2f:1 (%s)\n",
		fromdim ? "DDC size" : "preferred mode",
		crtnum, (float)aspect / 1000.0, UseWide ? "wide" : "normal");
	  } else {
	     xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"Unable to determine CRT%d aspect ratio, assuming \"normal\"\n",
		crtnum);
	  }
       }

    }

    /* Only overwrite if we haven't been overruled by options */
    if((crtnum == 1) && (!pSiS->havewide1)) {
       pSiS->SiS_Pr->SiS_UseWide = UseWide;
    } else if((crtnum == 2) && (!pSiS->havewide2)) {
       pSiS->SiS_Pr->SiS_UseWideCRT2 = UseWide;
    }
}

static void
SiSGetDDCAndEDID(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
    Bool didddc2 = FALSE;
    xf86MonPtr pMonitor = NULL;
    static const char *ddcsstr = "CRT%d DDC monitor info: *******************************************\n";
    static const char *ddcestr = "End of CRT%d DDC monitor info *************************************\n";

    /* For 300 series and later, we provide our own
     * routines (in order to probe CRT2 as well).
     * If these fail, use the VBE.
     * All other chipsets will use VBE. No need to re-invent
     * the wheel there.
     */

    if((pSiS->VGAEngine == SIS_300_VGA) || (pSiS->VGAEngine == SIS_315_VGA)) {

       if(SiSLoadInitDDCModule(pScrn)) {

	  int crtnum = 0;
	  if((pMonitor = SiSDoPrivateDDC(pScrn, &crtnum))) {
	     didddc2 = TRUE;
	     xf86DrvMsg(pScrn->scrnIndex, X_PROBED, ddcsstr, crtnum);
	     xf86PrintEDID(pMonitor);
	     pScrn->monitor->DDC = pMonitor;
	     /* Now try to find out aspect ratio */
	     SiSFindAspect(pScrn, pMonitor, crtnum, FALSE);
	     xf86DrvMsg(pScrn->scrnIndex, X_PROBED, ddcestr, crtnum);
	  }

       }

    }

#ifdef SISDUALHEAD
    /* In dual head mode, probe DDC using VBE only for CRT1 (second head) */
    if((pSiS->DualHeadMode) && (!didddc2) && (!pSiS->SecondHead)) {
       didddc2 = TRUE;
    }
#endif

    if(!didddc2) {
       /* If CRT1 is off or LCDA, skip DDC via VBE */
       if((pSiS->CRT1off) || (pSiS->VBFlags & CRT1_LCDA)) {
          didddc2 = TRUE;
       }
    }

    /* Now try via VBE */
    if(!didddc2) {

       if(SiSLoadInitDDCModule(pScrn)) {

	  SiS_LoadInitVBE(pScrn);

	  if(pSiS->pVbe) {
	     if((pMonitor = vbeDoEDID(pSiS->pVbe, NULL))) {
		xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		      "VBE CRT1 DDC monitor info:\n");
		xf86PrintEDID(pMonitor);
		if(pMonitor->rawData) {
		   /* Get rid of raw data */
		   free(pMonitor->rawData);
		   pMonitor->rawData = NULL;
		}
		pScrn->monitor->DDC = pMonitor = SiSSetEDIDPtr(&pSiS->currcrt1analogedid, pMonitor);
		/* Try to find out aspect ratio */
		SiSFindAspect(pScrn, pMonitor, 1, FALSE);
		xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		      "End of VBE CRT1 DDC monitor info\n");
	     }
	  } else {
	     xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		 "Failed to read DDC data\n");
	  }

       }

    }
}

/* Deal with HorizSync/VertRefresh */

/* If monitor section has no HSync/VRefresh data,
 * derive it from DDC data.
 */
static void
SiSSetSyncRangeFromEdid(ScrnInfoPtr pScrn, int flag)
{
   MonPtr      mon = pScrn->monitor;
   xf86MonPtr  ddc = mon->DDC;
   float       myhhigh = 0.0, myhlow = 0.0, htest;
   int         myvhigh = 0, myvlow = 0, vtest, i;
   UChar temp;
   const myhddctiming myhtiming[12] = {
       { 1, 0x20, 31.6 }, /* rounded up by .1 */
       { 1, 0x80, 31.6 },
       { 1, 0x02, 35.3 },
       { 1, 0x04, 37.6 },
       { 1, 0x08, 38.0 },
       { 1, 0x01, 38.0 },
       { 2, 0x40, 47.0 },
       { 2, 0x80, 48.2 },
       { 2, 0x08, 48.5 },
       { 2, 0x04, 56.6 },
       { 2, 0x02, 60.1 },
       { 2, 0x01, 80.1 }
   };
   const myvddctiming myvtiming[11] = {
       { 1, 0x02, 56 },
       { 1, 0x01, 60 },
       { 2, 0x08, 60 },
       { 2, 0x04, 70 },
       { 1, 0x80, 71 },
       { 1, 0x08, 72 },
       { 2, 0x80, 72 },
       { 1, 0x04, 75 },
       { 2, 0x40, 75 },
       { 2, 0x02, 75 },
       { 2, 0x01, 75 }
   };

   if(flag) { /* HSync */

      for(i = 0; i < 4; i++) {
	 if(ddc->det_mon[i].type == DS_RANGES) {
	    mon->nHsync = 1;
	    mon->hsync[0].lo = ddc->det_mon[i].section.ranges.min_h;
	    mon->hsync[0].hi = ddc->det_mon[i].section.ranges.max_h;
	    if(mon->hsync[0].lo > 32.0 || mon->hsync[0].hi < 31.0) {
	       if(ddc->timings1.t1 & 0x80) {
		  mon->nHsync++;
		  mon->hsync[1].lo = 31.0;
		  mon->hsync[1].hi = 32.0;
	       }
	    }
	    return;
	 }
      }

      /* If no sync ranges detected in detailed timing table, we
       * derive them from supported VESA modes.
       */

      for(i = 0; i < 12; i++) {
	 if(myhtiming[i].whichone == 1) temp = ddc->timings1.t1;
	 else                           temp = ddc->timings1.t2;
	 if(temp & myhtiming[i].mask) {
	    if((i == 0) || (myhlow > myhtiming[i].rate))
	       myhlow = myhtiming[i].rate;
	 }
	 if(myhtiming[11-i].whichone == 1) temp = ddc->timings1.t1;
	 else                              temp = ddc->timings1.t2;
	 if(temp & myhtiming[11-i].mask) {
	    if((i == 0) || (myhhigh < myhtiming[11-i].rate))
	       myhhigh = myhtiming[11-i].rate;
	 }
      }

      for(i = 0; i < STD_TIMINGS; i++) {
	 if(ddc->timings2[i].hsize > 256) {
	    htest = ddc->timings2[i].refresh * 1.05 * ddc->timings2[i].vsize / 1000.0;
	    if(htest < myhlow)  myhlow  = htest;
	    if(htest > myhhigh) myhhigh = htest;
	 }
      }

      if((myhhigh > 0.0) && (myhlow > 0.0)) {
	 mon->nHsync = 1;
	 mon->hsync[0].lo = myhlow - 0.1;
	 mon->hsync[0].hi = myhhigh;
      }


   } else {  /* Vrefresh */

      for(i = 0; i < 4; i++) {
         if(ddc->det_mon[i].type == DS_RANGES) {
	    mon->nVrefresh = 1;
	    mon->vrefresh[0].lo = ddc->det_mon[i].section.ranges.min_v;
	    mon->vrefresh[0].hi = ddc->det_mon[i].section.ranges.max_v;
	    if(mon->vrefresh[0].lo > 72 || mon->vrefresh[0].hi < 70) {
	       if(ddc->timings1.t1 & 0x80) {
		  mon->nVrefresh++;
		  mon->vrefresh[1].lo = 71;
		  mon->vrefresh[1].hi = 71;
	       }
	    }
	    return;
         }
      }

      for(i = 0; i < 11; i++) {
	 if(myvtiming[i].whichone == 1) temp = ddc->timings1.t1;
	 else				temp = ddc->timings1.t2;
	 if(temp & myvtiming[i].mask) {
	    if((i == 0) || (myvlow > myvtiming[i].rate))
	       myvlow = myvtiming[i].rate;
	 }
	 if(myvtiming[10-i].whichone == 1) temp = ddc->timings1.t1;
	 else				   temp = ddc->timings1.t2;
	 if(temp & myvtiming[10-i].mask) {
	    if((i == 0) || (myvhigh < myvtiming[10-i].rate))
	       myvhigh = myvtiming[10-i].rate;
	 }
      }

      for(i = 0; i < STD_TIMINGS; i++) {
	 if(ddc->timings2[i].hsize > 256) {
	    vtest = ddc->timings2[i].refresh;
	    if(vtest < myvlow)  myvlow  = vtest;
	    if(vtest > myvhigh) myvhigh = vtest;
	 }
      }

      if((myvhigh > 0) && (myvlow > 0)) {
	 mon->nVrefresh = 1;
	 mon->vrefresh[0].lo = myvlow;
	 mon->vrefresh[0].hi = myvhigh;
      }

   }
}

static Bool
SiSAllowSyncOverride(SISPtr pSiS, Bool fromDDC, int mfbcrt)
{
   if(!(pSiS->VBFlags2 & VB2_VIDEOBRIDGE)) return FALSE;

#ifdef SISDUALHEAD
   if(pSiS->DualHeadMode) {
      if(pSiS->SecondHead) {
         if((pSiS->VBFlags & CRT1_LCDA) && (!fromDDC)) return TRUE;
      } else {
         if((pSiS->VBFlags & CRT2_TV) ||
	    ((pSiS->VBFlags & CRT2_LCD) && (!fromDDC))) return TRUE;
      }
      return FALSE;
   }
#endif

#ifdef SISMERGED
   if(pSiS->MergedFB) {
      if(mfbcrt == 1) {
         if((pSiS->VBFlags & CRT1_LCDA) && (!fromDDC)) return TRUE;
      } else {
         if((pSiS->VBFlags & CRT2_TV) ||
	    ((pSiS->VBFlags & CRT2_LCD) && (!fromDDC))) return TRUE;
      }
      return FALSE;
   }
#endif

   if(!(pSiS->VBFlags & DISPTYPE_CRT1)) {
      if( (pSiS->VBFlags & CRT2_TV) ||
	  ((pSiS->VBFlags & CRT2_LCD) && (!fromDDC)) ) return TRUE;
   } else if((pSiS->VBFlags & CRT1_LCDA) && (!fromDDC)) return TRUE;

   return FALSE;
}

static Bool
SiSCheckForH(float hsync, MonPtr monitor)
{
   int i;
   for(i = 0; i < monitor->nHsync; i++) {
      if((hsync > monitor->hsync[i].lo * (1.0 - SYNC_TOLERANCE)) &&
	 (hsync < monitor->hsync[i].hi * (1.0 + SYNC_TOLERANCE)))
	 break;
   }
   if(i == monitor->nHsync) return FALSE;
   return TRUE;
}

static Bool
SiSCheckForV(float vrefresh, MonPtr monitor)
{
   int i;
   for(i = 0; i < monitor->nVrefresh; i++) {
      if((vrefresh > monitor->vrefresh[i].lo * (1.0 - SYNC_TOLERANCE)) &&
	 (vrefresh < monitor->vrefresh[i].hi * (1.0 + SYNC_TOLERANCE)))
	 break;
   }
   if(i == monitor->nVrefresh) return FALSE;
   return TRUE;
}

static Bool
SiSCheckAndOverruleH(ScrnInfoPtr pScrn, MonPtr monitor)
{
   DisplayModePtr mode = monitor->Modes;
   float mymin = 30.0, mymax = 80.0, hsync;
   Bool doit = FALSE;

   for(hsync = mymin; hsync <= mymax; hsync += .5) {
      if(!SiSCheckForH(hsync, monitor)) doit = TRUE;
   }

   if(mode) {
      do {
         if(mode->type & M_T_BUILTIN) {
	    hsync = (float)mode->Clock / (float)mode->HTotal;
	    if(!SiSCheckForH(hsync, monitor)) {
	       doit = TRUE;
	       if(hsync < mymin) mymin = hsync;
	       if(hsync > mymax) mymax = hsync;
	    }
	 }
      } while((mode = mode->next));
   }

   if(doit) {
      monitor->nHsync = 1;
      monitor->hsync[0].lo = mymin;
      monitor->hsync[0].hi = mymax;
      return TRUE;
   }

   return FALSE;
}

static Bool
SiSCheckAndOverruleV(ScrnInfoPtr pScrn, MonPtr monitor)
{
   DisplayModePtr mode = monitor->Modes;
   float mymin = 59.0, mymax = 61.0, vrefresh;
   Bool doit = FALSE, ret = FALSE;

   for(vrefresh = mymin; vrefresh <= mymax; vrefresh += 1.0) {
      if(!SiSCheckForV(vrefresh, monitor)) doit = TRUE;
   }

   if(mode) {
      do {
         if(mode->type & M_T_BUILTIN) {
	    vrefresh = mode->Clock * 1000.0 / (mode->HTotal * mode->VTotal);
	    if(mode->Flags & V_INTERLACE) vrefresh *= 2.0;
	    if(mode->Flags & V_DBLSCAN) vrefresh /= 2.0;
	    if(!SiSCheckForH(vrefresh, monitor)) {
	       doit = TRUE;
	       if(vrefresh < mymin) mymin = vrefresh;
	       if(vrefresh > mymax) mymax = vrefresh;
	    }
	 }
      } while((mode = mode->next));
   }

   if(doit) {
      monitor->nVrefresh = 1;
      monitor->vrefresh[0].lo = mymin;
      monitor->vrefresh[0].hi = mymax;
      ret = TRUE;
   }

   /* special for 640x400/320x200/@70Hz (VGA/IBM 720x480) */
   if( (!SiSCheckForV(71, monitor)) &&
       (monitor->nVrefresh < MAX_VREFRESH) ) {
      monitor->vrefresh[monitor->nVrefresh].lo = 71;
      monitor->vrefresh[monitor->nVrefresh].hi = 71;
      monitor->nVrefresh++;
      ret = TRUE;
   }
   return ret;
}

Bool
SiSFixupHVRanges(ScrnInfoPtr pScrn, int mfbcrt, Bool quiet)
{
    SISPtr pSiS = SISPTR(pScrn);
    Bool fromDDC, freqoverruled;
    static const char *subshstr = "Substituting missing CRT%d monitor HSync range by DDC data\n";
    static const char *subsvstr = "Substituting missing CRT%d monitor VRefresh range by DDC data\n";
    static const char *saneh = "Correcting %s CRT%d monitor HSync range\n";
    static const char *sanev = "Correcting %s CRT%d monitor VRefresh range\n";
    int crtnum;

#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
       crtnum = pSiS->SecondHead ? 1 : 2;
    } else
#endif
#ifdef SISMERGED
           if(pSiS->MergedFB) {
       crtnum = mfbcrt;
    } else
#endif
       crtnum = pSiS->CRT1off ? 2 : 1;


   /* If there is no HSync or VRefresh data for the monitor,
    * derive it from DDC data. Essentially done by common layer
    * since 4.3.99.14, but this is not usable since it is done
    * too late (in ValidateModes()).
    * Addendum: I overrule the ranges now in any case unless
    * it would affect a CRT output device or DDC data is available.
    * Hence, for LCD(A) and TV, we always get proper ranges. This
    * is entirely harmless. However, option "NoOverruleRanges" will
    * disable this behavior.
    * This should "fix" the - by far - most common configuration
    * mistakes.
    */

    freqoverruled = FALSE;

    fromDDC = FALSE;
    if((pScrn->monitor->nHsync <= 0) || (pSiS->OverruleRanges)) {
       if((pScrn->monitor->nHsync <= 0) && (pScrn->monitor->DDC)) {
	  SiSSetSyncRangeFromEdid(pScrn, 1);
	  if(pScrn->monitor->nHsync > 0) {
	     if(!quiet)
	        xf86DrvMsg(pScrn->scrnIndex, X_INFO, subshstr, crtnum);
	     fromDDC = TRUE;
	  }
       }
       if((pScrn->monitor->nHsync <= 0) || (pSiS->OverruleRanges)) {
	  if(SiSAllowSyncOverride(pSiS, fromDDC, mfbcrt)) {
	     Bool HaveNoRanges = (pScrn->monitor->nHsync <= 0);
	     /* Set sane ranges for LCD and TV
	      * (our strict checking will filter out invalid ones anyway)
	      */
	     if((freqoverruled = SiSCheckAndOverruleH(pScrn, pScrn->monitor))) {
	        if(!quiet)
		   xf86DrvMsg(pScrn->scrnIndex, X_INFO, saneh,
			HaveNoRanges ? "missing" : "bogus", crtnum);
	     }
	  }
       }
    }

    fromDDC = FALSE;
    if((pScrn->monitor->nVrefresh <= 0) || (pSiS->OverruleRanges)) {
       if((pScrn->monitor->nVrefresh <= 0) && (pScrn->monitor->DDC)) {
	  SiSSetSyncRangeFromEdid(pScrn, 0);
	  if(pScrn->monitor->nVrefresh > 0) {
	     if(!quiet)
	        xf86DrvMsg(pScrn->scrnIndex, X_INFO, subsvstr, crtnum);
	     fromDDC = TRUE;
          }
       }
       if((pScrn->monitor->nVrefresh <= 0) || (pSiS->OverruleRanges)) {
	  if(SiSAllowSyncOverride(pSiS, fromDDC, mfbcrt)) {
	     Bool HaveNoRanges = (pScrn->monitor->nVrefresh <= 0);
	     /* Set sane ranges for LCD and TV */
	     if((freqoverruled = SiSCheckAndOverruleV(pScrn, pScrn->monitor))) {
	        if(!quiet)
		   xf86DrvMsg(pScrn->scrnIndex, X_INFO, sanev,
			HaveNoRanges ? "missing" : "bogus", crtnum);
	     }
	  }
       }
    }

    return freqoverruled;
}


/* Mode list generators */

Bool
SiSMakeOwnModeList(ScrnInfoPtr pScrn, Bool acceptcustommodes, Bool includelcdmodes,
                   Bool isfordvi, Bool *havecustommodes, Bool fakecrt2modes, Bool IsForCRT2)
{
    DisplayModePtr tempmode, delmode, mymodes;

    if((mymodes = SiSBuildBuiltInModeList(pScrn, includelcdmodes, isfordvi, fakecrt2modes, IsForCRT2))) {
       if(!acceptcustommodes) {
          /* Delete all modes */
	  while(pScrn->monitor->Modes)
	     xf86DeleteMode(&pScrn->monitor->Modes, pScrn->monitor->Modes);
	  pScrn->monitor->Modes = mymodes;
       } else {
          /* Delete all default and built-in modes and link our
           * new modes at the end of the user-provided ones.
           * (There shouldn't be any builtins at server-start,
           * but if we are re-using this for our dynamic modelist
           * rebuilding, there might be some from previous
           * instances.)
           */
	  delmode = pScrn->monitor->Modes;
	  while(delmode) {
	     if(delmode->type & (M_T_DEFAULT | M_T_BUILTIN)) {
	        tempmode = delmode->next;
	        xf86DeleteMode(&pScrn->monitor->Modes, delmode);
	        delmode = tempmode;
	     } else {
	        delmode = delmode->next;
	     }
	  }
	  /* Link default modes AFTER user ones */
	  if((tempmode = pScrn->monitor->Modes)) {
	     *havecustommodes = TRUE;
	     while(tempmode) {
	        if(!tempmode->next) break;
	        else tempmode = tempmode->next;
	     }
	     tempmode->next = mymodes;
	     mymodes->prev = tempmode;
	  } else {
	     pScrn->monitor->Modes = mymodes;
	  }
       }
       return TRUE;
    } else
       return FALSE;
}

static void
SiSSetupModeListParmsCRT1(SISPtr pSiS, unsigned int VBFlags, unsigned int VBFlags3,
		Bool *acceptcustommodes, Bool *includelcdmodes, Bool *isfordvi,
		Bool *fakecrt2modes, Bool *IsForCRT2, Bool *AllowInterlace)
{
    (*acceptcustommodes) = TRUE;  /* Accept user modelines */
    (*includelcdmodes)   = TRUE;  /* Include modes reported by DDC */
    (*isfordvi)          = FALSE; /* Is for digital DVI output */
    (*fakecrt2modes)     = FALSE; /* Fake some modes for CRT2 */
    (*IsForCRT2)	 = FALSE;
    (*AllowInterlace)    = TRUE;  /* Allow interlace modes */

    if(pSiS->UseVESA) {
       (*acceptcustommodes) = FALSE;
       (*includelcdmodes)   = FALSE;
    }

#ifdef SISDUALHEAD  /* Dual head is static. Output devices will not change. */
    if(pSiS->DualHeadMode) {
       if(!pSiS->SecondHead) {  /* CRT2: */
	  if(pSiS->VBFlags2 & VB2_SISTMDSBRIDGE) {
	     if(!(pSiS->VBFlags2 & VB2_30xBDH)) {
		if(!(VBFlags & (CRT2_LCD|CRT2_VGA))) (*includelcdmodes)   = FALSE;
		if(VBFlags & CRT2_LCD)               (*isfordvi)          = TRUE;
		if(VBFlags & CRT2_TV)                (*acceptcustommodes) = FALSE;
	     } else {
		if(VBFlags & (CRT2_TV|CRT2_LCD)) {
		   (*acceptcustommodes) = FALSE;
		   (*includelcdmodes)   = FALSE;
		   (*fakecrt2modes)     = TRUE;
		}
	     }
	  } else {
	     (*acceptcustommodes) = FALSE;
	     (*includelcdmodes)   = FALSE;
	     if(VBFlags & (CRT2_TV|CRT2_LCD)) {
		(*fakecrt2modes)  = TRUE;
	     }
	  }
	  (*AllowInterlace) = FALSE;
	  (*IsForCRT2)      = TRUE;
       } else {		/* CRT1: */
	  if(VBFlags & CRT1_LCDA) {
	     if(!(pSiS->VBFlags2 & VB2_SISTMDSLCDABRIDGE)) {
		(*acceptcustommodes) = FALSE;
		(*includelcdmodes)   = FALSE;
		(*fakecrt2modes)     = TRUE;
		/* Will handle i-lace in mode-switching code */
	     } else {
		(*isfordvi)       = TRUE;
		/* Don't allow i-lace modes */
		(*AllowInterlace) = FALSE;
	     }
	  } else {
	     (*includelcdmodes) = FALSE;
	  }
       }
    } else
#endif
#ifdef SISMERGED  /* MergedFB mode is not static. Output devices may change. */
    /*else*/ if(pSiS->MergedFB) {
       if(VBFlags & CRT1_LCDA) {
	  if(!(pSiS->VBFlags2 & VB2_SISTMDSLCDABRIDGE)) {
	     (*acceptcustommodes) = FALSE;
	     (*includelcdmodes)   = FALSE;
	     (*fakecrt2modes)     = TRUE;
	     /* Will handle i-lace in mode-switching code */
	  } else {
	     (*isfordvi)       = TRUE;
	     /* Don't allow i-lace custom modes */
	     (*AllowInterlace) = FALSE;
	  }
       } else {
	  (*includelcdmodes) = FALSE;
       }
    } else
#endif		 /* Mirror mode is not static. Output devices may change. */
    /*else*/ if(pSiS->VBFlags2 & VB2_SISTMDSBRIDGE) {
       if(!(pSiS->VBFlags2 & VB2_30xBDH)) {
	  if(!(pSiS->VBFlags2 & VB2_SISTMDSLCDABRIDGE)) {
	     if(!(VBFlags & (CRT2_LCD|CRT2_VGA))) (*includelcdmodes) = FALSE;
	     if(VBFlags & CRT2_LCD)               (*isfordvi)        = TRUE;
	  } else {
	     if(!(VBFlags & (CRT2_LCD|CRT2_VGA|CRT1_LCDA))) (*includelcdmodes) = FALSE;
	     if(VBFlags & (CRT2_LCD|CRT1_LCDA))             (*isfordvi)        = TRUE;
	  }
	  if((!(VBFlags & DISPTYPE_CRT1)) && (!(VBFlags & CRT1_LCDA))) {
	     (*IsForCRT2) = TRUE;
	  }
	  /* Allow user modes, even if CRT2 is TV. Will be filtered through ValidMode();
	   * leaving the user modes here might have the advantage that such a mode, if
	   * it matches in resolution with a supported TV mode, allows us to drive eg.
	   * non standard panels, and still permits switching to TV. This mode will be
	   * "mapped" to a supported mode of identical resolution for TV. All this is
	   * taken care of by ValidMode() and ModeInit()/PresetMode().
	   */
       } else {
	  if(VBFlags & (CRT2_TV | CRT2_LCD)) {
	     (*acceptcustommodes) = FALSE;
	     (*includelcdmodes)   = FALSE;
	     if(!(VBFlags & DISPTYPE_CRT1)) {
		(*fakecrt2modes)  = TRUE;
		(*IsForCRT2)      = TRUE;
	     }
	  }
       }
    } else if(VBFlags & (CRT2_ENABLE | CRT1_LCDA)) {
       (*acceptcustommodes) = FALSE;
       (*includelcdmodes)   = FALSE;
       if((VBFlags & CRT1_LCDA) || (!(VBFlags & DISPTYPE_CRT1))) {
	  (*fakecrt2modes)  = TRUE;
	  (*IsForCRT2)      = TRUE;
       }
    } else {
       (*includelcdmodes)   = FALSE;
    }
    /* Ignore interlace, mode switching code will handle this */
}

static Bool
SiSReplaceModeList(ScrnInfoPtr pScrn, ClockRangePtr clockRanges, Bool quiet)
{
    SISPtr pSiS = SISPTR(pScrn);
    Bool acceptcustommodes;	/* Accept user modelines */
    Bool includelcdmodes;	/* Include modes reported by DDC */
    Bool isfordvi;		/* Is for digital DVI output */
    Bool fakecrt2modes;		/* Fake some modes for CRT2 */
    Bool IsForCRT2;
    Bool ret = TRUE;

    /*
     * Since we have lots of built-in modes for 300 series and later
     * with vb support, we replace the given default mode list with our
     * own. In case the video bridge is to be used, we only allow other
     * modes if
     *   -) vbtype is a tmds bridge (301, 301B, 301C, 302B), and
     *   -) crt2 device is not TV, and
     *   -) crt1 is not LCDA, unless bridge is TMDS/LCDA capable (301C)
     */

    if((pSiS->VGAEngine == SIS_300_VGA) || (pSiS->VGAEngine == SIS_315_VGA)) {

       if(!(pSiS->noInternalModes)) {
          SiSSetupModeListParmsCRT1(pSiS, pSiS->VBFlags, pSiS->VBFlags3,
			&acceptcustommodes, &includelcdmodes, &isfordvi,
			&fakecrt2modes, &IsForCRT2, &clockRanges->interlaceAllowed);


	  pSiS->HaveCustomModes = FALSE;

	  if(SiSMakeOwnModeList(pScrn, acceptcustommodes, includelcdmodes,
			isfordvi, &pSiS->HaveCustomModes, FALSE /*fakecrt2modes*/, IsForCRT2)) {

	     if(!quiet) {
	        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			"Replaced %s mode list with built-in modes\n",
				pSiS->HaveCustomModes ? "default" : "entire");
	     }

	     if(pSiS->VGAEngine == SIS_315_VGA) {
		int UseWide = pSiS->SiS_Pr->SiS_UseWide;
		if(IsForCRT2) UseWide = pSiS->SiS_Pr->SiS_UseWideCRT2;
		if((!IsForCRT2) || (pSiS->VBFlags2 & VB2_SISVGA2BRIDGE)) {
		   if(!quiet) {
		      xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			"Using %s widescreen modes for CRT%d VGA devices\n",
			UseWide ? "real" : "fake", IsForCRT2 ? 2 : 1);
		      xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			"\t[Use option \"ForceCRT%dVGAAspect\" to overrule]\n",
			IsForCRT2 ? 2 : 1);
		   }
		}
	     }

          } else {

	     ret = FALSE;
	     if(!quiet) {
	        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			"Building list of built-in modes failed, using server defaults\n");
	     }

	  }

       } else {

          pSiS->HaveCustomModes = TRUE;

       }
    }

    return ret;
}


void
SiSClearModesPrivate(DisplayModePtr modelist)
{
    DisplayModePtr tempmode;

    /* Make sure that the Private field is NULL */
    /* (This way we don't have to care for MergedFB
     * when freeing the mode; just check the Private
     * and free it if its anything but NULL)
     */
    tempmode = modelist;
    do {
       tempmode->Private = NULL;
       tempmode = tempmode->next;
    } while(tempmode && (tempmode != modelist));
}

/* Duplicate a mode (including name) */
DisplayModePtr
SiSDuplicateMode(DisplayModePtr source)
{
    DisplayModePtr dest = NULL;

    if(source) {
       if((dest = malloc(sizeof(DisplayModeRec)))) {
	  memcpy(dest, source, sizeof(DisplayModeRec));
	  dest->name = NULL;
	  dest->next = dest->prev = NULL;
	  if(!(dest->name = malloc(strlen(source->name) + 1))) {
	     free(dest);
	     dest = NULL;
	  } else {
	     strcpy(dest->name, source->name);
	  }
       }
    }

    return dest;
}


static void 
SiS6326AddHiresAndTVModes(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);

    if(pSiS->Chipset == PCI_CHIP_SIS6326) {
       if(pScrn->bitsPerPixel == 8) {
	  SiS6326SIS1600x1200_60Mode.next = pScrn->monitor->Modes;
	  pScrn->monitor->Modes = &SiS6326SIS1600x1200_60Mode;
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	  	"Adding mode \"SIS1600x1200-60\" (depth 8 only)\n");
       }
       if(pScrn->bitsPerPixel <= 16) {
	  SiS6326SIS1280x1024_75Mode.next = pScrn->monitor->Modes;
	  pScrn->monitor->Modes = &SiS6326SIS1280x1024_75Mode;
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	  	"Adding mode \"SIS1280x1024-75\" (depths 8, 15 and 16 only)\n");
       }
       if((pSiS->SiS6326Flags & SIS6326_HASTV) &&
	  (pSiS->SiS6326Flags & SIS6326_TVDETECTED)) {
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"Adding %s TV modes to mode list:\n",
		(pSiS->SiS6326Flags & SIS6326_TVPAL) ? "PAL" : "NTSC");
	  if(pSiS->SiS6326Flags & SIS6326_TVPAL) {
	     SiS6326PAL800x600Mode.next = pScrn->monitor->Modes;
	     pScrn->monitor->Modes = &SiS6326PAL640x480Mode;
	     xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"\t\"PAL800x600\" \"PAL800x600U\" \"PAL720x540\" \"PAL640x480\"\n");
	  } else {
	     SiS6326NTSC640x480Mode.next = pScrn->monitor->Modes;
	     pScrn->monitor->Modes = &SiS6326NTSC640x400Mode;
	     xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"\t\"NTSC640x480\" \"NTSC640x480U\" \"NTSC640x400\"\n");
	  }
       }
    }
}

/* Build a list of the VESA modes the BIOS reports as valid */
static void
SiSBuildVesaModeList(ScrnInfoPtr pScrn, vbeInfoPtr pVbe, VbeInfoBlock *vbe)
{
    SISPtr pSiS = SISPTR(pScrn);
    int i = 0;

    while(vbe->VideoModePtr[i] != 0xffff) {
       sisModeInfoPtr m;
       VbeModeInfoBlock *mode;
       int id = vbe->VideoModePtr[i++];

       if((mode = VBEGetModeInfo(pVbe, id)) == NULL) {
	  continue;
       }

       m = xnfcalloc(sizeof(sisModeInfoRec), 1);
       if(!m) {
	  VBEFreeModeInfo(mode);
	  continue;
       }
       m->width = mode->XResolution;
       m->height = mode->YResolution;
       m->bpp = mode->BitsPerPixel;
       m->n = id;
       m->next = pSiS->SISVESAModeList;

       pSiS->SISVESAModeList = m;

       VBEFreeModeInfo(mode);

       xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
	   "VESA BIOS supports mode number 0x%x: %ix%i (%i bpp)\n",
	   m->n, m->width, m->height, m->bpp);
    }
}

/* Set up min and max pixelclock */

static void
SiSSetMinMaxPixelClock(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
    MessageType from;

    /* Set the min pixel clock */
    pSiS->MinClock = 5000;
    if((pSiS->VGAEngine == SIS_300_VGA) || (pSiS->VGAEngine == SIS_315_VGA)) {
       pSiS->MinClock = 10000;
    }
    xf86DrvMsg(pScrn->scrnIndex, X_DEFAULT, "Min pixel clock is %d MHz\n",
                pSiS->MinClock / 1000);

    /* If the user has specified ramdac speed in the config
     * file, we respect that setting.
     */
    from = X_PROBED;
    if(pSiS->pEnt->device->dacSpeeds[0]) {
       int speed = 0;
       switch(pScrn->bitsPerPixel) {
       case 8:  speed = pSiS->pEnt->device->dacSpeeds[DAC_BPP8];
                break;
       case 16: speed = pSiS->pEnt->device->dacSpeeds[DAC_BPP16];
                break;
       case 24: speed = pSiS->pEnt->device->dacSpeeds[DAC_BPP24];
                break;
       case 32: speed = pSiS->pEnt->device->dacSpeeds[DAC_BPP32];
                break;
       }
       if(speed == 0) pSiS->MaxClock = pSiS->pEnt->device->dacSpeeds[0];
       else           pSiS->MaxClock = speed;
       from = X_CONFIG;
    }
    xf86DrvMsg(pScrn->scrnIndex, from, "Max pixel clock is %d MHz\n",
                pSiS->MaxClock / 1000);
}

/* Remove unsuitable modes not detected as such by
 * xf86ValidModes, and find out maximum dotclock of
 * all valid modes.
 */

#if defined(SISDUALHEAD) || defined(SISMERGED)
int
SiSRemoveUnsuitableModes(ScrnInfoPtr pScrn, DisplayModePtr initial, const char *reason, Bool quiet)
{
    SISPtr pSiS = SISPTR(pScrn);
    DisplayModePtr first, p, n;
    int maxUsedClock = 0;
    static const char *notsuitablestr = "Not using mode \"%s\" (not suitable for %s mode)\n";

    if((p = first = initial)) {

       do {

	  n = p->next;

	  /* Modes that require the bridge to operate in SlaveMode
	   * are not suitable for Dual Head and MergedFB mode.
	   */
	  if( (pSiS->VGAEngine == SIS_300_VGA) &&
	      ((p->Flags & V_DBLSCAN) || (strcmp(p->name, "640x400") == 0)) )  {
	     p->status = MODE_BAD;
	     if(!quiet) {
	        xf86DrvMsg(pScrn->scrnIndex, X_INFO, notsuitablestr, p->name, reason);
	     }
	  }

	  /* Search for the highest clock on first head in order to calculate
	   * max clock for second head (CRT1)
	   */
	  if((p->status == MODE_OK) && (p->Clock > maxUsedClock)) {
	     maxUsedClock = p->Clock;
	  }

	  p = n;

       } while (p != NULL && p != first);

    }

    return maxUsedClock;
}
#endif

/* Print available modes */

void
SiSPrintModes(ScrnInfoPtr pScrn, Bool printfreq)
{
    DisplayModePtr p;
    float hsync, refresh = 0.0;
    char *desc, *desc2, *prefix, *uprefix, *output;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Virtual size is %dx%d (pitch %d)\n",
	       pScrn->virtualX, pScrn->virtualY, pScrn->displayWidth);

    if((p = pScrn->modes) == NULL) return;

    do {
	desc = desc2 = "";
	uprefix = " ";
	prefix = "Mode";
	output = "For CRT device: ";
	if(p->HSync > 0.0)      hsync = p->HSync;
	else if (p->HTotal > 0) hsync = (float)p->Clock / (float)p->HTotal;
	else	                hsync = 0.0;
	refresh = 0.0;
        if(p->VRefresh > 0.0)   refresh = p->VRefresh;
        else if (p->HTotal > 0 && p->VTotal > 0) {
	   refresh = p->Clock * 1000.0 / p->HTotal / p->VTotal;
	   if(p->Flags & V_INTERLACE) refresh *= 2.0;
	   if(p->Flags & V_DBLSCAN)   refresh /= 2.0;
	   if(p->VScan > 1)  	      refresh /= p->VScan;
        }
	if(p->Flags & V_INTERLACE) desc = " (I)";
	if(p->Flags & V_DBLSCAN)   desc = " (D)";
	if(p->VScan > 1) 	   desc2 = " (VScan)";
#ifdef M_T_USERDEF
	if(p->type & M_T_USERDEF)  uprefix = "*";
#endif
	if(p->type & M_T_BUILTIN) {
	   prefix = "Built-in mode";
	   output = "";
	} else if (p->type & M_T_DEFAULT) {
	   prefix = "Default mode";
	} else {
	   output = "";
	}

	if(printfreq) {
	   xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		"%s%s \"%s\" (%dx%d) (%s%.1f MHz, %.1f kHz, %.1f Hz%s%s)\n",
		uprefix, prefix, p->name, p->HDisplay, p->VDisplay, output,
		p->Clock / 1000.0, hsync, refresh, desc, desc2);
	} else {
	   xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		"%s%s \"%s\" (%dx%d)\n",
		uprefix, prefix, p->name, p->HDisplay, p->VDisplay);
	}

	p = p->next;
    } while (p != NULL && p != pScrn->modes);
}

/* Find out if hw supports LCDA */

Bool SISDetermineLCDACap(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);

    if( ((pSiS->ChipType == SIS_315PRO) ||
	 (pSiS->ChipType == SIS_650)    ||
	 (pSiS->ChipType >= SIS_330))		&&
	(pSiS->ChipType != XGI_20)		&&
        (pSiS->VBFlags2 & VB2_SISLCDABRIDGE)	&&
	(pSiS->VESA != 1) ) {
       return TRUE;
    }
    return FALSE;
}

/* Store detected devices at a safe place */

void SISSaveDetectedDevices(ScrnInfoPtr pScrn)
{
    SISPtr  pSiS = SISPTR(pScrn);
    /* Backup detected CRT2 devices */
    pSiS->detectedCRT2Devices = pSiS->VBFlags & (CRT2_LCD|CRT2_TV|CRT2_VGA|TV_AVIDEO|TV_SVIDEO|
                                                 TV_SCART|TV_HIVISION|TV_YPBPR);
}

/* Hooks for unsupported functions */

static void
SiSPseudo(ScrnInfoPtr pScrn)
{
}


/**********************************************************/
/*                       PreInit()                        */
/**********************************************************/

static Bool
SISPreInit(ScrnInfoPtr pScrn, int flags)
{
    SISPtr pSiS;
#ifdef SISDUALHEAD
    SISEntPtr pSiSEnt = NULL;
#endif
    MessageType from;
    UChar usScratchCR17, usScratchCR32, usScratchCR63;
    UChar usScratchSR1F, srlockReg, crlockReg;
    unsigned int i;
    int pix24flags, temp;
    ClockRangePtr clockRanges;
    Bool crt1freqoverruled = FALSE;
    UChar CR5F, tempreg;
#ifdef SISMERGED
    static const char *mergednocrt1 = "CRT1 not detected or forced off. %s.\n";
    static const char *mergednocrt2 = "No CRT2 output selected or no video bridge detected. %s.\n";
    static const char *mergeddisstr = "MergedFB mode disabled";
    static const char *modesforstr = "Modes for CRT%d: **************************************************\n";
    static const char *crtsetupstr = "*************************** CRT%d setup ***************************\n";
#endif

    if(flags & PROBE_DETECT) {

       vbeInfoPtr   pVbe;

       if(xf86LoadSubModule(pScrn, "vbe")) {
          int index = xf86GetEntityInfo(pScrn->entityList[0])->index;
#if XF86_VERSION_CURRENT < XF86_VERSION_NUMERIC(4,2,99,0,0)
	  if((pVbe = VBEInit(NULL, index)))
#else
          if((pVbe = VBEExtendedInit(NULL, index, 0)))
#endif
          {
             ConfiguredMonitor = vbeDoEDID(pVbe, NULL);
             vbeFree(pVbe);
          }
       }
       return TRUE;
    }

    /*
     * Note: This function is only called once at server startup, and
     * not at the start of each server generation. This means that
     * only things that are persistent across server generations can
     * be initialised here. xf86Screens[] is the array of all screens,
     * (pScrn is a pointer to one of these). Privates allocated using
     * xf86AllocateScrnInfoPrivateIndex() are too, and should be used
     * for data that must persist across server generations.
     *
     * Per-generation data should be allocated with
     * AllocateScreenPrivateIndex() from the ScreenInit() function.
     */

    /* Check the number of entities, and fail if it isn't one. */
    if(pScrn->numEntities != 1) {
       SISErrorLog(pScrn, "Number of entities is not 1\n");
       return FALSE;
    }
    /* Print copyright, driver version */
    SiSPrintLogHeader(pScrn);

    /* Allocate the SISRec driverPrivate */
    if(!SISGetRec(pScrn)) {
       SISErrorLog(pScrn, "Could not allocate memory for pSiS private\n");
       return FALSE;
    }
    pSiS = SISPTR(pScrn);
    pSiS->pScrn = pScrn;

    pSiS->pInt = NULL;

    /* Save PCI Domain Base */
#if XF86_VERSION_CURRENT < XF86_VERSION_NUMERIC(4,2,99,0,0) || GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) >= 12
    pSiS->IODBase = 0;
#else
    pSiS->IODBase = pScrn->domainIOBase;
#endif

    /* Get the entity, and make sure it is PCI. */
    pSiS->pEnt = xf86GetEntityInfo(pScrn->entityList[0]);
    if(pSiS->pEnt->location.type != BUS_PCI) {
       SISErrorLog(pScrn, "Entity's bus type is not PCI\n");
       goto my_error_0;
    }
#ifdef SISDUALHEAD
    /* Allocate an entity private if necessary */
    if(xf86IsEntityShared(pScrn->entityList[0])) {
       pSiSEnt = xf86GetEntityPrivate(pScrn->entityList[0], SISEntityIndex)->ptr;
       pSiS->entityPrivate = pSiSEnt;

       /* If something went wrong, quit here */
       if((pSiSEnt->DisableDual) || (pSiSEnt->ErrorAfterFirst)) {
	  SISErrorLog(pScrn, "First head encountered fatal error, aborting...\n");
	  goto my_error_0;
       }
    }
#endif

    /* Find the PCI info for this screen */
    pSiS->PciInfo = xf86GetPciInfoForEntity(pSiS->pEnt->index);
    pSiS->PciBus = PCI_CFG_BUS(pSiS->PciInfo);
    pSiS->PciDevice = PCI_CFG_DEV(pSiS->PciInfo);
    pSiS->PciFunc = PCI_CFG_FUNC(pSiS->PciInfo);
    #ifndef XSERVER_LIBPCIACCESS
    pSiS->PciTag = pciTag(	PCI_DEV_BUS(pSiS->PciInfo),
							PCI_DEV_DEV(pSiS->PciInfo),
							PCI_DEV_FUNC(pSiS->PciInfo));
    #endif

#ifdef SIS_NEED_MAP_IOP
    /********************************************/
    /*     THIS IS BROKEN AND WON'T WORK        */
    /* Reasons:                                 */
    /* 1) MIPS and ARM have no i/o ports but    */
    /* use memory mapped i/o only. The inX/outX */
    /* macros in compiler.h are smart enough to */
    /* add "IOPortBase" to the port number, but */
    /* "IOPortBase" is never initialized.       */
    /* 2) IOPortBase is declared in compiler.h  */
    /* itself. So until somebody fixes all      */
    /* modules that #include compiler.h to set  */
    /* IOPortBase, vga support for MIPS and ARM */
    /* is unusable.                             */
    /* (In this driver this is solvable because */
    /* we have our own vgaHW routines. However, */
    /* we use /dev/port for now instead.)       */
    /********************************************/
    pSiS->IOPAddress = pSiS->IODBase + pSiS->PciInfo->ioBase[2];
    if(!SISMapIOPMem(pScrn)) {
       SISErrorLog(pScrn, "Could not map I/O port area at 0x%x\n", pSiS->IOPAddress);
       goto my_error_0;
    } else {
       xf86DrvMsg(pScrn->scrnIndex, X_INFO, "I/O port area mapped to %p, size 128\n", pSiS->IOPBase);
#if defined(__mips__) || defined(__arm32__)
       /* inX/outX macros on these use IOPortBase as offset */
       /* This is entirely skrewed. */
       IOPortBase = (unsigned int)pSiS->IOPBase;
#endif
    }
#endif

    /* Set up i/o port access (for non-x86) */
#ifdef SISUSEDEVPORT
    if((sisdevport = open("/dev/port", O_RDWR, 0)) == -1) {
       SISErrorLog(pScrn, "Failed to open /dev/port for read/write\n");
       goto my_error_0;
    }
    pSiS->sisdevportopen = TRUE;
#endif

    /*
     * Set the Chipset and ChipRev, allowing config file entries to
     * override. DANGEROUS!
     */
    {
       SymTabRec *myChipsets = SISChipsets;

       if(PCI_DEV_VENDOR_ID(pSiS->PciInfo) == PCI_VENDOR_XGI) {
          myChipsets = XGIChipsets;
       }

       if(pSiS->pEnt->device->chipset && *pSiS->pEnt->device->chipset) {

          pScrn->chipset = pSiS->pEnt->device->chipset;
          pSiS->Chipset = xf86StringToToken(myChipsets, pScrn->chipset);

       } else if(pSiS->pEnt->device->chipID >= 0) {

          pSiS->Chipset = pSiS->pEnt->device->chipID;
          pScrn->chipset = (char *)xf86TokenToString(myChipsets, pSiS->Chipset);

          xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "ChipID override: 0x%04X\n",
								pSiS->Chipset);
       } else {

          pSiS->Chipset = PCI_DEV_DEVICE_ID(pSiS->PciInfo);
          pScrn->chipset = (char *)xf86TokenToString(myChipsets, pSiS->Chipset);

       }
    }

    if(pSiS->pEnt->device->chipRev >= 0) {

       pSiS->ChipRev = pSiS->pEnt->device->chipRev;
       xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "ChipRev override: %d\n",
								pSiS->ChipRev);
    } else {

       pSiS->ChipRev = PCI_DEV_REVISION(pSiS->PciInfo);

    }

    /*
     * This shouldn't happen because such problems should be caught in
     * SISProbe(), but check it just in case the user has overridden them.
     */
    if(pScrn->chipset == NULL) {
       SISErrorLog(pScrn, "ChipID 0x%04X is not recognised\n", pSiS->Chipset);
       goto my_error_0;
    }
    if(pSiS->Chipset < 0) {
       SISErrorLog(pScrn, "Chipset \"%s\" is not recognised\n", pScrn->chipset);
       goto my_error_0;
    }

    pSiS->SiS6326Flags = 0;

    /* Determine VGA engine generation */
    switch(pSiS->Chipset) {
       case PCI_CHIP_SIS300:
       case PCI_CHIP_SIS540:
       case PCI_CHIP_SIS630: /* 630 + 730 */
          pSiS->VGAEngine = SIS_300_VGA;
	  break;
       case PCI_CHIP_SIS315H:
       case PCI_CHIP_SIS315:
       case PCI_CHIP_SIS315PRO:
       case PCI_CHIP_SIS550:
       case PCI_CHIP_SIS650: /* 650 + 740 */
       case PCI_CHIP_SIS330:
       case PCI_CHIP_SIS660: /* 660, 661, 741, 760, 761 */
       case PCI_CHIP_SIS340:
       case PCI_CHIP_XGIXG20:
       case PCI_CHIP_XGIXG40:
       case PCI_CHIP_SIS670: /* 670, 770 */
	case PCI_CHIP_SIS671: /* 670, 770 */
          pSiS->VGAEngine = SIS_315_VGA;
	  break;
       case PCI_CHIP_SIS530:
          pSiS->VGAEngine = SIS_530_VGA;
	  break;
       case PCI_CHIP_SIS6326:
          /* Determine SiS6326 revision. According to SiS the differences are:
	   * Chip name     Chip type      TV-Out       MPEG II decoder
	   * 6326 AGP      Rev. G0/H0     no           no
	   * 6326 DVD      Rev. D2        yes          yes
	   * 6326          Rev. Cx        yes          yes
	   */
	  xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		"Chipset is SiS6326 %s (revision 0x%02x)\n",
		(pSiS->ChipRev == 0xaf) ? "(Ax)" :
		   ((pSiS->ChipRev == 0x0a) ? "AGP (G0)" :
		      ((pSiS->ChipRev == 0x0b) ? "AGP (H0)" :
			 (((pSiS->ChipRev & 0xf0) == 0xd0) ? "DVD (Dx/H0)" :
			    (((pSiS->ChipRev & 0xf0) == 0x90) ? "(9x)" :
			       (((pSiS->ChipRev & 0xf0) == 0xc0) ? "(Cx)" :
				  "(unknown)"))))),
		pSiS->ChipRev);
	  if((pSiS->ChipRev != 0x0a) && (pSiS->ChipRev != 0x0b)) {
	     pSiS->SiS6326Flags |= SIS6326_HASTV;
	  }
	  /* fall through */
       default:
	  pSiS->VGAEngine = SIS_OLD_VGA;
    }

    /* We don't know about the current mode yet */
    pSiS->OldMode = 0;

    /* Determine whether this is the primary or a secondary
     * display adapter. And right here the problem starts:
     * On machines with integrated SiS chipsets, the system BIOS
     * usually sets VGA_EN on all PCI-to-PCI bridges in the system
     * (of which there usually are two: PCI and AGP). This and
     * the fact that any PCI card POSTed by sisfb naturally has
     * its PCI resources enabled, leads to X assuming that
     * there are more than one "primary" cards in the system.
     * In this case, X treats ALL cards as "secondary" -
     * which by no means is desireable. If sisfb is running,
     * we can determine which card really is "primary" (in
     * terms of if it's the one that occupies the A0000 area
     * etc.) in a better way (Linux 2.6.12 or later). See below.
     */
    if(!(pSiS->Primary = xf86IsPrimaryPci(pSiS->PciInfo))) {
       xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
	   SISMYSERVERNAME " assumes this adapter to be secondary\n");
    }

    /* Now check if sisfb is running, and if so, retrieve
     * all possible info from it. This also resets all
     * sisfb_* entries in pSiS regardless of the chipset.
     */
    SiS_CheckKernelFB(pScrn);

    /* Now again for that primary/secondary mess: Linux kernel
     * 2.6.12 and later knows what card is primary, and so
     * does any recent version of sisfb. XFree86/X.org takes
     * all adapters as "secondary" if more than one card's
     * memory and i/o resources are enabled, and more than
     * one PCI bridge in the system has VGA_EN set at server
     * start. So, let's start thinking: What is this
     * primary/secondary classification needed for anyway?
     * (This list might be incomplete for the entire server
     * infrastructure, but it's complete as regards the driver's
     * purposes of primary/secondary classification.)
     *    1) VGA/console font restoring: Here it's irrelevant
     *       whether more than one card's resources are enabled
     *       at server start or not. Relevant is whether the card
     *       occupies the A0000 area at this time. Assuming (?)
     *       that this does not change during machine up-time,
     *       it suffices to know which device was the boot video
     *       device (as determined by Linux 2.6.12 and later).
     *       Also, this is only relevant if the card is in text
     *       mode; if it's in graphics mode, fonts aren't saved
     *       or restored anyway.
     *       sisfb tells us if that card is considered the boot
     *       video device. The hardware registers tell us if
     *       the card's A0000 address decoding is enabled, and if
     *       the card currently is in text mode. These three bits
     *       of information are enough to decide on whether or not
     *       to save/restore fonts.
     *    2) POSTing. Same here. Relevant is only whether or not
     *       the card has been POSTed once before. POSTing cards
     *       on every server start is pretty ugly, especially
     *       if a framebuffer driver is already handling it.
     * SiS/XGI cards POSTed by sisfb can coexist well with other
     * active adapters. So we trust sisfb's information more
     * than X's (especially as we only use this information for
     * console font restoring and eventual POSTing.)
     * What we still need is a way to find out about all this if
     * sisfb is not running....
     */
    if(!pSiS->Primary && pSiS->sisfbprimary) {
       xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		"sisfb reports this adapter to be primary. Seems more reliable.\n");
       pSiS->Primary = TRUE;
    }

    /* If the card is "secondary" and has not been
     * POSTed by sisfb, POST it now through int10.
     * For cards POSTed by sisfb, we definitely don't
     * want that as it messes up our set up (eg. the
     * disabled A0000 area).
     * The int10 module decides on its own if the
     * card is primary or secondary. Since it uses
     * the generic technique described above, and since
     * for "secondary" cards it needs a real PCI BIOS
     * ROM, and since integrated chips don't have such
     * a PCI BIOS ROM, int10 will naturally fail to
     * find/read the BIOS on such machines. Great.
     * Using the integrated graphics as "secondary"
     * (which it will be as soon as X finds more than
     * one card's mem and i/o resources enabled, and more
     * than one PCI bridge's VGA_EN bit set during server
     * start) will therefore prevent us from restoring
     * the mode using the VBE. That means real fun if
     * the integrated chip is set up to use the video
     * bridge output for text mode (which is something
     * the driver doesn't really support since it's done
     * pretty much differently on every machine.)
     */
#if !defined(__alpha__)
    if(!pSiS->Primary) {
       if(!pSiS->sisfbcardposted) {
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"Initializing adapter through int10\n");
	  if(xf86LoadSubModule(pScrn, "int10")) {
	     pSiS->pInt = xf86InitInt10(pSiS->pEnt->index);
	  } else {
	     SISErrorLog(pScrn, "Failed to load int10 module\n");
	  }
       } else {
	  xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		"Adapter already initialized by sisfb\n");
       }
    }
#endif

    /* Get the address of our relocated IO registers.
     * These are enabled by the hardware during cold boot, and
     * by the BIOS. So we can pretty much rely on that these
     * are enabled.
     */
    pSiS->RelIO = (SISIOADDRESS) (PCI_REGION_BASE(pSiS->PciInfo, 2, REGION_IO) + pSiS->IODBase);

    xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "Relocated I/O registers at 0x%lX\n",
           (ULong)pSiS->RelIO);

    /* Request permissions to interact with IO registers */
    if (ioperm(pSiS->RelIO, 328, 1)) {
       xf86DrvMsg(pSiS->pScrn->scrnIndex, X_WARNING, "Failed to obtain I/O permissions\n");
    }

    /* Unlock extended registers */
    sisSaveUnlockExtRegisterLock(pSiS, &srlockReg, &crlockReg);

    /* Is a0000 memory address decoding enabled? */
    pSiS->VGADecodingEnabled = TRUE;
    switch(pSiS->VGAEngine) {
    case SIS_OLD_VGA:
       /* n/a */
       break;
    case SIS_530_VGA:
       inSISIDXREG(SISSR, 0x3d, tempreg);
       if(tempreg & 0x04) pSiS->VGADecodingEnabled = FALSE;
       break;
    case SIS_300_VGA:
    case SIS_315_VGA:
       inSISIDXREG(SISSR, 0x20, tempreg);
       if(tempreg & 0x04) pSiS->VGADecodingEnabled = FALSE;
       break;
    }
    /*debug information insert here for watching MISCW reg value before 2d driver start on X.*/
    #ifdef TWDEBUG
     unsigned char uc = inSISREG(SISMISCR);
      xf86DrvMsg(pScrn->scrnIndex, X_PROBED,"[SISPreInit()]:VGA_MISCR = 0x%x\n",uc);
      uc = inSISREG(SISMISCW);
      xf86DrvMsg(pScrn->scrnIndex, X_PROBED,"[SISPreInit()]:VGA_MISCW = 0x%x\n",uc);
    #endif

    if(!pSiS->VGADecodingEnabled) {
       xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		"Standard VGA (0xA0000) memory address decoding is disabled\n");
    }

#ifdef SIS_PC_PLATFORM
    /* Map 64k VGA window for saving/restoring CGA fonts.
     * For secondary cards or if A0000 address decoding
     * is disabled, this will map the beginning of the
     * linear (PCI) video RAM instead.
     */
    SiS_MapVGAMem(pScrn);
#endif

#ifndef XSERVER_LIBPCIACCESS
    /* Set operating state */

    /* 1. memory */
    /* [ResUnusedOpr: Resource decoded by hw, but not used]
     * [ResDisableOpr: Resource is not decoded by hw]
     * So, if a0000 memory decoding is disabled, one could
     * argue that we may say so, too. Hm. Quite likely that
     * the VBE (via int10) will eventually enable it. So we
     * cowardly say unused instead.
     */
    xf86SetOperatingState(resVgaMem, pSiS->pEnt->index, ResUnusedOpr);

    /* 2. i/o */
    /* Although we only use the relocated i/o ports, the hardware
     * also decodes the standard VGA port range. This could in
     * theory be disabled, but I don't dare to do this; in case of
     * a server crash, the card would be entirely dead. Also, this
     * would prevent int10 and the VBE from working at all. Generic
     * access control through the PCI configuration registers does
     * nicely anyway.
     */
    xf86SetOperatingState(resVgaIo, pSiS->pEnt->index, ResUnusedOpr);

    /* Operations for which memory access is required */
    pScrn->racMemFlags = RAC_FB | RAC_COLORMAP | RAC_CURSOR | RAC_VIEWPORT;

    /* Operations for which I/O access is required */
    pScrn->racIoFlags = RAC_COLORMAP | RAC_CURSOR | RAC_VIEWPORT;

#endif

    /* Load ramdac module */
    if(!xf86LoadSubModule(pScrn, "ramdac")) {
       SISErrorLog(pScrn, "Could not load ramdac module\n");
       goto my_error_1;
    }

    /* Set pScrn->monitor */
    pScrn->monitor = pScrn->confScreen->monitor;

    /* Reset some entries */
    pSiS->SiSFastVidCopy = SiSVidCopyGetDefault();
    pSiS->SiSFastMemCopy = SiSVidCopyGetDefault();
    pSiS->SiSFastVidCopyFrom = SiSVidCopyGetDefault();
    pSiS->SiSFastMemCopyFrom = SiSVidCopyGetDefault();
    pSiS->SiSFastVidCopyDone = FALSE;
#ifdef SIS_USE_XAA
    pSiS->RenderCallback = NULL;
#endif
#ifdef SIS_USE_EXA
    pSiS->ExaRenderCallback = NULL;
#endif
    pSiS->InitAccel = SiSPseudo;
    pSiS->SyncAccel = SiSPseudo;
    pSiS->FillRect  = NULL;
    pSiS->BlitRect  = NULL;
    pSiS->ForceCursorOff = FALSE;

    /* Always do a ValidMode() inside Switchmode() */
    pSiS->skipswitchcheck = FALSE;

    /* Determine chipset and its capabilities in detail */
    pSiS->ChipFlags = 0;
    pSiS->EngineType3D = 0;
    pSiS->SiS_SD_Flags = pSiS->SiS_SD2_Flags = 0;
    pSiS->SiS_SD3_Flags = pSiS->SiS_SD4_Flags = 0;
    pSiS->HWCursorMBufNum = pSiS->HWCursorCBufNum = 0;
    pSiS->NeedFlush = FALSE;
    pSiS->NewCRLayout = FALSE;
    pSiS->mmioSize = 64;

    switch(pSiS->Chipset) {
       case PCI_CHIP_SIS530:
	  pSiS->ChipType = SIS_530;
	  break;
       case PCI_CHIP_SIS300:
	  pSiS->ChipType = SIS_300;
	  pSiS->SiS_SD_Flags |= SiS_SD_IS300SERIES;
	  pSiS->ChipFlags |= SiSCF_NoCurHide;
	  break;
       case PCI_CHIP_SIS540:
	  pSiS->ChipType = SIS_540;
	  pSiS->SiS_SD_Flags |= SiS_SD_IS300SERIES;
	  break;
       case PCI_CHIP_SIS630: /* 630 + 730 */
	  pSiS->ChipType = SIS_630;
	  if(sis_pci_read_host_bridge_u32(0x00) == 0x07301039) {
	     pSiS->ChipType = SIS_730;
	  }
	  pSiS->SiS_SD_Flags |= SiS_SD_IS300SERIES;
	  pSiS->ChipFlags |= SiSCF_NoCurHide;
	  break;
       case PCI_CHIP_SIS315H:
	  pSiS->ChipType = SIS_315H;
	  pSiS->ChipFlags |= SiSCF_MMIOPalette;
	  pSiS->EngineType3D |= SiS3D_315Core;
	  pSiS->SiS_SD_Flags |= SiS_SD_IS315SERIES;
	  pSiS->SiS_SD2_Flags |= SiS_SD2_SUPPORTXVHUESAT;
	  pSiS->myCR63 = 0x63;
	  break;
       case PCI_CHIP_SIS315:
	  /* Override for simplicity */
	  pSiS->Chipset = PCI_CHIP_SIS315H;
	  pSiS->ChipType = SIS_315;
	  pSiS->ChipFlags |= SiSCF_MMIOPalette;
	  pSiS->EngineType3D |= SiS3D_315Core;
	  pSiS->SiS_SD_Flags |= SiS_SD_IS315SERIES;
	  pSiS->SiS_SD2_Flags |= SiS_SD2_SUPPORTXVHUESAT;
	  pSiS->myCR63 = 0x63;
	  break;
       case PCI_CHIP_SIS315PRO:
	  /* Override for simplicity */
	  pSiS->Chipset = PCI_CHIP_SIS315H;
	  pSiS->ChipType = SIS_315PRO;
	  pSiS->ChipFlags |= (	SiSCF_MMIOPalette	|
				SiSCF_NoCurHide );
	  pSiS->EngineType3D |= SiS3D_315Core;
	  pSiS->SiS_SD_Flags |= SiS_SD_IS315SERIES;
	  pSiS->SiS_SD2_Flags |= SiS_SD2_SUPPORTXVHUESAT;
	  pSiS->myCR63 = 0x63;
	  break;
       case PCI_CHIP_SIS550:
	  pSiS->ChipType = SIS_550;
	  pSiS->ChipFlags |= (SiSCF_Integrated | SiSCF_MMIOPalette);
	  pSiS->SiS_SD_Flags |= SiS_SD_IS315SERIES;
	  pSiS->SiS_SD2_Flags |= SiS_SD2_SUPPORTXVHUESAT;
	  pSiS->myCR63 = 0x63;
	  break;
       case PCI_CHIP_SIS650: /* 650 + 740 */
	  pSiS->ChipType = SIS_650;
	  if(sis_pci_read_host_bridge_u32(0x00) == 0x07401039) {
	     pSiS->ChipType = SIS_740;
	  }
	  pSiS->ChipFlags |= (	SiSCF_Integrated	|
				SiSCF_MMIOPalette	|
				SiSCF_NoCurHide );
	  pSiS->EngineType3D |= SiS3D_Real256ECore;
	  pSiS->SiS_SD_Flags |= SiS_SD_IS315SERIES;
	  pSiS->SiS_SD2_Flags |= SiS_SD2_SUPPORTXVHUESAT;
	  pSiS->myCR63 = 0x63;
	  break;
       case PCI_CHIP_SIS330:
	  pSiS->ChipType = SIS_330;
	  pSiS->ChipFlags |= (	SiSCF_MMIOPalette	|
				SiSCF_HaveStrBB		|
				SiSCF_NoCurHide		|
				SiSCF_CRT2HWCKaputt );
	  pSiS->EngineType3D |= SiS3D_XabreCore;
	  pSiS->SiS_SD_Flags |= SiS_SD_IS330SERIES;
	  pSiS->SiS_SD2_Flags |= SiS_SD2_SUPPORTXVHUESAT;
	  pSiS->SiS_SD3_Flags |= SiS_SD3_CRT1SATGAIN; /* FIXME ? */
	  pSiS->myCR63 = 0x53; /* sic! */
	  break;
       case PCI_CHIP_SIS660: /* 660, 661, 741, 760, 761, 662*/
	  {	  	
	     ULong hpciid = sis_pci_read_host_bridge_u32(0x00);
	     switch(hpciid) {
	     case 0x06601039:
		pSiS->ChipType = SIS_660;
		pSiS->EngineType3D |= SiS3D_Ultra256Core;
		break;
	     case 0x07601039:
		pSiS->ChipType = SIS_760;
		pSiS->EngineType3D |= SiS3D_Ultra256Core;
		pSiS->NeedFlush = TRUE;
		break;
	     case 0x07611039:
		pSiS->ChipType = SIS_761;
		pSiS->EngineType3D |= SiS3D_Mirage1;
		pSiS->NeedFlush = TRUE;
		pSiS->ChipFlags |= SiSCF_DualPipe; /* ? */
		break;
	     case 0x07411039:
		pSiS->ChipType = SIS_741;
		pSiS->EngineType3D |= SiS3D_Real256ECore;
		break;
	     case 0x06621039:
		pSiS->ChipType = SIS_662;
		pSiS->EngineType3D |= SiS3D_Real256ECore;
		break;
	     case 0x06611039:
	     default:
		pSiS->ChipType = SIS_661;
		pSiS->EngineType3D |= SiS3D_Real256ECore;
		break;

	     }
	     /* Detection could also be done by CR5C & 0xf8:
	      * 0x10 = 661 (CR5F & 0xc0: 0x00 both A0 and A1)
	      * 0x80 = 760 (CR5F & 0xc0: 0x00 A0, 0x40 A1)
	      * 0x90 = 741 (CR5F & 0xc0: 0x00 A0,A1 0x40 A2)
	      * other: 660 (CR5F & 0xc0: 0x00 A0 0x40 A1) (DOA?)
	      */
	     pSiS->ChipFlags |= ( SiSCF_Integrated	|
				  SiSCF_MMIOPalette	|
				  SiSCF_HaveStrBB	|
				  SiSCF_NoCurHide );
	     pSiS->SiS_SD_Flags |= SiS_SD_IS330SERIES;
	     pSiS->SiS_SD2_Flags |= SiS_SD2_SUPPORTXVHUESAT;
	     pSiS->SiS_SD3_Flags |= SiS_SD3_CRT1SATGAIN;
	     pSiS->myCR63 = 0x53; /* sic! */
	     pSiS->NewCRLayout = TRUE;
	  }
	  break;
       case PCI_CHIP_SIS340:
	  pSiS->ChipType = SIS_340;
	  pSiS->ChipFlags |= (	SiSCF_MMIOPalette	|
				SiSCF_HaveStrBB		|
				SiSCF_NoCurHide		|
				SiSCF_DualPipe );
	  pSiS->EngineType3D |= SiS3D_Mirage1;
	  pSiS->SiS_SD_Flags |= SiS_SD_IS340SERIES;
	  pSiS->SiS_SD2_Flags |= SiS_SD2_SUPPORTXVHUESAT;
	  pSiS->SiS_SD3_Flags |= SiS_SD3_CRT1SATGAIN;
	  pSiS->myCR63 = 0x53;
	  pSiS->NewCRLayout = TRUE;
	  break;
       case PCI_CHIP_SIS670: /* 670, 770 */
	  {
	     ULong hpciid = sis_pci_read_host_bridge_u32(0x00);
	     switch(hpciid) {
	     case 0x06701039:
		pSiS->ChipType = SIS_670;
		break;
	     case 0x07701039:
	     default:
		pSiS->ChipType = SIS_770;
		/* TODO: 772? */
		/* if() pSiS->ChipType = SIS_772; */
		pSiS->NeedFlush = TRUE;
		break;
	     }
	     pSiS->ChipFlags |= ( SiSCF_Integrated	|
				  SiSCF_MMIOPalette	|
				  SiSCF_HaveStrBB	|
				  SiSCF_NoCurHide);
	     pSiS->EngineType3D |= SiS3D_Mirage3;
	     pSiS->SiS_SD2_Flags |= SiS_SD2_SUPPORTXVHUESAT;
	     pSiS->SiS_SD3_Flags |= (SiS_SD3_IS350SERIES | SiS_SD3_CRT1SATGAIN);
	     pSiS->myCR63 = 0x53; /* sic! */
	     pSiS->NewCRLayout = TRUE;
	  }
	  break;
	case PCI_CHIP_SIS671:
	{	     
		pSiS->ChipType = SIS_671;	     
		pSiS->ChipFlags |= ( SiSCF_Integrated	|	
					SiSCF_MMIOPalette	|	
					SiSCF_HaveStrBB	|
					SiSCF_NoCurHide);
		pSiS->EngineType3D |= SiS3D_Mirage3;
		pSiS->SiS_SD2_Flags |= SiS_SD2_SUPPORTXVHUESAT;
		pSiS->SiS_SD3_Flags |= (SiS_SD3_IS350SERIES | SiS_SD3_CRT1SATGAIN);
		pSiS->myCR63 = 0x53; /* sic! */
		pSiS->NewCRLayout = TRUE;
		}
	break;
	
       case PCI_CHIP_XGIXG20:
	  pSiS->ChipType = XGI_20;
	  pSiS->ChipFlags |= (	SiSCF_MMIOPalette	|
				SiSCF_IsXGI		|
				SiSCF_NoCurHide );
	  pSiS->SiS_SD2_Flags |= (SiS_SD2_NOOVERLAY | SiS_SD2_ISXGI);
	  pSiS->myCR63 = 0x53;
	  pSiS->NewCRLayout = TRUE;
	  break;
	case PCI_CHIP_XGIXG40:
	  pSiS->ChipType = XGI_40;
	  pSiS->ChipFlags |= (	SiSCF_MMIOPalette	|
				SiSCF_IsXGI		|
				SiSCF_HaveStrBB		|
				SiSCF_NoCurHide		|
				SiSCF_DualPipe );
	  pSiS->SiS_SD2_Flags |= (SiS_SD2_SUPPORTXVHUESAT | SiS_SD2_ISXGI);
	  pSiS->SiS_SD3_Flags |= SiS_SD3_CRT1SATGAIN;
	  pSiS->myCR63 = 0x53;
	  pSiS->NewCRLayout = TRUE;
	  if(pSiS->ChipRev == 2) {
	     pSiS->ChipFlags |= SiSCF_IsXGIV3;
	     pSiS->EngineType3D |= SiS3D_XG42Core;
	  } else
	     pSiS->EngineType3D |= SiS3D_XG40Core;
	  break;
       default:
	  pSiS->ChipType = SIS_OLD;
	  break;
    }

    /* The following identifies the old chipsets. This is only
     * partly used since the really old chips are not supported,
     * but I keep it here for future use.
     * 205, 215 and 225 are to be treated the same way, 201 and 202
     * are different.
     */
    if(pSiS->VGAEngine == SIS_OLD_VGA || pSiS->VGAEngine == SIS_530_VGA) {
       switch(pSiS->Chipset) {
       case PCI_CHIP_SG86C201:
	  pSiS->oldChipset = OC_SIS86201; break;
       case PCI_CHIP_SG86C202:
	  pSiS->oldChipset = OC_SIS86202; break;
       case PCI_CHIP_SG86C205:
	  inSISIDXREG(SISSR, 0x10, tempreg);
	  if(tempreg & 0x80) pSiS->oldChipset = OC_SIS6205B;
	  else pSiS->oldChipset = (pSiS->ChipRev == 0x11) ?
					OC_SIS6205C : OC_SIS6205A;
	  break;
       case PCI_CHIP_SIS82C204:
	  pSiS->oldChipset = OC_SIS82204; break;
       case 0x6225:
	  pSiS->oldChipset = OC_SIS6225; break;
       case PCI_CHIP_SIS5597:
	  pSiS->oldChipset = OC_SIS5597; break;
       case PCI_CHIP_SIS6326:
	  pSiS->oldChipset = OC_SIS6326; break;
       case PCI_CHIP_SIS530:
	  if(sis_pci_read_host_bridge_u32(0x00) == 0x06201039) {
	     pSiS->oldChipset = OC_SIS620;
	  } else {
	     if((pSiS->ChipRev & 0x0f) < 0x0a)
		   pSiS->oldChipset = OC_SIS530A;
	     else  pSiS->oldChipset = OC_SIS530B;
	  }
	  break;
       default:
	  pSiS->oldChipset = OC_UNKNOWN;
       }
    }

    /* Further hardware determination:
     * - Sub-classes of chipsets
     * - one or two video overlays
     */
    pSiS->hasTwoOverlays = FALSE;
    switch(pSiS->Chipset) {
       case PCI_CHIP_SIS300:
       case PCI_CHIP_SIS540:  /* ? (If not, need to add the SwitchCRT Xv attribute!) */
       case PCI_CHIP_SIS630:
       case PCI_CHIP_SIS550:
	  pSiS->hasTwoOverlays = TRUE;
	  pSiS->SiS_SD_Flags |= SiS_SD_SUPPORT2OVL;
	  break;
       case PCI_CHIP_SIS315PRO:
       case PCI_CHIP_SIS330:
       case PCI_CHIP_SIS340:
       case PCI_CHIP_XGIXG40:
	  break;
       case PCI_CHIP_SIS650:
	  {
	     UChar tempreg1, tempreg2;
	     static const char *id650str[] = {
		"650",       "650",       "650",       "650",
		"650 A0 AA", "650 A2 CA", "650",       "650",
		"M650 A0",   "M650 A1 AA","651 A0 AA", "651 A1 AA",
		"M650",      "65?",       "651",       "65?"
	     };
	     if(pSiS->ChipType == SIS_650) {
		inSISIDXREG(SISCR, 0x5f, CR5F);
		CR5F &= 0xf0;
		andSISIDXREG(SISCR, 0x5c, 0x07);
		inSISIDXREG(SISCR, 0x5c, tempreg1);
		tempreg1 &= 0xf8;
		orSISIDXREG(SISCR, 0x5c, 0xf8);
		inSISIDXREG(SISCR, 0x5c, tempreg2);
		tempreg2 &= 0xf8;
		if((!tempreg1) || (tempreg2)) {
		   xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		      "SiS650 revision ID %x (%s)\n", CR5F, id650str[CR5F >> 4]);
		   if(CR5F & 0x80) {
		      pSiS->hasTwoOverlays = TRUE;  /* M650 or 651 */
		      pSiS->SiS_SD_Flags |= SiS_SD_SUPPORT2OVL;
		   }
		   switch(CR5F) {
		      case 0xa0:
		      case 0xb0:
		      case 0xe0:
		         pSiS->ChipFlags |= SiSCF_Is651;
		         break;
		      case 0x80:
		      case 0x90:
		      case 0xc0:
		         pSiS->ChipFlags |= SiSCF_IsM650;
		         break;
		   }
		} else {
		   pSiS->hasTwoOverlays = TRUE;
		   pSiS->SiS_SD_Flags |= SiS_SD_SUPPORT2OVL;
		   switch(CR5F) {
		      case 0x90:
			 inSISIDXREG(SISCR, 0x5c, tempreg1);
			 tempreg1 &= 0xf8;
			 switch(tempreg1) {
			    case 0x00:
			       pSiS->ChipFlags |= SiSCF_IsM652;
			       xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
			           "SiSM652 revision ID %x\n", CR5F);
			       break;
			    case 0x40:
			       pSiS->ChipFlags |= SiSCF_IsM653;
			       xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
			           "SiSM653 revision ID %x\n", CR5F);
			       break;
			    default:
			       pSiS->ChipFlags |= SiSCF_IsM650;
			       xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
			           "SiSM650 revision ID %x\n", CR5F);
			       break;
			 }
			 break;
		      case 0xb0:
			 pSiS->ChipFlags |= SiSCF_Is652;
			 xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
			     "SiS652 revision ID %x\n", CR5F);
			 break;
		      default:
			 pSiS->ChipFlags |= SiSCF_IsM650;
			 xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
			     "SiSM650 revision ID %x\n", CR5F);
			 break;
		   }
		}

		if(pSiS->ChipFlags & SiSCF_Is65x) {
		   pSiS->ChipFlags |= SiSCF_HaveStrBB;
		}
	     }
	  }
	  break;
       case PCI_CHIP_SIS660:
	  {
	     /* TODO: Find out about 761/A0, 761/A1 */
	     /* if() pSiS->ChipFlags |= SiSCF_761A0; */
	     if(pSiS->ChipType != SIS_761 && pSiS->ChipType != SIS_662) {
	        pSiS->hasTwoOverlays = TRUE;
	        pSiS->SiS_SD_Flags |= SiS_SD_SUPPORT2OVL;
	     }
	     /* 760:      - UMA only: one/two overlays - dotclock dependent
			  - UMA+LFB:  two overlays if video data in LFB
			  - LFB only: two overlays
		If UMA only: Must switch between one/two overlays on the fly (done
			     in PostSetMode())
		If LFB+UMA:  We use LFB memory only and leave UMA to an eventually
			     written DRI driver.
	      */
	  }
	  break;
      case PCI_CHIP_SIS670:
      case PCI_CHIP_SIS671:
          if(pSiS->ChipType != SIS_770) {
             /* ? - 770 2D is 342 based... (670, 772 are 350 based) */
             pSiS->hasTwoOverlays = FALSE;
	     /*pSiS->SiS_SD_Flags |= SiS_SD_SUPPORT2OVL;*/ /*ivans blocked*/
          }
      }

#ifdef SISDUALHEAD
    /* In case of Dual Head, we need to determine if we are the "master" head or
     * the "slave" head. In order to do that, we set PrimInit to DONE in the
     * shared entity at the end of the first initialization. The second
     * initialization then knows that some things have already been done. THIS
     * ALWAYS ASSUMES THAT THE FIRST DEVICE INITIALIZED IS THE MASTER!
     */
    if(xf86IsEntityShared(pScrn->entityList[0])) {
       if(pSiSEnt->lastInstance > 0) {
	  if(!xf86IsPrimInitDone(pScrn->entityList[0])) {
	     /* First Head (always CRT2) */
	     pSiS->SecondHead = FALSE;
	     pSiSEnt->pScrn_1 = pScrn;
	     pSiSEnt->CRT1ModeNo = pSiSEnt->CRT2ModeNo = -1;
	     pSiSEnt->CRT2ModeSet = FALSE;
	     pSiS->DualHeadMode = TRUE;
	     pSiSEnt->DisableDual = FALSE;
	     pSiSEnt->BIOS = NULL;
	     pSiSEnt->ROM661New = FALSE;
	     pSiSEnt->HaveXGIBIOS = FALSE;
	     pSiSEnt->SiS_Pr = NULL;
	     pSiSEnt->RenderAccelArray = NULL;
	     pSiSEnt->SiSFastVidCopy = pSiSEnt->SiSFastMemCopy = NULL;
	     pSiSEnt->SiSFastVidCopyFrom = pSiSEnt->SiSFastMemCopyFrom = NULL;
	  } else {
	     /* Second Head (always CRT1) */
	     pSiS->SecondHead = TRUE;
	     pSiSEnt->pScrn_2 = pScrn;
	     pSiS->DualHeadMode = TRUE;
	  }
       } else {
	  /* Only one screen in config file - disable dual head mode */
	  pSiS->SecondHead = FALSE;
	  pSiS->DualHeadMode = FALSE;
	  pSiSEnt->DisableDual = TRUE;
       }
    } else {
       /* Entity is not shared - disable dual head mode */
       pSiS->SecondHead = FALSE;
       pSiS->DualHeadMode = FALSE;
    }
#endif

    /* Save the name of our Device section for SiSCtrl usage */
    {
       int ttt = 0;
       GDevPtr device = xf86GetDevFromEntity(pScrn->entityList[0],
						pScrn->entityInstanceList[0]);
       if(device && device->identifier) {
          if((ttt = strlen(device->identifier)) > 31) ttt = 31;
	  strncpy(&pSiS->devsectname[0], device->identifier, 31);
       }
       pSiS->devsectname[ttt] = 0;
    }

    /* Allocate SiS_Private (for mode switching code) and initialize it */
    pSiS->SiS_Pr = NULL;
#ifdef SISDUALHEAD
    if(pSiSEnt) {
       if(pSiSEnt->SiS_Pr) pSiS->SiS_Pr = pSiSEnt->SiS_Pr;
    }
#endif

    if(!pSiS->SiS_Pr) {
       if(!(pSiS->SiS_Pr = xnfcalloc(sizeof(struct SiS_Private), 1))) {
	  SISErrorLog(pScrn, "Could not allocate memory for SiS_Pr structure\n");
	  goto my_error_1;
       }
#ifdef SISDUALHEAD
       if(pSiSEnt) pSiSEnt->SiS_Pr = pSiS->SiS_Pr;
#endif

       memset(pSiS->SiS_Pr, 0, sizeof(struct SiS_Private));
       #ifndef XSERVER_LIBPCIACCESS
       pSiS->SiS_Pr->PciTag = pSiS->PciTag;
       #endif
       pSiS->SiS_Pr->ChipType = pSiS->ChipType;
       pSiS->SiS_Pr->ChipRevision = pSiS->ChipRev;
       pSiS->SiS_Pr->SiS_Backup70xx = 0xff;
       pSiS->SiS_Pr->SiS_CHOverScan = -1;
       pSiS->SiS_Pr->SiS_ChSW = FALSE;
       pSiS->SiS_Pr->SiS_CustomT = CUT_NONE;
       pSiS->SiS_Pr->SiS_UseWide = -1;
       pSiS->SiS_Pr->SiS_UseWideCRT2 = -1;
       pSiS->SiS_Pr->SiS_TVBlue = -1;
       pSiS->SiS_Pr->PanelSelfDetected = FALSE;
       pSiS->SiS_Pr->UsePanelScaler = -1;
       pSiS->SiS_Pr->CenterScreen = -1;
       pSiS->SiS_Pr->CRT1UsesCustomMode = FALSE;
       pSiS->SiS_Pr->PDC = pSiS->SiS_Pr->PDCA = -1;
       pSiS->SiS_Pr->LVDSHL = -1;
       pSiS->SiS_Pr->HaveEMI = FALSE;
       pSiS->SiS_Pr->HaveEMILCD = FALSE;
       pSiS->SiS_Pr->OverruleEMI = FALSE;
       pSiS->SiS_Pr->SiS_SensibleSR11 = FALSE;
       if(pSiS->ChipType >= SIS_661) {
          pSiS->SiS_Pr->SiS_SensibleSR11 = TRUE;
       }
       pSiS->SiS_Pr->SiS_MyCR63 = pSiS->myCR63;
       pSiS->SiS_Pr->DDCPortMixup = FALSE;
    }

    /* Copy IO address to SiS_Pr and init the structure for
     * routines inside init.c/init301.c
     */
    pSiS->SiS_Pr->IOAddress = (SISIOADDRESS)(pSiS->RelIO + 0x30);
    pSiS->SiS_Pr->UseFutroTiming = FALSE;
    SiSRegInit(pSiS->SiS_Pr, pSiS->RelIO + 0x30);

    /*
     * Now back to real business: Figure out the depth, bpp, etc.
     * Set SupportConvert... flags since we use the fb layer which
     * supports this conversion. (24to32 seems not implemented though)
     * Additionally, determine the size of the HWCursor memory area.
     */

    switch(pSiS->VGAEngine) {
       case SIS_300_VGA:
	  pSiS->CursorSize = 4096;
	  pix24flags = Support32bppFb;
	  break;
       case SIS_315_VGA:
	  pSiS->CursorSize = 16384;
	  pix24flags = Support32bppFb;
	  break;
       case SIS_530_VGA:
	  pSiS->CursorSize = 2048;
	  pix24flags = Support32bppFb	  |
		       Support24bppFb	  |
		       SupportConvert32to24;
          break;
       default:
	  pSiS->CursorSize = 2048;
	  pix24flags = Support24bppFb	    |
		       SupportConvert32to24 |
		       PreferConvert32to24;
	  break;
    }

    if(!xf86SetDepthBpp(pScrn, 0, 0, 0, pix24flags)) {
       SISErrorLog(pScrn, "xf86SetDepthBpp() error\n");
       goto my_error_1;
    }

    /* Check that the returned depth is one we support */
    temp = 0;
    switch(pScrn->depth) {
       case 8:
       case 16:
       case 24:
          break;
       case 15:
	  if((pSiS->VGAEngine == SIS_300_VGA) ||
	     (pSiS->VGAEngine == SIS_315_VGA)) {
	     temp = 1;
	  }
	  break;
       default:
	  temp = 1;
    }

    if(temp) {
       SISErrorLog(pScrn,
            "Given color depth (%d) is not supported by this driver/chipset\n",
            pScrn->depth);
       goto my_error_1;
    }

    xf86PrintDepthBpp(pScrn);

    if( (((pSiS->VGAEngine == SIS_300_VGA) || (pSiS->VGAEngine == SIS_315_VGA)) &&
         (pScrn->bitsPerPixel == 24)) ||
	((pSiS->VGAEngine == SIS_OLD_VGA) && (pScrn->bitsPerPixel == 32)) ) {
       SISErrorLog(pScrn,
            "Framebuffer bpp %d not supported for this chipset\n", pScrn->bitsPerPixel);
       goto my_error_1;
    }

    /* Get the depth24 pixmap format */
    if(pScrn->depth == 24 && pix24bpp == 0) {
       pix24bpp = xf86GetBppFromDepth(pScrn, 24);
    }

    /*
     * This must happen after pScrn->display has been set because
     * xf86SetWeight references it.
     */
    if(pScrn->depth > 8) {
        /* The defaults are OK for us */
        rgb zeros = {0, 0, 0};

        if(!xf86SetWeight(pScrn, zeros, zeros)) {
	    SISErrorLog(pScrn, "xf86SetWeight() error\n");
	    goto my_error_1;
        } else {
	   Bool ret = FALSE;
	   switch(pScrn->depth) {
	   case 15:
	      if((pScrn->weight.red != 5) ||
	         (pScrn->weight.green != 5) ||
		 (pScrn->weight.blue != 5)) ret = TRUE;
	      break;
	   case 16:
	      if((pScrn->weight.red != 5) ||
	         (pScrn->weight.green != 6) ||
		 (pScrn->weight.blue != 5)) ret = TRUE;
	      break;
	   case 24:
	      if((pScrn->weight.red != 8) ||
	         (pScrn->weight.green != 8) ||
		 (pScrn->weight.blue != 8)) ret = TRUE;
	      break;
	   }
	   if(ret) {
	      SISErrorLog(pScrn,
		   "RGB weight %d%d%d at depth %d not supported by hardware\n",
		   (int)pScrn->weight.red, (int)pScrn->weight.green,
		   (int)pScrn->weight.blue, pScrn->depth);
	      goto my_error_1;
	   }
        }
    }

    /* Set the current layout parameters */
    pSiS->CurrentLayout.bitsPerPixel  = pScrn->bitsPerPixel;
    pSiS->CurrentLayout.bytesPerPixel = pScrn->bitsPerPixel >> 3;
    pSiS->CurrentLayout.depth         = pScrn->depth;
    /* (Inside this function, we can use pScrn's contents anyway) */

    if(!xf86SetDefaultVisual(pScrn, -1)) {
       SISErrorLog(pScrn, "xf86SetDefaultVisual() error\n");
       goto my_error_1;
    } else {
       /* We don't support DirectColor at > 8bpp */
       if(pScrn->depth > 8 && pScrn->defaultVisual != TrueColor) {
	  SISErrorLog(pScrn,
	       "Given default visual (%s) is not supported at depth %d\n",
	        xf86GetVisualName(pScrn->defaultVisual), pScrn->depth);
	  goto my_error_1;
       }
    }

#ifdef SISDUALHEAD
    /* Due to palette & timing problems we don't support 8bpp in DHM */
    if((pSiS->DualHeadMode) && (pScrn->bitsPerPixel <= 8)) {
       SISErrorLog(pScrn, "Color depth %d not supported in Dual Head mode.\n",
			pScrn->bitsPerPixel);
       goto my_error_1;
    }
#endif

    /* Read BIOS for 300/315/330/340/350 series customization */
    SiSReadROM(pScrn);

    /* Evaluate options */
    SiSOptions(pScrn);

#ifdef SISMERGED
    /* Due to palette & timing problems we don't support 8bpp in MFB */
    if((pSiS->MergedFB) && (pScrn->bitsPerPixel <= 8)) {
       SISErrorLog(pScrn, "MergedFB: Color depth %d not supported, %s\n",
			pScrn->bitsPerPixel, mergeddisstr);
       pSiS->MergedFB = pSiS->MergedFBAuto = FALSE;
    }

     pSiS->SiS_Pr->MergedFB = pSiS->MergedFB;
#endif

    /* Probe CPU features */
#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
       pSiS->CPUFlags = pSiSEnt->CPUFlags;
    }
#endif
    if(!pSiS->CPUFlags) {
       pSiS->CPUFlags = SiSGetCPUFlags(pScrn);
       pSiS->CPUFlags |= SIS_CPUFL_FLAG;
#ifdef SISDUALHEAD
       if(pSiS->DualHeadMode) pSiSEnt->CPUFlags = pSiS->CPUFlags;
#endif
    }

    /* We use a programamble clock */
    pScrn->progClock = TRUE;

    /* Set the bits per RGB for 8bpp mode */
    if(pScrn->depth == 8) pScrn->rgbBits = 8;

#ifdef SISDUALHEAD
    /* Copy data to/from pSiSEnt (mainly options) */
    SiSCopyFromToEntity(pScrn);
#endif

    /* Handle UseROMData, NoOEM and UsePanelScaler options */
    if((pSiS->VGAEngine == SIS_300_VGA) || (pSiS->VGAEngine == SIS_315_VGA)) {
       from = X_PROBED;
       if(pSiS->OptROMUsage == 0) {
	  pSiS->SiS_Pr->UseROM = FALSE;
	  from = X_CONFIG;
	  xf86DrvMsg(pScrn->scrnIndex, from, "Video ROM data usage is disabled\n");
       }

       if(!pSiS->OptUseOEM) {
	  xf86DrvMsg(pScrn->scrnIndex, from, "Internal OEM LCD/TV/VGA2 data usage is disabled\n");
       }

       pSiS->SiS_Pr->UsePanelScaler = pSiS->UsePanelScaler;
       pSiS->SiS_Pr->CenterScreen = pSiS->CenterLCD;
    }

    /* Do some HW configuration detection (memory amount & type, clock, etc) */
    SiSSetup(pScrn);

    /* Get framebuffer address */
    if(pSiS->pEnt->device->MemBase != 0) {
       /*
	* XXX Should check that the config file value matches one of the
	* PCI base address values.
	*/
       pSiS->FbAddress = pSiS->pEnt->device->MemBase;
       from = X_CONFIG;
    } else {
       pSiS->FbAddress = PCI_REGION_BASE(pSiS->PciInfo, 0, REGION_MEM) & 0xFFFFFFF0;
       from = X_PROBED;
    }

#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode)
       xf86DrvMsg(pScrn->scrnIndex, from, "Global linear framebuffer at 0x%lX\n",
	   (ULong)pSiS->FbAddress);
    else
#endif
       xf86DrvMsg(pScrn->scrnIndex, from, "Linear framebuffer at 0x%lX\n",
	   (ULong)pSiS->FbAddress);

    pSiS->realFbAddress = pSiS->FbAddress;

    /* Get MMIO address */
    if(pSiS->pEnt->device->IOBase != 0) {
       /*
	* XXX Should check that the config file value matches one of the
	* PCI base address values.
	*/
       pSiS->IOAddress = pSiS->pEnt->device->IOBase;
       from = X_CONFIG;
    } else {
        pSiS->IOAddress = PCI_REGION_BASE( pSiS->PciInfo, 1, REGION_MEM) & 0xFFFFFFF0;
		from = X_PROBED;
    }
    xf86DrvMsg(pScrn->scrnIndex, from, "MMIO registers at 0x%lX (size %ldK)\n",
	   (ULong)pSiS->IOAddress, pSiS->mmioSize);

#ifndef XSERVER_LIBPCIACCESS
    /* Register the PCI-assigned resources */
    if(xf86RegisterResources(pSiS->pEnt->index, NULL, ResExclusive)) {
       SISErrorLog(pScrn, "PCI resource conflicts detected\n");
#ifdef SISDUALHEAD
       if(pSiSEnt) pSiSEnt->ErrorAfterFirst = TRUE;
#endif
       sisRestoreExtRegisterLock(pSiS,srlockReg,crlockReg);
       if(pSiS->pInt) xf86FreeInt10(pSiS->pInt);
       SISFreeRec(pScrn);
       return FALSE;
    }
#endif

    from = X_PROBED;
    if(pSiS->pEnt->device->videoRam != 0) {
       if(pSiS->Chipset == PCI_CHIP_SIS6326) {
	  pScrn->videoRam = pSiS->pEnt->device->videoRam;
	  from = X_CONFIG;
       } else {
	  xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		"Option \"VideoRAM\" ignored\n");
       }
    }

    pSiS->RealVideoRam = pScrn->videoRam;

    if((pSiS->Chipset == PCI_CHIP_SIS6326) &&
       (pScrn->videoRam > 4096)            &&
       (from != X_CONFIG)) {
       pScrn->videoRam = 4096;
       xf86DrvMsg(pScrn->scrnIndex, from,
	   "SiS6326: Detected %d KB VideoRAM, limiting to %d KB\n",
	   pSiS->RealVideoRam, pScrn->videoRam);
    } else {
       xf86DrvMsg(pScrn->scrnIndex, from, "VideoRAM: %d KB\n", pScrn->videoRam);
    }

    if((pSiS->Chipset == PCI_CHIP_SIS6326) &&
       (pScrn->videoRam > 4096)) {
       xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	   "SiS6326 engines do not support more than 4096KB RAM, therefore\n");
       xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	   "TurboQueue, HWCursor, 2D acceleration and XVideo are disabled.\n");
       pSiS->TurboQueue = FALSE;
       pSiS->HWCursor   = FALSE;
       pSiS->NoXvideo   = TRUE;
       pSiS->NoAccel    = TRUE;
    }

    pSiS->FbMapSize = pSiS->availMem = pScrn->videoRam * 1024;

    /* Calculate real availMem according to Accel/TurboQueue and
     * HWCursur setting. Also, initialize some variables used
     * in other modules.
     */
    pSiS->cursorOffset = 0;
    pSiS->CurARGBDest = NULL;
    pSiS->CurMonoSrc = NULL;
    pSiS->CurFGCol = pSiS->CurBGCol = 0;
    pSiS->FbBaseOffset = 0; /*1024 * 1024;*/ /*chechun Kuo add offset for 512kB*/

    switch(pSiS->VGAEngine) {

      case SIS_300_VGA:
	pSiS->TurboQueueLen = 512;
	if(pSiS->TurboQueue) {
	   pSiS->availMem -= (pSiS->TurboQueueLen*1024);
	   pSiS->cursorOffset = 512;
	}
	if(pSiS->HWCursor) {
	   pSiS->availMem -= pSiS->CursorSize;
	   if(pSiS->OptUseColorCursor) pSiS->availMem -= pSiS->CursorSize;
	}
	pSiS->CmdQueLenMask = 0xFFFF;
	pSiS->CmdQueLenFix  = 0;
	pSiS->cursorBufferNum = 0;
#ifdef SISDUALHEAD
	if(pSiSEnt) pSiSEnt->cursorBufferNum = 0;
#endif
	break;

      case SIS_315_VGA:
#ifdef SISVRAMQ		/* VRAM queue */
	pSiS->cmdQueueSizeMask = pSiS->cmdQueueSize - 1;	/* VRAM Command Queue is variable (in therory) */
	pSiS->cmdQueueOffset = (pScrn->videoRam * 1024) - pSiS->cmdQueueSize;
	pSiS->cmdQueueLen = 0;
	pSiS->cmdQueueSize_div2 = pSiS->cmdQueueSize / 2;
	pSiS->cmdQueueSize_div4 = pSiS->cmdQueueSize / 4;
	pSiS->cmdQueueSize_4_3 = (pSiS->cmdQueueSize / 4) * 3;
	pSiS->availMem -= pSiS->cmdQueueSize;
	pSiS->cursorOffset = (pSiS->cmdQueueSize / 1024);

	/* Set up shared pointer to current offset */
#ifdef SISDUALHEAD
	if(pSiS->DualHeadMode)
	   pSiS->cmdQ_SharedWritePort = &(pSiSEnt->cmdQ_SharedWritePort_2D);
	else
#endif
	   pSiS->cmdQ_SharedWritePort = &(pSiS->cmdQ_SharedWritePort_2D);


#else			/* MMIO */
	if(pSiS->TurboQueue) {
	   pSiS->availMem -= (512*1024);			/* MMIO Command Queue is 512k (variable in theory) */
	   pSiS->cursorOffset = 512;
	}
#endif
	if(pSiS->HWCursor) {
	   pSiS->availMem -= (pSiS->CursorSize * 2);
	   if(pSiS->OptUseColorCursor) pSiS->availMem -= (pSiS->CursorSize * 2);
	}
	pSiS->cursorBufferNum = 0;
#ifdef SISDUALHEAD
	if(pSiSEnt) pSiSEnt->cursorBufferNum = 0;
#endif

	if((pSiS->SiS76xLFBSize) && (pSiS->SiS76xUMASize)) {
	   pSiS->availMem -= pSiS->SiS76xUMASize;
	   pSiS->FbBaseOffset = pSiS->SiS76xUMASize;
	}

	break;

      default:
	/* cursorOffset not used in cursor functions for 530 and
	 * older chips, because the cursor is *above* the TQ.
	 * On 5597 and older revisions of the 6326, the TQ is
	 * max 32K, on newer 6326 revisions and the 530 either 30
	 * (or 32?) or 62K (or 64?). However, to make sure, we
	 * use only 30K (or 32?), but reduce the available memory
	 * by 64, and locate the TQ at the beginning of this last
	 * 64K block. (We do this that way even when using the
	 * HWCursor, because the cursor only takes 2K and the
	 * queue does not seem to last that far anyway.)
	 * The TQ must be located at 32KB boundaries.
	 */
	if(pSiS->RealVideoRam < 3072) {
	   if(pSiS->TurboQueue) {
	      xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		    "Not enough video RAM for TurboQueue. TurboQueue disabled\n");
	      pSiS->TurboQueue = FALSE;
	   }
	}
	pSiS->CmdQueMaxLen = 32;
	if(pSiS->TurboQueue) {
			      pSiS->availMem -= (64*1024);
			      pSiS->CmdQueMaxLen = 900;   /* To make sure; should be 992 */
	} else if(pSiS->HWCursor) {
			      pSiS->availMem -= pSiS->CursorSize;
	}
	if(pSiS->Chipset == PCI_CHIP_SIS530) {
		/* Check if Flat Panel is enabled */
		inSISIDXREG(SISSR, 0x0e, tempreg);
		if(!(tempreg & 0x04)) pSiS->availMem -= pSiS->CursorSize;

		/* Set up mask for MMIO register */
		pSiS->CmdQueLenMask = (pSiS->TurboQueue) ? 0x1FFF : 0x00FF;
	} else {
	        /* TQ is never used on 6326/5597, because the accelerator
		 * always Syncs. So this is just cosmentic work. (And I
		 * am not even sure that 0x7fff is correct. MMIO 0x83a8
		 * holds 0xec0 if (30k) TQ is enabled, 0x20 if TQ disabled.
		 * The datasheet has no real explanation on the queue length
		 * if the TQ is enabled. Not syncing and waiting for a
		 * suitable queue length instead does not work.
		 */
	        pSiS->CmdQueLenMask = (pSiS->TurboQueue) ? 0x7FFF : 0x003F;
	}

	/* This is to be subtracted from MMIO queue length register contents
	 * for getting the real Queue length.
	 */
	pSiS->CmdQueLenFix  = (pSiS->TurboQueue) ? 32 : 0;
    }


#ifdef SISDUALHEAD
    /* In dual head mode, we share availMem equally - so align it
     * to 8KB; this way, the address of the FB of the second
     * head is aligned to 4KB for mapping.
     */
   if(pSiS->DualHeadMode) pSiS->availMem &= 0xFFFFE000;
#endif

    /* Check MaxXFBMem setting */
#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
        /* 1. Since DRI is not supported in dual head mode, we
	 *    don't need the MaxXFBMem setting - ignore it.
	 */
	if(pSiS->maxxfbmem) {
	   xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"MaxXFBMem ignored in Dual Head mode\n");
	}
	pSiS->maxxfbmem = pSiS->availMem;
    } else
#endif
	   if((pSiS->sisfbHeapStart) || (pSiS->sisfbHaveNewHeapDef)) {

       /*
	* 2. We have memory layout info from sisfb - ignore MaxXFBMem
	*/
	if(pSiS->maxxfbmem) {
	   xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		"Got memory layout info from sisfb, ignoring MaxXFBMem option\n");
	}
	if((pSiS->FbBaseOffset) && (!pSiS->sisfbHaveNewHeapDef)) {
	   xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		"Incompatible sisfb version detected, DRI disabled\n");
	   pSiS->loadDRI = FALSE;
	   pSiS->maxxfbmem = pSiS->availMem;
	} else {
	   if(pSiS->FbBaseOffset) {
	      /* Revert our changes to FbBaseOffset and availMem; use sisfb's info */
	      pSiS->availMem += pSiS->FbBaseOffset;
	      pSiS->FbBaseOffset = 0;
	   }
	   if(pSiS->sisfbVideoOffset) {
	      /* a. DRI heap BELOW framebuffer */
	      pSiS->FbBaseOffset = pSiS->sisfbVideoOffset;
	      pSiS->availMem -= pSiS->FbBaseOffset;
	      pSiS->maxxfbmem = pSiS->availMem;
	   } else {
	      /* b. DRI heap ABOVE framebuffer (traditional layout) */
	      if(pSiS->availMem < (pSiS->sisfbHeapStart * 1024)) {
		 xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"Internal error - sisfb memory layout corrupt\n");
		 pSiS->loadDRI = FALSE;
		 pSiS->maxxfbmem = pSiS->availMem;
	      } else {
	         pSiS->maxxfbmem = pSiS->sisfbHeapStart * 1024;
	      }
	   }
	}

    } else if(pSiS->maxxfbmem) {

       /*
	* 3. No sisfb, but user gave "MaxXFBMem"
	*/
	if(pSiS->FbBaseOffset) {
	   /* a. DRI heap BELOW framebuffer */
	   if(pSiS->maxxfbmem > (pSiS->availMem + pSiS->FbBaseOffset - pSiS->SiS76xUMASize)) {
	      xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"Invalid MaxXFBMem setting\n");
	      pSiS->maxxfbmem = pSiS->availMem;
	   } else {
	      /* Revert our changes */
	      pSiS->availMem += pSiS->FbBaseOffset;
	      /* Use user's MaxXFBMem setting */
	      pSiS->FbBaseOffset = pSiS->availMem - pSiS->maxxfbmem;
	      pSiS->availMem -= pSiS->FbBaseOffset;
	   }
	} else {
	   /* b. DRI heap ABOVE framebuffer (traditional layout) */
	   if(pSiS->maxxfbmem > pSiS->availMem) {
	      xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			 "Invalid MaxXFBMem setting.\n");
	      pSiS->maxxfbmem = pSiS->availMem;
	   }
	}

    } else {

       /*
	* 4. No MaxXFBMem, no sisfb: Use all memory
	*/
	pSiS->maxxfbmem = pSiS->availMem;

	/* ... except on chipsets, for which DRI is
	 * supported: If DRI is enabled, we now limit
	 * ourselves to a reasonable default:
	 */

	if(pSiS->loadDRI) {
	   if(pSiS->FbBaseOffset) {
	      /* a. DRI heap BELOW framebuffer */
	      /* See how much UMA and LFB memory we have,
	       * and calculate a reasonable default. We
	       * use more vram for ourselves because these
	       * chips are eg. capable of larger Xv
	       * overlays, etc.
	       */
	      unsigned long total = (pSiS->SiS76xLFBSize + pSiS->SiS76xUMASize) / 1024;
	      unsigned long mymax;
	      if(total <= 16384)			/* <= 16MB: Use 8MB for X */
	         mymax = 8192 * 1024;
	      else if(total <= 32768)			/* <= 32MB: Use 16MB for X */
	         mymax = 16384 * 1024;
	      else					/* Otherwise: Use 20MB for X */
	         mymax = 20 * 1024 * 1024;
	      /* availMem is right now adjusted to not use the UMA
	       * area. Make sure that our default doesn't reach
	       * into the UMA area either.
	       */
	      if(pSiS->availMem > mymax) {
		 /* Write our default to maxxfbmem */
		 pSiS->maxxfbmem = mymax;
		 /* Revert our changes to availMem */
		 pSiS->availMem += pSiS->FbBaseOffset;
		 /* Use our default setting */
		 pSiS->FbBaseOffset = pSiS->availMem - pSiS->maxxfbmem;
		 pSiS->availMem -= pSiS->FbBaseOffset;
	      }
	   } else {
	      /* b. DRI heap ABOVE framebuffer (traditional layout) */
	      /* See how much video memory we have, and calculate
	       * a reasonable default.
	       * Since DRI is pointless with less than 4MB of total
	       * video RAM, we disable it in that case.
	       */
	      if(pScrn->videoRam <= 4096)
	         pSiS->loadDRI = FALSE;
	      else if(pScrn->videoRam <= 8192)		/* <= 8MB: Use 4MB for X */
	         pSiS->maxxfbmem = 4096 * 1024;
	      else if(pScrn->videoRam <= 16384)		/* <= 16MB: Use 8MB for X */
	         pSiS->maxxfbmem = 8192 * 1024;
#ifdef SISMERGED					/* Otherwise: --- */
	      else if(pSiS->MergedFB) {
	         if(pScrn->videoRam <= 65536)
	            pSiS->maxxfbmem = 16384 * 1024;	/* If MergedFB and <=64MB, use 16MB for X */
		 else
		    pSiS->maxxfbmem = 20 * 1024 * 1024;	/* If MergedFB and > 64MB, use 20MB for X */
	      }
#endif
	        else if(pSiS->VGAEngine == SIS_315_VGA) {
	         if(pScrn->videoRam <= 65536)
	            pSiS->maxxfbmem = 16384 * 1024;	/* On >=315 series and <=64MB, use 16MB */
		 else
		    pSiS->maxxfbmem = 20 * 1024 * 1024;	/* On >=315 series and > 64MB, use 20MB */
	      } else
	         pSiS->maxxfbmem = 12288 * 1024;	/* On <315 series, use 12MB */

	      /* A final check */
	      if(pSiS->maxxfbmem > pSiS->availMem) {
		 pSiS->maxxfbmem = pSiS->availMem;
		 pSiS->loadDRI = FALSE;
	      }
	   }

	}
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Using %dK of framebuffer memory at offset %dK\n",
				pSiS->maxxfbmem / 1024, pSiS->FbBaseOffset / 1024);


    if(pSiS->SiS_SD2_Flags & SiS_SD2_SUPPORT760OO) {
       xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"\n\tDear SiS760 user, your machine is using a shared memory framebuffer.\n"
		  "\tDue to hardware limitations of the SiS chip in combination with the\n"
		  "\tAMD CPU, video overlay support is very limited on this machine. If you\n"
		  "\texperience flashing lines in the video and/or the graphics display\n"
		  "\tduring video playback, reduce the color depth and/or the resolution\n"
		  "\tand/or the refresh rate. Alternatively, use the video blitter.\n");
    }

    /* Backup VB connection and CRT1 on/off register */
    if((pSiS->VGAEngine == SIS_300_VGA) || (pSiS->VGAEngine == SIS_315_VGA)) {
       inSISIDXREG(SISSR, 0x1f, pSiS->oldSR1F);
       inSISIDXREG(SISCR, 0x17, pSiS->oldCR17);
       inSISIDXREG(SISCR, 0x32, pSiS->oldCR32);
       inSISIDXREG(SISCR, 0x36, pSiS->oldCR36);
       inSISIDXREG(SISCR, 0x37, pSiS->oldCR37);
       if(pSiS->VGAEngine == SIS_315_VGA) {
          inSISIDXREG(SISCR, pSiS->myCR63, pSiS->oldCR63);
       }

       pSiS->postVBCR32 = pSiS->oldCR32;
    }

    /* SiS630: Chrontel GPIO */
    SiSDetermineChrontelGPIO(pScrn);

    /* Determine custom timing */
    SiSDetermineCustomTiming(pScrn);

    /* Handle ForceCRT1 option */
    if(pSiS->forceCRT1 != -1) {
       if(pSiS->forceCRT1) pSiS->CRT1off = 0;
       else                pSiS->CRT1off = 1;
    } else                 pSiS->CRT1off = -1;

    /* Load DDC module (needed for device detection already) */
    SiSLoadInitDDCModule(pScrn);

    /* Detect video bridge and sense TV/VGA2 */
    SISVGAPreInit(pScrn);

    /* Detect CRT1 (via DDC1 and DDC2, hence via VGA port; regardless of LCDA) */
    SISCRT1PreInit(pScrn);

    /* Detect LCD (connected via CRT2, regardless of LCDA) and LCD resolution */
    SISLCDPreInit(pScrn, FALSE);

    /* LCDA only supported under these conditions: */
    if(pSiS->ForceCRT1Type == CRT1_LCDA) {
       if(!SISDetermineLCDACap(pScrn)) {
	  xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		"Chipset/Video bridge does not support LCD-via-CRT1\n");
	  pSiS->ForceCRT1Type = CRT1_VGA;
       } else if(!(pSiS->VBFlags & CRT2_LCD)) {
	  xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		"No digital LCD panel found, LCD-via-CRT1 disabled\n");
	  pSiS->ForceCRT1Type = CRT1_VGA;
       }
    }

    /* Setup SD flags */
    pSiS->SiS_SD_Flags |= SiS_SD_ADDLSUPFLAG;

    pSiS->SiS_SD2_Flags |= SiS_SD2_MERGEDUCLOCK;
    pSiS->SiS_SD2_Flags |= SiS_SD2_USEVBFLAGS2;
    pSiS->SiS_SD2_Flags |= SiS_SD2_VBINVB2ONLY;
    pSiS->SiS_SD2_Flags |= SiS_SD2_HAVESD34;
    pSiS->SiS_SD2_Flags |= SiS_SD2_NEWGAMMABRICON;

    pSiS->SiS_SD3_Flags |= SiS_SD3_MFBALLOWOFFCL;
    pSiS->SiS_SD3_Flags |= SiS_SD3_MFBDYNPOS;


    if(pSiS->VBFlags2 & VB2_VIDEOBRIDGE) {
       pSiS->SiS_SD2_Flags |= SiS_SD2_VIDEOBRIDGE;
       if(pSiS->VBFlags2 & VB2_SISBRIDGE) {
	  pSiS->SiS_SD2_Flags |= ( SiS_SD2_SISBRIDGE     |
				   SiS_SD2_SUPPORTGAMMA2 );
	  if(pSiS->VBFlags2 & VB2_SISLVDSBRIDGE) {
	     pSiS->SiS_SD2_Flags |= ( SiS_SD2_LCDLVDS    |
				      SiS_SD2_SUPPORTLCD );
	  } else if(pSiS->VBFlags2 & VB2_SISTMDSBRIDGE) {
	     if(!(pSiS->VBFlags2 & VB2_30xBDH)) {
		pSiS->SiS_SD2_Flags |= ( SiS_SD2_LCDTMDS    |
					 SiS_SD2_SUPPORTLCD );
	     } else if(pSiS->VBFlags & CRT2_LCD) {
		pSiS->SiS_SD2_Flags |= ( SiS_SD2_THIRDPARTYLVDS |
				         SiS_SD2_SUPPORTLCD );
	     }
	  }
       } else if(pSiS->VBFlags2 & VB2_LVDS) {
	  pSiS->SiS_SD2_Flags |= ( SiS_SD2_THIRDPARTYLVDS |
				   SiS_SD2_SUPPORTLCD );
       }

       if(pSiS->VBFlags2 & (VB2_SISTVBRIDGE | VB2_CHRONTEL)) {
	  pSiS->SiS_SD_Flags |= SiS_SD_SUPPORTTV;
	  if(pSiS->VBFlags2 & VB2_SISBRIDGE) {
	     pSiS->SiS_SD2_Flags |= ( SiS_SD2_SUPPORTTVTYPE |
				      SiS_SD2_SUPPORTTVSIZE );
	     if(!(pSiS->VBFlags2 & VB2_301)) {
		pSiS->SiS_SD2_Flags |= SiS_SD2_SUPPTVSAT;
	     } else {
		pSiS->SiS_SD2_Flags |= SiS_SD2_SUPPTVEDGE;
	     }
	  }
       }
    }

    if((pSiS->VGAEngine == SIS_315_VGA) &&
       (pSiS->VBFlags2 & VB2_SISYPBPRBRIDGE)) {
       pSiS->SiS_SD_Flags |= SiS_SD_SUPPORTYPBPR;
       pSiS->SiS_SD2_Flags |= SiS_SD2_SUPPORT625I;
       pSiS->SiS_SD2_Flags |= SiS_SD2_SUPPORT625P;
       if(pSiS->VBFlags2 & VB2_SISYPBPRARBRIDGE) {
          pSiS->SiS_SD_Flags |= SiS_SD_SUPPORTYPBPRAR;
       }
    }
    if(pSiS->VBFlags2 & VB2_SISHIVISIONBRIDGE) {
       pSiS->SiS_SD_Flags |= SiS_SD_SUPPORTHIVISION;
    }

    if((pSiS->VGAEngine != SIS_300_VGA) || (!(pSiS->VBFlags2 & VB2_TRUMPION))) {
       pSiS->SiS_SD_Flags |= SiS_SD_SUPPORTSCALE;
       if((pSiS->VBFlags2 & VB2_SISTMDSBRIDGE) &&
          (!(pSiS->VBFlags2 & VB2_30xBDH))) {
          pSiS->SiS_SD_Flags |= SiS_SD_SUPPORTCENTER;
       }
    }

#ifdef SISDUALHEAD
    if(!pSiS->DualHeadMode)
#endif
       pSiS->SiS_SD_Flags |= SiS_SD_SUPPORTREDETECT;

#ifndef SISCHECKOSSSE
    pSiS->SiS_SD2_Flags |= SiS_SD2_NEEDUSESSE;
#endif

#ifdef TWDEBUG	
    pSiS->SiS_SD_Flags |= SiS_SD_SUPPORTYPBPRAR;
    xf86DrvMsg(0, X_INFO, "TEST: Support Aspect Ratio\n");

#endif

    /* Detect CRT2-TV and PAL/NTSC mode */
    SISTVPreInit(pScrn, FALSE);

    /* Detect CRT2-VGA */
    SISCRT2PreInit(pScrn, FALSE);

    /* Backup detected CRT2 devices */
    SISSaveDetectedDevices(pScrn);

    if(!(pSiS->SiS_SD_Flags & SiS_SD_SUPPORTYPBPR)) {
       if((pSiS->ForceTVType != -1) && (pSiS->ForceTVType & TV_YPBPR)) {
	  pSiS->ForceTVType = -1;
	  xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "YPbPr TV output not supported\n");
       }
    }

    if(!(pSiS->SiS_SD_Flags & SiS_SD_SUPPORTHIVISION)) {
       if((pSiS->ForceTVType != -1) && (pSiS->ForceTVType & TV_HIVISION)) {
	  pSiS->ForceTVType = -1;
	  xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "HiVision TV output not supported\n");
       }
    }
    if((pSiS->VBFlags2 & VB2_SISTVBRIDGE) ||
       ((pSiS->VBFlags2 & VB2_CHRONTEL) && (pSiS->ChrontelType == CHRONTEL_701x))) {
       pSiS->SiS_SD_Flags |= (SiS_SD_SUPPORTPALMN | SiS_SD_SUPPORTNTSCJ);
    }
    if((pSiS->VBFlags2 & VB2_SISTVBRIDGE) ||
       ((pSiS->VBFlags2 & VB2_CHRONTEL) && (pSiS->ChrontelType == CHRONTEL_700x))) {
       pSiS->SiS_SD_Flags |= SiS_SD_SUPPORTTVPOS;
    }
    if(pSiS->VBFlags2 & VB2_SISVGA2BRIDGE) {
       pSiS->SiS_SD_Flags |= (SiS_SD_SUPPORTSCART | SiS_SD_SUPPORTVGA2);
    }
    if(pSiS->VBFlags2 & VB2_CHRONTEL) {
       pSiS->SiS_SD_Flags  |= SiS_SD_SUPPORTOVERSCAN;
       pSiS->SiS_SD2_Flags |= SiS_SD2_CHRONTEL;
       if(pSiS->ChrontelType == CHRONTEL_700x) {
	  pSiS->SiS_SD_Flags |= SiS_SD_SUPPORTSOVER;
       }
    }

    /* Determine if chipset LCDA-capable */
    pSiS->SiS_SD_Flags &= ~SiS_SD_SUPPORTLCDA;
    if(SISDetermineLCDACap(pScrn)) {
       pSiS->SiS_SD_Flags |= SiS_SD_SUPPORTLCDA;
    }

    /* Default to LCDA if LCD detected and
     * - TV detected (hence default to LCDA+TV), or
     * - in single head mode, on LCD panels with xres > 1600
     *   (Don't do this in MergedFB or DHM; LCDA and CRT1/VGA
     *   are mutually exclusive; if no TV is detected, the
     *   code below will default to VGA+LCD, so LCD is driven
     *   via CRT2.)
     *   (TODO: This might need some modification for the
     *   307 bridges, if these are capable of driving
     *   LCDs > 1600 via channel B)
     */
    if((pSiS->SiS_SD_Flags & SiS_SD_SUPPORTLCDA) &&
       (pSiS->VBFlags & CRT2_LCD) &&
       (pSiS->SiS_Pr->SiS_CustomT != CUT_UNKNOWNLCD)) {
       if((!pSiS->CRT1TypeForced) && (pSiS->ForceCRT2Type == CRT2_DEFAULT)) {
	  if(pSiS->VBFlags & CRT2_TV) {
	     /* 
	     	  If both LCD and TV present, default to LCDA+TV.
	     	  However for newer chips later than 662, (pSiS->VBFlags & CRT2_LCD) is always true.
	     	  (BIOS's fault?)
	     	  Therefore we do not set CRT1 as LCDA if new chips.
		*/
		if(pSiS->ChipType < SIS_662 || pSiS->ChipType >= XGI_20)	pSiS->ForceCRT1Type = CRT1_LCDA;
	     pSiS->ForceCRT2Type = CRT2_TV;
	  } else if(pSiS->LCDwidth > 1600) {
	     /* If LCD is > 1600, default to LCDA if we don't need CRT1/VGA for other head */
	     Bool NeedCRT1VGA = FALSE;
#ifdef SISDUALHEAD
	     if(pSiS->DualHeadMode) NeedCRT1VGA = TRUE;
#endif
#ifdef SISMERGED
	     if(pSiS->MergedFB &&
		(!pSiS->MergedFBAuto || pSiS->CRT1Detected)) NeedCRT1VGA = TRUE;
#endif
	     if(!NeedCRT1VGA) {
		pSiS->ForceCRT1Type = CRT1_LCDA;
	     }
	  }
       }
    }

    /* Set up pseudo-panel if LCDA forced on TMDS bridges */
    if(pSiS->SiS_SD_Flags & SiS_SD_SUPPORTLCDA) {
       if(pSiS->ForceCRT1Type == CRT1_LCDA) {
          if(pSiS->VBFlags2 & VB2_SISTMDSLCDABRIDGE) {
	     if(!(pSiS->VBLCDFlags)) {
		SiSSetupPseudoPanel(pScrn);
		pSiS->detectedCRT2Devices |= CRT2_LCD;
	     }
	  } else if(!(pSiS->VBLCDFlags)) {
	     pSiS->ForceCRT1Type = CRT1_VGA;
	  }
       }
    } else {
       pSiS->ForceCRT1Type = CRT1_VGA;
    }
	
    pSiS->VBFlags |= pSiS->ForceCRT1Type;

#ifdef TWDEBUG
    xf86DrvMsg(0, X_INFO, "SDFlags %lx pSiS->VBFlags = 0x%x\n", pSiS->SiS_SD_Flags, pSiS->VBFlags);
#endif

    /* Eventually overrule detected CRT2 type
     * If no type forced, use the detected devices in the order TV->LCD->VGA2
     * Since the Chrontel 7005 sometimes delivers wrong detection results,
     * we use a different order on such machines (LCD->TV)
     */
    if(pSiS->ForceCRT2Type == CRT2_DEFAULT) {
       if((pSiS->VBFlags & CRT2_TV) && (!((pSiS->VBFlags2 & VB2_CHRONTEL) && (pSiS->VGAEngine == SIS_300_VGA))))
	  pSiS->ForceCRT2Type = CRT2_TV;
       else if((pSiS->VBFlags & CRT2_LCD) && (pSiS->ForceCRT1Type == CRT1_VGA))
	  pSiS->ForceCRT2Type = CRT2_LCD;
       else if(pSiS->VBFlags & CRT2_TV)
	  pSiS->ForceCRT2Type = CRT2_TV;
       else if((pSiS->VBFlags & CRT2_VGA) && (pSiS->ForceCRT1Type == CRT1_VGA))
	  pSiS->ForceCRT2Type = CRT2_VGA;
    }
    switch(pSiS->ForceCRT2Type) {
       case CRT2_TV:
	  pSiS->VBFlags &= ~(CRT2_LCD | CRT2_VGA);
	  if(pSiS->VBFlags2 & (VB2_SISTVBRIDGE | VB2_CHRONTEL)) {
	     pSiS->VBFlags |= CRT2_TV;
	  } else {
	     pSiS->VBFlags &= ~(CRT2_TV);
	     xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		"Hardware does not support TV output\n");
	  }
	  break;
       case CRT2_LCD:
	  pSiS->VBFlags &= ~(CRT2_TV | CRT2_VGA);
	  if((pSiS->VBFlags2 & VB2_VIDEOBRIDGE) && (pSiS->VBLCDFlags)) {
	     pSiS->VBFlags |= CRT2_LCD;
	  } else if((pSiS->VBFlags2 & VB2_SISTMDSBRIDGE) && (!(pSiS->VBFlags2 & VB2_30xBDH))) {
	     SiSSetupPseudoPanel(pScrn);
	     pSiS->detectedCRT2Devices |= CRT2_LCD;
	  } else {
	     pSiS->VBFlags &= ~(CRT2_LCD);
	     xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		"Can't force CRT2 to LCD, no LCD detected\n");
	  }
	  break;
       case CRT2_VGA:
	  pSiS->VBFlags &= ~(CRT2_TV | CRT2_LCD);
	  if(pSiS->VBFlags2 & VB2_SISVGA2BRIDGE) {
	     pSiS->VBFlags |= CRT2_VGA;
	  } else {
	     pSiS->VBFlags &= ~(CRT2_VGA);
	     xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		 "Hardware does not support secondary VGA\n");
	  }
	  break;
       default:
	  pSiS->VBFlags &= ~(CRT2_TV | CRT2_LCD | CRT2_VGA);
    }

    /* Setup gamma (the cmap layer needs this to be initialised) */
    /* (Do this after evaluating options) */
    {
       Gamma zeros = {0.0, 0.0, 0.0};
       xf86SetGamma(pScrn, zeros);
    }
#ifdef SISDUALHEAD
    if((!pSiS->DualHeadMode) || (pSiS->SecondHead)) {
#endif
       xf86DrvMsg(pScrn->scrnIndex, pSiS->CRT1gammaGiven ? X_CONFIG : X_INFO,
	     "%samma correction is %s\n",
	     (pSiS->VBFlags2 & VB2_VIDEOBRIDGE) ? "CRT1 g" : "G",
	     pSiS->CRT1gamma ? "enabled" : "disabled");

       if((pSiS->VGAEngine == SIS_315_VGA)	&&
          (!(pSiS->NoXvideo))			&&
	  (!(pSiS->SiS_SD2_Flags & SiS_SD2_NOOVERLAY))) {
	  xf86DrvMsg(pScrn->scrnIndex, pSiS->XvGammaGiven ? X_CONFIG : X_INFO,
		"Separate Xv gamma correction %sis %s\n",
		(pSiS->VBFlags2 & VB2_VIDEOBRIDGE) ? "for CRT1 " : "",
		pSiS->XvGamma ? "enabled" : "disabled");
	  if(pSiS->XvGamma) {
	     xf86DrvMsg(pScrn->scrnIndex, pSiS->XvGammaGiven ? X_CONFIG : X_INFO,
		"Xv gamma correction: %.3f %.3f %.3f\n",
		(float)((float)pSiS->XvGammaRed / 1000),
		(float)((float)pSiS->XvGammaGreen / 1000),
		(float)((float)pSiS->XvGammaBlue / 1000));
	     if(!pSiS->CRT1gamma) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "Xv gamma correction requires %samma correction enabled\n",
		   (pSiS->VBFlags2 & VB2_VIDEOBRIDGE) ? "CRT1 g" : "G");
	     }
	  }
       }
#ifdef SISDUALHEAD
    }
#endif
#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) pSiS->CRT2SepGamma = FALSE;
#endif

#ifdef SISDUALHEAD
    if((!pSiS->DualHeadMode) || (!pSiS->SecondHead))
#endif
    {
       Bool isDH = FALSE;
       if(pSiS->CRT2gamma) {
          if( ((pSiS->VGAEngine != SIS_300_VGA) && (pSiS->VGAEngine != SIS_315_VGA)) ||
              (!(pSiS->VBFlags2 & VB2_SISBRIDGE)) ) {
	     if(pSiS->VBFlags2 & VB2_VIDEOBRIDGE) {
	        xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
			"CRT2 gamma correction not supported by hardware\n");
	     }
	     pSiS->CRT2gamma = pSiS->CRT2SepGamma = FALSE;
          } else if((pSiS->VBFlags2 & VB2_30xBDH) && (pSiS->VBFlags & CRT2_LCD)) {
	     isDH = TRUE;
	     xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
			"CRT2 gamma correction not supported for LCD\n");
	     /* But leave it on, will be caught in LoadPalette */
          }
       }
       if(pSiS->VBFlags2 & VB2_SISBRIDGE) {
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CRT2 gamma correction is %s%s%s\n",
		pSiS->CRT2gamma ? "enabled" : "disabled",
		isDH ? " (for TV and VGA2) " : "",
		pSiS->CRT2SepGamma ? " (separate from CRT1)" : "");
       }
    }

    /* Eventually overrule TV Type (SVIDEO, COMPOSITE, SCART, HIVISION, YPBPR) */
    if(pSiS->VBFlags2 & VB2_SISTVBRIDGE) {
       if(pSiS->ForceTVType != -1) {
	  pSiS->VBFlags &= ~(TV_INTERFACE);
	  if(!(pSiS->VBFlags2 & VB2_CHRONTEL)) {
	     pSiS->VBFlags &= ~(TV_CHSCART | TV_CHYPBPR525I);
	  }
	  pSiS->VBFlags |= pSiS->ForceTVType;
	  if(pSiS->VBFlags & TV_YPBPR) {
	     pSiS->VBFlags &= ~(TV_STANDARD);
	     pSiS->VBFlags &= ~(TV_YPBPRAR);
	     pSiS->VBFlags |= pSiS->ForceYPbPrType;
	     pSiS->VBFlags |= pSiS->ForceYPbPrAR;
	  }
       }
    }

    /* Handle ForceCRT1 option (part 2) */
    pSiS->CRT1changed = FALSE;
    if((pSiS->VGAEngine == SIS_300_VGA) || (pSiS->VGAEngine == SIS_315_VGA)) {
       usScratchCR17 = pSiS->oldCR17;
       usScratchCR63 = pSiS->oldCR63;
       usScratchSR1F = pSiS->oldSR1F;
       usScratchCR32 = pSiS->postVBCR32;
       if(pSiS->VESA != 1) {
          /* Copy forceCRT1 option to CRT1off if option is given */
#ifdef SISDUALHEAD
          /* In DHM, handle this option only for master head, not the slave */
          if( (pSiS->forceCRT1 != -1) &&
	       (!(pSiS->DualHeadMode && pSiS->SecondHead)) ) {
#else
          if(pSiS->forceCRT1 != -1) {
#endif
	     xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		 "CRT1 detection overruled by ForceCRT1 option\n");
	     if(pSiS->forceCRT1) {
		 pSiS->CRT1off = 0;
		 if(pSiS->VGAEngine == SIS_300_VGA) {
		    if(!(usScratchCR17 & 0x80)) pSiS->CRT1changed = TRUE;
		 } else {
		    if(usScratchCR63 & 0x40) pSiS->CRT1changed = TRUE;
		 }
		 usScratchCR17 |= 0x80;
		 usScratchCR32 |= 0x20;
		 usScratchCR63 &= ~0x40;
		 usScratchSR1F &= ~0xc0;
	     } else {
		 if( ! ( (pScrn->bitsPerPixel == 8) &&
		         ( (pSiS->VBFlags2 & (VB2_LVDS | VB2_CHRONTEL)) ||
		           ((pSiS->VBFlags2 & VB2_30xBDH) && (pSiS->VBFlags & CRT2_LCD)) ) ) ) {
		    pSiS->CRT1off = 1;
		    if(pSiS->VGAEngine == SIS_300_VGA) {
		       if(usScratchCR17 & 0x80) pSiS->CRT1changed = TRUE;
		    } else {
		       if(!(usScratchCR63 & 0x40)) pSiS->CRT1changed = TRUE;
		    }
		    usScratchCR32 &= ~0x20;
		    /* We must not actually switch off CRT1 before we changed the mode! */
		 }
	     }
	     /* Here we can write to CR17 even on 315 series as we only ENABLE
	      * the bit here
	      */
	     outSISIDXREG(SISCR, 0x17, usScratchCR17);
	     if(pSiS->VGAEngine == SIS_315_VGA) {
		outSISIDXREG(SISCR, pSiS->myCR63, usScratchCR63);
	     }
	     outSISIDXREG(SISCR, 0x32, usScratchCR32);
	     if(pSiS->CRT1changed) {
		outSISIDXREG(SISSR, 0x00, 0x01);    /* Synchronous Reset */
		usleep(10000);
		outSISIDXREG(SISSR, 0x00, 0x03);    /* End Reset */
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			"CRT1 status changed by ForceCRT1 option\n");
	     }
	     outSISIDXREG(SISSR, 0x1f, usScratchSR1F);
          }
       }
       /* Store the new VB connection register contents for later mode changes */
       pSiS->newCR32 = usScratchCR32;
    }


    /* Check if CRT1 used (or needed; this eg. if no CRT2 detected) */
    if(pSiS->VBFlags2 & VB2_VIDEOBRIDGE) {

        /* No CRT2 output? Then we NEED CRT1!
	 * We also need CRT1 if depth = 8 and bridge=LVDS|301B-DH
	 */
	if( (!(pSiS->VBFlags & (CRT2_VGA | CRT2_LCD | CRT2_TV))) ||
	    ( (pScrn->bitsPerPixel == 8) &&
	      ( (pSiS->VBFlags2 & (VB2_LVDS | VB2_CHRONTEL)) ||
	        ((pSiS->VBFlags2 & VB2_30xBDH) && (pSiS->VBFlags & CRT2_LCD)) ) ) ) {
	    pSiS->CRT1off = 0;
	}
	/* No CRT2 output? Then we can't use Xv on CRT2 */
	if(!(pSiS->VBFlags & (CRT2_VGA | CRT2_LCD | CRT2_TV))) {
	    pSiS->XvOnCRT2 = FALSE;
	}

    } else { /* no video bridge? */

	/* Then we NEED CRT1... */
	pSiS->CRT1off = 0;
	/* ... and can't use CRT2 for Xv output */
	pSiS->XvOnCRT2 = FALSE;
    }

    /* LCDA? Then we don't switch off CRT1 */
    if(pSiS->VBFlags & CRT1_LCDA) pSiS->CRT1off = 0;

    /* Handle TVStandard option */
    if((pSiS->NonDefaultPAL != -1) || (pSiS->NonDefaultNTSC != -1)) {
       if( (!(pSiS->VBFlags2 & VB2_SISTVBRIDGE)) &&
	   (!((pSiS->VBFlags2 & VB2_CHRONTEL)) && (pSiS->ChrontelType == CHRONTEL_701x)) ) {
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	   	"PALM, PALN and NTSCJ not supported on this hardware\n");
	  pSiS->NonDefaultPAL = pSiS->NonDefaultNTSC = -1;
	  pSiS->VBFlags &= ~(TV_PALN | TV_PALM | TV_NTSCJ);
	  pSiS->SiS_SD_Flags &= ~(SiS_SD_SUPPORTPALMN | SiS_SD_SUPPORTNTSCJ);
       }
    }
    if(pSiS->OptTVStand != -1) {
       if((pSiS->VGAEngine == SIS_300_VGA) || (pSiS->VGAEngine == SIS_315_VGA)) {
	  if( (!((pSiS->VBFlags2 & VB2_CHRONTEL) && (pSiS->VBFlags & (TV_CHSCART | TV_CHYPBPR525I)))) &&
	      (!(pSiS->VBFlags & (TV_HIVISION | TV_YPBPR))) ) {
	     pSiS->VBFlags &= ~(TV_PAL | TV_NTSC | TV_PALN | TV_PALM | TV_NTSCJ);
	     if(pSiS->OptTVStand) {
	        pSiS->VBFlags |= TV_PAL;
	        if(pSiS->NonDefaultPAL == 1)  pSiS->VBFlags |= TV_PALM;
	        else if(!pSiS->NonDefaultPAL) pSiS->VBFlags |= TV_PALN;
	     } else {
	        pSiS->VBFlags |= TV_NTSC;
		if(pSiS->NonDefaultNTSC == 1) pSiS->VBFlags |= TV_NTSCJ;
	     }
	  } else {
	     pSiS->OptTVStand = pSiS->NonDefaultPAL = -1;
	     xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	    	 "Option TVStandard ignored for YPbPr, HiVision and Chrontel-SCART\n");
	  }
       } else if(pSiS->Chipset == PCI_CHIP_SIS6326) {
	  pSiS->SiS6326Flags &= ~SIS6326_TVPAL;
	  if(pSiS->OptTVStand) pSiS->SiS6326Flags |= SIS6326_TVPAL;
       }
    }

    if((pSiS->VGAEngine == SIS_300_VGA) || (pSiS->VGAEngine == SIS_315_VGA)) {
       /* Default to PAL */
       if(pSiS->VBFlags & (TV_SVIDEO | TV_AVIDEO)) {
          if(!(pSiS->VBFlags & (TV_PAL | TV_NTSC))) {
	     pSiS->VBFlags &= ~(TV_PAL | TV_NTSC | TV_PALN | TV_PALM | TV_NTSCJ);
	     pSiS->VBFlags |= TV_PAL;
	  }
       }
       /* SCART only supported for PAL */
       if((pSiS->VBFlags2 & VB2_SISBRIDGE) && (pSiS->VBFlags & TV_SCART)) {
	  pSiS->VBFlags &= ~(TV_NTSC | TV_PALN | TV_PALM | TV_NTSCJ);
	  pSiS->VBFlags |= TV_PAL;
	  pSiS->OptTVStand = 1;
	  pSiS->NonDefaultPAL = pSiS->NonDefaultNTSC = -1;
       }
    }


    if((pSiS->Chipset == PCI_CHIP_SIS6326) && (pSiS->SiS6326Flags & SIS6326_HASTV)) {
       if(pSiS->sis6326tvplug != -1) {
          pSiS->SiS6326Flags &= ~(SIS6326_TVSVIDEO | SIS6326_TVCVBS);
	  pSiS->SiS6326Flags |= SIS6326_TVDETECTED;
	  if(pSiS->sis6326tvplug == 1) 	pSiS->SiS6326Flags |= SIS6326_TVCVBS;
	  else 				pSiS->SiS6326Flags |= SIS6326_TVSVIDEO;
	  xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
	      "SiS6326 TV plug type detection overruled by %s\n",
	      (pSiS->SiS6326Flags & SIS6326_TVCVBS) ? "COMPOSITE" : "SVIDEO");
       }
    }

    /* Do some checks */
    if(pSiS->OptTVOver != -1) {
       if(pSiS->VBFlags2 & VB2_CHRONTEL) {
	  pSiS->UseCHOverScan = pSiS->OptTVOver;
       } else {
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	      "CHTVOverscan only supported on CHRONTEL 70xx\n");
	  pSiS->UseCHOverScan = -1;
       }
    } else pSiS->UseCHOverScan = -1;

    if(pSiS->sistvedgeenhance != -1) {
       if(!(pSiS->VBFlags2 & VB2_301)) {
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	      "SISTVEdgeEnhance only supported on SiS301\n");
	  pSiS->sistvedgeenhance = -1;
       }
    }
    if(pSiS->sistvsaturation != -1) {
       if(pSiS->VBFlags2 & VB2_301) {
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	      "SISTVSaturation not supported on SiS301\n");
	  pSiS->sistvsaturation = -1;
       }
    }

    /* MergedFB: Create CRT2 pScrn and make it a copy of pScrn */
#ifdef SISMERGED
    if(pSiS->MergedFB) {
       pSiS->CRT2pScrn = malloc(sizeof(ScrnInfoRec));
       if(!pSiS->CRT2pScrn) {
          SISErrorLog(pScrn, "Failed to allocate memory for 2nd pScrn, %s\n", mergeddisstr);
	  pSiS->MergedFB = FALSE;
       } else {
          memcpy(pSiS->CRT2pScrn, pScrn, sizeof(ScrnInfoRec));
       }
    }
#endif

    /* Determine CRT1<>CRT2 mode
     *     Note: When using VESA or if the bridge is in slavemode, display
     *           is ALWAYS in MIRROR_MODE!
     *           This requires extra checks in functions using this flag!
     *           (see sis_video.c for example)
     */

    if(pSiS->VBFlags & DISPTYPE_DISP2) {

       if(pSiS->CRT1off) {	/* CRT2 only ------------------------------- */

#ifdef SISDUALHEAD
	  if(pSiS->DualHeadMode) {
	     SISErrorLog(pScrn,
		    "CRT1 not detected or forced off. Dual Head mode can't initialize.\n");
	     if(pSiSEnt) pSiSEnt->DisableDual = TRUE;
	     goto my_error_1;
	  }
#endif
#ifdef SISMERGED
	  if(pSiS->MergedFB) {
	     if(pSiS->MergedFBAuto) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, mergednocrt1, mergeddisstr);
	     } else {
		SISErrorLog(pScrn, mergednocrt1, mergeddisstr);
	     }
	     free(pSiS->CRT2pScrn);
	     pSiS->CRT2pScrn = NULL;
	     pSiS->MergedFB = FALSE;
	  }
#endif
	  pSiS->VBFlags |= VB_DISPMODE_SINGLE;
	  /* No CRT1? Then we use the video overlay on CRT2 */
	  pSiS->XvOnCRT2 = TRUE;

       } else			/* CRT1 and CRT2 - mirror or dual head ----- */

#ifdef SISDUALHEAD
       if(pSiS->DualHeadMode) {
	  if(pSiS->VESA != -1) {
	     xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		  "VESA not used in Dual Head mode. VESA disabled.\n");
	  }
	  if(pSiSEnt) pSiSEnt->DisableDual = FALSE;
	  pSiS->VESA = 0;
       } else
#endif
#ifdef SISMERGED
	      if(pSiS->MergedFB) {
	  pSiS->VBFlags |= (VB_DISPMODE_MIRROR | DISPTYPE_CRT1);
	  if(pSiS->VESA != -1) {
	     xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		  "VESA not used in MergedFB mode. VESA disabled.\n");
	  }
	  pSiS->VESA = 0;
       } else
#endif
	  pSiS->VBFlags |= (VB_DISPMODE_MIRROR | DISPTYPE_CRT1);
    } else {			/* CRT1 only ------------------------------- */

#ifdef SISDUALHEAD
	if(pSiS->DualHeadMode) {
	   SISErrorLog(pScrn,
		"No CRT2 output selected or no bridge detected. "
		"Dual Head mode can't initialize.\n");
	   goto my_error_1;
	}
#endif
#ifdef SISMERGED
	if(pSiS->MergedFB) {
	   if(pSiS->MergedFBAuto) {
	      xf86DrvMsg(pScrn->scrnIndex, X_INFO, mergednocrt2, mergeddisstr);
	   } else {
	      SISErrorLog(pScrn, mergednocrt2, mergeddisstr);
	   }
	   free(pSiS->CRT2pScrn);
	   pSiS->CRT2pScrn = NULL;
	   pSiS->MergedFB = FALSE;
	}
#endif
        pSiS->VBFlags |= (VB_DISPMODE_SINGLE | DISPTYPE_CRT1);
    }

    /* Init ptrs for Save/Restore functions and calc MaxClock */
    SISDACPreInit(pScrn);
	
    /* ********** end of VBFlags setup ********** */
    /* VBFlags are initialized now. Back them up for SlaveMode modes. */
    pSiS->VBFlags_backup = pSiS->VBFlags;
    #ifdef TWDEUG
	 xf86DrvMsg(0, X_INFO,"[SISPreInit( )]:Final VBFlags=0x%x.\n", pSiS->VBFlags);
    #endif	
    /* Backup CR32,36,37 (in order to write them back after a VT switch) */
    if((pSiS->VGAEngine == SIS_300_VGA) || (pSiS->VGAEngine == SIS_315_VGA)) {
       inSISIDXREG(SISCR,0x32,pSiS->myCR32);
       inSISIDXREG(SISCR,0x36,pSiS->myCR36);
       inSISIDXREG(SISCR,0x37,pSiS->myCR37);
    }

    /* Handle panel delay compensation and emi, and evaluate options */
    SiSHandlePDCEMI(pScrn);

    /* In dual head mode, both heads (currently) share the maxxfbmem equally.
     * If memory sharing is done differently, the following has to be changed;
     * the other modules (eg. accel and Xv) use dhmOffset for hardware
     * pointer settings relative to VideoRAM start and won't need to be changed.
     *
     * Addendum: dhmoffset is also used for skipping the UMA area on SiS76x. So
     * DO NOT ONLY add it in dualhead mode cases!
     */

    pSiS->dhmOffset = pSiS->FbBaseOffset;
    pSiS->FbAddress += pSiS->dhmOffset;

#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
       pSiS->FbAddress = pSiS->realFbAddress;
       if(!pSiS->SecondHead) {
	  /* ===== First head (always CRT2) ===== */
	  /* We use only half of the memory available */
	  pSiS->maxxfbmem /= 2;
	  /* dhmOffset is 0 (or LFB-base for SiS760 UMA skipping) */
	  pSiS->FbAddress += pSiS->dhmOffset;
	  xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
	      "%dKB video RAM at 0x%lx available for master head (CRT2)\n",
	      pSiS->maxxfbmem/1024, pSiS->FbAddress);
       } else {
	  /* ===== Second head (always CRT1) ===== */
	  /* We use only half of the memory available */
	  pSiS->maxxfbmem /= 2;
	  /* Initialize dhmOffset */
	  pSiS->dhmOffset += pSiS->maxxfbmem;
	  /* Adapt FBAddress */
	  pSiS->FbAddress += pSiS->dhmOffset;
	  xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
	     "%dKB video RAM at 0x%lx available for slave head (CRT1)\n",
	     pSiS->maxxfbmem/1024,  pSiS->FbAddress);
       }
    }
#endif

    /* Note: Do not use availMem for anything from now. Use
     * maxxfbmem instead. (availMem does not take dual head
     * mode into account.)
     */

    if(pSiS->FbBaseOffset) {
       /* Doubt that the DRM memory manager can deal
        * with a heap start of 0...
	*/
       pSiS->DRIheapstart = 16;
       pSiS->DRIheapend = pSiS->FbBaseOffset;
    } else {
       pSiS->DRIheapstart = pSiS->maxxfbmem;
       pSiS->DRIheapend = pSiS->availMem;
    }
#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
       pSiS->DRIheapstart = pSiS->DRIheapend = 0;
    } else
#endif
           if(pSiS->DRIheapstart >= pSiS->DRIheapend) {
#if 0  /* For future use */
       xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	  "No memory for DRI heap. Please set the option \"MaxXFBMem\" to\n"
	  "\tlimit the memory X should use and leave the rest to DRI\n");
#endif
       pSiS->DRIheapstart = pSiS->DRIheapend = 0;
    }


    /* DDC/EDID handling */
    /* (DDC eventually uses the VBE. Make sure that our ptr is NULL) */
    pSiS->pVbe = NULL;

    /* Read DDC (for CRT1; if not present, CRT2) */
    SiSGetDDCAndEDID(pScrn);

    /* MergedFB: Setup CRT2 Monitor and read DDC */
#ifdef SISMERGED
    if(pSiS->MergedFB) {
       SiSMFBInitMergedFB(pScrn);
    }
#endif

    /* Copy our detected monitor gammas, part 1. Note that device redetection
     * is not supported in DHM, so there is no need to do that anytime later.
     */
#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
       if(!pSiS->SecondHead) {
          /* CRT2: Got gamma for LCD(A) or VGA2 */
	  pSiSEnt->CRT2VGAMonitorGamma = pSiS->CRT2VGAMonitorGamma;
       } else {
          /* CRT1: Got gamma for LCD(A) or VGA */
	  pSiSEnt->CRT1VGAMonitorGamma = pSiS->CRT1VGAMonitorGamma;
       }
       if(pSiS->CRT2LCDMonitorGamma) pSiSEnt->CRT2LCDMonitorGamma = pSiS->CRT2LCDMonitorGamma;
    }
#endif

    /* From here, we mainly deal with clocks and modes */

#ifdef SISMERGED
    if(pSiS->MergedFB) xf86DrvMsg(pScrn->scrnIndex, X_INFO, crtsetupstr, 1);
#endif


    /* Set up min and max pixel clock */
    SiSSetMinMaxPixelClock(pScrn);

    /* Setup the ClockRanges */
    clockRanges = xnfcalloc(sizeof(ClockRange), 1);
    clockRanges->next = NULL;
    clockRanges->minClock = pSiS->MinClock;
    clockRanges->maxClock = pSiS->MaxClock;
    clockRanges->clockIndex = -1;               /* programmable */
    clockRanges->interlaceAllowed = TRUE;
    clockRanges->doubleScanAllowed = TRUE;

    /* Replace default mode list */
    SiSReplaceModeList(pScrn, clockRanges, FALSE);

    /* Add our built-in hi-res and TV modes on the 6326 */
    SiS6326AddHiresAndTVModes(pScrn);

    /* Fixup HorizSync, VertRefresh ranges */
    crt1freqoverruled = SiSFixupHVRanges(pScrn, 1, FALSE);


    /* Screen size determination, display mode validation */

    {
       int minpitch, maxpitch, minheight, maxheight;
       pointer backupddc = pScrn->monitor->DDC;

       minpitch = 256;
       minheight = 128;
       switch(pSiS->VGAEngine) {
       case SIS_OLD_VGA:
       case SIS_530_VGA:
          maxpitch = 2040;
          maxheight = 2048;
          break;
       case SIS_300_VGA:
       case SIS_315_VGA:
          maxpitch = 4088;
          maxheight = 4096;
          break;
       default:
          maxpitch = 2048;
          maxheight = 2048;
          break;
       }

       /* Find out user-desired virtual screen size.
        * If "Virtual" is given, use that. Otherwise,
        * scan all modes named in the "Modes" list and
        * find out about the largest one. We then use
        * this as the virtual, but use RandR in our
        * CreateScreenResources wrapper to switch to
        * the size of the largest mode that survived
        * validation.
        * All this in order to be able to change the
        * size upon device hotplugging and redetection.
        * Since device hotplugging is not supported in
        * dual head mode, we can skip all this.
        * TODO: MergedFB mode. That's a real bugger as
        * it requires scanning through the MetaModes
        * list. Sigh. I think this can be done way
        * later since we eventually correct the
        * virtual screen size anyway. Hm.
        */


      /*
       * xf86ValidateModes will check that the mode HTotal and VTotal values
       * don't exceed the chipset's limit if pScrn->maxHValue and
       * pScrn->maxVValue are set. Since our SISValidMode() already takes
       * care of this, we don't worry about setting them here.
       */

       if(pSiS->VGAEngine == SIS_300_VGA || pSiS->VGAEngine == SIS_315_VGA) {
          xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	     "\"Unknown reason\" in the following list means that the mode\n");
          xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	     "is not supported on the chipset/bridge/current output device.\n");
       }
#ifdef SISMERGED
       pSiS->CheckForCRT2 = FALSE;
#endif

       /* Suppress bogus DDC warning */
       if(crt1freqoverruled) pScrn->monitor->DDC = NULL;

#if !defined(XORG_VERSION_CURRENT) && (XF86_VERSION_CURRENT >= XF86_VERSION_NUMERIC(4,5,0,0,0))
       /* XFree86 4.5+ thinks it's smart to automatically
        * add EDID modes to the monitor mode list. We do
        * not like this if we discarded all default and
        * user modes because they aren't suppored. Hence,
        * we clear the DDC pointer in that case (and live
        * with the disadvantage that we don't get any
        * DDC warnings.)
        */
       if(!pSiS->HaveCustomModes) {
          pScrn->monitor->DDC = NULL;
       }
#endif

       i = xf86ValidateModes(pScrn,
			pScrn->monitor->Modes,
			pScrn->display->modes,
			clockRanges,
			NULL,
			minpitch, maxpitch,
			pScrn->bitsPerPixel * 8,
			minheight, maxheight,
			pScrn->display->virtualX,
			pScrn->display->virtualY,
			pSiS->maxxfbmem,
			LOOKUP_BEST_REFRESH);

       pScrn->monitor->DDC = backupddc;
    }

    if(i == -1) {
       SISErrorLog(pScrn, "xf86ValidateModes() error\n");
       goto my_error_1;
    }

    /* Check the virtual screen against the available memory */
    {
       ULong memreq = pScrn->virtualX * (pScrn->bitsPerPixel >> 3) * pScrn->virtualY;

       if(memreq > pSiS->maxxfbmem) {
	  SISErrorLog(pScrn,
	     "Virtual screen too big for memory; %ldK needed, %ldK available\n",
	     memreq/1024, pSiS->maxxfbmem/1024);
	  goto my_error_1;
       }
    }

    /* Dual Head:
     * -) Go through mode list and mark all those modes as bad,
     *    which are unsuitable for dual head mode.
     * -) Find the highest used pixelclock on the master head.
     */
#ifdef SISDUALHEAD
    if((pSiS->DualHeadMode) && (!pSiS->SecondHead)) {

       pSiSEnt->maxUsedClock = SiSRemoveUnsuitableModes(pScrn, pScrn->modes, "dual head", FALSE);

    }
#endif

    /* Prune the modes marked as invalid */
    xf86PruneDriverModes(pScrn);

    if(i == 0 || pScrn->modes == NULL) {
       SISErrorLog(pScrn, "No valid modes found - check VertRefresh/HorizSync\n");
       goto my_error_1;
    }

    xf86SetCrtcForModes(pScrn, INTERLACE_HALVE_V);

    /* Clear the modes' Private field */
    SiSClearModesPrivate(pScrn->modes);

    /* Save virtualX/Y calculated by ValidateModes
     * and overwrite them with our values assumed to
     * be desired.
     * We let PreInit and ScreenInit run with these
     * in order to get a screen pixmap and root
     * window of the maximum (desired) size. After
     * ScreenInit() and after RandR and other extensions
     * have been initialized, we reset the values (also
     * in RandR's private) and call RRSetMode in order
     * to make them effective.
     */

    /* Set the current mode to the first in the list */
    pScrn->currentMode = pScrn->modes;

    /* Copy to CurrentLayout */
    pSiS->CurrentLayout.mode = pScrn->currentMode;
    pSiS->CurrentLayout.displayWidth = pScrn->displayWidth;
    pSiS->CurrentLayout.displayHeight = pScrn->virtualY;
#ifdef SISMERGED
    if(pSiS->MergedFB) {
       xf86DrvMsg(pScrn->scrnIndex, X_INFO, modesforstr, 1);
    }
#endif

    /* Print the list of modes being used */
    {
       Bool usemyprint = FALSE, printfreq = TRUE;

#ifdef SISDUALHEAD
       if(pSiS->DualHeadMode) {
	  if(pSiS->SecondHead) {
	     if(pSiS->VBFlags & CRT1_LCDA) usemyprint = TRUE;
	  } else {
	     if(pSiS->VBFlags & (CRT2_LCD | CRT2_TV)) {
	        usemyprint = TRUE;
	        if(!(pSiS->VBFlags2 & VB2_SISVGA2BRIDGE)) {
	           printfreq = FALSE;
	        }
	     }
	  }
       } else
#endif
#ifdef SISMERGED
       if(pSiS->MergedFB) {
	  if(pSiS->VBFlags & CRT1_LCDA) usemyprint = TRUE;
       } else
#endif
       {
	  if( (pSiS->VBFlags & (CRT2_LCD | CRT2_TV)) &&
	      (!(pSiS->VBFlags & DISPTYPE_DISP1)) )
	     usemyprint = TRUE;
       }

       if(usemyprint) {
	  SiSPrintModes(pScrn, printfreq);
       } else {
	  xf86PrintModes(pScrn);
       }
    }

    /* MergedFB: Setup CRT2, MetaModes and display size */
#ifdef SISMERGED
    if(pSiS->MergedFB) {
       SiSMFBHandleModesCRT2(pScrn, clockRanges);
    }

    if(pSiS->MergedFB) {
       SiSMFBMakeModeList(pScrn);
    }

    if(pSiS->MergedFB) {
       SiSMFBCorrectVirtualAndLayout(pScrn);
    }
#endif

    /* Don't need the clock ranges from here on */
    free(clockRanges);

    /* Set display resolution */
#ifdef SISMERGED
    if(pSiS->MergedFB) {
       SiSMFBSetDpi(pScrn, pSiS->CRT2pScrn, pSiS->CRT2Position);
    } else
#endif
    {
       {
          xf86SetDpi(pScrn, 0, 0);
          pSiS->SiSDPIVX = pScrn->virtualX;
          pSiS->SiSDPIVY = pScrn->virtualY;
       }
    }

    /* Load fb module */
    switch(pScrn->bitsPerPixel) {
      case 8:
      case 16:
      case 24:
      case 32:
	if(!xf86LoadSubModule(pScrn, "fb")) {
           SISErrorLog(pScrn, "Failed to load fb module");
	   goto my_error_1;
	}
	break;
      default:
	SISErrorLog(pScrn, "Unsupported framebuffer bpp (%d)\n", pScrn->bitsPerPixel);
	goto my_error_1;
    }

    /* Load XAA/EXA (if needed) */
    if(!pSiS->NoAccel) {
       char *modName = NULL;
#ifdef SIS_USE_XAA
       if(!pSiS->useEXA) {
	  modName = "xaa";
       }
#endif
#ifdef SIS_USE_EXA
       if(pSiS->useEXA) {
	  modName = "exa";
       }
#endif
       if(modName && (!xf86LoadSubModule(pScrn, modName))) {
	  SISErrorLog(pScrn, "Could not load %s module\n", modName);
	  pSiS->NoAccel = TRUE;
#ifdef SIS_USE_EXA
	  if(pSiS->useEXA) {
	     pSiS->NoXvideo = TRUE;
	  }
#endif
       }
    }

    /* Load shadowfb (if needed) */
    if(pSiS->ShadowFB) {
       if(!xf86LoadSubModule(pScrn, "shadowfb")) {
	  SISErrorLog(pScrn, "Could not load shadowfb module\n");
	  if(pSiS->ShadowFB) {
	     xf86DrvMsg(pScrn->scrnIndex, X_INFO, "ShadowFB support disabled\n");
	     pSiS->ShadowFB = FALSE;
	     pSiS->Rotate = pSiS->Reflect = 0;
	  }
       }
    }

    /* Load the dri and glx modules if requested. */
#ifdef SISDRI
    if(pSiS->loadDRI) {
       if(!xf86LoaderCheckSymbol("DRIScreenInit")) {
	  if(xf86LoadSubModule(pScrn, "dri")) {
	     if(!xf86LoaderCheckSymbol("GlxSetVisualConfigs")) {
	        if(! xf86LoadSubModule(pScrn, "glx")) {
		   SISErrorLog(pScrn, "Failed to load glx module\n");
		}
	     }
	  } else {
	     SISErrorLog(pScrn, "Failed to load dri module\n");
	  }
       }
	else 
		pSiS->DRIEnabled = TRUE;
    }
#endif

    /* Now load and initialize VBE module for VESA mode switching */
    pSiS->UseVESA = 0;
    if(pSiS->VESA == 1) {
       SiS_LoadInitVBE(pScrn);
       if(pSiS->pVbe) {
	  VbeInfoBlock *vbe;
	  if((vbe = VBEGetVBEInfo(pSiS->pVbe))) {
	     pSiS->vesamajor = (unsigned)(vbe->VESAVersion >> 8);
	     pSiS->vesaminor = vbe->VESAVersion & 0xff;
	     SiSBuildVesaModeList(pScrn, pSiS->pVbe, vbe);
	     VBEFreeVBEInfo(vbe);
	     pSiS->UseVESA = 1;
	  } else {
	     xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
	     	 "Failed to read VBE Info Block\n");
	  }
       }
       if(pSiS->UseVESA == 0) {
	  xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
	      "VESA mode switching disabled.\n");
       }
    }

    if(pSiS->pVbe) {
       vbeFree(pSiS->pVbe);
       pSiS->pVbe = NULL;
    }

#ifdef SISDUALHEAD
    xf86SetPrimInitDone(pScrn->entityList[0]);
#endif

    sisRestoreExtRegisterLock(pSiS, srlockReg, crlockReg);

    if(pSiS->pInt) xf86FreeInt10(pSiS->pInt);
    pSiS->pInt = NULL;

    /* Set up flags */
    if(pSiS->VGAEngine == SIS_315_VGA)
       pSiS->SiS_SD_Flags |= SiS_SD_SUPPORTXVGAMMA1;

#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
	pSiS->SiS_SD_Flags |= SiS_SD_ISDUALHEAD;
	if(pSiS->SecondHead) pSiS->SiS_SD_Flags |= SiS_SD_ISDHSECONDHEAD;
	else		     pSiS->SiS_SD_Flags &= ~(SiS_SD_SUPPORTXVGAMMA1);
#ifdef PANORAMIX
	if(!noPanoramiXExtension) {
	   pSiS->SiS_SD_Flags |= SiS_SD_ISDHXINERAMA;
	}
#endif
    }
#endif

#ifdef SISMERGED
    if(pSiS->MergedFB)
       pSiS->SiS_SD_Flags |= SiS_SD_ISMERGEDFB;
#endif

    /* Try to determine if this is a laptop   */
    /* (only used for SiSCtrl visualisations) */
    pSiS->SiS_SD2_Flags |= SiS_SD2_SUPPLTFLAG;
    pSiS->SiS_SD2_Flags &= ~SiS_SD2_ISLAPTOP;
    if(pSiS->detectedCRT2Devices & CRT2_LCD) {
       if(pSiS->VBFlags2 & (VB2_SISLVDSBRIDGE | VB2_LVDS | VB2_30xBDH)) {
	  /* 1. By bridge type: LVDS in 99% of all cases;
	   * exclude unusual setups like Barco projectors
	   * and parallel flat panels. TODO: Exclude
	   * Sony W1, V1.
	   */
	  if((pSiS->SiS_Pr->SiS_CustomT != CUT_BARCO1366) &&
	     (pSiS->SiS_Pr->SiS_CustomT != CUT_BARCO1024) &&
	     (pSiS->SiS_Pr->SiS_CustomT != CUT_PANEL848)  &&
	     (pSiS->SiS_Pr->SiS_CustomT != CUT_PANEL856)  &&
	     (pSiS->SiS_Pr->SiS_CustomT != CUT_AOP8060)   &&
	     ( (pSiS->ChipType != SIS_550) ||
	       (!pSiS->DSTN && !pSiS->FSTN) ) ) {
	     pSiS->SiS_SD2_Flags |= SiS_SD2_ISLAPTOP;
	  }
       } else if((pSiS->VBFlags2 & (VB2_301 | VB2_301C)) &&
                 (pSiS->VBLCDFlags & (VB_LCD_1280x960  |
				      VB_LCD_1400x1050 |
				      VB_LCD_1024x600  |
				      VB_LCD_1280x800  |
				      VB_LCD_1280x854))) {
	  /* 2. By (odd) LCD resolutions on TMDS bridges
	   * (eg Averatec). TODO: Exclude IBM Netvista.
	   */
	  pSiS->SiS_SD2_Flags |= SiS_SD2_ISLAPTOP;
       }
    }

    if(pSiS->enablesisctrl)
       pSiS->SiS_SD_Flags |= SiS_SD_ENABLED;

    /* Back up currentMode and VBFlags */
    pSiS->currentModeLast = pScrn->currentMode;
    pSiS->VBFlagsInit = pSiS->VBFlags;
    pSiS->VBFlags3Init = pSiS->VBFlags3;

  /*hot-key switch state flag during booting record here.*/
    if((pSiS->VBFlags & SINGLE_MODE) && (pSiS->VBFlags & DISPTYPE_DISP2))
    {
        pSiS->Hkey_Device_Switch_State = LCD_only;  
    }	
    else
    {	if(pSiS->VBFlags & MIRROR_MODE)
	{
	    pSiS->Hkey_Device_Switch_State = LCD_VGA_mirror;
	}
    }	
    pSiS->suspended = FALSE;
    	
    /*xf86DrvMsg(0,X_INFO,"CurrentMode=%d. \n",pSiS->Hkey_Device_Switch_State);
    xf86DrvMsg(0,X_INFO,"Init_VBFlags=0X%x. \n",pSiS->VBFlags);*/
    
    return TRUE;

    /* ---- */

my_error_1:
    sisRestoreExtRegisterLock(pSiS, srlockReg, crlockReg);
my_error_0:
#ifdef SISDUALHEAD
    if(pSiSEnt) pSiSEnt->ErrorAfterFirst = TRUE;
#endif
    if(pSiS->pInt) xf86FreeInt10(pSiS->pInt);
    pSiS->pInt = NULL;
    SISFreeRec(pScrn);
    return FALSE;
}

/*************************************************/
/*             Map/unmap memory, mmio            */
/*************************************************/

/*
 * Map I/O port area for non-PC platforms
 */
#ifdef SIS_NEED_MAP_IOP
static Bool
SISMapIOPMem(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
    SISEntPtr pSiSEnt = pSiS->entityPrivate;

    if(pSiS->DualHeadMode) {
        pSiSEnt->MapCountIOPBase++;
        if(!(pSiSEnt->IOPBase)) {
	     /* Only map if not mapped previously */
#ifndef XSERVER_LIBPCIACCESS
	     pSiSEnt->IOPBase = xf86MapPciMem(pScrn->scrnIndex, VIDMEM_MMIO,
			pSiS->PciTag, pSiS->IOPAddress, 128);
#else
	     {
	       void **result = (void **)&pSiSEnt->IOPBase;
	       int err = pci_device_map_range(pSiS->PciInfo,
					      pSiS->IOPAddress,
					      128,
					      PCI_DEV_MAP_FLAG_WRITABLE,
					      result);

	       if (err) {
                 xf86DrvMsg (pScrn->scrnIndex, X_ERROR,
                             "Unable to map IO aperture. %s (%d)\n",
                             strerror (err), err);
	       }
	     }
#endif
        }
        pSiS->IOPBase = pSiSEnt->IOPBase;
    } else
#endif
#ifndef XSERVER_LIBPCIACCESS
	     pSiS->IOPBase = xf86MapPciMem(pScrn->scrnIndex, VIDMEM_MMIO,
			pSiS->PciTag, pSiS->IOPAddress, 128);
#else
	     {
	       void **result = (void **)&pSiS->IOPBase;
	       int err = pci_device_map_range(pSiS->PciInfo,
					      pSiS->IOPAddress,
					      128,
					      PCI_DEV_MAP_FLAG_WRITABLE,
					      result);

	       if (err) {
                 xf86DrvMsg (pScrn->scrnIndex, X_ERROR,
                             "Unable to map IO aperture. %s (%d)\n",
                             strerror (err), err);
	       }
	     }
#endif
    if(pSiS->IOPBase == NULL) {
	SISErrorLog(pScrn, "Could not map I/O port area\n");
	return FALSE;
    }

    return TRUE;
}

static Bool
SISUnmapIOPMem(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
    SISEntPtr pSiSEnt = pSiS->entityPrivate;;
#endif

/* In dual head mode, we must not unmap if the other head still
 * assumes memory as mapped
 */
#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
        if(pSiSEnt->MapCountIOPBase) {
	    pSiSEnt->MapCountIOPBase--;
	    if((pSiSEnt->MapCountIOPBase == 0) || (pSiSEnt->forceUnmapIOPBase)) {
#ifndef XSERVER_LIBPCIACCESS
                xf86UnMapVidMem(pScrn->scrnIndex, (pointer)pSiSEnt->IOPBase, 2048);
#else
                (void) pci_device_unmap_legacy(pSiS->PciInfo, (pointer)pSiSEnt->IOPBase, 2048);
#endif
		pSiSEnt->IOPBase = NULL;
		pSiSEnt->MapCountIOPBase = 0;
		pSiSEnt->forceUnmapIOPBase = FALSE;
	    }
	    pSiS->IOPBase = NULL;
	}
    } else {
#endif
#ifndef XSERVER_LIBPCIACCESS
	xf86UnMapVidMem(pScrn->scrnIndex, (pointer)pSiS->IOPBase, 2048);
#else
        (void) pci_device_unmap_legacy(pSiS->PciInfo, (pointer)pSiS->IOPBase, 2048);
#endif
	pSiS->IOPBase = NULL;
#ifdef SISDUALHEAD
    }
#endif
    return TRUE;
}
#endif

/*
 * Map the framebuffer and MMIO memory
 */

static Bool
SISMapMem(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
#ifndef XSERVER_LIBPCIACCESS
    int mmioFlags = VIDMEM_MMIO;
#endif
#ifdef SISDUALHEAD
    SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

    /*
     * Map IO registers to virtual address space
     * (For Alpha, we need to map SPARSE memory, since we need
     * byte/short access.)
     */
#if defined(__alpha__)
    mmioFlags |= VIDMEM_SPARSE;
#endif

#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
        pSiSEnt->MapCountIOBase++;
        if(!(pSiSEnt->IOBase)) {
	     /* Only map if not mapped previously */
#ifndef XSERVER_LIBPCIACCESS
    	     pSiSEnt->IOBase = xf86MapPciMem(pScrn->scrnIndex, mmioFlags,
                         pSiS->PciTag, pSiS->IOAddress, (pSiS->mmioSize * 1024));
#else
	     void **result = (void **)&pSiSEnt->IOBase;
	     int err = pci_device_map_range(pSiS->PciInfo,
 	                                    pSiS->IOAddress,
	                                    (pSiS->mmioSize * 1024),
                                            PCI_DEV_MAP_FLAG_WRITABLE,
                                            result);

             if (err) {
                 xf86DrvMsg (pScrn->scrnIndex, X_ERROR,
                             "Unable to map IO aperture. %s (%d)\n",
                             strerror (err), err);
	     }
#endif
        }
        pSiS->IOBase = pSiSEnt->IOBase;
    } else
#endif
#ifndef XSERVER_LIBPCIACCESS
    	pSiS->IOBase = xf86MapPciMem(pScrn->scrnIndex, mmioFlags,
                        pSiS->PciTag, pSiS->IOAddress, (pSiS->mmioSize * 1024));
#else
       {
	     void **result = (void **)&pSiS->IOBase;
	     int err = pci_device_map_range(pSiS->PciInfo,
 	                                    pSiS->IOAddress,
	                                    (pSiS->mmioSize * 1024),
                                            PCI_DEV_MAP_FLAG_WRITABLE,
                                            result);

             if (err) {
                 xf86DrvMsg (pScrn->scrnIndex, X_ERROR,
                             "Unable to map IO aperture. %s (%d)\n",
                             strerror (err), err);
	     }
       }
#endif

    if(pSiS->IOBase == NULL) {
    	SISErrorLog(pScrn, "Could not map MMIO area\n");
        return FALSE;
    }

#ifdef __alpha__
    /*
     * for Alpha, we need to map DENSE memory as well, for
     * setting CPUToScreenColorExpandBase.
     */
#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
        pSiSEnt->MapCountIOBaseDense++;
        if(!(pSiSEnt->IOBaseDense)) {
	     /* Only map if not mapped previously */
#ifndef XSERVER_LIBPCIACCESS
	     pSiSEnt->IOBaseDense = xf86MapPciMem(pScrn->scrnIndex, VIDMEM_MMIO,
                    pSiS->PciTag, pSiS->IOAddress, (pSiS->mmioSize * 1024));
#else
	     void **result = (void **)&pSiSEnt->IOBaseDense;
	     int err = pci_device_map_range(pSiS->PciInfo,
 	                                    pSiS->IOAddress,
	                                    (pSiS->mmioSize * 1024),
                                            PCI_DEV_MAP_FLAG_WRITABLE,
                                            result);

             if (err) {
                 xf86DrvMsg (pScrn->scrnIndex, X_ERROR,
                             "Unable to map IO dense aperture. %s (%d)\n",
                             strerror (err), err);
#endif
	}
	pSiS->IOBaseDense = pSiSEnt->IOBaseDense;
    } else
#endif
#ifndef XSERVER_LIBPCIACCESS
    	pSiS->IOBaseDense = xf86MapPciMem(pScrn->scrnIndex, VIDMEM_MMIO,
                    pSiS->PciTag, pSiS->IOAddress, (pSiS->mmioSize * 1024));
#else
	     void **result = (void **)&pSiS->IOBaseDense;
	     int err = pci_device_map_range(pSiS->PciInfo,
 	                                    pSiS->IOAddress,
	                                    (pSiS->mmioSize * 1024),
                                            PCI_DEV_MAP_FLAG_WRITABLE,
                                            result);

             if (err) {
                 xf86DrvMsg (pScrn->scrnIndex, X_ERROR,
                             "Unable to map IO dense aperture. %s (%d)\n",
                             strerror (err), err);
#endif

    if(pSiS->IOBaseDense == NULL) {
       SISErrorLog(pScrn, "Could not map MMIO dense area\n");
       return FALSE;
    }
#endif /* __alpha__ */

#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
	pSiSEnt->MapCountFbBase++;
	if(!(pSiSEnt->FbBase)) {
	     /* Only map if not mapped previously */
#ifndef XSERVER_LIBPCIACCESS
	     pSiSEnt->FbBase = pSiSEnt->RealFbBase =
			xf86MapPciMem(pScrn->scrnIndex, VIDMEM_FRAMEBUFFER,
			 pSiS->PciTag, (ULong)pSiS->realFbAddress,
			 pSiS->FbMapSize);
#else
         int err = pci_device_map_range(pSiS->PciInfo,
                                   (ULong)pSiS->realFbAddress,
                                   pSiS->FbMapSize,
                                   PCI_DEV_MAP_FLAG_WRITABLE |
                                   PCI_DEV_MAP_FLAG_WRITE_COMBINE,
                                   (void *)&pSiSEnt->FbBase);
	if (err) {
            xf86DrvMsg (pScrn->scrnIndex, X_ERROR,
                        "Unable to map FB aperture. %s (%d)\n",
                        strerror (err), err);
            return FALSE;
        }
	pSiSEnt->RealFbBase = pSiSEnt->FbBase;
#endif
	}
	pSiS->FbBase = pSiS->RealFbBase = pSiSEnt->FbBase;
	/* Adapt FbBase (for DHM and SiS76x UMA skipping; dhmOffset is 0 otherwise) */
	pSiS->FbBase += pSiS->dhmOffset;
    } else {
#endif

#ifndef XSERVER_LIBPCIACCESS
	pSiS->FbBase = pSiS->RealFbBase =
		xf86MapPciMem(pScrn->scrnIndex, VIDMEM_FRAMEBUFFER,
			 pSiS->PciTag, (ULong)pSiS->realFbAddress,
			 pSiS->FbMapSize);
#else
         int err = pci_device_map_range(pSiS->PciInfo,
                                   (ULong)pSiS->realFbAddress,
                                   pSiS->FbMapSize,
                                   PCI_DEV_MAP_FLAG_WRITABLE |
                                   PCI_DEV_MAP_FLAG_WRITE_COMBINE,
                                   (void *)&pSiS->FbBase);
	if (err) {
            xf86DrvMsg (pScrn->scrnIndex, X_ERROR,
                        "Unable to map FB aperture. %s (%d)\n",
                        strerror (err), err);
            return FALSE;
        }
	pSiS->RealFbBase = pSiS->FbBase;
#endif
	pSiS->FbBase += pSiS->dhmOffset;
#ifdef SISDUALHEAD
    }
#endif

    if(pSiS->FbBase == NULL) {
       SISErrorLog(pScrn, "Could not map framebuffer area\n");
       return FALSE;
    }

#ifdef TWDEBUG
    xf86DrvMsg(0, 0, "Framebuffer mapped to %p\n", pSiS->FbBase);
#endif

    return TRUE;
}


/*
 * Unmap the framebuffer and MMIO memory.
 */

static Bool
SISUnmapMem(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
    SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

/* In dual head mode, we must not unmap if the other head still
 * assumes memory as mapped
 */
#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
        if(pSiSEnt->MapCountIOBase) {
	    pSiSEnt->MapCountIOBase--;
	    if((pSiSEnt->MapCountIOBase == 0) || (pSiSEnt->forceUnmapIOBase)) {
#ifndef XSERVER_LIBPCIACCESS
		xf86UnMapVidMem(pScrn->scrnIndex, (pointer)pSiSEnt->IOBase, (pSiS->mmioSize * 1024));
#else
                (void) pci_device_unmap_legacy(pSiS->PciInfo, (pointer)pSiSEnt->IOBase, (pSiS->mmioSize * 1024));
#endif
		pSiSEnt->IOBase = NULL;
		pSiSEnt->MapCountIOBase = 0;
		pSiSEnt->forceUnmapIOBase = FALSE;
	    }
	    pSiS->IOBase = NULL;
	}
#ifdef __alpha__
	if(pSiSEnt->MapCountIOBaseDense) {
	    pSiSEnt->MapCountIOBaseDense--;
	    if((pSiSEnt->MapCountIOBaseDense == 0) || (pSiSEnt->forceUnmapIOBaseDense)) {
#ifndef XSERVER_LIBPCIACCESS
		xf86UnMapVidMem(pScrn->scrnIndex, (pointer)pSiSEnt->IOBaseDense, (pSiS->mmioSize * 1024));
#else
                (void) pci_device_unmap_legacy(pSiS->PciInfo, (pointer)pSiSEnt->IOBaseDense, (pSiS->mmioSize * 1024));
#endif
		pSiSEnt->IOBaseDense = NULL;
		pSiSEnt->MapCountIOBaseDense = 0;
		pSiSEnt->forceUnmapIOBaseDense = FALSE;
	    }
	    pSiS->IOBaseDense = NULL;
	}
#endif /* __alpha__ */
	if(pSiSEnt->MapCountFbBase) {
	    pSiSEnt->MapCountFbBase--;
	    if((pSiSEnt->MapCountFbBase == 0) || (pSiSEnt->forceUnmapFbBase)) {
#ifndef XSERVER_LIBPCIACCESS
		xf86UnMapVidMem(pScrn->scrnIndex, (pointer)pSiSEnt->RealFbBase, pSiS->FbMapSize);
#else
                (void) pci_device_unmap_legacy(pSiS->PciInfo, (pointer)pSiSEnt->RealFbBase, pSiS->FbMapSize);
#endif
		pSiSEnt->FbBase = pSiSEnt->RealFbBase = NULL;
		pSiSEnt->MapCountFbBase = 0;
		pSiSEnt->forceUnmapFbBase = FALSE;

	    }
	    pSiS->FbBase = pSiS->RealFbBase = NULL;
	}
    } else {
#endif
#ifndef XSERVER_LIBPCIACCESS
	xf86UnMapVidMem(pScrn->scrnIndex, (pointer)pSiS->IOBase, (pSiS->mmioSize * 1024));
#else
        (void) pci_device_unmap_legacy(pSiS->PciInfo, (pointer)pSiS->IOBase, (pSiS->mmioSize * 1024));
#endif
	pSiS->IOBase = NULL;
#ifdef __alpha__
#ifndef XSERVER_LIBPCIACCESS
	xf86UnMapVidMem(pScrn->scrnIndex, (pointer)pSiS->IOBaseDense, (pSiS->mmioSize * 1024));
#else
        (void) pci_device_unmap_legacy(pSiS->PciInfo, (pointer)pSiS->IOBaseDense, (pSiS->mmioSize * 1024));
#endif
	pSiS->IOBaseDense = NULL;
#endif
#ifndef XSERVER_LIBPCIACCESS
	xf86UnMapVidMem(pScrn->scrnIndex, (pointer)pSiS->RealFbBase, pSiS->FbMapSize);
#else
        (void) pci_device_unmap_legacy(pSiS->PciInfo, (pointer)pSiS->RealFbBase, pSiS->FbMapSize);
#endif
	pSiS->FbBase = pSiS->RealFbBase = NULL;
#ifdef SISDUALHEAD
    }
#endif
    return TRUE;
}

/*******************************************************/
/*                       Various                       */
/*******************************************************/

/* Check if video bridge is in slave mode */
Bool
SiSBridgeIsInSlaveMode(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
    UChar  usScrP1_00;

    if(!(pSiS->VBFlags2 & VB2_VIDEOBRIDGE))
       return FALSE;

    inSISIDXREG(SISPART1,0x00,usScrP1_00);
    if( ((pSiS->VGAEngine == SIS_300_VGA) && (usScrP1_00 & 0xa0) == 0x20) ||
        ((pSiS->VGAEngine == SIS_315_VGA) && (usScrP1_00 & 0x50) == 0x10) ) {
       return TRUE;
    }

    return FALSE;
}

/* Calc dotclock from registers */
static int
SiSGetClockFromRegs(UChar sr2b, UChar sr2c)
{
   float num, denum, postscalar, divider;
   int   myclock;

   divider = (sr2b & 0x80) ? 2.0 : 1.0;
   postscalar = (sr2c & 0x80) ?
              ( (((sr2c >> 5) & 0x03) == 0x02) ? 6.0 : 8.0 ) :
	      ( ((sr2c >> 5) & 0x03) + 1.0 );
   num = (sr2b & 0x7f) + 1.0;
   denum = (sr2c & 0x1f) + 1.0;
   myclock = (int)((14318 * (divider / postscalar) * (num / denum)) / 1000);
   return myclock;
}

/* Wait for retrace */
void
SISWaitRetraceCRT1(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
   int    watchdog;
   UChar  temp;

   inSISIDXREG(SISCR,0x17,temp);
   if(!(temp & 0x80)) return;

   inSISIDXREG(SISSR,0x1f,temp);
   if(temp & 0xc0) return;

   watchdog = 65536;
   while((inSISREG(SISINPSTAT) & 0x08) && --watchdog);
   watchdog = 65536;
   while((!(inSISREG(SISINPSTAT) & 0x08)) && --watchdog);
}

void
SISWaitRetraceCRT2(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
   int    watchdog;
   UChar  temp, reg;

   if(SiSBridgeIsInSlaveMode(pScrn)) {
      SISWaitRetraceCRT1(pScrn);
      return;
   }

   switch(pSiS->VGAEngine) {
   case SIS_300_VGA:
   	reg = 0x25;
	break;
   case SIS_315_VGA:
   	reg = 0x30;
	break;
   default:
        return;
   }

   watchdog = 65536;
   do {
   	inSISIDXREG(SISPART1, reg, temp);
	if(!(temp & 0x02)) break;
   } while(--watchdog);
   watchdog = 65536;
   do {
   	inSISIDXREG(SISPART1, reg, temp);
	if(temp & 0x02) break;
   } while(--watchdog);
}

static void
SISWaitVBRetrace(ScrnInfoPtr pScrn)
{
   SISPtr  pSiS = SISPTR(pScrn);

   if((pSiS->VGAEngine == SIS_300_VGA) || (pSiS->VGAEngine == SIS_315_VGA)) {
#ifdef SISDUALHEAD
      if(pSiS->DualHeadMode) {
   	 if(pSiS->SecondHead)
	    SISWaitRetraceCRT1(pScrn);
         else
	    SISWaitRetraceCRT2(pScrn);
      } else {
#endif
	 if(pSiS->VBFlags & DISPTYPE_DISP1) {
	    SISWaitRetraceCRT1(pScrn);
	 }
	 if(pSiS->VBFlags & DISPTYPE_DISP2) {
	    if(!(SiSBridgeIsInSlaveMode(pScrn))) {
	       SISWaitRetraceCRT2(pScrn);
	    }
	 }
#ifdef SISDUALHEAD
      }
#endif
   } else {
      SISWaitRetraceCRT1(pScrn);
   }
}

/* Enable the Turboqueue/Commandqueue (For 300 series and later only) */
static void
SiSEnableTurboQueue(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
    UShort SR26, SR27;
    ULong  temp;

    switch(pSiS->VGAEngine) {
	case SIS_300_VGA:
	   if((!pSiS->NoAccel) && (pSiS->TurboQueue)) {
		/* TQ size is always 512k */
		temp = (pScrn->videoRam/64) - 8;
		SR26 = temp & 0xFF;
		inSISIDXREG(SISSR, 0x27, SR27);
		SR27 &= 0xFC;
		SR27 |= (0xF0 | ((temp >> 8) & 3));
		outSISIDXREG(SISSR, 0x26, SR26);
		outSISIDXREG(SISSR, 0x27, SR27);
	   }
	   break;

	case SIS_315_VGA:
	   if(!pSiS->NoAccel) {
	      /* On 315/330/340/350 series, there are three queue modes
	       * available which are chosen by setting bits 7:5 in SR26:
	       * 1. MMIO queue mode (bit 5, 0x20). The hardware will keep
	       *    track of the queue, the FIFO, command parsing and so
	       *    on. This is the one comparable to the 300 series.
	       * 2. VRAM queue mode (bit 6, 0x40). In this case, one will
	       *    have to do queue management himself.
	       * 3. AGP queue mode (bit 7, 0x80). Works as 2., but keeps the
	       *    queue in AGP memory space.
	       * We go VRAM or MMIO here.
	       * SR26 bit 4 is called "Bypass H/W queue".
	       * SR26 bit 1 is called "Enable Command Queue Auto Correction"
	       * SR26 bit 0 resets the queue
	       * Size of queue memory is encoded in bits 3:2 like this:
	       *    00  (0x00)  512K
	       *    01  (0x04)  1M
	       *    10  (0x08)  2M
	       *    11  (0x0C)  4M
	       * The queue location is to be written to 0x85C0.
	       */
#ifdef SISVRAMQ
	      /* We use VRAM Cmd Queue, not MMIO or AGP */
	      UChar tempCR55 = 0;

	      /* Set Command Queue Threshold to max value 11111b (?) */
	      outSISIDXREG(SISSR, 0x27, 0x1F);

	      /* Disable queue flipping */
	      inSISIDXREG(SISCR, 0x55, tempCR55);
	      andSISIDXREG(SISCR, 0x55, 0x33);
	      /* Synchronous reset for Command Queue */
	      outSISIDXREG(SISSR, 0x26, 0x01);
	      SIS_MMIO_OUT32(pSiS->IOBase, 0x85c4, 0);
	      /* Enable VRAM Command Queue mode */
	      if(pSiS->ChipType == XGI_20) {
		 /* On XGI_20, always 128K */
		 SR26 = 0x40 | 0x04 | 0x01;
	      } else {
	         switch(pSiS->cmdQueueSize) {
		    case 1*1024*1024: SR26 = (0x40 | 0x04 | 0x01); break;
		    case 2*1024*1024: SR26 = (0x40 | 0x08 | 0x01); break;
		    case 4*1024*1024: SR26 = (0x40 | 0x0C | 0x01); break;
		    default:
		                      pSiS->cmdQueueSize = 512 * 1024;
		    case    512*1024: SR26 = (0x40 | 0x00 | 0x01);
	         }
	      }
	      outSISIDXREG(SISSR, 0x26, SR26);
	      SR26 &= 0xfe;
	      outSISIDXREG(SISSR, 0x26, SR26);
	      *(pSiS->cmdQ_SharedWritePort) = (unsigned int)(SIS_MMIO_IN32(pSiS->IOBase, 0x85c8));
	      SIS_MMIO_OUT32(pSiS->IOBase, 0x85c4, (CARD32)(*(pSiS->cmdQ_SharedWritePort)));
	      SIS_MMIO_OUT32(pSiS->IOBase, 0x85C0, pSiS->cmdQueueOffset);
	      temp = (ULong)pSiS->RealFbBase;
#ifdef SISDUALHEAD
	      if(pSiS->DualHeadMode) {
	         SISEntPtr pSiSEnt = pSiS->entityPrivate;
	         temp = (ULong)pSiSEnt->RealFbBase;
	      }
#endif
	      temp += pSiS->cmdQueueOffset;
	      pSiS->cmdQueueBase = (unsigned int *)temp;
	      outSISIDXREG(SISCR, 0x55, tempCR55);
#ifdef TWDEBUG
	      xf86DrvMsg(0, 0, "CmdQueueOffs 0x%x, CmdQueueAdd %p, shwrp 0x%x, status %x, base %p\n",
		pSiS->cmdQueueOffset, pSiS->cmdQueueBase, *(pSiS->cmdQ_SharedWritePort),
		SIS_MMIO_IN32(pSiS->IOBase, 0x85cc), (ULong *)temp);
#endif
#else
	      /* For MMIO */
	      /* Syncronous reset for Command Queue */
	      orSISIDXREG(SISSR, 0x26, 0x01);
	      /* Set Command Queue Threshold to max value 11111b */
	      outSISIDXREG(SISSR, 0x27, 0x1F);
	      /* Do some magic (cp readport to writeport) */
	      temp = SIS_MMIO_IN32(pSiS->IOBase, 0x85C8);
	      SIS_MMIO_OUT32(pSiS->IOBase, 0x85C4, temp);
	      /* Enable MMIO Command Queue mode (0x20),
	       * Enable_command_queue_auto_correction (0x02)
	       *        (no idea, but sounds good, so use it)
	       * 512k (0x00) (does this apply to MMIO mode?) */
	      outSISIDXREG(SISSR, 0x26, 0x22);
	      /* Calc Command Queue position (Q is always 512k)*/
	      temp = (pScrn->videoRam - 512) * 1024;
	      /* Set Q position */
	      SIS_MMIO_OUT32(pSiS->IOBase, 0x85C0, temp);
#endif
	   }
	   break;
	default:
	   break;
    }
}

#ifdef SISVRAMQ
static void
SiSRestoreQueueMode(SISPtr pSiS, SISRegPtr sisReg)
{
    UChar tempCR55=0;

    if(pSiS->VGAEngine == SIS_315_VGA) {
       inSISIDXREG(SISCR,0x55,tempCR55);
       andSISIDXREG(SISCR,0x55,0x33);
       outSISIDXREG(SISSR,0x26,0x01);
       SIS_MMIO_OUT32(pSiS->IOBase, 0x85c4, 0);
       outSISIDXREG(SISSR,0x27,sisReg->sisRegs3C4[0x27]);
       outSISIDXREG(SISSR,0x26,sisReg->sisRegs3C4[0x26]);
       SIS_MMIO_OUT32(pSiS->IOBase, 0x85C0, sisReg->sisMMIO85C0);
       outSISIDXREG(SISCR,0x55,tempCR55);
    }
}
#endif

/* Calculate the vertical refresh rate from a mode */
float
SiSCalcVRate(DisplayModePtr mode)
{
   float hsync, refresh = 0;

   if(mode->HSync > 0.0)
       	hsync = mode->HSync;
   else if(mode->HTotal > 0)
       	hsync = (float)mode->Clock / (float)mode->HTotal;
   else
       	hsync = 0.0;

   if(mode->VTotal > 0)
       	refresh = hsync * 1000.0 / mode->VTotal;

   if(mode->Flags & V_INTERLACE)
       	refresh *= 2.0;

   if(mode->Flags & V_DBLSCAN)
       	refresh /= 2.0;

   if(mode->VScan > 1)
        refresh /= mode->VScan;

   if(mode->VRefresh > 0.0)
	refresh = mode->VRefresh;

   if(hsync == 0.0 || refresh == 0.0) return 0.0;

   return refresh;
}

/* Calculate CR33 (rate index) for CRT1.
 * Calculation is done using currentmode, therefore it is
 * recommended to set VertRefresh and HorizSync to correct
 * values in config file.
 */
UChar
SISSearchCRT1Rate(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
   SISPtr  pSiS = SISPTR(pScrn);
   int     i = 0, irefresh;
   UShort  xres = mode->HDisplay;
   UShort  yres = mode->VDisplay;
   UChar   index, defindex;
   Bool    checksis730 = FALSE;

   defindex = (xres == 800 || xres == 1024 || xres == 1280) ? 0x02 : 0x01;

   irefresh = (int)SiSCalcVRate(mode);
   if(!irefresh) return defindex;

   /* SiS730 has troubles on CRT2 if CRT1 is at 32bpp */
   if( (pSiS->ChipType == SIS_730)        &&
       (pSiS->VBFlags2 & VB2_VIDEOBRIDGE) &&
       (pSiS->CurrentLayout.bitsPerPixel == 32) ) {
#ifdef SISDUALHEAD
      if(pSiS->DualHeadMode) {
         if(pSiS->SecondHead) {
	    checksis730 = TRUE;
	 }
      } else
#endif
      if((!pSiS->UseVESA) && (pSiS->VBFlags & CRT2_ENABLE) && (!pSiS->CRT1off)) {
         checksis730 = TRUE;
      }
   }

#ifdef TWDEBUG
   xf86DrvMsg(0, X_INFO, "Debug: CalcVRate returned %d\n", irefresh);
#endif

   /* We need the REAL refresh rate here */
   if(mode->Flags & V_INTERLACE) irefresh /= 2;

   /* Do not multiply by 2 when DBLSCAN! */

#ifdef TWDEBUG
   xf86DrvMsg(0, X_INFO, "Debug: Rate after correction = %d\n", irefresh);
#endif

   index = 0;
   while((sisx_vrate[i].idx != 0) && (sisx_vrate[i].xres <= xres)) {
      if((sisx_vrate[i].xres == xres) && (sisx_vrate[i].yres == yres)) {
	 if((checksis730 == FALSE) || (sisx_vrate[i].SiS730valid32bpp == TRUE)) {
	    if(sisx_vrate[i].refresh == irefresh) {
	       index = sisx_vrate[i].idx;
	       break;
	    } else if(sisx_vrate[i].refresh > irefresh) {
	       if((sisx_vrate[i].refresh - irefresh) <= 3) {
		  index = sisx_vrate[i].idx;
	       } else if( ((checksis730 == FALSE) || (sisx_vrate[i - 1].SiS730valid32bpp == TRUE)) &&
		          ((irefresh - sisx_vrate[i - 1].refresh) <=  2) &&
			  (sisx_vrate[i].idx != 1) ) {
		  index = sisx_vrate[i - 1].idx;
	       }
	       break;
	    } else if((irefresh - sisx_vrate[i].refresh) <= 2) {
	       index = sisx_vrate[i].idx;
	       break;
	    }
	 }
      }
      i++;
   }

   if(index > 0) return index;
   else          return defindex;
}

/* Restore CR32, CR36, CR37 (= our detection results) */
static void
SiSRestoreCR323637(SISPtr pSiS)
{
    if((pSiS->VGAEngine == SIS_300_VGA) || (pSiS->VGAEngine == SIS_315_VGA)) {
       outSISIDXREG(SISCR,0x32,pSiS->myCR32);
       outSISIDXREG(SISCR,0x36,pSiS->myCR36);
       outSISIDXREG(SISCR,0x37,pSiS->myCR37);
    }
}

/*******************************************************/
/*                        Save()                       */
/*******************************************************/


/* VESASaveRestore taken from vesa driver */
static void
SISVESASaveRestore(ScrnInfoPtr pScrn, vbeSaveRestoreFunction function)
{
    SISPtr pSiS = SISPTR(pScrn);

    /* Query amount of memory to save state */
    if((function == MODE_QUERY) ||
       (function == MODE_SAVE && pSiS->state == NULL)) {

       /* Make sure we save at least this information in case of failure */
       (void)VBEGetVBEMode(pSiS->pVbe, &pSiS->stateMode);
       SiSVGASaveFonts(pScrn);

       if(pSiS->vesamajor > 1) {
	  if(!VBESaveRestore(pSiS->pVbe, function, (pointer)&pSiS->state,
				&pSiS->stateSize, &pSiS->statePage)) {
	     return;
	  }
       }
    }

    /* Save/Restore Super VGA state */
    if(function != MODE_QUERY) {

       if(pSiS->vesamajor > 1) {
	  if(function == MODE_RESTORE) {
	     memcpy(pSiS->state, pSiS->pstate, pSiS->stateSize);
	  }

	  if(VBESaveRestore(pSiS->pVbe,function,(pointer)&pSiS->state,
			    &pSiS->stateSize,&pSiS->statePage) &&
	     (function == MODE_SAVE)) {
	     /* don't rely on the memory not being touched */
	     if(!pSiS->pstate) {
		pSiS->pstate = malloc(pSiS->stateSize);
	     }
	     memcpy(pSiS->pstate, pSiS->state, pSiS->stateSize);
	  }
       }

       if(function == MODE_RESTORE) {
	  VBESetVBEMode(pSiS->pVbe, pSiS->stateMode, NULL);
	  SiSVGARestoreFonts(pScrn);
       }

    }
}

static void
SISSave(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
    SISRegPtr sisReg;
    int flags;

#ifdef SISDUALHEAD
    /* We always save master & slave */
    if(pSiS->DualHeadMode && pSiS->SecondHead) return;
#endif

    sisReg = &pSiS->SavedReg;

    if( ((pSiS->VGAEngine == SIS_300_VGA) || (pSiS->VGAEngine == SIS_315_VGA)) &&
        ((pSiS->VBFlags2 & VB2_VIDEOBRIDGE) && (SiSBridgeIsInSlaveMode(pScrn))) ) {
       SiSVGASave(pScrn, sisReg, SISVGA_SR_CMAP | SISVGA_SR_MODE);
#ifdef SIS_PC_PLATFORM
       if(pSiS->VGAMemBase) {
          SiSRegInit(pSiS->SiS_Pr, pSiS->RelIO+0x30);
          SiSSetLVDSetc(pSiS->SiS_Pr, 0);
          SiS_GetVBType(pSiS->SiS_Pr);
          SiS_DisableBridge(pSiS->SiS_Pr);
          SiSVGASave(pScrn, sisReg, SISVGA_SR_FONTS);
          SiS_EnableBridge(pSiS->SiS_Pr);
       }
#endif
    } else {
       flags = SISVGA_SR_CMAP | SISVGA_SR_MODE;
#ifdef SIS_PC_PLATFORM
       if(pSiS->VGAMemBase) flags |= SISVGA_SR_FONTS;
#endif
       SiSVGASave(pScrn, sisReg, flags);
    }

    sisSaveUnlockExtRegisterLock(pSiS, &sisReg->sisRegs3C4[0x05], &sisReg->sisRegs3D4[0x80]);

    (*pSiS->SiSSave)(pScrn, sisReg);

    if(pSiS->UseVESA) SISVESASaveRestore(pScrn, MODE_SAVE);

    /* "Save" these again as they may have been changed prior to SISSave() call */
    if((pSiS->VGAEngine == SIS_300_VGA) || (pSiS->VGAEngine == SIS_315_VGA)) {
       sisReg->sisRegs3C4[0x1f] = pSiS->oldSR1F;
       sisReg->sisRegs3D4[0x17] = pSiS->oldCR17;
       sisReg->sisRegs3D4[0x32] = pSiS->oldCR32;
       sisReg->sisRegs3D4[0x36] = pSiS->oldCR36;
       sisReg->sisRegs3D4[0x37] = pSiS->oldCR37;
       if(pSiS->VGAEngine == SIS_315_VGA) {
	  sisReg->sisRegs3D4[pSiS->myCR63] = pSiS->oldCR63;
       }
    }
}


/*******************************************************/
/*                   Restore(), etc                    */
/*******************************************************/

static void
SiS_SiSFB_Lock(ScrnInfoPtr pScrn, Bool lock)
{
    SISPtr  pSiS = SISPTR(pScrn);
    int     fd;
    CARD32  parm;

    if(!pSiS->sisfbfound) return;
    if(!pSiS->sisfb_havelock) return;

    if((fd = open(pSiS->sisfbdevname, O_RDONLY)) != -1) {
       parm = lock ? 1 : 0;
       ioctl(fd, SISFB_SET_LOCK, &parm);
       close(fd);
    }
}

static void
SISSpecialRestore(ScrnInfoPtr pScrn)
{
    SISPtr    pSiS = SISPTR(pScrn);
    SISRegPtr sisReg = &pSiS->SavedReg;
    UChar temp;
    int i;

    /* 1.11.04 and later for 651 and 301B(DH) do strange register
     * fiddling after the usual mode change. This happens
     * depending on the result of a call of int 2f (with
     * ax=0x1680) and if modeno <= 0x13. I have no idea if
     * that is specific for the 651 or that very machine.
     * So this perhaps requires some more checks in the beginning
     * (although it should not do any harm on other chipsets/bridges
     * etc.) However, even if I call the VBE to restore mode 0x03,
     * these registers don't get restored correctly, possibly
     * because that int-2f-call for some reason results non-zero. So
     * what I do here is to restore these few registers
     * manually.
     */

    if(!(pSiS->ChipFlags & SiSCF_Is65x))
       return;

    inSISIDXREG(SISCR, 0x34, temp);
    temp &= 0x7f;
    if(temp > 0x13)
       return;

#ifdef UNLOCK_ALWAYS
    sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

    SiS_UnLockCRT2(pSiS->SiS_Pr);

    outSISIDXREG(SISCAP, 0x3f, sisReg->sisCapt[0x3f]);
    outSISIDXREG(SISCAP, 0x00, sisReg->sisCapt[0x00]);
    for(i = 0; i < 0x4f; i++) {
       outSISIDXREG(SISCAP, i, sisReg->sisCapt[i]);
    }
    outSISIDXREG(SISVID, 0x32, (sisReg->sisVid[0x32] & ~0x05));
    outSISIDXREG(SISVID, 0x30, sisReg->sisVid[0x30]);
    outSISIDXREG(SISVID, 0x32, ((sisReg->sisVid[0x32] & ~0x04) | 0x01));
    outSISIDXREG(SISVID, 0x30, sisReg->sisVid[0x30]);

    if(!(pSiS->ChipFlags & SiSCF_Is651))
       return;

    if(!(pSiS->VBFlags2 & VB2_SISBRIDGE))
       return;

    inSISIDXREG(SISCR, 0x30, temp);
    if(temp & 0x40) {
       UChar myregs[] = {
       		0x2f, 0x08, 0x09, 0x03, 0x0a, 0x0c,
		0x0b, 0x0d, 0x0e, 0x12, 0x0f, 0x10,
		0x11, 0x04, 0x05, 0x06, 0x07, 0x00,
		0x2e
       };
       for(i = 0; i <= 18; i++) {
          outSISIDXREG(SISPART1, myregs[i], sisReg->VBPart1[myregs[i]]);
       }
    } else if((temp & 0x20) || (temp & 0x9c)) {
       UChar myregs[] = {
       		0x04, 0x05, 0x06, 0x07, 0x00, 0x2e
       };
       for(i = 0; i <= 5; i++) {
          outSISIDXREG(SISPART1, myregs[i], sisReg->VBPart1[myregs[i]]);
       }
    }
}

/* Fix SR11 for 661 and later */
static void
SiSFixupSR11(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
    CARD8  tmpreg;

#ifdef UNLOCK_ALWAYS
    sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

    if(pSiS->SiS_Pr->SiS_SensibleSR11) {
       inSISIDXREG(SISSR,0x11,tmpreg);
       if(tmpreg & 0x20) {
          inSISIDXREG(SISSR,0x3e,tmpreg);
	  tmpreg = (tmpreg + 1) & 0xff;
	  outSISIDXREG(SISSR,0x3e,tmpreg);
       }

       inSISIDXREG(SISSR,0x11,tmpreg);
       if(tmpreg & 0xf0) {
          andSISIDXREG(SISSR,0x11,0x0f);
       }
    }
}

/* Subroutine for restoring sisfb's TV parameters (used by SiSRestore()) */

static void
SiSRestore_SiSFB_TVParms(ScrnInfoPtr pScrn)
{
    SISPtr  pSiS = SISPTR(pScrn);
    int     fd;
    CARD32  parm;

    if(!pSiS->sisfbfound) return;
    if(!pSiS->sisfb_tvposvalid) return;
    if(!(pSiS->sisfbdevname[0])) return;

    if((fd = open(pSiS->sisfbdevname, O_RDONLY)) != -1) {
       parm = (CARD32)((pSiS->sisfb_tvxpos << 16) | (pSiS->sisfb_tvypos & 0xffff));
       ioctl(fd, SISFB_SET_TVPOSOFFSET, &parm);
       close(fd);
    }
}

/*
 * Restore the initial mode. To be used internally only!
 */
static void
SISRestore(ScrnInfoPtr pScrn)
{
    SISPtr    pSiS = SISPTR(pScrn);
    SISRegPtr sisReg = &pSiS->SavedReg;
    Bool      doit = FALSE, doitlater = FALSE;
    Bool      vesasuccess = FALSE;
    int	      flags;

    /* WARNING: Don't ever touch this. It now seems to work on
     * all chipset/bridge combinations - but finding out the
     * correct combination was pure hell.
     */

    /* Wait for the accelerators */
    (*pSiS->SyncAccel)(pScrn);

    /* Set up restore flags */
    flags = SISVGA_SR_MODE | SISVGA_SR_CMAP;
#ifdef SIS_PC_PLATFORM
    /* We now restore ALL to overcome the vga=extended problem */
    if(pSiS->VGAMemBase) flags |= SISVGA_SR_FONTS;
#endif

    if((pSiS->VGAEngine == SIS_300_VGA) || (pSiS->VGAEngine == SIS_315_VGA)) {

#ifdef SISDUALHEAD
       /* We always restore master AND slave */
       if(pSiS->DualHeadMode && pSiS->SecondHead) return;
#endif

#ifdef UNLOCK_ALWAYS
       sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

       /* We must not disable the sequencer if the bridge is in SlaveMode! */
       if(!(SiSBridgeIsInSlaveMode(pScrn))) {
	  SiSVGAProtect(pScrn, TRUE);
       }

       /* First, restore CRT1 on/off and VB connection registers */
       outSISIDXREG(SISCR, 0x32, pSiS->oldCR32);
       if(!(pSiS->oldCR17 & 0x80)) {			/* CRT1 was off */
	  if(!(SiSBridgeIsInSlaveMode(pScrn))) {        /* Bridge is NOT in SlaveMode now -> do it */
	     doit = TRUE;
	  } else {
	     doitlater = TRUE;
	  }
       } else {						/* CRT1 was on -> do it now */
	  doit = TRUE;
       }

       if(doit) {
	  outSISIDXREG(SISCR, 0x17, pSiS->oldCR17);
       }
       if(pSiS->VGAEngine == SIS_315_VGA) {
	  outSISIDXREG(SISCR, pSiS->myCR63, pSiS->oldCR63);
       }

       outSISIDXREG(SISSR, 0x1f, pSiS->oldSR1F);

       /* For 30xB/LV, restoring the registers does not
	* work. We "manually" set the old mode, instead.
	* The same applies for SiS730 machines with LVDS.
	* Finally, this behavior can be forced by setting
	* the option RestoreBySetMode.
	*/
	if( ( (pSiS->restorebyset) ||
	      (pSiS->VBFlags2 & VB2_30xBLV) ||
	      ((pSiS->ChipType == SIS_730) && (pSiS->VBFlags2 & VB2_LVDS)) )     &&
	    (pSiS->OldMode) ) {

	   Bool changedmode = FALSE;

	   xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, 3,
	         "Restoring by setting old mode 0x%02x\n", pSiS->OldMode);
	  
	   if(((pSiS->OldMode <= 0x13) || (!pSiS->sisfbfound)) && (pSiS->pVbe)) {
	      int vmode = SiSTranslateToVESA(pScrn, pSiS->OldMode);
	      if(vmode > 0) {
		 if(vmode > 0x13) vmode |= ((1 << 15) | (1 << 14));
		 outSISIDXREG(SISCR, 0x7d, sisReg->sisRegs3D4[0x7d]); /* Timing ID */
		 if(VBESetVBEMode(pSiS->pVbe, vmode, NULL) == TRUE) {
		    SISSpecialRestore(pScrn);
		    SiS_GetSetModeID(pScrn,pSiS->OldMode);
		    vesasuccess = TRUE;
		 } else {
		    xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, 3,
			"VBE failed to restore mode 0x%x\n", pSiS->OldMode);
		 }
	      } else {
		 xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, 3,
		 	"Can't identify VESA mode number for mode 0x%x\n", pSiS->OldMode);
	      }
	   }

	   if(vesasuccess == FALSE) {

	      int backupscaler = pSiS->SiS_Pr->UsePanelScaler;
	      int backupcenter = pSiS->SiS_Pr->CenterScreen;
	      ULong backupspecialtiming = pSiS->SiS_Pr->SiS_CustomT;
	      int mymode = pSiS->OldMode;

	      if((pSiS->VGAEngine == SIS_315_VGA)			&&
	         ((pSiS->ROM661New) || (pSiS->ChipFlags & SiSCF_IsXGI)) &&
		 (!pSiS->sisfbfound)) {
	         /* New SiS BIOS or XGI BIOS has set mode, therefore eventually translate number */
	         mymode = SiSTranslateToOldMode(mymode);
	      }

 	      if((pSiS->VBFlags2 & VB2_30xBLV)) {
	        /* !!! REQUIRED for 630+301B-DH, otherwise the text modes
	         *     will not be restored correctly !!!
	         * !!! Do this ONLY for LCD; VGA2 will not be restored
	         *     correctly otherwise.
	         */
	         UChar temp;
	         inSISIDXREG(SISCR, 0x30, temp);
	         if(temp & 0x20) {
	            if(mymode == 0x03) {
		       mymode = 0x13;
		       changedmode = TRUE;
	            }
	         }
	      }

	      pSiS->SiS_Pr->UseCustomMode = FALSE;
	      pSiS->SiS_Pr->CRT1UsesCustomMode = FALSE;
	      pSiS->SiS_Pr->CenterScreen = 0;
	      if(pSiS->sisfbfound) {
		 pSiS->SiS_Pr->UsePanelScaler = pSiS->sisfbscalelcd;
		 pSiS->SiS_Pr->SiS_CustomT = pSiS->sisfbspecialtiming;
	      } else {
		 pSiS->SiS_Pr->UsePanelScaler = -1;
		 /* Leave CustomT as it is */
	      }
	      SiS_SetEnableDstn(pSiS->SiS_Pr, FALSE);
	      SiS_SetEnableFstn(pSiS->SiS_Pr, FALSE);
	      if((pSiS->ChipType == SIS_550) && (pSiS->sisfbfound)) {
		 if(pSiS->sisfbxSTN) {
		    SiS_SetEnableDstn(pSiS->SiS_Pr, pSiS->sisfbDSTN);
		    SiS_SetEnableFstn(pSiS->SiS_Pr, pSiS->sisfbFSTN);
		 } else if(mymode == 0x5a || mymode == 0x5b) {
		    SiS_SetEnableFstn(pSiS->SiS_Pr, TRUE);
		 }
	      }
	      pSiS->SiS_Pr->SiS_EnableBackLight = TRUE;
	      SiSSetMode(pSiS->SiS_Pr, pScrn, mymode, FALSE);
	      if(changedmode) {
		 outSISIDXREG(SISCR,0x34,0x03);
	      }
	      SISSpecialRestore(pScrn);
	      SiS_GetSetModeID(pScrn, pSiS->OldMode); /* NOT mymode! */
	      pSiS->SiS_Pr->UsePanelScaler = backupscaler;
	      pSiS->SiS_Pr->CenterScreen = backupcenter;
	      pSiS->SiS_Pr->SiS_CustomT = backupspecialtiming;
	      SiS_SiSFB_Lock(pScrn, FALSE);
	      SiSRestore_SiSFB_TVParms(pScrn);
	      SiS_SiSFB_Lock(pScrn, TRUE);

	   }

	   /* Restore CRT1 status */
	   if(pSiS->VGAEngine == SIS_315_VGA) {
              outSISIDXREG(SISCR, pSiS->myCR63, pSiS->oldCR63);
           }
           outSISIDXREG(SISSR, 0x1f, pSiS->oldSR1F);

#ifdef SISVRAMQ
	   /* Restore queue mode registers on 315/330/340/350 series */
	   /* (This became necessary due to the switch to VRAM queue) */
	   SiSRestoreQueueMode(pSiS, sisReg);
#endif

        } else {

	   if(pSiS->VBFlags2 & VB2_VIDEOBRIDGE) {
	      /* If a video bridge is present, we need to restore
	       * non-extended (=standard VGA) SR and CR registers
	       * before restoring the extended ones and the bridge
	       * registers.
	       */
	      if(!(SiSBridgeIsInSlaveMode(pScrn))) {
                 SiSVGAProtect(pScrn, TRUE);
	         SiSVGARestore(pScrn, sisReg, SISVGA_SR_MODE);
              }
	   }

           (*pSiS->SiSRestore)(pScrn, sisReg);

        }

	if(doitlater) {
           outSISIDXREG(SISCR, 0x17, pSiS->oldCR17);
	}

	if((pSiS->VBFlags2 & VB2_VIDEOBRIDGE) && (SiSBridgeIsInSlaveMode(pScrn))) {

	   /* IMPORTANT: The 30xLV does not handle well being disabled if in
	    * LCDA mode! In LCDA mode, the bridge is NOT in slave mode,
	    * so this is the only safe way: Disable the bridge ONLY if
	    * in Slave Mode, and don't bother if not.
	    */

	   if(flags & SISVGA_SR_FONTS) {
              SiSRegInit(pSiS->SiS_Pr, pSiS->RelIO+0x30);
	      SiSSetLVDSetc(pSiS->SiS_Pr, 0);
	      SiS_GetVBType(pSiS->SiS_Pr);
	      SiS_DisableBridge(pSiS->SiS_Pr);
	      SiSVGAProtect(pScrn, TRUE);
	   }

	   SiSVGARestore(pScrn, sisReg, flags);

	   if(flags & SISVGA_SR_FONTS) {
	      SiSVGAProtect(pScrn, FALSE);
	      SiS_EnableBridge(pSiS->SiS_Pr);
	      andSISIDXREG(SISSR, 0x01, ~0x20);  /* Display on */
	   }

	} else {

	   SiSVGAProtect(pScrn, TRUE);
	   SiSVGARestore(pScrn, sisReg, flags);
           SiSVGAProtect(pScrn, FALSE);

	}

	SiSFixupSR11(pScrn);

#ifdef TWDEBUG
	{
	  SISRegPtr pReg = &pSiS->ModeReg;
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"REAL REGISTER CONTENTS AFTER RESTORE BY SETMODE:\n");
	  (*pSiS->SiSSave)(pScrn, pReg);
	}
#endif

	sisRestoreExtRegisterLock(pSiS,sisReg->sisRegs3C4[0x05],sisReg->sisRegs3D4[0x80]);

    } else {	/* All other chipsets */

        SiSVGAProtect(pScrn, TRUE);

#ifdef UNLOCK_ALWAYS
        sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

        (*pSiS->SiSRestore)(pScrn, sisReg);

        SiSVGAProtect(pScrn, TRUE);

	SiSVGARestore(pScrn, sisReg, flags);

	/* Restore TV. This is rather complicated, but if we don't do it,
	 * TV output will flicker terribly
	 */
        if((pSiS->Chipset == PCI_CHIP_SIS6326) && (pSiS->SiS6326Flags & SIS6326_HASTV)) {
	   if(sisReg->sis6326tv[0] & 0x04) {
	      UChar tmp;
	      int val;

              orSISIDXREG(SISSR, 0x01, 0x20);
              tmp = SiS6326GetTVReg(pScrn,0x00);
              tmp &= ~0x04;
              while(!(inSISREG(SISINPSTAT) & 0x08));  /* Wait while NOT vb */
              SiS6326SetTVReg(pScrn,0x00,tmp);
              for(val=0; val < 2; val++) {
                 while(!(inSISREG(SISINPSTAT) & 0x08));  /* Wait while NOT vb */
                 while(inSISREG(SISINPSTAT) & 0x08);     /* wait while vb     */
              }
              SiS6326SetTVReg(pScrn, 0x00, sisReg->sis6326tv[0]);
              tmp = inSISREG(SISINPSTAT);
              outSISREG(SISAR, 0x20);
              tmp = inSISREG(SISINPSTAT);
              while(inSISREG(SISINPSTAT) & 0x01);
              while(!(inSISREG(SISINPSTAT) & 0x01));
              andSISIDXREG(SISSR, 0x01, ~0x20);
              for(val=0; val < 10; val++) {
                 while(!(inSISREG(SISINPSTAT) & 0x08));  /* Wait while NOT vb */
                 while(inSISREG(SISINPSTAT) & 0x08);     /* wait while vb     */
              }
              andSISIDXREG(SISSR, 0x01, ~0x20);
	   }
        }

        sisRestoreExtRegisterLock(pSiS,sisReg->sisRegs3C4[5],sisReg->sisRegs3D4[0x80]);

        SiSVGAProtect(pScrn, FALSE);
    }
}

static void
SISVESARestore(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISVRAMQ
   SISRegPtr sisReg = &pSiS->SavedReg;
#endif

   if(pSiS->UseVESA) {
      SISVESASaveRestore(pScrn, MODE_RESTORE);
#ifdef SISVRAMQ
      /* Restore queue mode registers on 315/330/340/350 series */
      /* (This became necessary due to the switch to VRAM queue) */
      SiSRestoreQueueMode(pSiS, sisReg);
#endif
   }
}

/* Restore bridge config registers - to be called BEFORE VESARestore */
static void
SISBridgeRestore(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);

#ifdef SISDUALHEAD
    /* We only restore for master head */
    if(pSiS->DualHeadMode && pSiS->SecondHead) return;
#endif

    if(pSiS->VGAEngine == SIS_300_VGA || pSiS->VGAEngine == SIS_315_VGA) {
	SiSRestoreBridge(pScrn, &pSiS->SavedReg);
    }
}

/************************************************/
/*                Mode switching                */
/************************************************/

/* Things to do before a ModeSwitch. We set up the
 * video bridge configuration and the TurboQueue.
 * (300 series and later)
 */
static void
SiSPreSetMode(ScrnInfoPtr pScrn, DisplayModePtr mode, int viewmode)
{
    SISPtr pSiS = SISPTR(pScrn);
    UChar  CR30, CR31, CR32, CR33;
    UChar  CR39 = 0, CR3B = 0;
    UChar  CR17, CR38 = 0;
    UChar  CR35 = 0, CR79 = 0;
    int    temp = 0, crt1rateindex = 0;
    ULong  vbflag = pSiS->VBFlags;
    Bool   hcm = pSiS->HaveCustomModes;
    DisplayModePtr mymode = mode;

    pSiS->IsCustom = FALSE;

    /* NEVER call this with viewmode = SIS_MODE_SIMU
     * if mode->type is not M_T_DEFAULT!
     */

#ifdef SISMERGED
    if(pSiS->MergedFB) {
       switch(viewmode) {
       case SIS_MODE_CRT1:
	  mymode = ((SiSMergedDisplayModePtr)mode->Private)->CRT1;
	  break;
       case SIS_MODE_CRT2:
	  mymode = ((SiSMergedDisplayModePtr)mode->Private)->CRT2;
	  hcm = pSiS->HaveCustomModes2;
       }
    }
#endif

    switch(viewmode) {
    case SIS_MODE_CRT1:
       if(SiS_CheckModeCRT1(pScrn, mymode, vbflag, pSiS->VBFlags3, hcm) == 0xfe) {
          pSiS->IsCustom = TRUE;
       }
       break;
    case SIS_MODE_CRT2:
       if(vbflag & CRT2_ENABLE) {
          if(SiS_CheckModeCRT2(pScrn, mymode, vbflag, pSiS->VBFlags3, hcm) == 0xfe) {
	     pSiS->IsCustom = TRUE;
          }
       } else {
          /* This can only happen in mirror mode */
          if(SiS_CheckModeCRT1(pScrn, mymode, vbflag, pSiS->VBFlags3, hcm) == 0xfe) {
             pSiS->IsCustom = TRUE;
          }
       }
    }

#ifdef UNLOCK_ALWAYS
    sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);    /* Unlock Registers */
#endif

    inSISIDXREG(SISCR, 0x30, CR30);
    inSISIDXREG(SISCR, 0x31, CR31);
    CR32 = pSiS->newCR32;
    inSISIDXREG(SISCR, 0x33, CR33);

    if(pSiS->NewCRLayout) {

       inSISIDXREG(SISCR, 0x35, CR35);
       inSISIDXREG(SISCR, 0x38, CR38);
       inSISIDXREG(SISCR, 0x39, CR39);

       xf86DrvMsgVerb(pScrn->scrnIndex, X_PROBED, SISVERBLEVEL,
	   "Before: CR30=0x%02x,CR31=0x%02x,CR32=0x%02x,CR33=0x%02x,CR35=0x%02x,CR38=0x%02x\n",
              CR30, CR31, CR32, CR33, CR35, CR38);

       CR38 &= ~0x07;

    } else {

       if(pSiS->Chipset != PCI_CHIP_SIS300) {
          switch(pSiS->VGAEngine) {
             case SIS_300_VGA: temp = 0x35; break;
             case SIS_315_VGA: temp = 0x38; break;
          }
          if(temp) inSISIDXREG(SISCR, temp, CR38);
       }
       if(pSiS->VGAEngine == SIS_315_VGA) {
          inSISIDXREG(SISCR, 0x79, CR79);
          CR38 &= ~0x3b;   			/* Clear LCDA/DualEdge and YPbPr bits */
       }
       inSISIDXREG(SISCR, 0x3b, CR3B);

       xf86DrvMsgVerb(pScrn->scrnIndex, X_PROBED, SISVERBLEVEL,
	   "Before: CR30=0x%02x, CR31=0x%02x, CR32=0x%02x, CR33=0x%02x, CR%02x=0x%02x\n",
              CR30, CR31, CR32, CR33, temp, CR38);
    }

    xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, SISVERBLEVEL, "VBFlags=0x%x\n", pSiS->VBFlags);

    CR30 = 0x00;
    CR31 &= ~0x60;  /* Clear VB_Drivermode & VB_OutputDisable */
    CR31 |= 0x04;   /* Set VB_NotSimuMode (not for 30xB/1400x1050?) */
    CR35 = 0x00;

    if(!pSiS->NewCRLayout) {
       if(!pSiS->AllowHotkey) {
          CR31 |= 0x80;   /* Disable hotkey-switch */
       }
       CR79 &= ~0x10;     /* Enable Backlight control on 315 series */
    }

    SiS_SetEnableDstn(pSiS->SiS_Pr, FALSE);
    SiS_SetEnableFstn(pSiS->SiS_Pr, FALSE);

    if((vbflag & CRT1_LCDA) && (viewmode == SIS_MODE_CRT1)) {

       CR38 |= 0x02;

    } else {

       switch(vbflag & (CRT2_TV|CRT2_LCD|CRT2_VGA)) {

       case CRT2_TV:
          CR38 &= ~0xC0; 	/* Clear Pal M/N bits */

          if((pSiS->VBFlags2 & VB2_CHRONTEL) && (vbflag & TV_CHSCART)) {		/* Chrontel */
	     CR30 |= 0x10;
	     CR38 |= 0x04;
	     CR38 &= ~0x08;
	     CR31 |= 0x01;
	  } else if((pSiS->VBFlags2 & VB2_CHRONTEL) && (vbflag & TV_CHYPBPR525I)) {	/* Chrontel */
	     CR38 |= 0x08;
	     CR38 &= ~0x04;
	     CR31 &= ~0x01;
          } else if(vbflag & TV_HIVISION) {	/* SiS bridge */
	     if(pSiS->NewCRLayout) {
	        CR38 |= 0x04;
	        CR35 |= 0x60;
	     } else {
	        CR30 |= 0x80;
		if(pSiS->VGAEngine == SIS_315_VGA) {
		   if(pSiS->VBFlags2 & VB2_SISYPBPRBRIDGE) {
		      CR38 |= (0x08 | 0x30);
		   }
		}
	     }
	     CR31 |= 0x01;
	     CR35 |= 0x01;
	  } else if(vbflag & TV_YPBPR) {					/* SiS bridge */
	     if(pSiS->NewCRLayout) {
		CR38 |= 0x04;
		CR31 &= ~0x01;
		CR35 &= ~0x01;
		if(vbflag & (TV_YPBPR525P | TV_YPBPR625P)) CR35 |= 0x20;
		else if(vbflag & TV_YPBPR750P)             CR35 |= 0x40;
		else if(vbflag & TV_YPBPR1080I)            CR35 |= 0x60;

		if(vbflag & (TV_YPBPR625I | TV_YPBPR625P)) {
		   CR31 |= 0x01;
		   CR35 |= 0x01;
		}

		CR39 &= ~0x03;
		if((vbflag & TV_YPBPRAR) == TV_YPBPR43LB)     CR39 |= 0x00;
		else if((vbflag & TV_YPBPRAR) == TV_YPBPR43)  CR39 |= 0x01;
		else if((vbflag & TV_YPBPRAR) == TV_YPBPR169) CR39 |= 0x02;
		else					      CR39 |= 0x03;
	     } else if(pSiS->SiS_SD_Flags & SiS_SD_SUPPORTYPBPR) {
		CR30 |= 0x80;
		CR38 |= 0x08;
		CR31 &= ~0x01;
		if(vbflag & (TV_YPBPR525P|TV_YPBPR625P)) CR38 |= 0x10;
		else if(vbflag & TV_YPBPR750P)  	 CR38 |= 0x20;
		else if(vbflag & TV_YPBPR1080I)		 CR38 |= 0x30;

		if(vbflag & (TV_YPBPR625I | TV_YPBPR625P)) CR31 |= 0x01;

		if(pSiS->SiS_SD_Flags & SiS_SD_SUPPORTYPBPRAR) {
		   CR3B &= ~0x03;
		   if((vbflag & TV_YPBPRAR) == TV_YPBPR43LB)     CR3B |= 0x00;
		   else if((vbflag & TV_YPBPRAR) == TV_YPBPR43)  CR3B |= 0x03;
		   else if((vbflag & TV_YPBPRAR) == TV_YPBPR169) CR3B |= 0x01;
		   else					         CR3B |= 0x03;
		}
	     }
          } else {								/* All */
	     if(vbflag & TV_SCART)  CR30 |= 0x10;
	     if(vbflag & TV_SVIDEO) CR30 |= 0x08;
	     if(vbflag & TV_AVIDEO) CR30 |= 0x04;
	     if(!(CR30 & 0x1C))	    CR30 |= 0x08;    /* default: SVIDEO */

	     if(vbflag & TV_PAL) {
		CR31 |= 0x01;
		CR35 |= 0x01;
		if( (pSiS->VBFlags2 & VB2_SISBRIDGE) ||
		    ((pSiS->VBFlags2 & VB2_CHRONTEL) && (pSiS->ChrontelType == CHRONTEL_701x)) )  {
		   if(vbflag & TV_PALM) {
		      CR38 |= 0x40;
		      CR35 |= 0x04;
		   } else if(vbflag & TV_PALN) {
		      CR38 |= 0x80;
		      CR35 |= 0x08;
		   }
	        }
	     } else {
		CR31 &= ~0x01;
		CR35 &= ~0x01;
		if(vbflag & TV_NTSCJ) {
		   CR38 |= 0x40;  /* TW, not BIOS */
		   CR35 |= 0x02;
		}
	     }
	     if(vbflag & TV_SCART) {
		CR31 |= 0x01;
		CR35 |= 0x01;
	     }
	  }

	  CR31 &= ~0x04;   /* Clear NotSimuMode */
	  pSiS->SiS_Pr->SiS_CHOverScan = pSiS->UseCHOverScan;
	  if((pSiS->OptTVSOver == 1) && (pSiS->ChrontelType == CHRONTEL_700x)) {
	     pSiS->SiS_Pr->SiS_CHSOverScan = TRUE;
	  } else {
	     pSiS->SiS_Pr->SiS_CHSOverScan = FALSE;
	  }
	  break;

       case CRT2_LCD:
	  CR30 |= 0x20;
	  SiS_SetEnableDstn(pSiS->SiS_Pr, pSiS->DSTN);
	  SiS_SetEnableFstn(pSiS->SiS_Pr, pSiS->FSTN);
	  break;

       case CRT2_VGA:
	  CR30 |= 0x40;
	  break;

       default:
	  CR30 |= 0x00;
	  CR31 |= 0x20;    /* VB_OUTPUT_DISABLE */
	  if(pSiS->UseVESA) {
	     crt1rateindex = SISSearchCRT1Rate(pScrn, mymode);
	  }
       }

    }

    if(vbflag & CRT1_LCDA) {
       switch(viewmode) {
       case SIS_MODE_CRT1:
	  CR38 |= 0x01;
	  break;
       case SIS_MODE_CRT2:
	  if(vbflag & (CRT2_TV|CRT2_VGA)) {
	     CR30 |= 0x02;
	     CR38 |= 0x01;
	  } else {
	     CR38 |= 0x03;
	  }
	  break;
       case SIS_MODE_SIMU:
       default:
	  if(vbflag & (CRT2_TV|CRT2_LCD|CRT2_VGA)) {
	     CR30 |= 0x01;
	  }
	  break;
       }
    } else {
       if(vbflag & (CRT2_TV|CRT2_LCD|CRT2_VGA)) {
          CR30 |= 0x01;
       }
    }

    if(pSiS->UseVESA) {
       CR31 &= ~0x40;   /* Clear Drivermode */
       CR31 |= 0x06;    /* Set SlaveMode, Enable SimuMode in Slavemode */
#ifdef TWDEBUG
       CR31 |= 0x40;    /* DEBUG (for non-slave mode VESA) */
       crt1rateindex = SISSearchCRT1Rate(pScrn, mymode);
#endif
    } else {
       CR31 |=  0x40;  /* Set Drivermode */
       CR31 &=  ~0x06; /* Disable SlaveMode, disable SimuMode in SlaveMode */
       if(!pSiS->IsCustom) {
          crt1rateindex = SISSearchCRT1Rate(pScrn, mymode);
       }
    }

    switch(viewmode) {
	case SIS_MODE_SIMU:
	   CR33 = 0;
	   if(!(vbflag & CRT1_LCDA)) {
	      CR33 |= (crt1rateindex & 0x0f);
	   }
	   if(vbflag & CRT2_VGA) {
	      CR33 |= ((crt1rateindex & 0x0f) << 4);
	   }
	   break;
	case SIS_MODE_CRT1:
	   CR33 &= 0xf0;
	   if(!(vbflag & CRT1_LCDA)) {
	      CR33 |= (crt1rateindex & 0x0f);
	   }
	   break;
	case SIS_MODE_CRT2:
	   CR33 &= 0x0f;
	   if(vbflag & CRT2_VGA) {
	      CR33 |= ((crt1rateindex & 0x0f) << 4);
	   }
	   break;
     }

     if((!pSiS->UseVESA) && (vbflag & CRT2_ENABLE)) {
	if(pSiS->CRT1off) CR33 &= 0xf0;
     }

     if(pSiS->NewCRLayout) {

	CR31 &= 0xfe;   /* Clear PAL flag (now in CR35) */
	CR38 &= 0x07;   /* Use only LCDA and HiVision/YPbPr bits */
	outSISIDXREG(SISCR, 0x30, CR30);
	outSISIDXREG(SISCR, 0x31, CR31);
	outSISIDXREG(SISCR, 0x33, CR33);
	outSISIDXREG(SISCR, 0x35, CR35);
	setSISIDXREG(SISCR, 0x38, 0xf8, CR38);
	outSISIDXREG(SISCR, 0x39, CR39);

	xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, SISVERBLEVEL,
		"After:  CR30=0x%02x,CR31=0x%02x,CR33=0x%02x,CR35=0x%02x,CR38=%02x\n",
		    CR30, CR31, CR33, CR35, CR38);

     } else {

	outSISIDXREG(SISCR, 0x30, CR30);
	outSISIDXREG(SISCR, 0x31, CR31);
	outSISIDXREG(SISCR, 0x33, CR33);
	if(temp) {
	   outSISIDXREG(SISCR, temp, CR38);
	}
	if(pSiS->VGAEngine == SIS_315_VGA) {
	   outSISIDXREG(SISCR, 0x3b, CR3B);
	   outSISIDXREG(SISCR, 0x79, CR79);
	}

	xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, SISVERBLEVEL,
		"After:  CR30=0x%02x,CR31=0x%02x,CR33=0x%02x,CR%02x=%02x\n",
		    CR30, CR31, CR33, temp, CR38);
     }

     pSiS->SiS_Pr->SiS_UseOEM = pSiS->OptUseOEM;

     /* Enable TurboQueue */
#ifdef SISVRAMQ
     if(pSiS->VGAEngine != SIS_315_VGA)
#endif
	SiSEnableTurboQueue(pScrn);

     if((!pSiS->UseVESA) && (pSiS->VBFlags & CRT2_ENABLE)) {
	/* Switch on CRT1 for modes that require the bridge in SlaveMode */
	andSISIDXREG(SISSR,0x1f,0x3f);
	inSISIDXREG(SISCR, 0x17, CR17);
	if(!(CR17 & 0x80)) {
	   orSISIDXREG(SISCR, 0x17, 0x80);
	   outSISIDXREG(SISSR, 0x00, 0x01);
	   usleep(10000);
	   outSISIDXREG(SISSR, 0x00, 0x03);
	}
     }
}

#ifdef SISDUALHEAD
static void
SiS_SetDHFlags(SISPtr pSiS, unsigned int misc, unsigned int sd2)
{
   SISEntPtr pSiSEnt = pSiS->entityPrivate;

   if(pSiS->DualHeadMode) {
      if(pSiSEnt->pScrn_1) {
	 SISPTR(pSiSEnt->pScrn_1)->MiscFlags |= misc;
	 SISPTR(pSiSEnt->pScrn_1)->SiS_SD2_Flags |= sd2;
      }
      if(pSiSEnt->pScrn_2) {
	 SISPTR(pSiSEnt->pScrn_2)->MiscFlags |= misc;
	 SISPTR(pSiSEnt->pScrn_2)->SiS_SD2_Flags |= sd2;
      }
   }
}
#endif

/* PostSetMode: Things to do after a mode switch. (300 and later)
 * -) Disable CRT1 for saving bandwidth. This doesn't work with VESA;
 *    VESA uses the bridge in SlaveMode and switching CRT1 off while
 *    the bridge is in SlaveMode is not that clever...
 * -) Check if overlay can be used (depending on dotclock)
 * -) Check if Panel Scaler is active on LVDS for overlay re-scaling
 * -) Save TV registers for further processing
 * -) Apply TV and other settings
 */
static void
SiSPostSetMode(ScrnInfoPtr pScrn, SISRegPtr sisReg)
{
    SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
    SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif
    UChar usScratchCR17, sr2b, sr2c, tmpreg;
    int   myclock1, myclock2, mycoldepth1, mycoldepth2, temp;
    Bool  flag = FALSE;
    Bool  doit = TRUE;
    Bool  IsInSlaveMode;

#ifdef TWDEBUG
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
    	"CRT1off is %d\n", pSiS->CRT1off);
#endif

    pSiS->CRT1isoff = pSiS->CRT1off;

#ifdef UNLOCK_ALWAYS
    sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

    SiSFixupSR11(pScrn);

    IsInSlaveMode = SiSBridgeIsInSlaveMode(pScrn);

    if(pSiS->VGAEngine == SIS_315_VGA) {
       andSISIDXREG(SISSR, 0x31, 0xcf);
       inSISIDXREG(SISSR, 0x2b, pSiS->SR2b);
       inSISIDXREG(SISSR, 0x2c, pSiS->SR2c);
    }

    if((!pSiS->UseVESA) && (pSiS->VBFlags & CRT2_ENABLE)) {

	if(pSiS->VBFlags != pSiS->VBFlags_backup) {
	   pSiS->VBFlags = pSiS->VBFlags_backup;
	   xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, 4,
			"VBFlags restored to %0x\n", pSiS->VBFlags);
	}

	/* -) We can't switch off CRT1 if bridge is in SlaveMode.
	 * -) If we change to a SlaveMode-Mode (like 512x384), we
	 *    need to adapt VBFlags for eg. Xv.
	 */
#ifdef SISDUALHEAD
	if(!pSiS->DualHeadMode) {
#endif
	   if(IsInSlaveMode) {
	      doit = FALSE;
	      temp = pSiS->VBFlags;
	      pSiS->VBFlags &= (~VB_DISPMODE_SINGLE);
	      pSiS->VBFlags |= (VB_DISPMODE_MIRROR | DISPTYPE_DISP1);
              if(temp != pSiS->VBFlags) {
		 xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, 4,
		 	"VBFlags changed to 0x%0x\n", pSiS->VBFlags);
	      }
	   }
#ifdef SISDUALHEAD
	}
#endif

	if(pSiS->VGAEngine == SIS_315_VGA) {

	   if((pSiS->CRT1off) && (doit)) {
	      orSISIDXREG(SISCR,pSiS->myCR63,0x40);
	      orSISIDXREG(SISSR,0x1f,0xc0);
	      andSISIDXREG(SISSR,0x07,~0x10);
	      andSISIDXREG(SISSR,0x06,0xe2);
	      andSISIDXREG(SISSR,0x31,0xcf);
	      outSISIDXREG(SISSR,0x2b,0x1b);
	      outSISIDXREG(SISSR,0x2c,0xe1);
	      outSISIDXREG(SISSR,0x2d,0x01);
	      outSISIDXREG(SISSR, 0x00, 0x01);
	      usleep(10000);
	      outSISIDXREG(SISSR, 0x00, 0x03);
	   } else {
	      andSISIDXREG(SISCR,pSiS->myCR63,0xBF);
	      andSISIDXREG(SISSR,0x1f,0x3f);
	      orSISIDXREG(SISSR,0x07,0x10);
	   }

	} else {

	   if(doit) {
	      inSISIDXREG(SISCR, 0x17, usScratchCR17);
	      if(pSiS->CRT1off) {
		 if(usScratchCR17 & 0x80) {
		    flag = TRUE;
		    usScratchCR17 &= ~0x80;
		 }
		 orSISIDXREG(SISSR,0x1f,0xc0);
	      } else {
		 if(!(usScratchCR17 & 0x80)) {
		    flag = TRUE;
		    usScratchCR17 |= 0x80;
		 }
		 andSISIDXREG(SISSR,0x1f,0x3f);
	      }
	      /* Reset only if status changed */
	      if(flag) {
		 outSISIDXREG(SISCR, 0x17, usScratchCR17);
		 outSISIDXREG(SISSR, 0x00, 0x01);
		 usleep(10000);
		 outSISIDXREG(SISSR, 0x00, 0x03);
	      }
	   }
	}

    }

    /* Set bridge to "disable CRT2" mode if CRT2 is disabled, LCD-A is enabled */
    /* (Not needed for CRT1=VGA since CRT2 will really be disabled then) */
#ifdef SISDUALHEAD
    if(!pSiS->DualHeadMode) {
#endif
       if((pSiS->VGAEngine == SIS_315_VGA)  && (pSiS->VBFlags2 & VB2_SISLCDABRIDGE)) {
	  if((!pSiS->UseVESA) && (!(pSiS->VBFlags & CRT2_ENABLE)) && (pSiS->VBFlags & CRT1_LCDA)) {
	     if(!IsInSlaveMode) {
	        andSISIDXREG(SISPART4,0x0d,~0x07);
	     }
	  }
       }
#ifdef SISDUALHEAD
    }
#endif

    /* Reset flags */
    pSiS->MiscFlags &= ~( MISC_CRT1OVERLAY	|
			  MISC_CRT2OVERLAY	|
			  MISC_CRT1OVERLAYGAMMA	|
			  MISC_SIS760ONEOVERLAY	|
			  MISC_PANELLINKSCALER	|
			  MISC_STNMODE		|
			  MISC_INTERLACE	|
			  MISC_NOMONOHWCURSOR	|
			  MISC_NORGBHWCURSOR	|
			  MISC_CURSORDOUBLESIZE	|
			  MISC_CURSORMAXHALF );

    pSiS->SiS_SD2_Flags &= ~SiS_SD2_SIS760ONEOVL;

#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
       if(pSiSEnt->pScrn_1) {
	  SISPTR(pSiSEnt->pScrn_1)->MiscFlags &= ~(MISC_SIS760ONEOVERLAY	|
						   MISC_CRT1OVERLAY		|
						   MISC_CRT2OVERLAY		|
						   MISC_CRT1OVERLAYGAMMA	|
						   MISC_PANELLINKSCALER		|
						   MISC_STNMODE);
	  SISPTR(pSiSEnt->pScrn_1)->SiS_SD2_Flags &= ~SiS_SD2_SIS760ONEOVL;
       }
       if(pSiSEnt->pScrn_2) {
	  SISPTR(pSiSEnt->pScrn_2)->MiscFlags &= ~(MISC_SIS760ONEOVERLAY	|
						   MISC_CRT1OVERLAY		|
						   MISC_CRT2OVERLAY		|
						   MISC_CRT1OVERLAYGAMMA	|
						   MISC_PANELLINKSCALER		|
						   MISC_STNMODE);
	  SISPTR(pSiSEnt->pScrn_2)->SiS_SD2_Flags &= ~SiS_SD2_SIS760ONEOVL;
       }
    }
#endif

    /* Determine if the video overlay can be used */
    if(!pSiS->NoXvideo) {

       int clklimit1=0, clklimit2=0, clklimitg=0;
       Bool OverlayHandled = FALSE;

       inSISIDXREG(SISSR,0x2b,sr2b);
       inSISIDXREG(SISSR,0x2c,sr2c);
       myclock1 = myclock2 = SiSGetClockFromRegs(sr2b, sr2c);
       inSISIDXREG(SISSR,0x06,tmpreg);
       switch((tmpreg & 0x1c) >> 2) {
       case 0:  mycoldepth1 = 1; break;
       case 1:
       case 2:  mycoldepth1 = 2; break;
       default: mycoldepth1 = 4;
       }
       mycoldepth2 = mycoldepth1;

       if((!IsInSlaveMode) && (pSiS->VBFlags & CRT2_ENABLE)) {
	  if(pSiS->VBFlags2 & VB2_SISBRIDGE) {
	     inSISIDXREG(SISPART4,0x0a,sr2b);
	     inSISIDXREG(SISPART4,0x0b,sr2c);
	  } else {
	     inSISIDXREG(SISSR,0x2e,sr2b);
	     inSISIDXREG(SISSR,0x2f,sr2c);
	  }
	  myclock2 = SiSGetClockFromRegs(sr2b, sr2c);
	  inSISIDXREG(SISPART1,0x00,tmpreg);
	  tmpreg &= 0x0f;
	  switch(tmpreg) {
	  case 8:  mycoldepth2 = 1; break;
	  case 4:
	  case 2:  mycoldepth2 = 2; break;
	  default: mycoldepth2 = 4;
	  }
       }

       switch(pSiS->ChipType) {

	 case SIS_300:
	 case SIS_540:
	 case SIS_630:
	 case SIS_730:
	    clklimit1 = clklimit2 = clklimitg = 150;
	    break;

	 case SIS_550:
	    clklimit1 = clklimit2 = clklimitg = 150; /* ? */
	    break;

	 case SIS_650:
	 case SIS_740:
	    clklimit1 = clklimit2 = 175;  /* verified for 65x */
	    clklimitg = 166;		  /* ? */
	    break;

	 case SIS_661:
	 case SIS_741:
	 case SIS_670:
        case SIS_662:
	 case SIS_671:
#ifndef SIS761MEMFIX
	 case SIS_761:
#endif
#ifndef SIS770MEMFIX
	 case SIS_770:
#endif
	    clklimit1 = clklimit2 = 170; /* verified M661FX */
	    clklimitg = 170;		 /* verified M661FX */
	    break;

	 case SIS_760:
#ifdef SIS761MEMFIX
	 case SIS_761:
#endif
#ifdef SIS770MEMFIX
	 case SIS_770:
#endif
	    clklimit1 = clklimit2 = 170;    /* ? */
	    if(pSiS->ChipFlags & SiSCF_760LFB) {		/* LFB only or hybrid */
	       clklimit1 = clklimit2 = 180; /* ? */
	    }
	    clklimitg = 170;		    /* ? */

	    if(pSiS->SiS_SD2_Flags & SiS_SD2_SUPPORT760OO) {	/* UMA only */

	       Bool OnlyOne = FALSE, NoOverlay = FALSE;
	       int dotclocksum = 0;

	       if(pSiS->VBFlags & DISPTYPE_CRT1)                     dotclocksum += myclock1;
	       if((!IsInSlaveMode) && (pSiS->VBFlags & CRT2_ENABLE)) dotclocksum += myclock2;

	       /* TODO: Find out under what circumstances only one
		*	overlay is usable in UMA-only mode.
		*	This is not entirely accurate; the overlay
		*	scaler also requires some time, so even though
		*	the dotclocks are below these values, some
		*	distortions in the overlay may occure.
		*	Solution: Don't use a 760 with shared memory
		*       or use the video blitter by default.
		*/
	       if( (pSiS->VBFlags & DISPTYPE_CRT1) &&
		   (pSiS->VBFlags & CRT2_ENABLE) &&
		   (mycoldepth1 != mycoldepth2) ) {

		  /* 0. If coldepths are different (only possible in dual head mode),
		   *    I have no idea to calculate the limits; hence, allow only one
		   *    overlay in all cases.
		   */
		  OnlyOne = TRUE;

	       } else if(pSiS->MemClock < 150000) {

		  /* 1. MCLK <150: If someone seriously considers using such
		   *    slow RAM, so be it. Only one overlay in call cases.
		   */
		  OnlyOne = TRUE;

	       } else if(pSiS->MemClock < 170000) {

		  /* 2. MCLK 166 */
		  switch(pSiS->CurrentLayout.bitsPerPixel) {
		     case 32: if(dotclocksum > 133) OnlyOne = TRUE;		/* One overlay; verified */
			      if(dotclocksum > 180) NoOverlay = TRUE;		/* No overlay; verified */
			      break;
		     case 16: if(dotclocksum > 175) OnlyOne = TRUE;		/* One overlay; verified */
			      if(dotclocksum > 260) NoOverlay = TRUE;;		/* No overlay; FIXME */
			      break;
		  }

	       } else if(pSiS->MemClock < 210000) {

		  /* 3. MCLK 200 */
		  switch(pSiS->CurrentLayout.bitsPerPixel) {
		     case 32: if(dotclocksum > 160) OnlyOne = TRUE;		/* One overlay; FIXME */
			      if(dotclocksum > 216) NoOverlay = TRUE;;		/* No overlay; FIXME */
			      break;
		     case 16: if(dotclocksum > 210) OnlyOne = TRUE;		/* One overlay; FIXME */
			      if(dotclocksum > 312) NoOverlay = TRUE;;		/* No overlay; FIXME */
			      break;
		  }

	       }

	       if(OnlyOne || NoOverlay) {

		  ULong tmpflags = 0;

		  if(!NoOverlay) {
		     if(myclock1 <= clklimit1) tmpflags |= MISC_CRT1OVERLAY;
		     if(myclock2 <= clklimit2) tmpflags |= MISC_CRT2OVERLAY;
		     if(myclock1 <= clklimitg) tmpflags |= MISC_CRT1OVERLAYGAMMA;
		     pSiS->MiscFlags |= tmpflags;
		  }
		  pSiS->MiscFlags |= MISC_SIS760ONEOVERLAY;
		  pSiS->SiS_SD2_Flags |= SiS_SD2_SIS760ONEOVL;
#ifdef SISDUALHEAD
		  SiS_SetDHFlags(pSiS, (tmpflags | MISC_SIS760ONEOVERLAY), SiS_SD2_SIS760ONEOVL);
#endif
		  OverlayHandled = TRUE;
	       }

	       xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, 3,
			"SiS76x/UMA: %s video overlay(s) available in current mode\n",
			NoOverlay ? "no" : ((pSiS->MiscFlags & MISC_SIS760ONEOVERLAY) ? "one" : "two"));

#ifdef TWDEBUG
	       xf86DrvMsg(0, 0, "SiS760: Memclock %d, c1 %d/%d c2 %d/%d, sum %d / %x\n",
			pSiS->MemClock, myclock1, mycoldepth1,
			myclock2, mycoldepth2, dotclocksum, pSiS->SiS_SD2_Flags);
#endif
	    }
	    break;

	 case SIS_660:
	    clklimit1 = clklimit2 = 170;  /* ? */
	    if(pSiS->ChipFlags & SiSCF_760LFB) {		/* LFB only */
	       clklimit1 = clklimit2 = 180;
	    }
	    clklimitg = 180;		  /* ? */
	    break;

	 case SIS_315H:
	 case SIS_315:
	 case SIS_315PRO:
	 case SIS_330:
	    clklimit1 = clklimit2 = 180;  /* ? */
	    clklimitg = 166;		  /* ? */
	    break;

	 case SIS_340:
	 case SIS_341:
	 case SIS_342:
	 case XGI_40:
	    clklimit1 = clklimit2 = 200;  /* ? */
	    clklimitg = 200;		  /* ? */
	    break;

	 default:
	    OverlayHandled = TRUE;
       }

       if(!OverlayHandled) {
          ULong tmpflags = 0;
          if(myclock1 <= clklimit1) tmpflags |= MISC_CRT1OVERLAY;
          if(myclock2 <= clklimit2) tmpflags |= MISC_CRT2OVERLAY;
          if(myclock1 <= clklimitg) tmpflags |= MISC_CRT1OVERLAYGAMMA;
	  pSiS->MiscFlags |= tmpflags;
#ifdef SISDUALHEAD
	  SiS_SetDHFlags(pSiS, tmpflags, 0);
#endif
          if(!(pSiS->MiscFlags & MISC_CRT1OVERLAY)) {
#ifdef SISDUALHEAD
             if((!pSiS->DualHeadMode) || (pSiS->SecondHead))
#endif
		xf86DrvMsgVerb(pScrn->scrnIndex, X_WARNING, 3,
		   "Current dotclock (%dMhz) too high for video overlay on CRT1\n",
		   myclock1);
          }
          if((pSiS->VBFlags & CRT2_ENABLE) && (!(pSiS->MiscFlags & MISC_CRT2OVERLAY))) {
#ifdef SISDUALHEAD
	     if((!pSiS->DualHeadMode) || (!pSiS->SecondHead))
#endif
		xf86DrvMsgVerb(pScrn->scrnIndex, X_WARNING, 3,
		   "Current dotclock (%dMhz) too high for video overlay on CRT2\n",
		   myclock2);
	  }
       }

    }

    /* Determine if mode is interlace (CRT1 only) */
    /* (Dual head: Do NOT copy this flags over to pSiSEnt;
     * instead decide for CRT1 only)
     */
#ifdef SISDUALHEAD
    if((!pSiS->DualHeadMode) || (pSiS->SecondHead)) {
#endif
       if(pSiS->VBFlags & DISPTYPE_DISP1) {
	  inSISIDXREG(SISSR, 0x06, tmpreg);
	  if(tmpreg & 0x20) pSiS->MiscFlags |= MISC_INTERLACE;
       }
#ifdef SISDUALHEAD
    }
#endif

    /* Determine if hw cursor is supported with current mode */
    /* (Dual head: Do NOT copy these flags over to pSiSEnt;
     * instead decide for each head)
     * TODO: Check for interlace on 550, 740
     */
    switch(pSiS->VGAEngine) {
    case SIS_300_VGA:
       if((pSiS->CurrentLayout.bitsPerPixel == 8) &&
	  (pSiS->VBFlags & CRT2_ENABLE) &&
	  !IsInSlaveMode)
	  pSiS->MiscFlags |= MISC_NORGBHWCURSOR;
       if(pSiS->MiscFlags & MISC_INTERLACE)
	  pSiS->MiscFlags |= (MISC_NOMONOHWCURSOR | MISC_NORGBHWCURSOR);
       break;
    case SIS_315_VGA:
       if((pSiS->CurrentLayout.bitsPerPixel == 8) &&
	  (pSiS->VBFlags & CRT2_ENABLE) &&
	  !IsInSlaveMode)
	  pSiS->MiscFlags |= MISC_NORGBHWCURSOR;
       if(pSiS->Chipset == PCI_CHIP_SIS550) {
#ifdef SISDUALHEAD
	  if((!pSiS->DualHeadMode) || (!pSiS->SecondHead))
#endif
	     if((pSiS->FSTN || pSiS->DSTN) && (pSiS->VBFlags & CRT2_LCD))
	        pSiS->MiscFlags |= (MISC_NOMONOHWCURSOR | MISC_NORGBHWCURSOR);
       }
       break;
    }

    /* Determine whether the HW cursor image needs to stretched */
    /* (which is the case in double scan modes)
     */
    {
       unsigned char tmp1 = 0;

       tmpreg = 0;

       if(pSiS->VBFlags & DISPTYPE_DISP1) {
          inSISIDXREG(SISCR, 0x09, tmpreg);
          tmpreg &= 0x80;
       }
       if(pSiS->VBFlags & DISPTYPE_DISP2) {
          if(IsInSlaveMode) {
             inSISIDXREG(SISCR, 0x09, tmp1);
             tmp1 &= 0x80;
          } else if(pSiS->VGAEngine == SIS_315_VGA) {
             inSISIDXREG(SISPART1, 0x2c, tmp1);
             tmp1 &= 0x80;
          }
       }

#ifdef SISDUALHEAD
       if(pSiS->DualHeadMode) {
          if(pSiS->SecondHead) {
             if(tmpreg)
                pSiS->MiscFlags |= (MISC_CURSORMAXHALF | MISC_CURSORDOUBLESIZE);
          } else {
             if(tmp1)
                pSiS->MiscFlags |= (MISC_CURSORMAXHALF | MISC_CURSORDOUBLESIZE);
          }
       } else
#endif
#ifdef SISMERGED
	      if(pSiS->MergedFB) {
	  if(tmpreg || tmp1) {
	     pSiS->MiscFlags |= MISC_CURSORMAXHALF;
	  }
	  if(tmpreg && tmp1) {
	     pSiS->MiscFlags |= MISC_CURSORDOUBLESIZE;
	  }
       } else
#endif
	      if(tmpreg || tmp1) {
	  pSiS->MiscFlags |= (MISC_CURSORMAXHALF | MISC_CURSORDOUBLESIZE);
       }
    }

    /* Determine if the Panel Link scaler is active */
    if(pSiS->VBFlags & (CRT2_LCD | CRT1_LCDA)) {
       ULong tmpflags = 0;
       if(pSiS->VGAEngine == SIS_300_VGA) {
	  if(pSiS->VBFlags2 & (VB2_LVDS | VB2_30xBDH)) {
	     inSISIDXREG(SISPART1,0x1e,tmpreg);
	     tmpreg &= 0x3f;
	     if(tmpreg) tmpflags |= MISC_PANELLINKSCALER;
	  }
       } else {
	  if((pSiS->VBFlags2 & (VB2_LVDS | VB2_30xBDH)) || (pSiS->VBFlags & CRT1_LCDA)) {
	     inSISIDXREG(SISPART1,0x35,tmpreg);
	     tmpreg &= 0x04;
	     if(!tmpreg)  tmpflags |= MISC_PANELLINKSCALER;
	  }
       }
       pSiS->MiscFlags |= tmpflags;
#ifdef SISDUALHEAD
       SiS_SetDHFlags(pSiS, tmpflags, 0);
#endif
    }

    /* Determine if STN is active */
    if(pSiS->ChipType == SIS_550) {
       if((pSiS->VBFlags & CRT2_LCD) && (pSiS->FSTN || pSiS->DSTN)) {
	  inSISIDXREG(SISCR,0x34,tmpreg);
	  tmpreg &= 0x7f;
	  if(tmpreg == 0x5a || tmpreg == 0x5b) {
	     pSiS->MiscFlags |= MISC_STNMODE;
#ifdef SISDUALHEAD
	     SiS_SetDHFlags(pSiS, MISC_STNMODE, 0);
#endif
	  }
       }
    }

    if(pSiS->VGAEngine == SIS_315_VGA) {
       int i;
#ifdef SISVRAMQ
       /* Re-Enable and reset command queue */
       SiSEnableTurboQueue(pScrn);
#endif
       /* Get HWCursor register contents for backup */
       for(i = 0; i < 16; i++) {
          pSiS->HWCursorBackup[i] = SIS_MMIO_IN32(pSiS->IOBase, 0x8500 + (i << 2));
       }
       if(pSiS->ChipType >= SIS_330) {
          /* Enable HWCursor protection (Y pos as trigger) */
          andSISIDXREG(SISCR, 0x5b, ~0x30);
       }
    }

    /* Re-initialize accelerator engine */
    /* (We are sync'ed here) */
    if(!pSiS->NoAccel) {
       if(pSiS->InitAccel) {
          (pSiS->InitAccel)(pScrn);
       }
    }

    /* Set display device gamma (for SISCTRL) */
    if(pSiS->VBFlags3 & VB3_CRT1_LCD)
       pSiS->CRT1MonGamma = pSiS->CRT1LCDMonitorGamma;
    else if(pSiS->VBFlags & CRT1_LCDA)
       pSiS->CRT1MonGamma = pSiS->CRT2LCDMonitorGamma;
    else
       pSiS->CRT1MonGamma = pSiS->CRT1VGAMonitorGamma;

    if(pSiS->VBFlags & CRT2_LCD)
       pSiS->CRT2MonGamma = pSiS->CRT2LCDMonitorGamma;
    else if(pSiS->VBFlags & CRT2_TV) {
       if(pSiS->VBFlags & TV_YPBPR)
          pSiS->CRT2MonGamma = 2200; /* ? */
       else if(pSiS->VBFlags & TV_HIVISION)
          pSiS->CRT2MonGamma = 2200; /* ? */
       else if(pSiS->VBFlags & TV_NTSC)
          pSiS->CRT2MonGamma = 2200; /* NTSC */
       else
          pSiS->CRT2MonGamma = 2800; /* All PAL modes? */
    } else if(pSiS->VBFlags & CRT2_VGA)
       pSiS->CRT2MonGamma = pSiS->CRT2VGAMonitorGamma;
    else
       pSiS->CRT2MonGamma = 0; /* Unknown */

    /* Reset XV display properties (such as number of overlays, etc) */
    /* (And copy monitor gamma) */
#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
       if(pSiSEnt->pScrn_1) {
	  if(SISPTR(pSiSEnt->pScrn_1)->ResetXvDisplay) {
	     (SISPTR(pSiSEnt->pScrn_1)->ResetXvDisplay)(pSiSEnt->pScrn_1);
	  }
	  SISPTR(pSiSEnt->pScrn_1)->CRT1MonGamma = pSiS->CRT1MonGamma;
	  SISPTR(pSiSEnt->pScrn_1)->CRT2MonGamma = pSiS->CRT2MonGamma;
       }
       if(pSiSEnt->pScrn_2) {
	  if(SISPTR(pSiSEnt->pScrn_2)->ResetXvDisplay) {
	     (SISPTR(pSiSEnt->pScrn_1)->ResetXvDisplay)(pSiSEnt->pScrn_2);
	  }
	  SISPTR(pSiSEnt->pScrn_2)->CRT1MonGamma = pSiS->CRT1MonGamma;
	  SISPTR(pSiSEnt->pScrn_2)->CRT2MonGamma = pSiS->CRT2MonGamma;
       }
    } else {
#endif
       if(pSiS->ResetXvDisplay) {
	  (pSiS->ResetXvDisplay)(pScrn);
       }
#ifdef SISDUALHEAD
    }
#endif

    /* Reset XV gamma correction */
    if(pSiS->ResetXvGamma) {
       (pSiS->ResetXvGamma)(pScrn);
    }

    /* Reset various display parameters */
    {
       int val = pSiS->siscrt1satgain;
#ifdef SISDUALHEAD
       if(pSiS->DualHeadMode && pSiSEnt) val = pSiSEnt->siscrt1satgain;
#endif
       SiS_SetSISCRT1SaturationGain(pScrn, val);
    }

    /*  Apply TV settings given by options */
    if(pSiS->VBFlags & CRT2_TV) {
       SiSPostSetModeTVParms(pScrn);
    }
}

/* Post-set SiS6326 TV registers */
static void
SiS6326PostSetMode(ScrnInfoPtr pScrn, SISRegPtr sisReg)
{
    SISPtr pSiS = SISPTR(pScrn);
    UChar tmp;
    int val;

    if(!(pSiS->SiS6326Flags & SIS6326_TVDETECTED)) return;

#ifdef UNLOCK_ALWAYS
    sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

    /* Backup default TV position registers */
    pSiS->tvx1 = SiS6326GetTVReg(pScrn,0x3a);
    pSiS->tvx1 |= ((SiS6326GetTVReg(pScrn,0x3c) & 0x0f) << 8);
    pSiS->tvx2 = SiS6326GetTVReg(pScrn,0x26);
    pSiS->tvx2 |= ((SiS6326GetTVReg(pScrn,0x27) & 0xf0) << 4);
    pSiS->tvx3 = SiS6326GetTVReg(pScrn,0x12);
    pSiS->tvx3 |= ((SiS6326GetTVReg(pScrn,0x13) & 0xC0) << 2);
    pSiS->tvy1 = SiS6326GetTVReg(pScrn,0x11);
    pSiS->tvy1 |= ((SiS6326GetTVReg(pScrn,0x13) & 0x30) << 4);

    /* Handle TVPosOffset options (BEFORE switching on TV) */
    if((val = pSiS->tvxpos) != 0) {
       SiS_SetTVxposoffset(pScrn, val);
    }
    if((val = pSiS->tvypos) != 0) {
       SiS_SetTVyposoffset(pScrn, val);
    }

    /* Switch on TV output. This is rather complicated, but
     * if we don't do it, TV output will flicker terribly.
     */
    if(pSiS->SiS6326Flags & SIS6326_TVON) {
       orSISIDXREG(SISSR, 0x01, 0x20);
       tmp = SiS6326GetTVReg(pScrn,0x00);
       tmp &= ~0x04;
       while(!(inSISREG(SISINPSTAT) & 0x08));    /* Wait while NOT vb */
       SiS6326SetTVReg(pScrn,0x00,tmp);
       for(val=0; val < 2; val++) {
         while(!(inSISREG(SISINPSTAT) & 0x08));  /* Wait while NOT vb */
         while(inSISREG(SISINPSTAT) & 0x08);     /* wait while vb     */
       }
       SiS6326SetTVReg(pScrn, 0x00, sisReg->sis6326tv[0]);
       tmp = inSISREG(SISINPSTAT);
       outSISREG(SISAR, 0x20);
       tmp = inSISREG(SISINPSTAT);
       while(inSISREG(SISINPSTAT) & 0x01);
       while(!(inSISREG(SISINPSTAT) & 0x01));
       andSISIDXREG(SISSR, 0x01, ~0x20);
       for(val=0; val < 10; val++) {
         while(!(inSISREG(SISINPSTAT) & 0x08));  /* Wait while NOT vb */
         while(inSISREG(SISINPSTAT) & 0x08);     /* wait while vb     */
       }
       andSISIDXREG(SISSR, 0x01, ~0x20);
    }

    tmp = SiS6326GetTVReg(pScrn,0x00);
    if(!(tmp & 0x04)) return;

    /* Apply TV settings given by options */
    if((val = pSiS->sistvantiflicker) != -1) {
       SiS_SetSIS6326TVantiflicker(pScrn, val);
    }
    if((val = pSiS->sis6326enableyfilter) != -1) {
       SiS_SetSIS6326TVenableyfilter(pScrn, val);
    }
    if((val = pSiS->sis6326yfilterstrong) != -1) {
       SiS_SetSIS6326TVyfilterstrong(pScrn, val);
    }

}

/* Get VESA mode number from given resolution/depth */
static UShort
SiSCalcVESAModeIndex(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
    SISPtr pSiS = SISPTR(pScrn);
    sisModeInfoPtr m = pSiS->SISVESAModeList;
    UShort i = pSiS->CurrentLayout.bytesPerPixel - 1;
    UShort ModeNumber = 0;
    int j;

    while(m) {
       if( (pSiS->CurrentLayout.bitsPerPixel == m->bpp) &&
	   (mode->HDisplay == m->width)    &&
	   (mode->VDisplay == m->height) )
	  return m->n;
       m = m->next;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
        "No valid VESA BIOS mode found for %dx%d (%d bpp)\n",
        mode->HDisplay, mode->VDisplay, pScrn->bitsPerPixel);

    if(!pSiS->ROM661New) {  /* VESA numbers changed! */
       j = 0;
       while(VESAModeIndices[j] != 9999) {
          if( (mode->HDisplay == VESAModeIndices[j]) &&
	      (mode->VDisplay == VESAModeIndices[j+1]) ) {
	     ModeNumber = VESAModeIndices[j + 2 + i];
	     break;
          }
          j += 6;
       }

       if(!ModeNumber) {
	  xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
	      "No valid mode found for %dx%dx%d in built-in table either.\n",
	      mode->HDisplay, mode->VDisplay, pScrn->bitsPerPixel);
       }
    }

    return(ModeNumber);
}

static Bool
SiSSetVESAMode(ScrnInfoPtr pScrn, DisplayModePtr pMode)
{
    SISPtr pSiS;
    int mode;

    pSiS = SISPTR(pScrn);

    if(!(mode = SiSCalcVESAModeIndex(pScrn, pMode)))
       return FALSE;

    mode |= (1 << 15);	/* Don't clear framebuffer */
    mode |= (1 << 14); 	/* Use linear adressing */

    if(VBESetVBEMode(pSiS->pVbe, mode, NULL) == FALSE) {
       SISErrorLog(pScrn, "Setting VESA mode 0x%x failed\n",
	             	mode & 0x0fff);
       return FALSE;
    }

    if(pMode->HDisplay != pScrn->virtualX) {
       VBESetLogicalScanline(pSiS->pVbe, pScrn->virtualX);
    }

    xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
    	"Setting VESA mode 0x%x succeeded\n",
	mode & 0x0fff);

    return TRUE;
}

static void
SISModifyModeInfo(DisplayModePtr mode)
{
    if(mode->CrtcHBlankStart == mode->CrtcHDisplay)
        mode->CrtcHBlankStart++;
    if(mode->CrtcHBlankEnd == mode->CrtcHTotal)
        mode->CrtcHBlankEnd--;
    if(mode->CrtcVBlankStart == mode->CrtcVDisplay)
        mode->CrtcVBlankStart++;
    if(mode->CrtcVBlankEnd == mode->CrtcVTotal)
        mode->CrtcVBlankEnd--;
}

static void
SiS_SiSLVDSBackLight(SISPtr pSiS, Bool blon)
{
    unsigned char p4_26;
    if(pSiS->VBFlags2 & VB2_SISLVDSBRIDGE) {
       inSISIDXREG(SISPART4, 0x26, p4_26);
       if(!blon) {
	  SiS_SiS30xBLOff(pSiS->SiS_Pr);
       } else {
          if(p4_26 & 0x02) {
	     SiS_DDC2Delay(pSiS->SiS_Pr, 0xff00);
	     SiS_SiS30xBLOn(pSiS->SiS_Pr);
	  }
       }
    }
}

/* Initialize a new mode */

static Bool
SISModeInit(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
    SISPtr pSiS = SISPTR(pScrn);
    SISRegPtr sisReg;
#ifdef SISDUALHEAD
    SISEntPtr pSiSEnt = NULL;
#endif

    andSISIDXREG(SISCR,0x11,0x7f);	/* Unlock CRTC registers */

    SISModifyModeInfo(mode);		/* Quick check of the mode parameters */

    if(pSiS->VGAEngine == SIS_300_VGA || pSiS->VGAEngine == SIS_315_VGA) {
       SiSRegInit(pSiS->SiS_Pr, pSiS->RelIO+0x30);
    }

    if(pSiS->UseVESA) {  /* With VESA: */

#ifdef SISDUALHEAD
       /* No dual head mode when using VESA */
       if(pSiS->SecondHead) return TRUE;
#endif

       pScrn->vtSema = TRUE;

       /*
	* This order is required:
	* The video bridge needs to be adjusted before the
	* BIOS is run as the BIOS sets up CRT2 according to
	* these register settings.
	* After the BIOS is run, the bridges and turboqueue
	* registers need to be readjusted as the BIOS may
	* very probably have messed them up.
	*/
       if(pSiS->VGAEngine == SIS_300_VGA || pSiS->VGAEngine == SIS_315_VGA) {
	  SiSPreSetMode(pScrn, mode, SIS_MODE_SIMU);
       }
       if(!SiSSetVESAMode(pScrn, mode)) {
	  SISErrorLog(pScrn, "SiSSetVESAMode() failed\n");
	  return FALSE;
       }
       sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
       if(pSiS->VGAEngine == SIS_300_VGA || pSiS->VGAEngine == SIS_315_VGA) {
	  SiSPreSetMode(pScrn, mode, SIS_MODE_SIMU);
	  SiSPostSetMode(pScrn, &pSiS->ModeReg);
       }
#ifdef TWDEBUG
       xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "REAL REGISTER CONTENTS AFTER SETMODE:\n");
#endif
       if(!(*pSiS->ModeInit)(pScrn, mode)) {
	  SISErrorLog(pScrn, "ModeInit() failed\n");
	  return FALSE;
       }

       SiSVGAProtect(pScrn, TRUE);
       (*pSiS->SiSRestore)(pScrn, &pSiS->ModeReg);
       SiSVGAProtect(pScrn, FALSE);

    } else { /* Without VESA: */

#ifdef SISDUALHEAD
       if(pSiS->DualHeadMode) {

	  if(!(*pSiS->ModeInit)(pScrn, mode)) {
	     SISErrorLog(pScrn, "ModeInit() failed\n");
	     return FALSE;
	  }

	  pScrn->vtSema = TRUE;

	  pSiSEnt = pSiS->entityPrivate;

	  if(!(pSiS->SecondHead)) {
	     /* Head 1 (master) is always CRT2 */
	     pSiS->SiS_Pr->SiS_EnableBackLight = TRUE;
	     SiSPreSetMode(pScrn, mode, SIS_MODE_CRT2);
	     if(!SiSBIOSSetModeCRT2(pSiS->SiS_Pr, pScrn, mode, pSiS->IsCustom)) {
		SISErrorLog(pScrn, "SiSBIOSSetModeCRT2() failed\n");
		return FALSE;
	     }
	     SiSPostSetMode(pScrn, &pSiS->ModeReg);
	     if(pSiSEnt->pScrn_2) {
	        /* No need to go through pScrn->AdjustFrame; the coords
	         * didn't change
	         */
		SISAdjustFrame(pSiSEnt->pScrn_2,
			       pSiSEnt->pScrn_2->frameX0,
			       pSiSEnt->pScrn_2->frameY0);
	     }
	  } else {
	     /* Head 2 (slave) is always CRT1 */
	     pSiS->SiS_Pr->SiS_EnableBackLight = FALSE;
	     SiSPreSetMode(pScrn, mode, SIS_MODE_CRT1);
	     if(!SiSBIOSSetModeCRT1(pSiS->SiS_Pr, pScrn, mode, pSiS->IsCustom)) {
		SiS_SiSLVDSBackLight(pSiS, TRUE);
		SISErrorLog(pScrn, "SiSBIOSSetModeCRT1() failed\n");
		return FALSE;
	     }
	     SiS_SiSLVDSBackLight(pSiS, TRUE);
	     SiSPostSetMode(pScrn, &pSiS->ModeReg);
	     if(pSiSEnt->pScrn_1) {
	        /* No need to go through pScrn->AdjustFrame; the coords
	         * didn't change
	         */
		SISAdjustFrame(pSiSEnt->pScrn_1,
			       pSiSEnt->pScrn_1->frameX0,
			       pSiSEnt->pScrn_1->frameY0);
	     }
	  }

       } else {
#endif

	  if(pSiS->VGAEngine == SIS_300_VGA || pSiS->VGAEngine == SIS_315_VGA) {

	     if(!(*pSiS->ModeInit)(pScrn, mode)) {
		SISErrorLog(pScrn, "ModeInit() failed\n");
	        return FALSE;
	     }

	     pScrn->vtSema = TRUE;

#ifdef SISMERGED
	     if(pSiS->MergedFB) {

		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Setting MergedFB mode %dx%d\n",
				mode->HDisplay, mode->VDisplay);

		pSiS->SiS_Pr->SiS_EnableBackLight = FALSE;

		SiSPreSetMode(pScrn, mode, SIS_MODE_CRT1);

		if(!SiSBIOSSetModeCRT1(pSiS->SiS_Pr, pScrn,
		                       ((SiSMergedDisplayModePtr)mode->Private)->CRT1,
				       pSiS->IsCustom)) {
		   SiS_SiSLVDSBackLight(pSiS, TRUE);
		   SISErrorLog(pScrn, "SiSBIOSSetModeCRT1() failed\n");
		   return FALSE;
		}

		SiSPreSetMode(pScrn, mode, SIS_MODE_CRT2);

		if(!SiSBIOSSetModeCRT2(pSiS->SiS_Pr, pScrn,
		                       ((SiSMergedDisplayModePtr)mode->Private)->CRT2,
				       pSiS->IsCustom)) {
		   SiS_SiSLVDSBackLight(pSiS, TRUE);
		   SISErrorLog(pScrn, "SiSBIOSSetModeCRT2() failed\n");
		   return FALSE;
		}

		SiS_SiSLVDSBackLight(pSiS, TRUE);

		(*pScrn->AdjustFrame)(pScrn, pScrn->frameX0, pScrn->frameY0);

	     } else {
#endif

		if((pSiS->VBFlags & CRT1_LCDA) || (!(mode->type & M_T_DEFAULT))) {

		   pSiS->SiS_Pr->SiS_EnableBackLight = FALSE;

		   SiSPreSetMode(pScrn, mode, SIS_MODE_CRT1);

		   if(!SiSBIOSSetModeCRT1(pSiS->SiS_Pr, pScrn,
				mode, pSiS->IsCustom)) {
		      SiS_SiSLVDSBackLight(pSiS, TRUE);
		      SISErrorLog(pScrn, "SiSBIOSSetModeCRT1() failed\n");
		      return FALSE;
		   }

		   SiSPreSetMode(pScrn, mode, SIS_MODE_CRT2);

		   if(!SiSBIOSSetModeCRT2(pSiS->SiS_Pr, pScrn,
				mode, pSiS->IsCustom)) {
		      SiS_SiSLVDSBackLight(pSiS, TRUE);
		      SISErrorLog(pScrn, "SiSBIOSSetModeCRT2() failed\n");
		      return FALSE;
		   }

		   SiS_SiSLVDSBackLight(pSiS, TRUE);

		} else {

		   pSiS->SiS_Pr->SiS_EnableBackLight = TRUE;

		   SiSPreSetMode(pScrn, mode, SIS_MODE_SIMU);

		   if(!SiSBIOSSetMode(pSiS->SiS_Pr, pScrn,
					mode, pSiS->IsCustom)) {
		      SISErrorLog(pScrn, "SiSBIOSSetMode() failed\n");
		      return FALSE;
		   }

		}

#ifdef SISMERGED
	     }
#endif
	     SiSPostSetMode(pScrn, &pSiS->ModeReg);
#ifdef TWDEBUG
	     xf86DrvMsg(pScrn->scrnIndex, X_INFO, "VBFlags %lx\n", pSiS->VBFlags);
	     xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			"REAL REGISTER CONTENTS AFTER SETMODE:\n");
             (*pSiS->ModeInit)(pScrn, mode);
#endif

	  } else {

	     /* For other chipsets, use the old method */

	     /* Prepare the register contents */
	     if(!(*pSiS->ModeInit)(pScrn, mode)) {
	        SISErrorLog(pScrn, "ModeInit() failed\n");
	        return FALSE;
	     }

	     pScrn->vtSema = TRUE;

	     /* Program the registers */
	     SiSVGAProtect(pScrn, TRUE);
	     sisReg = &pSiS->ModeReg;

	     sisReg->sisRegsATTR[0x10] = 0x01;
	     if(pScrn->bitsPerPixel > 8) {
		sisReg->sisRegsGR[0x05] = 0x00;
	     }

	     SiSVGARestore(pScrn, sisReg, SISVGA_SR_MODE);

	     (*pSiS->SiSRestore)(pScrn, sisReg);

	     if((pSiS->Chipset == PCI_CHIP_SIS6326) && (pSiS->SiS6326Flags & SIS6326_HASTV)) {
	        SiS6326PostSetMode(pScrn, &pSiS->ModeReg);
	     }

#ifdef TWDEBUG
	     xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			"REAL REGISTER CONTENTS AFTER SETMODE:\n");
	     (*pSiS->ModeInit)(pScrn, mode);
#endif

	     SiSVGAProtect(pScrn, FALSE);

	  }

#ifdef SISDUALHEAD
       }
#endif
    }

    /* Update Currentlayout */
    pSiS->CurrentLayout.mode = pSiS->currentModeLast = mode;

    return TRUE;
}


/*******************************************************/
/*                 Our BlockHandler                    */
/*******************************************************/

static void
SISBlockHandler(ScreenPtr pScreen, pointer pTimeout)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    SISPtr pSiS = SISPTR(pScrn);

    pScreen->BlockHandler = pSiS->BlockHandler;
    (*pScreen->BlockHandler) (pScreen, pTimeout);
    pScreen->BlockHandler = SISBlockHandler;

#ifdef SISDUALHEAD
    if(pSiS->NeedCopyFastVidCpy) {
       SISEntPtr pSiSEnt = pSiS->entityPrivate;
       if(pSiSEnt->HaveFastVidCpy) {
	  pSiS->NeedCopyFastVidCpy = FALSE;
	  pSiS->SiSFastVidCopy = pSiSEnt->SiSFastVidCopy;
	  pSiS->SiSFastMemCopy = pSiSEnt->SiSFastMemCopy;
	  pSiS->SiSFastVidCopyFrom = pSiSEnt->SiSFastVidCopyFrom;
	  pSiS->SiSFastMemCopyFrom = pSiSEnt->SiSFastMemCopyFrom;
       }
    }
#endif

    if(pSiS->AdjustFramePending && pSiS->AdjustFrame) {
       (*pSiS->AdjustFrame)(pScrn, pSiS->AdjustFrameX, pSiS->AdjustFrameY);
       /* Reset it since Xv insists on installing its own every time. */
       pScrn->AdjustFrame = SISNewAdjustFrame;
       pSiS->AdjustFramePending = FALSE;
    }


    if(pSiS->VideoTimerCallback) {
       (*pSiS->VideoTimerCallback)(pScrn, currentTime.milliseconds);
    }

#ifdef SIS_USE_XAA
    if(pSiS->RenderCallback) {
       (*pSiS->RenderCallback)(pScrn);
    }
#endif

#ifdef SIS_USE_EXA
    if(pSiS->ExaRenderCallback) {
       (*pSiS->ExaRenderCallback)(pScrn);
    }
#endif
}

/*******************************************************/
/*               DPMS, Screen blanking                 */
/*******************************************************/

static void
SiSHandleBackLight(SISPtr pSiS, Bool blon)
{
    UChar sr11mask = (pSiS->SiS_Pr->SiS_SensibleSR11) ? 0x03 : 0xf3;
    if(pSiS->VBFlags2 & VB2_SISLVDSBRIDGE) {

       if(!blon) {
	  SiS_SiS30xBLOff(pSiS->SiS_Pr);
       } else {
	  SiS_SiS30xBLOn(pSiS->SiS_Pr);
       }

    } else if( ((pSiS->VGAEngine == SIS_300_VGA) &&
	        (pSiS->VBFlags2 & (VB2_LVDS | VB2_30xBDH))) ||
	       ((pSiS->VGAEngine == SIS_315_VGA) &&
	        ((pSiS->VBFlags2 & (VB2_LVDS | VB2_CHRONTEL)) == VB2_LVDS)) ) {

       if(!blon) {
	   		setSISIDXREG(SISSR, 0x11, sr11mask, 0x08);
       } else {
	  setSISIDXREG(SISSR, 0x11, sr11mask, 0x00);
       }

    } else if((pSiS->VGAEngine == SIS_315_VGA) &&
	      (pSiS->VBFlags2 & VB2_CHRONTEL)) {

       if(!blon) {
	  SiS_Chrontel701xBLOff(pSiS->SiS_Pr);
       } else {
	  SiS_Chrontel701xBLOn(pSiS->SiS_Pr);
       }

    }
}

static Bool
SISSaveScreen(ScreenPtr pScreen, int mode)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SISPtr pSiS;
    Bool IsUnblank = xf86IsUnblank(mode) ? TRUE : FALSE;

    if((pScrn == NULL) || (!pScrn->vtSema)) return TRUE;

    pSiS = SISPTR(pScrn);

#ifdef UNLOCK_ALWAYS
    sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

    if(pSiS->VBFlags & (CRT2_LCD | CRT1_LCDA)) {
       SiSHandleBackLight(pSiS, IsUnblank);
    }

    if(!SiSBridgeIsInSlaveMode(pScrn)) {
       return SiSVGASaveScreen(pScreen, mode);
    }

    return TRUE;
}

#ifdef SISDUALHEAD
/* SaveScreen for dual head mode */
static Bool
SISSaveScreenDH(ScreenPtr pScreen, int mode)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SISPtr pSiS;
    Bool IsUnblank = xf86IsUnblank(mode) ? TRUE : FALSE;

    if((pScrn == NULL) || (!pScrn->vtSema)) return TRUE;

    pSiS = SISPTR(pScrn);

    if( (pSiS->SecondHead) &&
        ((!(pSiS->VBFlags & CRT1_LCDA)) || (pSiS->VBFlags2 & VB2_SISTMDSLCDABRIDGE)) ) {

       /* Slave head is always CRT1 */
       /* (No backlight handling on TMDS bridges) */
       return SiSVGASaveScreen(pScreen, mode);

    } else {

       /* Master head is always CRT2 */
       /* But we land here for LCDA, too (if bridge is SiS LVDS type) */

       /* We can only blank LCD, not other CRT2 devices */
       if(pSiS->VBFlags & (CRT2_LCD|CRT1_LCDA)) {

#ifdef UNLOCK_ALWAYS
	  sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif
	  SiSHandleBackLight(pSiS, IsUnblank);

       }

    }
    return TRUE;
}
#endif

static void
SISDisplayPowerManagementSet(ScrnInfoPtr pScrn, int PowerManagementMode, int flags)
{
    SISPtr pSiS = SISPTR(pScrn);
    Bool   docrt1 = TRUE, docrt2 = TRUE, backlight = TRUE;
    UChar  sr1=0, cr17=0, cr63=0, pmreg=0, sr7=0;
    UChar  p1_13=0, p2_0=0, oldpmreg=0;

    if(!pScrn->vtSema) return;
    xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, 4,
          "SISDisplayPowerManagementSet(%d)\n", PowerManagementMode);

#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
       if(pSiS->SecondHead) docrt2 = FALSE;
       else                 docrt1 = FALSE;
    }
#endif

    /* FIXME: in old servers, DPMSSet was supposed to be called without open
     * the correct PCI bridges before access the hardware. Now we have this
     * hook wrapped by the vga arbiter which should do all the work, in
     * kernels that implement it. For this case we might not want this hack
     * bellow.
     */
    outSISIDXREG(SISSR,0x05,0x86);
    inSISIDXREG(SISSR,0x05,pmreg);
    if(pmreg != 0xa1) return;

#ifdef UNLOCK_ALWAYS
    sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

    switch(PowerManagementMode) {

       case DPMSModeOn:      /* HSync: On, VSync: On */
	  sr1   = 0x00;
	  cr17  = 0x80;
	  pmreg = 0x00;
	  cr63  = 0x00;
	  sr7   = 0x10;
	  p2_0  = 0x20;
	  p1_13 = 0x00;
	  backlight = TRUE;
	  break;

       case DPMSModeSuspend: /* HSync: On, VSync: Off */
	  sr1   = 0x20;
	  cr17  = 0x80;
	  pmreg = 0x80;
	  cr63  = 0x40;
	  sr7   = 0x00;
	  p2_0  = 0x40;
	  p1_13 = 0x80;
	  backlight = FALSE;
	  break;

       case DPMSModeStandby: /* HSync: Off, VSync: On */
	  sr1   = 0x20;
	  cr17  = 0x80;
	  pmreg = 0x40;
	  cr63  = 0x40;
	  sr7   = 0x00;
	  p2_0  = 0x80;
	  p1_13 = 0x40;
	  backlight = FALSE;
	  break;

       case DPMSModeOff:     /* HSync: Off, VSync: Off */
	  sr1   = 0x20;
	  cr17  = 0x00;
	  pmreg = 0xc0;
	  cr63  = 0x40;
	  sr7   = 0x00;
	  p2_0  = 0xc0;
	  p1_13 = 0xc0;
	  backlight = FALSE;
	  break;

       default:
	    return;
    }

    oldpmreg = pmreg;

    if((docrt2 && (pSiS->VBFlags & CRT2_LCD)) ||
       (docrt1 && (pSiS->VBFlags & CRT1_LCDA))) {
       SiSHandleBackLight(pSiS, backlight);
    }

    if(docrt1) {
       switch(pSiS->VGAEngine) {
       case SIS_OLD_VGA:
       case SIS_530_VGA:
	    setSISIDXREG(SISSR, 0x01, ~0x20, sr1);    /* Set/Clear "Display On" bit */
	    inSISIDXREG(SISSR, 0x11, oldpmreg);
	    setSISIDXREG(SISCR, 0x17, 0x7f, cr17);
	    setSISIDXREG(SISSR, 0x11, 0x3f, pmreg);
	    break;
       case SIS_315_VGA:
	    if( (!pSiS->CRT1off) &&
	        ((!(pSiS->VBFlags & CRT1_LCDA)) || (pSiS->VBFlags2 & VB2_SISTMDSLCDABRIDGE)) ) {
	       setSISIDXREG(SISCR, pSiS->myCR63, 0xbf, cr63);
	       setSISIDXREG(SISSR, 0x07, 0xef, sr7);
	    }
	    /* fall through */
       default:
	    if(!SiSBridgeIsInSlaveMode(pScrn)) {
	       setSISIDXREG(SISSR, 0x01, ~0x20, sr1);    /* Set/Clear "Display On" bit */
	    }
	    if((!(pSiS->VBFlags & CRT1_LCDA)) || (pSiS->VBFlags2 & VB2_SISTMDSLCDABRIDGE)) {
	       inSISIDXREG(SISSR, 0x1f, oldpmreg);
	       if((!pSiS->CRT1off) && (!SiSBridgeIsInSlaveMode(pScrn))) {
		  setSISIDXREG(SISSR, 0x1f, 0x3f, pmreg);
	       }
	    }
       }
       oldpmreg &= 0xc0;
    }

    if(docrt2) {
       if(pSiS->VBFlags & CRT2_LCD) {
          if((pSiS->VBFlags2 & VB2_SISBRIDGE) &&
	     (!(pSiS->VBFlags2 & VB2_30xBDH))) {
	     if(pSiS->VGAEngine == SIS_300_VGA) {
	        SiS_UnLockCRT2(pSiS->SiS_Pr);
	        setSISIDXREG(SISPART1, 0x13, 0x3f, p1_13);
	     }
	     if(pSiS->VBFlags2 & VB2_SISLVDSBRIDGE) p2_0 |= 0x20;
	     setSISIDXREG(SISPART2, 0x00, 0x1f, p2_0);
	  }
       } else if(pSiS->VBFlags & (CRT2_VGA | CRT2_TV)) {
	  if(pSiS->VBFlags2 & VB2_SISBRIDGE) {
	     setSISIDXREG(SISPART2, 0x00, 0x1f, p2_0);
	  }
       }
    }

    if( (docrt1) &&
        (pmreg != oldpmreg) &&
        ((!(pSiS->VBFlags & CRT1_LCDA)) || (pSiS->VBFlags2 & VB2_SISTMDSLCDABRIDGE)) ) {
       outSISIDXREG(SISSR, 0x00, 0x01);    /* Synchronous Reset */
       usleep(10000);
       outSISIDXREG(SISSR, 0x00, 0x03);    /* End Reset */
    }

}

/*************************************************************/
/*                  ScreenInit() helpers                     */
/*************************************************************/

/* Gamma, brightness, contrast */

static unsigned short
calcgammaval(int j, int nramp, float invgamma, float bri, float c)
{
    float k = (float)j;
    float nrm1 = (float)(nramp - 1);
    float con = c * nrm1 / 3.0;
    float l, v;

    if(con != 0.0) {
       l = nrm1 / 2.0;
       if(con <= 0.0) {
          k -= l;
          k *= (l + con) / l;
       } else {
          l -= 1.0;
          k -= l;
          k *= l / (l - con);
       }
       k += l;
       if(k < 0.0) k = 0.0;
    }

    if(invgamma == 1.0) {
       v = k / nrm1 * 65535.0;
    } else {
       v = pow(k / nrm1, invgamma) * 65535.0 + 0.5;
    }

    v += (bri * (65535.0 / 3.0)) ;

    if(v < 0.0) v = 0.0;
    else if(v > 65535.0) v = 65535.0;
    return (unsigned short)v;
}

#ifdef SISGAMMARAMP
void
SISCalculateGammaRamp(ScreenPtr pScreen, ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
   int    i, j, nramp;
   UShort *ramp[3];
   float  gamma_max[3], framp;
   Bool   newmethod = FALSE;

   if(!(pSiS->SiS_SD3_Flags & SiS_SD3_OLDGAMMAINUSE)) {
      newmethod = TRUE;
   } else {
      gamma_max[0] = (float)pSiS->GammaBriR / 1000;
      gamma_max[1] = (float)pSiS->GammaBriG / 1000;
      gamma_max[2] = (float)pSiS->GammaBriB / 1000;
   }

   if(!(nramp = xf86GetGammaRampSize(pScreen))) return;

   for(i=0; i<3; i++) {
      ramp[i] = (UShort *)malloc(nramp * sizeof(UShort));
      if(!ramp[i]) {
	 if(ramp[0]) { free(ramp[0]); ramp[0] = NULL; }
	 if(ramp[1]) { free(ramp[1]); ramp[1] = NULL; }
	 return;
      }
   }

   if(newmethod) {

      for(i = 0; i < 3; i++) {

         float invgamma = 0.0, bri = 0.0, con = 0.0;

         switch(i) {
         case 0: invgamma = 1. / pScrn->gamma.red;
		 bri = pSiS->NewGammaBriR;
		 con = pSiS->NewGammaConR;
		 break;
         case 1: invgamma = 1. / pScrn->gamma.green;
		 bri = pSiS->NewGammaBriG;
		 con = pSiS->NewGammaConG;
		 break;
         case 2: invgamma = 1. / pScrn->gamma.blue;
		 bri = pSiS->NewGammaBriB;
                 con = pSiS->NewGammaConB;
		 break;
         }

	 for(j = 0; j < nramp; j++) {
	    ramp[i][j] = calcgammaval(j, nramp, invgamma, bri, con);
	 }

      }

   } else {

      for(i = 0; i < 3; i++) {
         int fullscale = 65535 * gamma_max[i];
         float dramp = 1. / (nramp - 1);
         float invgamma = 0.0, v;

         switch(i) {
         case 0: invgamma = 1. / pScrn->gamma.red; break;
         case 1: invgamma = 1. / pScrn->gamma.green; break;
         case 2: invgamma = 1. / pScrn->gamma.blue; break;
         }

         for(j = 0; j < nramp; j++) {
	    framp = pow(j * dramp, invgamma);

	    v = (fullscale < 0) ? (65535 + fullscale * framp) :
			       fullscale * framp;
	    if(v < 0) v = 0;
	    else if(v > 65535) v = 65535;
	    ramp[i][j] = (UShort)v;
         }
      }

   }

   xf86ChangeGammaRamp(pScreen, nramp, ramp[0], ramp[1], ramp[2]);

   free(ramp[0]);
   free(ramp[1]);
   free(ramp[2]);
   ramp[0] = ramp[1] = ramp[2] = NULL;
}
#endif

void
SISCalculateGammaRampCRT2(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
   int    i;
   int    myshift = 16 - pScrn->rgbBits;
   int    maxvalue = (1 << pScrn->rgbBits) - 1;
   int    reds = pScrn->mask.red >> pScrn->offset.red;
   int    greens = pScrn->mask.green >> pScrn->offset.green;
   int    blues = pScrn->mask.blue >> pScrn->offset.blue;
   float  framp, invgamma1, invgamma2, invgamma3, v;

   invgamma1  = 1. / pSiS->GammaR2;
   invgamma2  = 1. / pSiS->GammaG2;
   invgamma3  = 1. / pSiS->GammaB2;

   if(!(pSiS->SiS_SD3_Flags & SiS_SD3_OLDGAMMAINUSE)) {

      for(i = 0; i < pSiS->CRT2ColNum; i++) {
         pSiS->crt2gcolortable[i].red = calcgammaval(i, pSiS->CRT2ColNum, invgamma1,
			pSiS->NewGammaBriR2, pSiS->NewGammaConR2) >> myshift;
         pSiS->crt2gcolortable[i].green = calcgammaval(i, pSiS->CRT2ColNum, invgamma2,
			pSiS->NewGammaBriG2, pSiS->NewGammaConG2) >> myshift;
         pSiS->crt2gcolortable[i].blue = calcgammaval(i, pSiS->CRT2ColNum, invgamma3,
			pSiS->NewGammaBriB2, pSiS->NewGammaConB2) >> myshift;
      }

   } else {

      int fullscale1 = 65536 * (float)pSiS->GammaBriR2 / 1000;
      int fullscale2 = 65536 * (float)pSiS->GammaBriG2 / 1000;
      int fullscale3 = 65536 * (float)pSiS->GammaBriB2 / 1000;

      float dramp = 1. / (pSiS->CRT2ColNum - 1);

      for(i = 0; i < pSiS->CRT2ColNum; i++) {
         framp = pow(i * dramp, invgamma1);
         v = (fullscale1 < 0) ? (65535 + fullscale1 * framp) : fullscale1 * framp;
         if(v < 0) v = 0;
         else if(v > 65535) v = 65535;
         pSiS->crt2gcolortable[i].red = ((UShort)v) >> myshift;
         framp = pow(i * dramp, invgamma2);
         v = (fullscale2 < 0) ? (65535 + fullscale2 * framp) : fullscale2 * framp;
         if(v < 0) v = 0;
         else if(v > 65535) v = 65535;
         pSiS->crt2gcolortable[i].green = ((UShort)v) >> myshift;
         framp = pow(i * dramp, invgamma3);
         v = (fullscale3 < 0) ? (65535 + fullscale3 * framp) : fullscale3 * framp;
         if(v < 0) v = 0;
         else if(v > 65535) v = 65535;
         pSiS->crt2gcolortable[i].blue = ((UShort)v) >> myshift;
      }

   }

   for(i = 0; i < pSiS->CRT2ColNum; i++) {
      pSiS->crt2colors[i].red =
         pSiS->crt2gcolortable[i * maxvalue / reds].red;
      pSiS->crt2colors[i].green =
         pSiS->crt2gcolortable[i * maxvalue / greens].green;
      pSiS->crt2colors[i].blue  =
         pSiS->crt2gcolortable[i * maxvalue / blues].blue;
   }
}


/*************************************************************/
/*                       ScreenInit()                        */
/*************************************************************/

/* We use pScrn and not CurrentLayout here, because the
 * properties we use have not changed (displayWidth,
 * depth, bitsPerPixel)
 */
static Bool
SISScreenInit(ScreenPtr pScreen, int argc, char **argv)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SISPtr pSiS = SISPTR(pScrn);
    VisualPtr visual;
    ULong OnScreenSize;
    int ret, height, width, displayWidth;
    UChar *FBStart;
#ifdef SISDUALHEAD
    SISEntPtr pSiSEnt = NULL;
#endif

#ifdef SISDUALHEAD
    if((!pSiS->DualHeadMode) || (!pSiS->SecondHead)) {
#endif
       SiS_LoadInitVBE(pScrn);
#ifdef SISDUALHEAD
    }
#endif

#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
       pSiSEnt = pSiS->entityPrivate;
       pSiSEnt->refCount++;
    }
#endif

#ifdef SIS_PC_PLATFORM
    /* Map 64k VGA window for saving/restoring CGA fonts */
    SiS_MapVGAMem(pScrn);
#endif

    /* Map the SiS memory and MMIO areas */
    if(!SISMapMem(pScrn)) {
       SISErrorLog(pScrn, "SiSMapMem() failed\n");
       return FALSE;
    }

    SiS_SiSFB_Lock(pScrn, TRUE);

#ifdef UNLOCK_ALWAYS
    sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

    /* Enable TurboQueue so that SISSave() saves it in enabled
     * state. If we don't do this, X will hang after a restart!
     * (Happens for some unknown reason only when using VESA
     * for mode switching; assumingly a BIOS issue.)
     * This is done on 300 and 315 series only.
     */
    if(pSiS->UseVESA) {
#ifdef SISVRAMQ
       if(pSiS->VGAEngine != SIS_315_VGA)
#endif
          SiSEnableTurboQueue(pScrn);
    }

    /* Save the current state */
    SISSave(pScrn);

    if((pSiS->VGAEngine == SIS_300_VGA) || (pSiS->VGAEngine == SIS_315_VGA)) {

       /* Set up (reset) CR32, CR36, CR37 according to our detection results */
       /* (For ScreenInit() called after CloseScreen()) */
       SiSRestoreCR323637(pSiS);

       if(!pSiS->OldMode) {

          /* Try to find out current (=old) mode number
	   * (Do this only if not sisfb has told us its mode yet)
	   */

	  /* Read 0:449 which the BIOS sets to the current mode number
	   * Unfortunately, this not reliable since the int10 emulation
	   * does not change this. So if we call the VBE later, this
	   * byte won't be touched (which is why we set this manually
	   * then).
	   */
          UChar myoldmode = SiS_GetSetModeID(pScrn, 0xFF);
	  UChar cr30, cr31;

          /* Read CR34 which the BIOS sets to the current mode number for CRT2
	   * This is - of course - not reliable if the machine has no video
	   * bridge...
	   */
          inSISIDXREG(SISCR, 0x34, pSiS->OldMode);
	  inSISIDXREG(SISCR, 0x30, cr30);
	  inSISIDXREG(SISCR, 0x31, cr31);

	  /* What if CR34 is different from the BIOS scratch byte? */
	  if(pSiS->OldMode != myoldmode) {
	     /* If no bridge output is active, trust the BIOS scratch byte */
	     if( (!(pSiS->VBFlags2 & VB2_VIDEOBRIDGE)) ||
	         (pSiS->OldMode == 0)                  ||
	         (!cr31 && !cr30)                      ||
		 (cr31 & 0x20) ) {
		pSiS->OldMode = myoldmode;
 	     }
	     /* ..else trust CR34 */
	  }

	  /* Newer 650 BIOSes set CR34 to 0xff if the mode has been
	   * "patched", for instance for 80x50 text mode. (That mode
	   * has no number of its own, it's 0x03 like 80x25). In this
	   * case, we trust the BIOS scratch byte (provided that any
	   * of these two is valid).
	   */
	  if(pSiS->OldMode > 0x7f) {
	     pSiS->OldMode = myoldmode;
	  }
       }
#ifdef SISDUALHEAD
       if(pSiS->DualHeadMode) {
          if(!pSiS->SecondHead) pSiSEnt->OldMode = pSiS->OldMode;
          else                  pSiS->OldMode = pSiSEnt->OldMode;
       }
#endif
    }

    /* RandR resets screen mode and size in CloseScreen(), hence
     * we need to adapt our VBFlags to the initial state if the
     * current mode has changed since closescreen() (or Screeninit()
     * for the first instance)
     */
    if(pScrn->currentMode != pSiS->currentModeLast) {
       pSiS->VBFlags = pSiS->VBFlags_backup = pSiS->VBFlagsInit;
       pSiS->VBFlags3 = pSiS->VBFlags3Init;
    }

    /* Copy our detected monitor gammas, part 2. Note that device redetection
     * is not supported in DHM, so there is no need to do that anytime later.
     */
#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
       if(!pSiS->SecondHead) {
          /* CRT2 */
	  pSiS->CRT1VGAMonitorGamma = pSiSEnt->CRT1VGAMonitorGamma;
       } else {
          /* CRT1 */
	  pSiS->CRT2VGAMonitorGamma = pSiSEnt->CRT2VGAMonitorGamma;
       }
       if(!pSiS->CRT2LCDMonitorGamma) pSiS->CRT2LCDMonitorGamma = pSiSEnt->CRT2LCDMonitorGamma;
    }
#endif


    /* Initialize the first mode */
    if(!SISModeInit(pScrn, pScrn->currentMode)) {
       SISErrorLog(pScrn, "SiSModeInit() failed\n");
       return FALSE;
    }

    /* Darken the screen for aesthetic reasons */
    /* Not using Dual Head variant on purpose; we darken
     * the screen for both displays, and un-darken
     * it when the second head is finished
     */
    SISSaveScreen(pScreen, SCREEN_SAVER_ON);

    /* Set the viewport. Reset it if the current settings are bad. */
    /* If we reserved a larger virtual for later, we run on "false"
     * pScrn->virtuals here. Hence, we use the ones that will be
     * set by our CreateScreenResources wrapper later on.
     */
    if(pScrn->frameX0 < 0) pScrn->frameX0 = 0;
    if(pScrn->frameY0 < 0) pScrn->frameY0 = 0;
    {
       int virtX = pScrn->virtualX, virtY = pScrn->virtualY;
       pScrn->frameX1 = pScrn->frameX0 + pScrn->currentMode->HDisplay - 1;
       pScrn->frameY1 = pScrn->frameY0 + pScrn->currentMode->VDisplay - 1;
       if(pScrn->frameX1 >= virtX) {
	  pScrn->frameX0 = virtX - pScrn->currentMode->HDisplay;
	  pScrn->frameX1 = pScrn->frameX0 + pScrn->currentMode->HDisplay - 1;
       }
       if(pScrn->frameY1 >= virtY) {
	  pScrn->frameY0 = virtY - pScrn->currentMode->VDisplay;
	  pScrn->frameY1 = pScrn->frameY0 + pScrn->currentMode->VDisplay - 1;
       }
    }
    SISAdjustFrame(pScrn, pScrn->frameX0, pScrn->frameY0);

    /* Reset visual list. */
    miClearVisualTypes();

    /* Setup the visuals we support. */

    /*
     * For bpp > 8, the default visuals are not acceptable because we only
     * support TrueColor and not DirectColor.
     */
    if(!miSetVisualTypes(pScrn->depth,
			 (pScrn->bitsPerPixel > 8) ?
				TrueColorMask : miGetDefaultVisualMask(pScrn->depth),
			 pScrn->rgbBits, pScrn->defaultVisual)) {
       SISSaveScreen(pScreen, SCREEN_SAVER_OFF);
       SISErrorLog(pScrn, "miSetVisualTypes() failed (bpp %d)\n",
			pScrn->bitsPerPixel);
       return FALSE;
    }

    width = pScrn->virtualX;
    height = pScrn->virtualY;
    displayWidth = pScrn->displayWidth;

    if(pSiS->Rotate) {
       height = pScrn->virtualX;
       width = pScrn->virtualY;
    }

    if(pSiS->ShadowFB) {
       pSiS->ShadowPitch = BitmapBytePad(pScrn->bitsPerPixel * width);
       pSiS->ShadowPtr = malloc(pSiS->ShadowPitch * height);
       displayWidth = pSiS->ShadowPitch / (pScrn->bitsPerPixel >> 3);
       FBStart = pSiS->ShadowPtr;
    } else {
       pSiS->ShadowPtr = NULL;
       FBStart = pSiS->FbBase;
    }

    if(!miSetPixmapDepths()) {
       SISSaveScreen(pScreen, SCREEN_SAVER_OFF);
       SISErrorLog(pScrn, "miSetPixmapDepths() failed\n");
       return FALSE;
    }

    /* Point cmdQueuePtr to pSiSEnt for shared usage
     * (same technique is then eventually used in DRIScreeninit)
     * For 315+ series, this is done in EnableTurboQueue
     * which has already been called during ModeInit().
     */
#ifdef SISDUALHEAD
    if(pSiS->SecondHead)
       pSiS->cmdQueueLenPtr = &(SISPTR(pSiSEnt->pScrn_1)->cmdQueueLen);
    else
#endif
       pSiS->cmdQueueLenPtr = &(pSiS->cmdQueueLen);

    pSiS->cmdQueueLen = 0; /* Force an EngineIdle() at start */

#ifdef SISDRI
    if(pSiS->loadDRI) {
#ifdef SISDUALHEAD
       /* No DRI in dual head mode */
       if(pSiS->DualHeadMode) {
	  pSiS->directRenderingEnabled = FALSE;
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"DRI not supported in Dual Head mode\n");
       } else
#endif
       if((pSiS->VGAEngine == SIS_315_VGA)||(pSiS->VGAEngine == SIS_300_VGA)) {
	  /* Force the initialization of the context */
	  pSiS->directRenderingEnabled = SISDRIScreenInit(pScreen);
       } else {
	  xf86DrvMsg(pScrn->scrnIndex, X_NOT_IMPLEMENTED,
		"DRI not supported on this chipset\n");
	  pSiS->directRenderingEnabled = FALSE;
       }
    }
#endif

    /* Call the framebuffer layer's ScreenInit function and fill in other
     * pScreen fields.
     */
    switch(pScrn->bitsPerPixel) {
       case 24:
	  if((pSiS->VGAEngine == SIS_300_VGA) || (pSiS->VGAEngine == SIS_315_VGA)) {
	     ret = FALSE;
	     break;
	  }
	  /* fall through */
       case 8:
       case 16:
       case 32:
	  ret = fbScreenInit(pScreen, FBStart, width,
			height, pScrn->xDpi, pScrn->yDpi,
			displayWidth, pScrn->bitsPerPixel);
	  break;
       default:
	  ret = FALSE;
	  break;
    }
    if(!ret) {
       SISErrorLog(pScrn, "Unsupported bpp (%d) or fbScreenInit() failed\n",
			pScrn->bitsPerPixel);
       SISSaveScreen(pScreen, SCREEN_SAVER_OFF);
       return FALSE;
    }

    /* Fixup RGB ordering */
    if(pScrn->bitsPerPixel > 8) {
       visual = pScreen->visuals + pScreen->numVisuals;
       while (--visual >= pScreen->visuals) {
          if((visual->class | DynamicClass) == DirectColor) {
             visual->offsetRed = pScrn->offset.red;
             visual->offsetGreen = pScrn->offset.green;
             visual->offsetBlue = pScrn->offset.blue;
             visual->redMask = pScrn->mask.red;
             visual->greenMask = pScrn->mask.green;
             visual->blueMask = pScrn->mask.blue;
          }
       }
    }

    /* Initialize RENDER extension (must be after RGB ordering fixed) */
    fbPictureInit(pScreen, 0, 0);

    /* Initialize DGA (must do before cursor initialization) */
    if(!pSiS->ShadowFB) {
#ifndef SISISXORG6899900
       if(pSiS->UseDynamicModelists) {
          xf86DrvMsg(pScrn->scrnIndex, X_INFO,
          	"Using dynamic modelist. DGA disabled due to incompatibility.\n");
          xf86DrvMsg(pScrn->scrnIndex, X_INFO,
          	"\tOption \"DynamicModelist\" \"off\" will re-enable DGA.\n");
       } else
#endif
          SISDGAInit(pScreen);
    }

    xf86SetBlackWhitePixels(pScreen);

    /* Initialize the accelerators */
    switch(pSiS->VGAEngine) {
    case SIS_530_VGA:
    case SIS_300_VGA:
       SiS300AccelInit(pScreen);
       break;
    case SIS_315_VGA:
       SiS315AccelInit(pScreen);
       break;
    default:
       SiSAccelInit(pScreen);
    }

#ifdef TWDEBUG
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CPUFlags %x\n", pSiS->CPUFlags);
#endif

    /* Benchmark memcpy() methods (needs FB manager initialized) */
    /* Dual head: Do this AFTER the mode for CRT1 has been set */
    pSiS->NeedCopyFastVidCpy = FALSE;
    if(!pSiS->SiSFastVidCopyDone) {
#ifdef SISDUALHEAD
       if(pSiS->DualHeadMode) {
	  if(pSiS->SecondHead) {
	     pSiSEnt->SiSFastVidCopy = SiSVidCopyInit(pScreen, &pSiSEnt->SiSFastMemCopy, FALSE);
	     pSiSEnt->SiSFastVidCopyFrom = SiSVidCopyGetDefault();
	     pSiSEnt->SiSFastMemCopyFrom = SiSVidCopyGetDefault();
	     if(pSiS->useEXA
						       ) {
	        pSiSEnt->SiSFastVidCopyFrom = SiSVidCopyInit(pScreen, &pSiSEnt->SiSFastMemCopyFrom, TRUE);
	     }
	     pSiSEnt->HaveFastVidCpy = TRUE;
	     pSiS->SiSFastVidCopy = pSiSEnt->SiSFastVidCopy;
	     pSiS->SiSFastMemCopy = pSiSEnt->SiSFastMemCopy;
	     pSiS->SiSFastVidCopyFrom = pSiSEnt->SiSFastVidCopyFrom;
	     pSiS->SiSFastMemCopyFrom = pSiSEnt->SiSFastMemCopyFrom;
	  } else {
	     pSiS->NeedCopyFastVidCpy = TRUE;
	  }
       } else {
#endif
	  pSiS->SiSFastVidCopy = SiSVidCopyInit(pScreen, &pSiS->SiSFastMemCopy, FALSE);
	  pSiS->SiSFastVidCopyFrom = SiSVidCopyGetDefault();
	  pSiS->SiSFastMemCopyFrom = SiSVidCopyGetDefault();
	  if(pSiS->useEXA
						    ) {
	     pSiS->SiSFastVidCopyFrom = SiSVidCopyInit(pScreen, &pSiS->SiSFastMemCopyFrom, TRUE);
	  }
#ifdef SISDUALHEAD
       }
#endif
    }
    pSiS->SiSFastVidCopyDone = TRUE;

    xf86SetBackingStore(pScreen);
    xf86SetSilkenMouse(pScreen);

    /* Initialise cursor functions */
    miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

    /* Initialise HW cursor */
    if(pSiS->HWCursor) {
       SiSHWCursorInit(pScreen);
    }

    /* Set up gamma correction for CRT2 */
#ifdef SISDUALHEAD
    if(!pSiS->DualHeadMode) {
#endif
       if((pSiS->VBFlags2 & VB2_SISBRIDGE) && (pScrn->depth > 8)) {

	  pSiS->CRT2ColNum = 1 << pScrn->rgbBits;

	  if((pSiS->crt2gcolortable = malloc(pSiS->CRT2ColNum * 2 * sizeof(LOCO)))) {
	     pSiS->crt2colors = &pSiS->crt2gcolortable[pSiS->CRT2ColNum];
	     if((pSiS->crt2cindices = malloc(256 * sizeof(int)))) {
		int i = pSiS->CRT2ColNum;
		SISCalculateGammaRampCRT2(pScrn);
		while(i--) pSiS->crt2cindices[i] = i;
	     } else {
		free(pSiS->crt2gcolortable);
		pSiS->crt2gcolortable = NULL;
		pSiS->CRT2SepGamma = FALSE;
	     }
	  } else {
	     pSiS->CRT2SepGamma = FALSE;
	  }

	  if(!pSiS->crt2cindices) {
	     xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
	  	"Failed to allocate cmap for CRT2, separate gamma correction disabled\n");
	  }

       }
#ifdef SISDUALHEAD
    } else pSiS->CRT2SepGamma = FALSE;
#endif

    /* Initialise default colormap */
    if(!miCreateDefColormap(pScreen)) {
       SISSaveScreen(pScreen, SCREEN_SAVER_OFF);
       SISErrorLog(pScrn, "miCreateDefColormap() failed\n");
       return FALSE;
    }

    if(!xf86HandleColormaps(pScreen, 256, (pScrn->depth == 8) ? 8 : pScrn->rgbBits,
                    SISLoadPalette, NULL,
                    CMAP_PALETTED_TRUECOLOR | CMAP_RELOAD_ON_MODE_SWITCH)) {
       SISSaveScreen(pScreen, SCREEN_SAVER_OFF);
       SISErrorLog(pScrn, "xf86HandleColormaps() failed\n");
       return FALSE;
    }

    /* Recalculate our gamma ramp for brightness, contrast feature */
#ifdef SISGAMMARAMP
    if((pSiS->GammaBriR != 1000) ||
       (pSiS->GammaBriB != 1000) ||
       (pSiS->GammaBriG != 1000) ||
       (pSiS->NewGammaBriR != 0.0) ||
       (pSiS->NewGammaBriG != 0.0) ||
       (pSiS->NewGammaBriB != 0.0) ||
       (pSiS->NewGammaConR != 0.0) ||
       (pSiS->NewGammaConG != 0.0) ||
       (pSiS->NewGammaConB != 0.0)) {
       SISCalculateGammaRamp(pScreen, pScrn);
    }
#endif

    /* Backup pScrn->PointerMoved */
    if(!pSiS->PointerMoved) {
       pSiS->PointerMoved = pScrn->PointerMoved;
    }

    /* Initialize shadow framebuffer and screen rotation/reflection */
    if(pSiS->ShadowFB) {
       RefreshAreaFuncPtr refreshArea = SISRefreshArea;
       if(pSiS->Rotate) {
	  pScrn->PointerMoved = SISPointerMoved;
	  switch(pScrn->bitsPerPixel) {
	     case 8:  refreshArea = SISRefreshArea8;  break;
	     case 16: refreshArea = SISRefreshArea16; break;
	     case 24: refreshArea = SISRefreshArea24; break;
	     case 32: refreshArea = SISRefreshArea32; break;
	  }
#if XF86_VERSION_CURRENT >= XF86_VERSION_NUMERIC(4,3,0,0,0)
	  xf86DisableRandR();
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"Driver rotation enabled, disabling RandR\n");
#endif
       } else if(pSiS->Reflect) {
          switch(pScrn->bitsPerPixel) {
	     case 8:
	     case 16:
	     case 32:
		pScrn->PointerMoved = SISPointerMoved;
		refreshArea = SISRefreshAreaReflect;
#if XF86_VERSION_CURRENT >= XF86_VERSION_NUMERIC(4,3,0,0,0)
		xf86DisableRandR();
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			"Driver reflection enabled, disabling RandR\n");
#endif
		break;
	     default:
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"Reflection not supported at this framebuffer depth\n");
	  }
       }

       ShadowFBInit(pScreen, refreshArea);

    }

    /* Init DPMS */
    xf86DPMSInit(pScreen, (DPMSSetProcPtr)SISDisplayPowerManagementSet, 0);

    /* Init memPhysBase and fbOffset in pScrn */
    pScrn->memPhysBase = pSiS->FbAddress;
    pScrn->fbOffset = 0;

    /* Initialize Xv */
    pSiS->ResetXv = pSiS->ResetXvGamma = pSiS->ResetXvDisplay = NULL;
#if (XF86_VERSION_CURRENT >= XF86_VERSION_NUMERIC(4,3,99,0,0)) || (defined(XvExtension))
    if((!pSiS->NoXvideo) && (!(pSiS->SiS_SD2_Flags & SiS_SD2_NOOVERLAY))) {

       xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		"Hardware supports %s video overlay%s\n",
		pSiS->hasTwoOverlays ? "two" : "one",
		pSiS->hasTwoOverlays ? "s" : "");

       if((pSiS->VGAEngine == SIS_300_VGA) ||
	  (pSiS->VGAEngine == SIS_315_VGA)) {

	  const char *using = "Using SiS300/315/330/340/350 series HW Xv";

#ifdef SISDUALHEAD
	  if(pSiS->DualHeadMode) {
	     xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		    "%s on CRT%d\n", using, (pSiS->SecondHead ? 1 : 2));
	     if(!pSiS->hasTwoOverlays) {
		if( (pSiS->XvOnCRT2 && pSiS->SecondHead) ||
		    (!pSiS->XvOnCRT2 && !pSiS->SecondHead) ) {
		   xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			   "However, video overlay will by default only be visible on CRT%d\n",
			   pSiS->XvOnCRT2 ? 2 : 1);
		}
	     }
	  } else {
#endif
	     if(pSiS->hasTwoOverlays) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s\n", using);
	     } else {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s by default on CRT%d\n",
			using, (pSiS->XvOnCRT2 ? 2 : 1));
	     }
#ifdef SISDUALHEAD
	  }
#endif

	  SISInitVideo(pScreen);

	  if(pSiS->blitadaptor) {
	     xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		     "Default Xv adaptor is Video %s\n",
		     pSiS->XvDefAdaptorBlit ? "Blitter" : "Overlay");
	  }

       } else if(pSiS->Chipset == PCI_CHIP_SIS530  ||
		 pSiS->Chipset == PCI_CHIP_SIS6326 ||
		 pSiS->Chipset == PCI_CHIP_SIS5597) {

	  xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		        "Using SiS5597/5598/6326/530/620 HW Xv\n" );

	  SIS6326InitVideo(pScreen);

       } else { /* generic Xv */

	  XF86VideoAdaptorPtr *ptr;
	  int n = xf86XVListGenericAdaptors(pScrn, &ptr);

	  if(n) {
	     xf86XVScreenInit(pScreen, ptr, n);
	     xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Using generic Xv\n" );
          }

       }
    }
#endif

   /* Finish DRI initialisation */
#ifdef SISDRI
    if(pSiS->loadDRI) {
       if(pSiS->directRenderingEnabled) {
          /* Now that mi, drm and others have done their thing,
           * complete the DRI setup.
           */
          pSiS->directRenderingEnabled = SISDRIFinishScreenInit(pScreen);
       }
       xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Direct rendering %s\n",
		pSiS->directRenderingEnabled ? "enabled" : "disabled");
       /* TODO */
       /* if(pSiS->directRenderingEnabled) SISSetLFBConfig(pSiS); */
    }
#endif

    /* Wrap some funcs, initialize pseudo-Xinerama and setup remaining SD flags */

    pSiS->SiS_SD_Flags &= ~(SiS_SD_PSEUDOXINERAMA);
#ifdef SISMERGED
    if(pSiS->MergedFB) {
       pScrn->PointerMoved = SISMFBPointerMoved;
       pSiS->Rotate = 0;
       pSiS->Reflect = 0;
       pSiS->ShadowFB = FALSE;
#if XF86_VERSION_CURRENT >= XF86_VERSION_NUMERIC(4,3,0,0,0)
       if(pSiS->CRT1XOffs || pSiS->CRT1YOffs || pSiS->CRT2XOffs || pSiS->CRT2YOffs) {
	  xf86DisableRandR();
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"MergedFB: CRT2Position offset used, disabling RandR\n");
       }
#endif
#ifdef SISXINERAMA
       if(pSiS->UseSiSXinerama) {
	  SiSnoPanoramiXExtension = FALSE;
	  SiSXineramaExtensionInit(pScrn);
	  if(!SiSnoPanoramiXExtension) {
	     pSiS->SiS_SD_Flags |= SiS_SD_PSEUDOXINERAMA;
	     if(pSiS->HaveNonRect) {
		/* Reset the viewport (now eventually non-recangular) */
		SISAdjustFrame(pScrn, pScrn->frameX0, pScrn->frameY0);
	     }
	  }
       } else {
	  pSiS->MouseRestrictions = FALSE;
       }
#endif
    }
#endif

    /* Wrap CloseScreen and set up SaveScreen */
    pSiS->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = SISCloseScreen;
#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode)
       pScreen->SaveScreen = SISSaveScreenDH;
    else
#endif
       pScreen->SaveScreen = SISSaveScreen;

    /* Install BlockHandler */
    pSiS->BlockHandler = pScreen->BlockHandler;
    pScreen->BlockHandler = SISBlockHandler;

    /* Report any unused options (only for the first generation) */
    if(serverGeneration == 1) {
       xf86ShowUnusedOptions(pScrn->scrnIndex, pScrn->options);
    }

    /* Clear frame buffer */
    /* For CRT2, we don't do that at this point in dual head
     * mode since the mode isn't switched at this time (it will
     * be reset when setting the CRT1 mode). Hence, we just
     * save the necessary data and clear the screen when
     * going through this for CRT1.
     */

    OnScreenSize = pScrn->displayWidth * pScrn->currentMode->VDisplay
                               * (pScrn->bitsPerPixel >> 3);

    /* Turn on the screen now */
    /* We do this in dual head mode after second head is finished */
#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
       if(pSiS->SecondHead) {
	  sisclearvram(pSiS->FbBase, OnScreenSize);
	  sisclearvram(pSiSEnt->FbBase1, pSiSEnt->OnScreenSize1);
	  SISSaveScreen(pScreen, SCREEN_SAVER_OFF);
       } else {
	  pSiSEnt->FbBase1 = pSiS->FbBase;
	  pSiSEnt->OnScreenSize1 = OnScreenSize;
       }
    } else {
#endif
       sisclearvram(pSiS->FbBase, OnScreenSize);
       SISSaveScreen(pScreen, SCREEN_SAVER_OFF);
#ifdef SISDUALHEAD
    }
#endif

    pSiS->SiS_SD_Flags &= ~SiS_SD_SUPPORTSGRCRT2;
#ifdef SISDUALHEAD
    if(!pSiS->DualHeadMode) {
#endif
       if(pSiS->VBFlags2 & VB2_SISBRIDGE) {
          if((pSiS->crt2cindices) && (pSiS->crt2gcolortable)) {
             pSiS->SiS_SD_Flags |= SiS_SD_SUPPORTSGRCRT2;
	  }
       }
#ifdef SISDUALHEAD
    }
#endif

    pSiS->SiS_SD_Flags &= ~SiS_SD_ISDEPTH8;
    if(pSiS->CurrentLayout.bitsPerPixel == 8) {
       pSiS->SiS_SD_Flags |= SiS_SD_ISDEPTH8;
       pSiS->SiS_SD_Flags &= ~SiS_SD_SUPPORTXVGAMMA1;
       pSiS->SiS_SD_Flags &= ~SiS_SD_SUPPORTSGRCRT2;
    }

#ifdef SISGAMMARAMP
    pSiS->SiS_SD_Flags |= SiS_SD_CANSETGAMMA;
#else
    pSiS->SiS_SD_Flags &= ~SiS_SD_CANSETGAMMA;
#endif

    /* Initialize SISCTRL extension */
    SiSCtrlExtInit(pScrn);

    pSiS->virtualX = pScrn->virtualX;
    pSiS->virtualY = pScrn->virtualY;
#ifdef SIS_HAVE_RR_FUNC
#ifdef SIS_HAVE_DRIVER_FUNC
    pScrn->DriverFunc = SISDriverFunc;
#else
    pScrn->RRFunc = SISDriverFunc;
#endif
#endif

    /* Wrap Adjustframe again (Xv has wrapped it in the meantime)
     * in order to avoid race conditions due to AdjustFrame being
     * called asynchonously if silken mouse is enabled.
     */
    pSiS->AdjustFramePending = FALSE;
    pSiS->AdjustFrame = pScrn->AdjustFrame;
    pScrn->AdjustFrame = SISNewAdjustFrame;

    /* Wrap CreateScreenResources in order to be able to fiddle
     * with RandR sizes and set an initial RandR size. We do this
     * only once at server start. Later it's not needed. Although
     * RandR resets the size on CloseScreen(), the sizes must match
     * the then current modelist (think dynamic modelist). So
     * resetting it every time ScreenInit() is called would be
     * wrong.
     */

    return TRUE;
}


/*********************************************************/
/*                   ValidMode() etc                     */
/*********************************************************/


static Bool
SiSValidLCDUserMode(SISPtr pSiS, unsigned int VBFlags, DisplayModePtr mode, Bool isforlcda)
{
   if(mode->Flags & V_INTERLACE) return FALSE;

   if(mode->HDisplay > 2048) return FALSE;
   if(mode->VDisplay > 1536) return FALSE;

   if(pSiS->VBFlags2 & VB2_LCDOVER1600BRIDGE) {

      /* Will this be dual-link? */
      if(mode->Clock > 162500) return FALSE;  /* temp */

   } else if(pSiS->VBFlags2 & VB2_LCD162MHZBRIDGE) {

      if(mode->Clock > 162500) return FALSE;
      if(!isforlcda) {
         if(mode->HDisplay > 1600) return FALSE;
      }

   } else { /* 301, 301B, 302B (no LCDA!) */

      if(mode->Clock > 130000)  return FALSE;
      if(mode->Clock > 111000) {
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_WARNING,
	 	"WARNING: Mode clock beyond video bridge specs (%dMHz). Hardware damage might occure.\n",
		mode->Clock / 1000);
      }
      if(mode->HDisplay > 1600) return FALSE;
      if(mode->VDisplay > 1024) return FALSE;

   }

   return TRUE;
}

static Bool
SiSValidVGA2UserMode(SISPtr pSiS, unsigned int VBFlags, DisplayModePtr mode)
{
   if(mode->Flags & V_INTERLACE) return FALSE;

   if(mode->HDisplay > 2048) return FALSE;
   if(mode->VDisplay > 1536) return FALSE;

   if(pSiS->VBFlags2 & VB2_RAMDAC202MHZBRIDGE) {
      if(mode->Clock > 203000) return FALSE;
   } else if(pSiS->VBFlags2 & VB2_30xBLV) {
      if(mode->Clock > 162500) return FALSE;
   } else {
      if(mode->Clock > 135500) return FALSE;
   }

   return TRUE;
}

UShort
SiS_CheckModeCRT1(ScrnInfoPtr pScrn, DisplayModePtr mode, unsigned int VBFlags,
			unsigned int VBFlags3, Bool havecustommodes)
{
   SISPtr pSiS = SISPTR(pScrn);
   UShort i = pSiS->CurrentLayout.bytesPerPixel - 1;
   int j;

   if((!(VBFlags & CRT1_LCDA)) && (!(VBFlags3 & VB3_CRT1_LCD))) {

      if((havecustommodes) && (!(mode->type & M_T_DEFAULT))) {
         return 0xfe;
      }

   } else if(pSiS->VBFlags2 & VB2_SISTMDSLCDABRIDGE) {

      /* if(pSiS->ChipType < ?) {  */
         if(!(mode->type & M_T_DEFAULT)) {
            if(mode->HTotal > 2055) return 0;
	    /* (Default mode will be caught in mode switching code) */
	 }
      /* } */

      if(pSiS->SiS_Pr->CP_HaveCustomData) {
         for(j=0; j<7; j++) {
            if((pSiS->SiS_Pr->CP_DataValid[j]) &&
               (mode->HDisplay == pSiS->SiS_Pr->CP_HDisplay[j]) &&
               (mode->VDisplay == pSiS->SiS_Pr->CP_VDisplay[j]) &&
               (mode->type & M_T_BUILTIN))
               return 0xfe;
	 }
      }

      if((pSiS->AddedPlasmaModes) && (mode->type & M_T_BUILTIN))
         return 0xfe;

      if((havecustommodes) &&
         (pSiS->LCDwidth)  &&	/* = test if LCD present */
         (!(mode->type & M_T_DEFAULT)) &&
	 (SiSValidLCDUserMode(pSiS, VBFlags, mode, TRUE)))
         return 0xfe;

      if((mode->HDisplay > pSiS->LCDwidth) ||
         (mode->VDisplay > pSiS->LCDheight)) {
	 return 0;
      }

   } else {

      if((mode->HDisplay > pSiS->LCDwidth) ||
         (mode->VDisplay > pSiS->LCDheight)) {
	  if((pSiS->EnablePanel_1366x768) && /*let the 1366x768 mode valid. Ivans@090109*/
	     (pSiS->LCDwidth==1366)       &&
	     (mode->HDisplay==1368)){
	     ;
	  } 
	  else {
	    return 0;
	  }
      }

   }

   return(SiS_GetModeID(pSiS->VGAEngine, VBFlags, mode->HDisplay, mode->VDisplay,
   			i, pSiS->FSTN, pSiS->LCDwidth, pSiS->LCDheight));
}

UShort
SiS_CheckModeCRT2(ScrnInfoPtr pScrn, DisplayModePtr mode, unsigned int VBFlags,
			unsigned int VBFlags3, Bool havecustommodes)
{
   SISPtr pSiS = SISPTR(pScrn);
   UShort i = pSiS->CurrentLayout.bytesPerPixel - 1;
   UShort ModeIndex = 0;
   int    j;

#ifdef TWDEBUG
   xf86DrvMsg(0, X_INFO, "Inside CheckCalcModeIndex (VBFlags %lx, mode %dx%d)\n",
	VBFlags,mode->HDisplay, mode->VDisplay);
#endif

   if(VBFlags & CRT2_LCD) {			/* CRT2 is LCD */

      if((pSiS->VBFlags2 & VB2_SISTMDSBRIDGE) && (!(pSiS->VBFlags2 & VB2_30xBDH))) {

         if(pSiS->SiS_Pr->CP_HaveCustomData) {
            for(j=0; j<7; j++) {
               if((pSiS->SiS_Pr->CP_DataValid[j]) &&
                  (mode->HDisplay == pSiS->SiS_Pr->CP_HDisplay[j]) &&
                  (mode->VDisplay == pSiS->SiS_Pr->CP_VDisplay[j]) &&
#ifdef VB_FORBID_CRT2LCD_OVER_1600
		  (mode->HDisplay <= 1600) 			   &&
#endif
                  (mode->type & M_T_BUILTIN))
                  return 0xfe;
	    }
         }

	 /* All plasma modes have HDisplay <= 1600 */
         if((pSiS->AddedPlasmaModes) && (mode->type & M_T_BUILTIN))
            return 0xfe;

         if((havecustommodes) &&
            (pSiS->LCDwidth)  &&	/* = test if LCD present */
	    (!(mode->type & M_T_DEFAULT)) &&
	    (SiSValidLCDUserMode(pSiS, VBFlags, mode, FALSE)))
            return 0xfe;

      }

      if( ((mode->HDisplay <= pSiS->LCDwidth) &&
           (mode->VDisplay <= pSiS->LCDheight)) ||
	  ((pSiS->SiS_Pr->SiS_CustomT == CUT_PANEL848) &&
	   (((mode->HDisplay == 1360) && (mode->HDisplay == 768)) ||
	    ((mode->HDisplay == 1024) && (mode->HDisplay == 768)) ||
	    ((mode->HDisplay ==  800) && (mode->HDisplay == 600)))) ||
	  ((pSiS->SiS_Pr->SiS_CustomT == CUT_PANEL856) &&
	   (((mode->HDisplay == 1024) && (mode->HDisplay == 768)) ||
	    ((mode->HDisplay ==  800) && (mode->HDisplay == 600)))) ||
	   ((pSiS->EnablePanel_1366x768)&&(pSiS->LCDwidth==1366)&&(mode->HDisplay==1368))) {/*let 1366x768 mode valid. Ivans@090109*/

	 ModeIndex = SiS_GetModeID_LCD(pSiS->VGAEngine, VBFlags, mode->HDisplay, mode->VDisplay, i,
				pSiS->FSTN, pSiS->SiS_Pr->SiS_CustomT, pSiS->LCDwidth, pSiS->LCDheight,
				pSiS->VBFlags2);

      }

   } else if(VBFlags & CRT2_TV) {		/* CRT2 is TV */

      ModeIndex = SiS_GetModeID_TV(pSiS->VGAEngine, VBFlags, mode->HDisplay, mode->VDisplay, i,
					pSiS->VBFlags2);

   } else if(VBFlags & CRT2_VGA) {		/* CRT2 is VGA2 */

      if((pSiS->AddedPlasmaModes) && (mode->type & M_T_BUILTIN))
	 return 0xfe;

      if((havecustommodes) &&
	 (!(mode->type & M_T_DEFAULT)) &&
	 (SiSValidVGA2UserMode(pSiS, VBFlags, mode)))
         return 0xfe;

      ModeIndex = SiS_GetModeID_VGA2(pSiS->VGAEngine, VBFlags, mode->HDisplay, mode->VDisplay, i,
					pSiS->VBFlags2);

   } else {					/* no CRT2 */

      /* Return a valid mode number */
      ModeIndex = 0xfe;

   }

   return ModeIndex;
}

static ModeStatus
SISValidMode(ScrnInfoPtr pScrn, DisplayModePtr mode, Bool verbose, int flags)
{
    SISPtr pSiS = SISPTR(pScrn);

    if(pSiS->UseVESA) {
       if(SiSCalcVESAModeIndex(pScrn, mode))
	  return MODE_OK;
       else
	  return MODE_BAD;
    }

    if(pSiS->VGAEngine == SIS_300_VGA || pSiS->VGAEngine == SIS_315_VGA) {
#ifdef SISDUALHEAD
       if(pSiS->DualHeadMode) {
          if(pSiS->SecondHead) {
	     if(SiS_CheckModeCRT1(pScrn, mode, pSiS->VBFlags,
			pSiS->VBFlags3, pSiS->HaveCustomModes) < 0x14)
	        return MODE_BAD;
	  } else {
	     if(SiS_CheckModeCRT2(pScrn, mode, pSiS->VBFlags,
			pSiS->VBFlags3, pSiS->HaveCustomModes) < 0x14)
	        return MODE_BAD;
	  }
       } else
#endif
#ifdef SISMERGED
       if(pSiS->MergedFB) {
	  if(!mode->Private) {
	     if(!pSiS->CheckForCRT2) {
	        if(SiS_CheckModeCRT1(pScrn, mode, pSiS->VBFlags,
				pSiS->VBFlags3, pSiS->HaveCustomModes) < 0x14)
	           return MODE_BAD;
	     } else {
	        if(SiS_CheckModeCRT2(pScrn, mode, pSiS->VBFlags,
				pSiS->VBFlags3, pSiS->HaveCustomModes2) < 0x14)
	           return MODE_BAD;
	     }
	  } else {
	     if(SiS_CheckModeCRT1(pScrn, ((SiSMergedDisplayModePtr)mode->Private)->CRT1,
		                  pSiS->VBFlags, pSiS->VBFlags3, pSiS->HaveCustomModes) < 0x14)
	        return MODE_BAD;

	     if(SiS_CheckModeCRT2(pScrn, ((SiSMergedDisplayModePtr)mode->Private)->CRT2,
		                  pSiS->VBFlags, pSiS->VBFlags3, pSiS->HaveCustomModes2) < 0x14)
	        return MODE_BAD;
 	  }
       } else
#endif
       {
	  if(SiS_CheckModeCRT1(pScrn, mode, pSiS->VBFlags,
			pSiS->VBFlags3, pSiS->HaveCustomModes) < 0x14)
	     return MODE_BAD;
          #ifdef TWDEBUG
          xf86DrvMsg(0,X_INFO,"[SISValidMode()]: else condition, passing CheckModeCRT1 and MODE_OK.\n");
          #endif
	  if(SiS_CheckModeCRT2(pScrn, mode, pSiS->VBFlags,
			pSiS->VBFlags3, pSiS->HaveCustomModes) < 0x14){
	       #ifdef TWDEBUG
		  xf86DrvMsg(0,X_INFO,"[SISValidMode()]: else condition. passing CheckModeCRT2 and MODE_OK.\n");
	       #endif	  
	       return MODE_BAD;
	  }
          #ifdef  TWDEBUG
	  xf86DrvMsg(0,X_INFO,"[SISValidMode()]: else condition. passing CheckModeCRT2 and MODE_OK.\n");
          #endif
       }
    }

    return MODE_OK;
}

/*********************************************************/
/*                   SwitchMode() etc.                   */
/*********************************************************/

#ifdef SIS_HAVE_RR_GET_MODE_MM
static Bool
SiS_GetModeMM(ScrnInfoPtr pScrn, DisplayModePtr mode, int virtX, int virtY,
			int *mmWidth, int *mmHeight)
{
    SISPtr pSiS = SISPTR(pScrn);
    int width = virtX, height = virtY;

#ifdef SISMERGED
    if(pSiS->MergedFB) {
       SiSMFBCalcDPIPerMode(pScrn, mode, virtX, virtY, mmWidth, mmHeight);
       return TRUE;
    }
#endif

    if(pSiS->constantDPI) {

       /* Provide clients with constant DPI values
        * regardless of the screen size.
        * DPI will always be what they were at
        * server start.
        */

       if(mode) {
          width = mode->HDisplay;
          height = mode->VDisplay;
       }
       *mmWidth = (width * 254 + pScrn->xDpi * 5) / (pScrn->xDpi * 10);
       *mmHeight = (height * 254 + pScrn->yDpi * 5) / (pScrn->yDpi * 10);

    } else {

       /* Let RandR mess with the DPI by leaving mmWidth/mmHeight
	* alone.
	*
	* Quick brainstorming on what could be done here:
	* - 300+: What about a grow of the screen size? Should
	*   the startup-DPI be the same for the new virtual
	*   screen size? Right now, the display size will be
	*   left untouched. This will lead to a growing DPI
	*   value for larger resolutions, and a shrinking one
	*   for lower resolutions.
	*   Alternative: Redo the mmWidth/Height based on the
	*   largest screen mode/virtual size (whichever is
	*   the larger) so that the start-up DPI are now based
	*   on this largest dimension. Disadvantage: This will
	*   result in even smaller DPI values for lower
	*   resolutions.
	*
	* Note: Due to our hack in RebuildModelist, the provided
	* virtX/Y does not necessarily represent the maximum!
	*/

    }

    return TRUE;
}
#endif

#if defined(RANDR) && !defined(SIS_HAVE_RR_GET_MODE_MM)
static void
SiSResetDPI(ScrnInfoPtr pScrn, Bool force)
{
    SISPtr pSiS = SISPTR(pScrn);
    ScreenPtr pScreen = screenInfo.screens[pScrn->scrnIndex];

    if(force					||
       (pSiS->SiSDPIVX != pScrn->virtualX)	||
       (pSiS->SiSDPIVY != pScrn->virtualY)
					  ) {

       pScreen->mmWidth = (pScrn->virtualX * 254 + pScrn->xDpi * 5) / (pScrn->xDpi * 10);
       pScreen->mmHeight = (pScrn->virtualY * 254 + pScrn->yDpi * 5) / (pScrn->yDpi * 10);

       pSiS->SiSDPIVX = pScrn->virtualX;
       pSiS->SiSDPIVY = pScrn->virtualY;

    }
}
#endif

Bool
SISSwitchMode(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
    SISPtr pSiS = SISPTR(pScrn);
   /* This is part 2 of the ugly hack in sis_shadow.c:
    * There we set pScrn->currentMode to something
    * different in order to overcome the
    * "if(mode == pscrn->currentMode) return TRUE"
    * statement in xf86SwitchMode. Now, if we
    * find pScrn->currentMode == &pSiS->PseudoMode,
    * we reset pScrn->currentMode, and if the
    * mode to set is already set, we just recalc
    * the DPI eventually, and bail out.
    * (In non-MergedFB mode this is also here in order
    * to get a cheap update of the HWCursor image)
    */

    if(!pSiS->skipswitchcheck) {
       if(SISValidMode(pScrn, mode, TRUE, 0) != MODE_OK) {
          return FALSE;
       }
    }
/* Mark for 3D full-screen bug */
/*
#ifdef SISDRI    
    if(pSiS->directRenderingEnabled) {       
	DRILock(screenInfo.screens[scrnIndex], DRM_LOCK_QUIESCENT);
    }
#endif
*/

    (*pSiS->SyncAccel)(pScrn);

    if(!(SISModeInit(pScrn, mode)))
       return FALSE;

    /* Since RandR (indirectly) uses SwitchMode(), we need to
     * update our Xinerama info here, too, in case of resizing
     * (and we didn't bail out above already).
     * Furthermore, we recalc the DPI eventually. (Note that
     * the availability of RR_GET_MODE_MM makes DPI recalculation
     * at this place redundant.)
     */
#ifdef SISMERGED
    if(pSiS->MergedFB) {
#if defined(RANDR) && !defined(SIS_HAVE_RR_GET_MODE_MM)
       SiSMFBResetDpi(pScrn, FALSE);
#endif
#ifdef SISXINERAMA
       SiSUpdateXineramaScreenInfo(pScrn);
#endif
/* Mark for 3D full-screen bug */
/*
#ifdef SISDRI
    if(pSiS->directRenderingEnabled) {
       DRIUnlock(screenInfo.screens[scrnIndex]);
    }
#endif
*/
       return TRUE;
    }
#endif

#if defined(RANDR) && !defined(SIS_HAVE_RR_GET_MODE_MM)
    if(pSiS->constantDPI) {
       SiSResetDPI(pScrn, FALSE);
    }
#endif

    return TRUE;
}


/*********************************************************/
/*                     AdjustFrame()                     */
/*********************************************************/


static void
SISSetStartAddressCRT1(SISPtr pSiS, ULong base)
{
    UChar cr11backup;

    inSISIDXREG(SISCR,  0x11, cr11backup);  /* Unlock CRTC registers */
    andSISIDXREG(SISCR, 0x11, 0x7F);
    outSISIDXREG(SISCR, 0x0D, base & 0xFF);
    outSISIDXREG(SISCR, 0x0C, (base >> 8) & 0xFF);
    outSISIDXREG(SISSR, 0x0D, (base >> 16) & 0xFF);
    if(pSiS->VGAEngine == SIS_315_VGA) {
       setSISIDXREG(SISSR, 0x37, 0xFE, (base >> 24) & 0x01);
    }
    /* Eventually lock CRTC registers */
    setSISIDXREG(SISCR, 0x11, 0x7F,(cr11backup & 0x80));
}

static void
SISSetStartAddressCRT2(SISPtr pSiS, ULong base)
{
    SiS_UnLockCRT2(pSiS->SiS_Pr);
    outSISIDXREG(SISPART1, 0x06, GETVAR8(base));
    outSISIDXREG(SISPART1, 0x05, GETBITS(base, 15:8));
    outSISIDXREG(SISPART1, 0x04, GETBITS(base, 23:16));
    if(pSiS->VGAEngine == SIS_315_VGA) {
       setSISIDXREG(SISPART1, 0x02, 0x7F, ((base >> 24) & 0x01) << 7);
    }
    SiS_LockCRT2(pSiS->SiS_Pr);
}


void
SISAdjustFrameHW_CRT1(ScrnInfoPtr pScrn, int x, int y)
{
    SISPtr pSiS = SISPTR(pScrn);
    ULong base;

    RecalcScreenPitch(pScrn);

    base = y * (pSiS->scrnPitch / (pSiS->CurrentLayout.bitsPerPixel >> 3)) + x;
    switch(pSiS->CurrentLayout.bitsPerPixel) {
       case 16:  base >>= 1; 	break;
       case 32:  		break;
       default:  base >>= 2;
    }
    base += (pSiS->dhmOffset/4);
    SISSetStartAddressCRT1(pSiS, base);

    SiS_SetPitchCRT1(pSiS->SiS_Pr, pScrn);
}

void
SISAdjustFrameHW_CRT2(ScrnInfoPtr pScrn, int x, int y)
{
    SISPtr pSiS = SISPTR(pScrn);
    ULong base;

    RecalcScreenPitch(pScrn);

    base = y * (pSiS->scrnPitch / (pSiS->CurrentLayout.bitsPerPixel >> 3)) + x;
    switch(pSiS->CurrentLayout.bitsPerPixel) {
       case 16:  base >>= 1; 	break;
       case 32:  		break;
       default:  base >>= 2;
    }
    base += (pSiS->dhmOffset/4);
    SISSetStartAddressCRT2(pSiS, base);

    SiS_SetPitchCRT2(pSiS->SiS_Pr, pScrn);
}

static void
SISNewAdjustFrame(ScrnInfoPtr pScrn, int x, int y)
{
    SISPtr      pSiS = SISPTR(pScrn);

    pSiS->AdjustFramePending = TRUE;
    pSiS->AdjustFrameX = x;
    pSiS->AdjustFrameY = y;
}

void
SISAdjustFrame(ScrnInfoPtr pScrn, int x, int y)
{
    SISPtr      pSiS = SISPTR(pScrn);
    UChar       temp, cr11backup;
    ULong       base;

#ifdef UNLOCK_ALWAYS
    sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif


#ifdef SISMERGED
    if(pSiS->MergedFB) {
	SISMFBAdjustFrame(pScrn, x, y);
	return;
    }
#endif

    if(pSiS->UseVESA) {
	VBESetDisplayStart(pSiS->pVbe, x, y, TRUE);
	return;
    }

#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
       if(!pSiS->SecondHead) {
	  SISAdjustFrameHW_CRT2(pScrn, x, y);
       } else {
	  SISAdjustFrameHW_CRT1(pScrn, x, y);
       }
       return;
    }
#endif

    switch(pSiS->VGAEngine) {
    case SIS_300_VGA:
    case SIS_315_VGA:
       SISAdjustFrameHW_CRT1(pScrn, x, y);
       if(pSiS->VBFlags & CRT2_ENABLE) {
	  if(!SiSBridgeIsInSlaveMode(pScrn)) {
	     SISAdjustFrameHW_CRT2(pScrn, x, y);
	  }
       }
       break;
    default:
       if(pScrn->bitsPerPixel < 8) {
          base = (y * pSiS->CurrentLayout.displayWidth + x + 3) >> 3;
       } else {
          base  = y * pSiS->CurrentLayout.displayWidth + x;
          switch(pSiS->CurrentLayout.bitsPerPixel) {
	     case 8:	base >>= 2;
			break;
	     case 16:	base >>= 1;
			break;
	     case 24:	base = ((base * 3)) >> 2;
			base -= base % 6;
          }
       }
       base += (pSiS->dhmOffset/4);
       /* Unlock CRTC registers */
       inSISIDXREG(SISCR,  0x11, cr11backup);
       andSISIDXREG(SISCR, 0x11, 0x7F);
       outSISIDXREG(SISCR, 0x0D, base & 0xFF);
       outSISIDXREG(SISCR, 0x0C, (base >> 8) & 0xFF);
       inSISIDXREG(SISSR,  0x27, temp);
       temp &= 0xF0;
       temp |= (base & 0x0F0000) >> 16;
       outSISIDXREG(SISSR, 0x27, temp);
       /* Eventually lock CRTC registers */
       setSISIDXREG(SISCR, 0x11, 0x7F, (cr11backup & 0x80));
    }

}

/*********************************************************/
/*                       EnterVT()                       */
/*********************************************************/


static Bool
SISEnterVT(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
    SiS_SiSFB_Lock(pScrn, TRUE);

    sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);

    SiSRestoreCR323637(pSiS);

    if(!SISModeInit(pScrn, pScrn->currentMode)) {
       SISErrorLog(pScrn, "SiSEnterVT: SISModeInit() failed\n");
       return FALSE;
    }

    /* No need to go through pScrn->AdjustFrame; Xv's
     * EnterVT handles the overlay(s) anyway.
     */
    SISAdjustFrame(pScrn, pScrn->frameX0, pScrn->frameY0);


/* Mark for 3D full-screen bug */
/*
#ifdef SISDRI
    if(pSiS->directRenderingEnabled) {
       DRIUnlock(screenInfo.screens[scrnIndex]);
    }
#endif
*/

#ifdef SISDUALHEAD
    if((!pSiS->DualHeadMode) || (!pSiS->SecondHead))
#endif
       if(pSiS->ResetXv) {
          (pSiS->ResetXv)(pScrn);
       }

    return TRUE;
}


/*********************************************************/
/*                       LeaveVT()                       */
/*********************************************************/


static void
SISLeaveVT(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDRI
    ScreenPtr pScreen;

    if(pSiS->directRenderingEnabled) {
       pScreen = xf86ScreenToScrn(pScrn);
/* Mark for 3D full-screen bug */
/*   DRILock(pScreen, 0); */
    }
#endif

#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode && pSiS->SecondHead) return;
#endif

    if(pSiS->CursorInfoPtr) {
#ifdef SISDUALHEAD
       if(pSiS->DualHeadMode) {
          if(!pSiS->SecondHead) {
	     pSiS->ForceCursorOff = TRUE;
	     pSiS->CursorInfoPtr->HideCursor(pScrn);
	     SISWaitVBRetrace(pScrn);
	     pSiS->ForceCursorOff = FALSE;
	  }
       } else {
#endif
          pSiS->CursorInfoPtr->HideCursor(pScrn);
          SISWaitVBRetrace(pScrn);
#ifdef SISDUALHEAD
       }
#endif
    }

    SISBridgeRestore(pScrn);

    if(pSiS->UseVESA) {

       /* This is a q&d work-around for a BIOS bug. In case we disabled CRT2,
    	* VBESaveRestore() does not restore CRT1. So we set any mode now,
	* because VBESetVBEMode correctly restores CRT1. Afterwards, we
	* can call VBESaveRestore to restore original mode.
	*/
       if((pSiS->VBFlags2 & VB2_VIDEOBRIDGE) && (!(pSiS->VBFlags & DISPTYPE_DISP2)))
	  VBESetVBEMode(pSiS->pVbe, (pSiS->SISVESAModeList->n) | 0xc000, NULL);

       SISVESARestore(pScrn);

    } else {

       SISRestore(pScrn);

    }

    /* We use (otherwise unused) bit 7 to indicate that we are running
     * to keep sisfb to change the displaymode (this would result in
     * lethal display corruption upon quitting X or changing to a VT
     * until a reboot)
     */
    if(pSiS->VGAEngine == SIS_300_VGA || pSiS->VGAEngine == SIS_315_VGA) {
       orSISIDXREG(SISCR,0x34,0x80);
    }

    SISVGALock(pSiS);

    SiS_SiSFB_Lock(pScrn, FALSE);
}


/*********************************************************/
/*                      CloseScreen()                    */
/*********************************************************/


static Bool
SISCloseScreen(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
    SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif


    if(pSiS->SiSCtrlExtEntry) {
       SiSCtrlExtUnregister(pSiS, pScrn->scrnIndex);
    }

#ifdef SISDRI
    if(pSiS->directRenderingEnabled) {
       SISDRICloseScreen(pScreen);
       pSiS->directRenderingEnabled = FALSE;
    }
#endif

    if(pScrn->vtSema) {

        if(pSiS->CursorInfoPtr) {
#ifdef SISDUALHEAD
           if(pSiS->DualHeadMode) {
              if(!pSiS->SecondHead) {
	         pSiS->ForceCursorOff = TRUE;
	         pSiS->CursorInfoPtr->HideCursor(pScrn);
	         SISWaitVBRetrace(pScrn);
	         pSiS->ForceCursorOff = FALSE;
	      }
           } else {
#endif
             pSiS->CursorInfoPtr->HideCursor(pScrn);
             SISWaitVBRetrace(pScrn);
#ifdef SISDUALHEAD
           }
#endif
	}

        SISBridgeRestore(pScrn);

	if(pSiS->UseVESA) {

	  /* This is a q&d work-around for a BIOS bug. In case we disabled CRT2,
    	   * VBESaveRestore() does not restore CRT1. So we set any mode now,
	   * because VBESetVBEMode correctly restores CRT1. Afterwards, we
	   * can call VBESaveRestore to restore original mode.
	   */
           if((pSiS->VBFlags2 & VB2_VIDEOBRIDGE) && (!(pSiS->VBFlags & DISPTYPE_DISP2)))
	      VBESetVBEMode(pSiS->pVbe, (pSiS->SISVESAModeList->n) | 0xc000, NULL);

	   SISVESARestore(pScrn);

	} else {

	   SISRestore(pScrn);

	}

        SISVGALock(pSiS);

    }

    SiS_SiSFB_Lock(pScrn, FALSE);

    /* We should restore the mode number in case vtsema = false as well,
     * but since we haven't register access then we can't do it. I think
     * I need to rework the save/restore stuff, like saving the video
     * status when returning to the X server and by that save me the
     * trouble if sisfb was started from a textmode VT while X was on.
     */

    SISUnmapMem(pScrn);
#ifdef SIS_PC_PLATFORM
    SiSVGAUnmapMem(pScrn);
#endif

#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
       pSiSEnt = pSiS->entityPrivate;
       pSiSEnt->refCount--;
    }
#endif

    if(pSiS->pInt) {
       xf86FreeInt10(pSiS->pInt);
       pSiS->pInt = NULL;
    }

#ifdef SIS_USE_XAA
    if(!pSiS->useEXA) {
       if(pSiS->AccelLinearScratch) {
          xf86FreeOffscreenLinear(pSiS->AccelLinearScratch);
          pSiS->AccelLinearScratch = NULL;
       }
       if(pSiS->AccelInfoPtr) {
          XAADestroyInfoRec(pSiS->AccelInfoPtr);
          pSiS->AccelInfoPtr = NULL;
       }
    }
#endif

#ifdef SIS_USE_EXA
    if(pSiS->useEXA) {
       if(pSiS->EXADriverPtr) {
          exaDriverFini(pScreen);
          free(pSiS->EXADriverPtr);
          pSiS->EXADriverPtr = NULL;
          pSiS->exa_scratch = NULL;
       }
    }
#endif

    if(pSiS->CursorInfoPtr) {
       xf86DestroyCursorInfoRec(pSiS->CursorInfoPtr);
       pSiS->CursorInfoPtr = NULL;
    }

    if(pSiS->CursorScratch) {
       free(pSiS->CursorScratch);
       pSiS->CursorScratch = NULL;
    }

    if(pSiS->ShadowPtr) {
       free(pSiS->ShadowPtr);
       pSiS->ShadowPtr = NULL;
    }

    if(pSiS->DGAModes) {
       free(pSiS->DGAModes);
       pSiS->DGAModes = NULL;
    }

    if(pSiS->adaptor) {
       free(pSiS->adaptor);
       pSiS->adaptor = NULL;
       pSiS->ResetXv = pSiS->ResetXvGamma = pSiS->ResetXvDisplay = NULL;
    }

    if(pSiS->blitadaptor) {
       free(pSiS->blitadaptor);
       pSiS->blitadaptor = NULL;
    }

    if(pSiS->crt2gcolortable) {
       free(pSiS->crt2gcolortable);
       pSiS->crt2gcolortable = NULL;
    }

    if(pSiS->crt2cindices) {
       free(pSiS->crt2cindices);
       pSiS->crt2cindices = NULL;
    }

    pScrn->vtSema = FALSE;

#ifdef SIS_HAVE_RR_FUNC
#ifdef SIS_HAVE_DRIVER_FUNC
    pScrn->DriverFunc = NULL;
#else
    pScrn->RRFunc = NULL;
#endif
#endif

    /* Restore Blockhandler */
    pScreen->BlockHandler = pSiS->BlockHandler;

    pScreen->CloseScreen = pSiS->CloseScreen;

    return(*pScreen->CloseScreen)(pScreen);
}


/*********************************************************/
/*                     FreeScreen()                      */
/*********************************************************/

/* Free up any per-generation data structures */

static void
SISFreeScreen(ScrnInfoPtr pScrn)
{
#ifdef SIS_NEED_MAP_IOP
    SISPtr pSiS = SISPTR(pScrn);

    if(pSiS) {
#ifdef SISDUALHEAD
       SISEntPtr pSiSEnt = pSiS->entityPrivate;
       if(pSiSEnt) {
          pSiSEnt->forceUnmapIOPBase = TRUE;
       }
#endif
       SISUnmapIOPMem(pScrn);
    }
#endif

    SISFreeRec(pScrn);
}


/*********************************************************/
/*                        Helpers                        */
/*********************************************************/

#define MODEID_OFF 0x449

UChar
SiS_GetSetBIOSScratch(ScrnInfoPtr pScrn, UShort offset, UChar value)
{
    UChar ret = 0;
#ifdef SIS_USE_BIOS_SCRATCH
    UChar *base;
#endif

    /* For some reasons (like detecting the current display mode),
     * we need to read (or write-back) values from the BIOS
     * scratch area. This area is only valid for the primary
     * graphics card. For the secondary, we just return some
     * defaults and ignore requests to write data. As regards
     * the display mode: If sisfb is loaded for the secondary
     * card, it very probably has set a mode, but in any case
     * informed us via its info packet. So this here will not be
     * called for mode detection in this case.
     */

    switch(offset) {
    case 0x489:
       ret = 0x11;  /* Default VGA Info */
       break;
    case MODEID_OFF:
       ret = 0x03;  /* Default current display mode */
       break;
    }

#ifdef SIS_USE_BIOS_SCRATCH
    if(SISPTR(pScrn)->Primary) {

#ifndef XSERVER_LIBPCIACCESS
       base = xf86MapVidMem(pScrn->scrnIndex, VIDMEM_MMIO, 0, 0x2000);
#else
       (void) pci_device_map_legacy(SISPTR(pScrn)->PciInfo, 0, 0x2000, 1, &base); // HA HA HA MAGIC NUMBER
#endif

       if(!base) {
          SISErrorLog(pScrn, "(Could not map BIOS scratch area)\n");
          return ret;
       }

       ret = *(base + offset);

       /* value != 0xff means: set register */
       if(value != 0xff) {
          *(base + offset) = value;
       }

#ifndef XSERVER_LIBPCIACCESS
       xf86UnMapVidMem(pScrn->scrnIndex, base, 0x2000);
#else
       (void) pci_device_unmap_legacy(SISPTR(pScrn)->PciInfo, base, 0x2000);
#endif
    }
#endif
    return ret;
}

UChar
SiS_GetSetModeID(ScrnInfoPtr pScrn, UChar id)
{
    return(SiS_GetSetBIOSScratch(pScrn, MODEID_OFF, id));
}

void
SiSMemCopyToVideoRam(SISPtr pSiS, UChar *to, UChar *from, int size)
{
   if((ULong)to & 15) (*pSiS->SiSFastMemCopy)(to, from, size);
   else       	      (*pSiS->SiSFastVidCopy)(to, from, size);
}

void
SiSMemCopyFromVideoRam(SISPtr pSiS, UChar *to, UChar *from, int size)
{
   if((ULong)to & 15) (*pSiS->SiSFastMemCopyFrom)(to, from, size);
   else       	      (*pSiS->SiSFastVidCopyFrom)(to, from, size);
}

void
sisSaveUnlockExtRegisterLock(SISPtr pSiS, UChar *reg1, UChar *reg2)
{
    register UChar val;
    ULong mylockcalls;
#ifdef TWDEBUG
    UChar val1, val2;
    int i;
#endif

    pSiS->lockcalls++;
    mylockcalls = pSiS->lockcalls;

    /* check if already unlocked */
    inSISIDXREG(SISSR, 0x05, val);

    if(val != 0xa1) {

       /* save State */
       if(reg1) *reg1 = val;

       /* unlock */
       outSISIDXREG(SISSR, 0x05, 0x86);

       /* Now check again */
       inSISIDXREG(SISSR, 0x05, val);

       if(val != 0xA1) {

          xf86DrvMsg(pSiS->pScrn->scrnIndex, X_WARNING,
               "Failed to unlock SR registers at relocated i/o ports\n");

#ifdef TWDEBUG
          for(i = 0; i <= 0x3f; i++) {
		inSISIDXREG(SISSR, i, val1);
		inSISIDXREG(0x3c4, i, val2);
		xf86DrvMsg(pSiS->pScrn->scrnIndex, X_INFO,
			"SR%02d: RelIO=0x%02x 0x3c4=0x%02x (%ld)\n",
			i, val1, val2, mylockcalls);
	  }
#endif

	  /* Emergency measure: unlock at 0x3c4, and try to enable relocated IO ports */
	  switch(pSiS->VGAEngine) {
          case SIS_OLD_VGA:
	  case SIS_530_VGA:
	     outSISIDXREG(0x3c4, 0x05, 0x86);
	     andSISIDXREG(0x3c4, 0x33, ~0x20);
	     break;
	  case SIS_300_VGA:
	  case SIS_315_VGA:
	     outSISIDXREG(0x3c4, 0x05, 0x86);
	     orSISIDXREG(0x3c4, 0x20, 0x20);
	     break;
          }
	  outSISIDXREG(SISSR, 0x05, 0x86);
	  inSISIDXREG(SISSR, 0x05, val);
	  if(val != 0xa1) {
	     SISErrorLog(pSiS->pScrn,
			"Failed to unlock SR registers (%p, %lx, 0x%02x; %ld)\n",
			(void *)pSiS, (ULong)pSiS->RelIO, val, mylockcalls);
	     /* Now await doom... */
	  }
       }
    }
    if((pSiS->VGAEngine == SIS_OLD_VGA) || (pSiS->VGAEngine == SIS_530_VGA)) {
       inSISIDXREG(SISCR, 0x80, val);
       if(val != 0xa1) {
          /* save State */
          if(reg2) *reg2 = val;
          outSISIDXREG(SISCR, 0x80, 0x86);
	  inSISIDXREG(SISCR, 0x80, val);
	  if(val != 0xA1) {
	     SISErrorLog(pSiS->pScrn,
	        "Failed to unlock cr registers (%p, %lx, 0x%02x)\n",
	       (void *)pSiS, (ULong)pSiS->RelIO, val);
	  }
       }
    }
}

void
sisRestoreExtRegisterLock(SISPtr pSiS, UChar reg1, UChar reg2)
{
    /* restore lock */
#ifndef UNLOCK_ALWAYS
    outSISIDXREG(SISSR, 0x05, reg1 == 0xA1 ? 0x86 : 0x00);
    if((pSiS->VGAEngine == SIS_OLD_VGA) || (pSiS->VGAEngine == SIS_530_VGA)) {
       outSISIDXREG(SISCR, 0x80, reg2 == 0xA1 ? 0x86 : 0x00);
    }
#endif
}

/**************************************************************************/
/**                             SISPMEvent   :: Ivans                    **/
/**************************************************************************/
static Bool
SISHotkeySwitchCRT2Status(ScrnInfoPtr pScrn,ULong newvbflags,ULong newvbflags3)
{
   SISPtr pSiS = SISPTR(pScrn);	
   Bool hcm = pSiS->HaveCustomModes;
   DisplayModePtr mode = pScrn->currentMode;

   if((pSiS->VGAEngine != SIS_300_VGA)&&(pSiS->VGAEngine != SIS_315_VGA))
   return FALSE;

   if(!(pSiS->VBFlags2 & VB2_VIDEOBRIDGE))
   return FALSE;
   
   if(pSiS->DualHeadMode)
   return FALSE;

   if(pSiS->MergedFB)
   return FALSE;

   newvbflags &= CRT2_ENABLE;
   newvbflags |= pSiS->VBFlags & ~CRT2_ENABLE;

   newvbflags3 &= VB3_CRT1_TYPE;
   newvbflags3 |= pSiS->VBFlags3 & ~VB3_CRT1_TYPE;

   if((!(newvbflags & CRT2_ENABLE)) && (!newvbflags & DISPTYPE_CRT1))
   {
	  xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"CRT2 can't be switched off while CRT1 is off.\n");
	   return FALSE;
   }
   if(newvbflags & (CRT2_LCD|CRT2_VGA))
   {
          newvbflags &= ~CRT1_LCDA;
   }

   newvbflags &= ~(SINGLE_MODE|MIRROR_MODE);
   if((newvbflags & DISPTYPE_CRT1)&&(newvbflags & CRT2_ENABLE))
   {
          newvbflags |= MIRROR_MODE; 
   }	   
   else
   {
          newvbflags |= SINGLE_MODE; 
   }
  /* while(mode->HDisplay != 1024)
   {
	   mode = mode->next;
   }
   if(mode->HDisplay == 1024)
   {
      xf86DrvMsg(0,X_INFO,"virtualX=%d,virtualY=%d,pitch=%d.\n",pScrn->virtualX,pScrn->virtualY,pScrn->displayWidth);
   }*/
   
   (*pSiS->SyncAccel)(pScrn);

   pSiS->VBFlags = pSiS->VBFlags_backup = newvbflags;
   pSiS->VBFlags3 = pSiS->VBFlags_backup3 = newvbflags3;  
 
   pSiS->skipswitchcheck = TRUE;
   if(!((*pScrn->SwitchMode)(pScrn,pScrn->currentMode)))
   {
          pSiS->skipswitchcheck = FALSE;
	  return FALSE;
   }
   pSiS->skipswitchcheck = FALSE;

   /*xf86DrvMsg(0,X_INFO,"frameX0=%d, frameY0=%d.\n",pScrn->frameX0,pScrn->frameY0);*/

   SISAdjustFrame(pScrn, pScrn->frameX0, pScrn->frameY0);

   return TRUE;

}

/**************************************************************************/
static Bool
SISHotkeySwitchCRT1Status(ScrnInfoPtr pScrn, int onoff) 
{
  SISPtr pSiS = SISPTR(pScrn);
  DisplayModePtr mode = pScrn->currentMode;
  ULong vbflags = pSiS->VBFlags;
  ULong vbflags3 = pSiS->VBFlags3;
  int crt1off;/*onoff: 0=OFF,1=ON(VGA),2=ON(LCDA),3=CRT1_LCD, 2 AND 3 not support.*/

  if((pSiS->VGAEngine != SIS_300_VGA)&&(pSiS->VGAEngine != SIS_315_VGA))
     return FALSE;

  if(pSiS->DualHeadMode)
     return FALSE;

  if((!onoff) && (!(vbflags & CRT2_ENABLE)))
     return FALSE; 

  if((onoff==2)|(onoff==3))
     return FALSE;

  if(pSiS->MergedFB)
     return FALSE; /*now we not support mergedfb switch on/off the CRT1.*/

  vbflags &= ~(DISPTYPE_CRT1 | SINGLE_MODE | MIRROR_MODE | CRT1_LCDA);
  vbflags3 &= ~(VB3_CRT1_TV | VB3_CRT1_LCD | VB3_CRT1_VGA); 
  crt1off = 1;
  
  if(onoff > 0)
  {
          if(onoff == 1)
	  {
		  vbflags |= DISPTYPE_CRT1;
	          crt1off = 0;
	  }
          else
	  {
		  vbflags3 |= VB3_CRT1_VGA;
	  }
	  if(vbflags & CRT2_ENABLE) vbflags |= MIRROR_MODE;
	  else vbflags |= SINGLE_MODE;
  }
  else
  {
	  vbflags |= SINGLE_MODE;
  }

  pSiS->CRT1off = crt1off;
  pSiS->VBFlags = pSiS->VBFlags_backup = vbflags;
  pSiS->VBFlags3 = pSiS->VBFlags_backup3 = vbflags3;

 (*pSiS->SyncAccel)(pScrn); 
  
 pSiS->skipswitchcheck = TRUE;
 if(!((*pScrn->SwitchMode)(pScrn,pScrn->currentMode)))
 {
       pSiS->skipswitchcheck = FALSE;
       return FALSE;
 }
 pSiS->skipswitchcheck = FALSE;

 SISAdjustFrame(pScrn, pScrn->frameX0, pScrn->frameY0);
 
 return TRUE;
}
/**************************************************************************/
static Bool
SISHotkeySwitchMode(ScrnInfoPtr pScrn, Bool adjust)
{
   pointer hkeymode;
   SISPtr pSiS = SISPTR(pScrn);
   ScreenPtr pScreen = screenInfo.screens[pScrn->scrnIndex];
   int dotClock;
   int dotclock=65146;
   int hdisplay=1024;

   if(!VidModeGetCurrentModeline(pScrn->scrnIndex,&hkeymode,&dotClock))
   return FALSE;
          
   if(!VidModeGetFirstModeline(pScrn->scrnIndex,&hkeymode,&dotClock))
   return FALSE;
  
   do{   /* dotclock and hdisplay must given by parameters of 1024*768 */
       if((VidModeGetDotClock(pScrn->scrnIndex,dotclock)==dotClock)&&(VidModeGetModeValue(hkeymode,0)==hdisplay)) 
       {
             pScrn->virtualX = 1024;
	     pScrn->virtualY = 768;
	    /* pSiS->scrnPitch = 4096;
	     pSiS->scrnOffset = 4096;*/
	     pScrn->zoomLocked=0;/* try for xf86ZoomViewport.*/
	     pScrn->display->virtualX = 1024;
	     pScrn->display->virtualY = 768;   
	    /* xf86RandRSetNewVirtualAndDimensions(pScreen,1024,768,0,0,0);*/  
	     if(!VidModeSwitchMode(pScrn->scrnIndex,hkeymode))
             { 
	            return FALSE;
	     }	
            /* xf86ZoomViewport(pScrn,1);*/	     
       }
   
   } while(VidModeGetNextModeline(pScrn->scrnIndex,&hkeymode,&dotClock));   

   xf86DrvMsg(0,X_INFO,"[Layout]:(display)scrnPitch=%d,(data)scrnOffset=%d.\n",pSiS->scrnPitch,pSiS->scrnOffset);
   
   xf86DrvMsg(0,X_INFO,"[Layout]:displayWidth=%d,displayHeight=%d.\n",pSiS->CurrentLayout.displayWidth,pSiS->CurrentLayout.displayHeight); 
  
   
               xf86ZoomViewport(pScreen,1);

	       SISAdjustFrame(pScrn,0,0);

   
   return TRUE;
}


/**************************************************************************/
static Bool
SISPMEvent(ScrnInfoPtr pScrn, pmEvent event, Bool undo)
{
  SISPtr pSiS = SISPTR(pScrn);
  unsigned char hotkeyflag = 0;/*check BIOS flag.*/
  unsigned char checkflag = 0;/*just for test using.*/

  xf86DrvMsg(0,X_INFO,"Enter VT, event %d, undo: %d.\n",event,undo);
 
  switch(event)
  {
         case XF86_APM_SYS_SUSPEND:
         case XF86_APM_CRITICAL_SUSPEND: /*do we want to delay a critical suspend?*/
         case XF86_APM_USER_SUSPEND:
         case XF86_APM_SYS_STANDBY:
         case XF86_APM_USER_STANDBY:
         {        
              xf86DrvMsg(0,X_INFO,"PM_EVENT:event=%d,undo=%d.\n",event,undo);		 
		 if (!undo && !pSiS->suspended) {
	               pScrn->LeaveVT(pScrn);
	               pSiS->suspended = TRUE;
	               sleep(0);
                   } 
		     else if (undo && pSiS->suspended) {
	            sleep(0);
	            pScrn->EnterVT(pScrn);
	            pSiS->suspended = FALSE;
                  }
	   }
      break;

      case XF86_APM_STANDBY_RESUME:
      case XF86_APM_NORMAL_RESUME:
      case XF86_APM_CRITICAL_RESUME:
      {
	  	  if (pSiS->suspended) {
	        sleep(0);
	        pScrn->EnterVT(pScrn);
	        pSiS->suspended = FALSE;
	        SaveScreens(SCREEN_SAVER_FORCER, ScreenSaverReset);
               }
	 }
      break;
	  
	case XF86_APM_CAPABILITY_CHANGED:
      {     
	                inSISIDXREG(SISCR,0x3d,hotkeyflag);/*check device switch flag from BIOS CR 0x3d bit[2].*/
                        
			
			if(pSiS->IgnoreHotkeyFlag || (hotkeyflag & 0x04))
			{	
						
			SISCRT1PreInit(pScrn); /*redetecting CRT1, pSiS->CRT1detected will update.*/
		  
			switch(pSiS->Hkey_Device_Switch_State) /*checking current stste for next state.*/
			{ 
				case LCD_only:
				{         	
                                      if( pSiS->CRT1Detected )
				      {
				  	      SISHotkeySwitchCRT1Status(pScrn,1); /*open VGA to mirror mode.*/
					      pSiS->Hkey_Device_Switch_State = LCD_VGA_mirror;
					      xf86DrvMsg(0,X_INFO,"[Device Switch]LCD->Mirror.(current Mirror mode.)\n");
					      /*xf86DrvMsg(0,X_INFO,"[Device Switch]:pSiS->CRT1Detected=%d,pSiS->CRT1off=%d\n",pSiS->CRT1Detected,pSiS->CRT1off);*/
				      } 
			              else
				      {
				            /* SISHotkeySwitchCRT1Status(pScrn,0);*//*close CRT1*/ 
					      SISHotkeySwitchCRT2Status(pScrn,0x20080002,0x0); /*redetect LCD.*/	     
					      pSiS->Hkey_Device_Switch_State = LCD_only;
                          xf86DrvMsg(0,X_INFO,"[Device Switch]LCD->LCD.(current LCD ONLY.)\n");
					      /*xf86DrvMsg(0,X_INFO,"[Device Switch]:pSiS->CRT1Detected=%d,pSiS->CRT1off=%d\n",pSiS->CRT1Detected,pSiS->CRT1off);*/

				      }
				}	      
				break;	
			       
				case LCD_VGA_mirror:
				{      
                                      if( pSiS->CRT1Detected )
				      { 
				              SISHotkeySwitchCRT2Status(pScrn,0x40080000,0x0); /*close LCD.*/
                                              pSiS->Hkey_Device_Switch_State = VGA_only;
					      xf86DrvMsg(0,X_INFO,"[Device Switch]Mirror->VGA.(current VGA ONLY.)\n");
					      /*xf86DrvMsg(0,X_INFO,"[Device Switch]:pSiS->CRT1Detected=%d,pSiS->CRT1off=%d\n",pSiS->CRT1Detected,pSiS->CRT1off);*/

				      }
			              else
				      {
				              SISHotkeySwitchCRT1Status(pScrn,0); /*close VGA.*/
                                              pSiS->Hkey_Device_Switch_State = LCD_only;
					      xf86DrvMsg(0,X_INFO,"[Device Swiatach]Mirror->LCD.(current LCD ONLY.)\n");
					      /*xf86DrvMsg(0,X_INFO,"[Device Switch]:pSiS->CRT1Detected=%d,pSiS->CRT1off=%d\n",pSiS->CRT1Detected,pSiS->CRT1off);*/

				      }
				}	      
				break;

				case VGA_only:
				{  /*SISHotkeySwitchCRT1Status(pScrn,0);*//*close VGA*/
                                      SISHotkeySwitchCRT2Status(pScrn,0x20000002,0x0); /*open LCD*/
				      SISHotkeySwitchCRT1Status(pScrn,0); /*close VGA*/
                                      pSiS->Hkey_Device_Switch_State = LCD_only;
				      xf86DrvMsg(0,X_INFO,"[Device Swiatach]VGA->LCD.(current LCD ONLY.)\n");
				      /*xf86DrvMsg(0,X_INFO,"[Device Switch]:pSiS->CRT1Detected=%d,pSiS->CRT1off=%d\n",pSiS->CRT1Detected,pSiS->CRT1off);*/
				}
				break;

				default:
				        xf86DrvMsg(0,X_INFO,"Unknow current hotkey DS state, Hkey do nothing.\n");
			   }
		           hotkeyflag &= 0xfb;/*clean hotkey flag.*/
		           outSISIDXREG(SISCR,0x3d,hotkeyflag);
		           inSISIDXREG(SISCR,0X3d,checkflag);	
		     }/*hotkeyflag*/
		     	
	  }
	  break;
        
	  default:
          xf86DrvMsg(0,X_INFO,"SISPMEvent: Unknow Event %d is received.\n",event);
  } 
  return 1;/*TRUE*/
}

void
sis_print_registers(SISPtr pSiS)
{
#define print(...)	xf86ErrorFVerb(1, __VA_ARGS__)
    auto void print_range(char *name, int base, int first, int last) {
	int		i, j;
	unsigned char	c;
	char		buffer[9];
	print("%s:\n", name);
	buffer[8] = 0;
	for (i = first; i <= last; i++) {
	    inSISIDXREG(base, i, c);
	    for (j = 0; j < 8; j++)
		buffer[7 - j] = c & (1 << j) ? '1' : '0';
	    print("\t%02x: %02x:%s\n", i, c, buffer);
	}
    }

    auto void print_range_int(char *name, int base, int first, int last) {
	int		i, j;
	unsigned int	l;
	char		buffer[33];
	print("%s:\n", name);
	buffer[32] = 0;
	for (i = first; i <= last; i += 4) {
	    l = inSISREGL(base + i);
	    for (j = 0; j < 32; j++)
		buffer[31 - j] = l & (1 << j) ? '1' : '0';
	    print("\t%02x: %08x:%s\n", i, l, buffer);
	}
    }

    print_range_int	("PCI: CNF00 - CNF1B", pSiS->RelIO, 0x00, 0x1b);
    print_range_int	("PCI: CNF2C - CNF47", pSiS->RelIO, 0x2C, 0x47);
    print_range_int	("AGP: CNF50 - CNF5B", pSiS->RelIO, 0x50, 0x5B);
    print_range		("CRT1: SR05 - SR12", SISSR, 0x05, 0x12);
    print_range		("CRT1: SR13 - SR16 (reserved)", SISSR, 0x13, 0x16);
    print		("CRT1: SR19 - SR1A (reserved)\n");
    print_range		("CRT1: SR1B - SR3A", SISSR, 0x1b, 0x3a);
    print		("CRT1: SR3B (reserved)\n");
    print_range		("CRT1: SR3C - SR3F", SISSR, 0x3c, 0x3f);
    print_range		("CRT1: CR19 - CR1A", SISCR, 0x19, 0x1a);
    print		("CRT1: CR1B - CR27 (undocumented?)\n");
    print_range		("CRT1: CR28 - CR2E", SISCR, 0x28, 0x2e);
    print		("CRT1: CR2F (reserved)\n");
    print_range		("VGA BIOS: CR30 - CR3F", SISCR, 0x30, 0x3f);
    print_range		("CRT1: CR40 - CR43", SISCR, 0x40, 0x43);
    print		("CRT1: CR44 - CR45 (reserved)\n");
    print_range		("CRT1: CR46 - CR67", SISCR, 0x46, 0x67);
    print		("CRT1: CR68 - CR6F (DRAM registers reserved for backward compatibility with 760)\n");
    print		("CRT1: CR70 - CR77 (undocumented?)\n");
    print_range		("SMA BIOS: CR78 - CR7F", SISCR, 0x78, 0x7f);
    print_range_int	("CRT1: CR80 - CR9B", SISCR, 0x80, 0xb3);
    print_range_int	("CRT1: CRC0 - CRF3", SISCR, 0xc0, 0xf3);
    print_range		("CRT2: SIGNAL REGISTERS, PART1 00 - 45", SISPART1, 0x00, 0x45);
    print_range		("CRT2: TV SIGNAL REGISTERS, PART2 00 - 4d", SISPART2, 0x00, 0x4d);
    print_range		("CRT2: TV COPY PROTECTION, PART3 00 - 40", SISPART3, 0x00, 0x40);
    print_range		("CRT2: SIGNAL REGISTERS, PART4 00 - 3A", SISPART4, 0x00, 0x3a);
    print_range		("CRT2: PALETTE SIGNAL REGISTERS, PART5 00 - 00 (?)", SISPART5, 0x00, 0x00);
#undef print
}
