/* $XFree86$ */
/* $XdotOrg$ */
/*
 * Video bridge detection and configuration for 300 series and later
 *
 * Copyright (C) 2001-2005 by Thomas Winischhofer, Vienna, Austria
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
 * Author: 	Thomas Winischhofer <thomas@winischhofer.net>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "sis.h"
#define SIS_NEED_inSISREG
#define SIS_NEED_inSISIDXREG
#define SIS_NEED_outSISIDXREG
#define SIS_NEED_orSISIDXREG
#define SIS_NEED_andSISIDXREG
#define SIS_NEED_setSISIDXREG
#include "sis_regs.h"
#include "sis_dac.h"

#include "sis_vb.h"

static Bool
TestDDC1(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
    UShort old;
    int    count = 48;

    old = SiS_ReadDDC1Bit(pSiS->SiS_Pr);
    do {
       if(old != SiS_ReadDDC1Bit(pSiS->SiS_Pr)) break;
    } while(count--);
    return (count == -1) ? FALSE : TRUE;
}

static int
SiS_SISDetectCRT1(ScrnInfoPtr pScrn, unsigned char *buffer)
{
    SISPtr pSiS = SISPTR(pScrn);
    UShort temp = 0xffff;
    UChar  SR1F, CR63=0, CR17, SR1;
    int    i, j, ret = 0;
    Bool   mustwait = FALSE, longwait = FALSE;
    
    

    inSISIDXREG(SISSR, 0x1F, SR1F);
    setSISIDXREG(SISSR, 0x1F, 0x3f, 0x04);
    if(SR1F & 0xc0) mustwait = TRUE;

    inSISIDXREG(SISSR, 0x01, SR1);

    inSISIDXREG(SISCR, 0x17, CR17);
    CR17 &= 0x80;
    if(!CR17) {
       orSISIDXREG(SISCR, 0x17, 0x80);
       mustwait = TRUE;
    }

    if(pSiS->VGAEngine == SIS_315_VGA) {
       inSISIDXREG(SISCR, pSiS->myCR63, CR63);
       CR63 &= 0x40;
       andSISIDXREG(SISCR, pSiS->myCR63, 0xbf);
    }

    if(mustwait) {
       orSISIDXREG(SISSR, 0x01, 0x20);
       outSISIDXREG(SISSR, 0x00, 0x01);
       usleep(10000);
       outSISIDXREG(SISSR, 0x00, 0x03);
       for(i = 0; i < 10; i++) SISWaitRetraceCRT1(pScrn);
    }

    if(pSiS->ChipType >= SIS_330) {
       int watchdog, sr7;
       inSISIDXREG(SISSR, 0x07, sr7);
       orSISIDXREG(SISSR, 0x07, 0x10);
       if(pSiS->ChipType >= SIS_340) {
          outSISIDXREG(SISCR, 0x57, 0x4a);
       } else {
          outSISIDXREG(SISCR, 0x57, 0x5f);
       }
       orSISIDXREG(SISCR, 0x53, 0x02);
       watchdog = 655360;
       while((!((inSISREG(SISINPSTAT)) & 0x01)) && --watchdog);
       watchdog = 655360;
       while(((inSISREG(SISINPSTAT)) & 0x01) && --watchdog);
       
       #ifdef TWDEBUG
        unsigned char uc=0;
        uc = inSISREG(SISMISCR);
       xf86DrvMsg(0,X_INFO,"[SiS_SISDetectCRT1()]:MISC REG Read Port context=%x\n.",uc);
      
        uc = inSISREG(SISMISCW);
       xf86DrvMsg(0,X_INFO,"[SiS_SISDetectCRT1()]:MISC REG Write Port context=%x\n.",uc);
       #endif
       if(pSiS->trace_VGA_MISCW)
       {if((inSISREG(SISMISCW)) & 0x10) {temp = 1;}}  /*blocked CRT DDC detect refer from VBIOS.*/
       
       andSISIDXREG(SISCR, 0x53, 0xfd);
       outSISIDXREG(SISCR, 0x57, 0x00);
       outSISIDXREG(SISSR, 0x07, sr7);
    }

    if((temp == 0xffff) && (!pSiS->SiS_Pr->DDCPortMixup)) {
       i = 4;
       do {
          temp = SiS_HandleDDC(pSiS->SiS_Pr, pSiS->VBFlags, pSiS->VGAEngine, 0, 0, NULL, pSiS->VBFlags2);
          if(((temp == 0) || (temp == 0xffff)) && mustwait) {
             for(j = 0; j < 10; j++) SISWaitRetraceCRT1(pScrn);
	  }
       } while(((temp == 0) || (temp == 0xffff)) && i--);

       if((temp == 0) || (temp == 0xffff)) {
          if(TestDDC1(pScrn)) temp = 1;
       }

       longwait = TRUE;
    }

    if((temp) && (temp != 0xffff)) {
       orSISIDXREG(SISCR, 0x32, 0x20);
       ret = 1;
       if(buffer) {
          i = 5;
          do {
             temp = SiS_HandleDDC(pSiS->SiS_Pr, pSiS->VBFlags, pSiS->VGAEngine, 0, 0, NULL, pSiS->VBFlags2);
             if(((temp == 0) || (temp == 0xffff)) && mustwait && !longwait) {
                for(j = 0; j < 10; j++) SISWaitRetraceCRT1(pScrn);
             }
          } while(((temp == 0) || (temp == 0xffff)) && i--);
          if((temp != 0xffff) && (temp & 0x02)) {
             i = 3;
	     do {
	       temp = SiS_HandleDDC(pSiS->SiS_Pr, pSiS->VBFlags, pSiS->VGAEngine,
				0, 1, buffer, pSiS->VBFlags2);
	     } while((temp) && i--);
	     if(!temp) ret = 2;
          }
       }
    } else if(pSiS->ChipType >= SIS_330) {
       andSISIDXREG(SISCR, 0x32, ~0x20);
       ret = 0;
    }

    outSISIDXREG(SISSR, 0x01, SR1);

    if(pSiS->VGAEngine == SIS_315_VGA) {
       setSISIDXREG(SISCR, pSiS->myCR63, 0xBF, CR63);
    }

    setSISIDXREG(SISCR, 0x17, 0x7F, CR17);

    outSISIDXREG(SISSR, 0x1F, SR1F);

    return ret;
}

/* Detect CRT1 */
void SISCRT1PreInit(ScrnInfoPtr pScrn)
{
    SISPtr  pSiS = SISPTR(pScrn);
    UChar CR32;
    UChar OtherDevices = 0;

    pSiS->CRT1Detected = FALSE;

    if(!(pSiS->VBFlags2 & VB2_VIDEOBRIDGE)) {
       pSiS->CRT1Detected = TRUE;
       pSiS->CRT1off = 0;
       
       #ifdef TWDEBUG
        xf86DrvMsg(0,X_INFO,"[ SISCRT1PreInit() -> != VB2_VIDEOBRIDGE ]:CRT1=%d.\n",pSiS->CRT1Detected );
       #endif

       return;
    }
#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) {
       pSiS->CRT1Detected = TRUE;
       pSiS->CRT1off = 0;
       #ifdef TWDEBUG
        xf86DrvMsg(0,X_INFO,"[ SISCRT1PreInit() -> pSiS->DualHeadMode ]:CRT1=%d.\n",pSiS->CRT1Detected );
       #endif

       return;
    }
#endif

#ifdef SISMERGED
    if((pSiS->MergedFB) && (!(pSiS->MergedFBAuto))) {
       pSiS->CRT1Detected = TRUE;
       pSiS->CRT1off = 0;
       return;
    }
#endif

    inSISIDXREG(SISCR, 0x32, CR32);

    if(pSiS->ChipType >= SIS_330) {
        pSiS->CRT1Detected = SiS_SISDetectCRT1(pScrn, NULL);
        #ifdef TWDEBUG
        xf86DrvMsg(0,X_INFO,"[ SISCRT1PreInit() -> >= SIS_330 ]:CRT1=%d.\n",pSiS->CRT1Detected);
       #endif

    } else {
        
           if(CR32 & 0x20) 
           {      pSiS->CRT1Detected = TRUE;
                  #ifdef TWDEBUG
                  xf86DrvMsg(0,X_INFO,"[ SISCRT1PreInit() -> < SIS_330 ]:CRT1=%d.\n",pSiS->CRT1Detected);
                  #endif 
           }

           else { 
                  pSiS->CRT1Detected = SiS_SISDetectCRT1(pScrn, NULL);
                  #ifdef TWDEBUG
                  xf86DrvMsg(0,X_INFO,"[ SISCRT1PreInit() -> CR32 & 0X20 = 0 ]:CRT1=%d.\n",pSiS->CRT1Detected);
                  #endif
                }
    }

    if(CR32 & 0x5F) OtherDevices = 1;

    if(pSiS->CRT1off == -1) {
       if(!pSiS->CRT1Detected) {

	  /* No CRT1 detected. */
	  /* If other devices exist, switch it off */
	  if(OtherDevices) pSiS->CRT1off = 1;
	  else             pSiS->CRT1off = 0;

       } else {

	  /* CRT1 detected, leave/switch it on */
	  pSiS->CRT1off = 0;

       }
    }

    xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		"%sCRT1/VGA detected\n",
		pSiS->CRT1Detected ? "" : "No ");
}
/*------------  only detect VGA1   ------------*/
unsigned int SiS_DetectVGA1(ScrnInfoPtr pScrn)
{
	SISPtr  pSiS = SISPTR(pScrn);
	unsigned int detected = 0;
	unsigned short temp = 0xffff;
        int            i,j;


	i = 0;/*Ivans changed.*/
        do {
	           temp = SiS_HandleDDC(pSiS->SiS_Pr, pSiS->VBFlags, pSiS->VGAEngine, 0, 0, NULL, pSiS->VBFlags2);
		      
		  /* if(((temp == 0) || (temp == 0xffff))) {
		           for(j = 0; j < 2; j++) SISWaitRetraceCRT1(pScrn);
		   }*/
           } while(((temp == 0) || (temp == 0xffff)) && i--);

        /*if((temp == 0) || (temp == 0xffff)) {
	          if(TestDDC1(pScrn)) temp = 1;
        }*/

	
	detected = temp;
		
	return detected;
}
/*---------------------------------------------*/
/* Detect CRT2-LCD and LCD size */
void SISLCDPreInit(ScrnInfoPtr pScrn, Bool quiet)
{
    SISPtr  pSiS = SISPTR(pScrn);
    UChar CR32, CR36, CR37, CR7D=0, tmp;

    pSiS->VBFlags &= ~(CRT2_LCD);
    pSiS->VBLCDFlags = 0;
    pSiS->LCDwidth   = 0;
    pSiS->LCDheight  = 0;

    if(!(pSiS->VBFlags2 & VB2_VIDEOBRIDGE)) return;
    if(pSiS->forceLCDcrt1)	return;

    inSISIDXREG(SISCR, 0x32, CR32);
    if(CR32 & 0x08) pSiS->VBFlags |= CRT2_LCD;

    /* If no panel has been detected by the BIOS during booting,
     * we try to detect it ourselves at this point. We do that
     * if forcecrt2redetection was given, too.
     * This is useful on machines with DVI connectors where the
     * panel was connected after booting. This is only supported
     * on the 315+ series and the 301/30xB/C bridge (because the
     * 30xLV don't seem to have a DDC port and operate only LVDS
     * panels which mostly don't support DDC). We only do this if
     * there was no secondary VGA detected by the BIOS, because LCD
     * and VGA2 share the same DDC channel and might be misdetected
     * as the wrong type (especially if the LCD panel only supports
     * EDID Version 1).
     * Addendum: For DVI-I connected panels, this is not ideal.
     * Therefore, we disregard an eventually detected secondary
     * VGA if the user forced CRT2 type to LCD.
     *
     * By default, CRT2 redetection is forced since 12/09/2003, as
     * I encountered numerous panels which deliver more or less
     * bogus DDC data confusing the BIOS. Since our DDC detection
     * is waaaay better, we prefer it over the primitive and
     * buggy BIOS method.
     *
     */
#ifdef SISDUALHEAD
    if((!pSiS->DualHeadMode) || (!pSiS->SecondHead)) {
#endif
       if((pSiS->VGAEngine == SIS_315_VGA) &&
	  (pSiS->VBFlags2 & VB2_SISTMDSBRIDGE) &&
	  (!(pSiS->VBFlags2 & VB2_30xBDH)) &&
	  (pSiS->VESA != 1)) {

	  if(pSiS->forcecrt2redetection) {
	     pSiS->VBFlags &= ~CRT2_LCD;
	     /* Do NOT clear CR32[D3] here! */
	  }

	  if(!(pSiS->nocrt2ddcdetection)) {
	     if((!(pSiS->VBFlags & CRT2_LCD)) &&
			( (!(CR32 & 0x10)) ||
			  (pSiS->ForceCRT2Type == CRT2_LCD) ) ) {
		if(!quiet) {
		   xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		      "%s LCD/plasma panel, sensing via DDC\n",
		      pSiS->forcecrt2redetection ?
		         "(Re)-detecting" : "BIOS detected no");
		}
		if(SiS_SenseLCDDDC(pSiS->SiS_Pr, pSiS)) {
		   xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		      "DDC error during LCD panel detection\n");
		} else {
		   inSISIDXREG(SISCR, 0x32, CR32);
		   if(CR32 & 0x08) {
		      pSiS->VBFlags |= CRT2_LCD;
		      pSiS->postVBCR32 |= 0x08;
		   } else {
		      xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			   "No LCD/plasma panel detected\n");
		   }
		}
	     }
	  }

       }
#ifdef SISDUALHEAD
    }
#endif

    if(pSiS->VBFlags & CRT2_LCD) {
       inSISIDXREG(SISCR, 0x36, CR36);
       if(pSiS->VGAEngine == SIS_300_VGA) {
	  if(pSiS->VBFlags2 & VB2_301) {
	     if((CR36 & 0x0f) < 0x0f) CR36 &= 0xf7;
	  }
       }
       if(pSiS->PRGB != -1) {
          tmp = 0x37;
          if((pSiS->VGAEngine == SIS_315_VGA) &&
             (pSiS->ChipType < SIS_661)       &&
             (pSiS->ROM661New)                &&
             (!(pSiS->SiS_Pr->PanelSelfDetected))) {
             tmp = 0x35;
          }
          if(pSiS->PRGB == 18)      orSISIDXREG(SISCR, tmp, 0x01);
          else if(pSiS->PRGB == 24) andSISIDXREG(SISCR, tmp, 0xfe);
       }
       else{/*if no Option force Panel RGB style,we set default 18bits(Ivans Lee)*/
           if((pSiS->VBFlags2 & VB2_SISLVDSBRIDGE))/*default LVDS used 18bits*/
            { pSiS->PRGB = 18;}
          else /*default DVI used 24bits*/
          {pSiS->PRGB = 24;}
          tmp = 0x37;
          if((pSiS->VGAEngine == SIS_315_VGA) &&
             (pSiS->ChipType < SIS_661)       &&
             (pSiS->ROM661New)                &&
             (!(pSiS->SiS_Pr->PanelSelfDetected))) {
             tmp = 0x35;
          }
           if(pSiS->PRGB == 18)      orSISIDXREG(SISCR, tmp, 0x01);
          else if(pSiS->PRGB == 24) andSISIDXREG(SISCR, tmp, 0xfe);
           #ifdef TWDEBUG
           xf86DrvMsg(0,X_INFO,"No Options force Panel RGB,Default Panel RGB is %d bits.\n",pSiS->PRGB);
           #endif
       }     
      inSISIDXREG(SISCR, 0x37, CR37);
       if(pSiS->ChipType < SIS_661) {
	  inSISIDXREG(SISCR, 0x3C, CR7D);
       } else {
	  inSISIDXREG(SISCR, 0x7D, CR7D);
       }
       if(pSiS->SiS_Pr->SiS_CustomT == CUT_BARCO1366) {
	  pSiS->VBLCDFlags |= VB_LCD_BARCO1366;
	  pSiS->LCDwidth = 1360;
	  pSiS->LCDheight = 1024;
	  if(CR37 & 0x10) pSiS->VBLCDFlags |= VB_LCD_EXPANDING;
	  xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		"Detected LCD panel (%dx%d, type %d, %sexpanding, RGB%d)\n",
		pSiS->LCDwidth, pSiS->LCDheight,
		((CR36 & 0xf0) >> 4),
		(CR37 & 0x10) ? "" : "non-",
		(CR37 & 0x01) ? 18 : 24);
       } else if(pSiS->SiS_Pr->SiS_CustomT == CUT_PANEL848) {
	  pSiS->VBLCDFlags |= VB_LCD_848x480;
	  pSiS->LCDwidth = pSiS->SiS_Pr->CP_MaxX = 848;
	  pSiS->LCDheight = pSiS->SiS_Pr->CP_MaxY = 480;
	  pSiS->VBLCDFlags |= VB_LCD_EXPANDING;
	  xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		"Assuming LCD/plasma panel (848x480, expanding, RGB24)\n");
       } else if(pSiS->SiS_Pr->SiS_CustomT == CUT_PANEL856) {
	  pSiS->VBLCDFlags |= VB_LCD_856x480;
	  pSiS->LCDwidth = pSiS->SiS_Pr->CP_MaxX = 856;
	  pSiS->LCDheight = pSiS->SiS_Pr->CP_MaxY = 480;
	  pSiS->VBLCDFlags |= VB_LCD_EXPANDING;
	  xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		"Assuming LCD/plasma panel (856x480, expanding, RGB24)\n");
       } else if(pSiS->FSTN) {
	  pSiS->VBLCDFlags |= VB_LCD_320x240;
	  pSiS->LCDwidth = pSiS->SiS_Pr->CP_MaxX = 320;
	  pSiS->LCDheight = pSiS->SiS_Pr->CP_MaxY = 240;
	  pSiS->VBLCDFlags &= ~VB_LCD_EXPANDING;
	  xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		"Assuming FSTN LCD panel (320x240, non-expanding)\n");
       } else {
	  if(CR36 == 0) {
	     /* Old 650/301LV and ECS A907 BIOS versions "forget" to set CR36, CR37 */
	     if(pSiS->VGAEngine == SIS_315_VGA) {
		if(pSiS->ChipType < SIS_661) {
		   xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		      "BIOS provided invalid panel size, probing...\n");
		   if(pSiS->VBFlags2 & VB2_LVDS) pSiS->SiS_Pr->SiS_IF_DEF_LVDS = 1;
		   else pSiS->SiS_Pr->SiS_IF_DEF_LVDS = 0;
		   SiS_GetPanelID(pSiS->SiS_Pr);
		   inSISIDXREG(SISCR, 0x36, CR36);
		   inSISIDXREG(SISCR, 0x37, CR37);
		} else {
		   xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		      "Broken BIOS, unable to determine panel size, disabling LCD\n");
		   pSiS->VBFlags &= ~CRT2_LCD;
		   return;
		}
	     } else if(pSiS->VGAEngine == SIS_300_VGA) {
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "BIOS provided invalid panel size, assuming 1024x768, RGB18\n");
		setSISIDXREG(SISCR,0x36,0xf0,0x02);
		setSISIDXREG(SISCR,0x37,0xee,0x01);
		CR36 = 0x02;
		inSISIDXREG(SISCR,0x37,CR37);
	     }
	  }
	  /*1366x768x60Hz,jump out VB_LCD_CUSTOM, we must clean current CR36. Ivans@090109*/
          if(pSiS->EnablePanel_1366x768 && (((CR36 & 0x0f) == 0x0f))){
	     CR36 &= 0xf0;
	  }
	  /*Ivans@090109*/
	  if((CR36 & 0x0f) == 0x0f) {
	     pSiS->VBLCDFlags |= VB_LCD_CUSTOM;
	     pSiS->LCDheight = pSiS->SiS_Pr->CP_MaxY;
	     pSiS->LCDwidth = pSiS->SiS_Pr->CP_MaxX;
	     if(CR37 & 0x10) pSiS->VBLCDFlags |= VB_LCD_EXPANDING;
	     xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		"Detected LCD/Plasma panel (max. X %d Y %d, pref. %dx%d, RGB%d)\n",
		pSiS->SiS_Pr->CP_MaxX, pSiS->SiS_Pr->CP_MaxY,
		pSiS->SiS_Pr->CP_PreferredX, pSiS->SiS_Pr->CP_PreferredY,
		(CR37 & 0x01) ? 18 : 24);
	  } else {
	     if(pSiS->VGAEngine == SIS_300_VGA) {
		pSiS->VBLCDFlags |= SiS300_LCD_Type[(CR36 & 0x0f)].VBLCD_lcdflag;
		pSiS->LCDheight = SiS300_LCD_Type[(CR36 & 0x0f)].LCDheight;
		pSiS->LCDwidth = SiS300_LCD_Type[(CR36 & 0x0f)].LCDwidth;
		if(CR37 & 0x10) pSiS->VBLCDFlags |= VB_LCD_EXPANDING;
	     } else if((pSiS->ChipType >= SIS_661) || (pSiS->ROM661New)) {
	     /*1366x768x60hz. can't refer from CR36. Ivans@090109*/
		 if(pSiS->EnablePanel_1366x768){
                    pSiS->VBLCDFlags |= SiS661_LCD_Type[(0x10)].VBLCD_lcdflag;
		    pSiS->LCDheight = SiS661_LCD_Type[(0x10)].LCDheight;
		    pSiS->LCDwidth = SiS661_LCD_Type[(0x10)].LCDwidth;
		    CR36 |= 0x0f;/*restore original CR36 value.*/   
		}/*Ivans@090109*/
            else{
		    pSiS->VBLCDFlags |= SiS661_LCD_Type[(CR36 & 0x0f)].VBLCD_lcdflag;
		    pSiS->LCDheight = SiS661_LCD_Type[(CR36 & 0x0f)].LCDheight;
		    pSiS->LCDwidth = SiS661_LCD_Type[(CR36 & 0x0f)].LCDwidth;
            	}
		if(CR37 & 0x10) pSiS->VBLCDFlags |= VB_LCD_EXPANDING;
		if(pSiS->ChipType < SIS_661) {
		   if(!(pSiS->SiS_Pr->PanelSelfDetected)) {
		      inSISIDXREG(SISCR,0x35,tmp);
		      CR37 &= 0xfc;
		      CR37 |= (tmp & 0x01);
		   }
		}
	     } else {
		pSiS->VBLCDFlags |= SiS315_LCD_Type[(CR36 & 0x0f)].VBLCD_lcdflag;
		pSiS->LCDheight = SiS315_LCD_Type[(CR36 & 0x0f)].LCDheight;
		pSiS->LCDwidth = SiS315_LCD_Type[(CR36 & 0x0f)].LCDwidth;
		if(CR37 & 0x10) pSiS->VBLCDFlags |= VB_LCD_EXPANDING;
	     }
	     xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
			"Detected LCD/plasma panel (%dx%d, %d, %sexp., RGB%d [%02x%02x%02x])\n",
			pSiS->LCDwidth, pSiS->LCDheight,
			((pSiS->VGAEngine == SIS_315_VGA) && (!pSiS->ROM661New)) ?
			 	((CR36 & 0x0f) - 1) : ((CR36 & 0xf0) >> 4),
			(CR37 & 0x10) ? "" : "non-",
			(CR37 & 0x01) ? 18 : 24,
			CR36, CR37, CR7D);
	  }
       }
    }
#ifdef TWDEBUG
	xf86DrvMsg(pScrn->scrnIndex,X_PROBED, "Detected CRT2 : %s\n",
	(SiS_GetReg(SISCR, 0x7E) &0x01) ? "LVDS" : "TMDS");
#endif 
}

void SiSSetupPseudoPanel(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
    int i;

    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
	"No LCD detected, but forced to enable digital output\n");
    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
    	"Will not be able to properly filter display modes!\n");

    pSiS->VBFlags |= CRT2_LCD;
    pSiS->SiS_Pr->SiS_CustomT = CUT_UNKNOWNLCD;
    pSiS->SiS_Pr->CP_PrefClock = 0;
    pSiS->SiS_Pr->CP_PreferredIndex = -1;
    pSiS->VBLCDFlags |= (VB_LCD_UNKNOWN | VB_LCD_EXPANDING);
    pSiS->LCDwidth = pSiS->SiS_Pr->CP_MaxX = 2048;
    pSiS->LCDheight = pSiS->SiS_Pr->CP_MaxY = 2048;
    for(i=0; i<7; i++) pSiS->SiS_Pr->CP_DataValid[i] = FALSE;
    pSiS->SiS_Pr->CP_HaveCustomData = FALSE;
    pSiS->SiS_Pr->PanelSelfDetected = TRUE;
// PCF?
//  outSISIDXREG(SISCR,0x36,0x0f);
   
    setSISIDXREG(SISCR,0x37,0x0e,0x10);
    orSISIDXREG(SISCR,0x32,0x08);
}

/* Detect CRT2-TV connector type and PAL/NTSC flag */
void SISTVPreInit(ScrnInfoPtr pScrn, Bool quiet)
{
    SISPtr pSiS = SISPTR(pScrn);
    UChar SR16, SR38, CR32, CR35=0, CR38=0, CR79, CR39;
    int temp = 0;

    if(!(pSiS->VBFlags2 & VB2_VIDEOBRIDGE)) return;

    inSISIDXREG(SISCR, 0x32, CR32);
    inSISIDXREG(SISCR, 0x35, CR35);
    inSISIDXREG(SISSR, 0x16, SR16);
    inSISIDXREG(SISSR, 0x38, SR38);

    switch(pSiS->VGAEngine) {
    case SIS_300_VGA:
       if(pSiS->Chipset == PCI_CHIP_SIS630) temp = 0x35;
       break;
    case SIS_315_VGA:
       temp = 0x38;
       break;
    }
    if(temp) {
       inSISIDXREG(SISCR, temp, CR38);
    }

#ifdef TWDEBUG
    xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
    	"(vb.c: SISTVPreInit 1: CR32=%02x SR16=%02x SR38=%02x VBFlags 0x%x)\n",
	CR32, SR16, SR38, pSiS->VBFlags);
#endif

    if(pSiS->ChipType>=SIS_761){
        if(CR32 & 0x07) pSiS->VBFlags |= CRT2_TV;
    }else{
        if(CR32 & 0x47) pSiS->VBFlags |= CRT2_TV;
    }
	
    if(pSiS->SiS_SD_Flags & SiS_SD_SUPPORTYPBPR) {
       if(CR32 & 0x80) pSiS->VBFlags |= CRT2_TV;
    } else {
       CR32 &= 0x7f;
    }

    if(CR32 & 0x01)
       pSiS->VBFlags |= TV_AVIDEO;
    else if(CR32 & 0x02)
       pSiS->VBFlags |= TV_SVIDEO;
    else if(CR32 & 0x04)
       pSiS->VBFlags |= TV_SCART;
    else if((CR32 & 0x40) && (pSiS->SiS_SD_Flags & SiS_SD_SUPPORTHIVISION))
       pSiS->VBFlags |= (TV_HIVISION | TV_PAL);
    else if((CR32 & 0x80) && (pSiS->SiS_SD_Flags & SiS_SD_SUPPORTYPBPR)) {
       pSiS->VBFlags |= TV_YPBPR;
       if(pSiS->NewCRLayout) {
          if(CR38 & 0x04) {
             switch(CR35 & 0xE0) {
             case 0x20: pSiS->VBFlags |= TV_YPBPR525P; break;
	     case 0x40: pSiS->VBFlags |= TV_YPBPR750P; break;
	     case 0x60: pSiS->VBFlags |= TV_YPBPR1080I; break;
	     default:   pSiS->VBFlags |= TV_YPBPR525I;
	     }
          } else        pSiS->VBFlags |= TV_YPBPR525I;		  
          inSISIDXREG(SISCR,0x39,CR39);
	  CR39 &= 0x03;
	  if(CR39 == 0x00)      pSiS->VBFlags |= TV_YPBPR43LB;
	  else if(CR39 == 0x01) pSiS->VBFlags |= TV_YPBPR43;
	  else if(CR39 == 0x02) pSiS->VBFlags |= TV_YPBPR169;
	  else			pSiS->VBFlags |= TV_YPBPR43;
       } else if(pSiS->SiS_SD_Flags & SiS_SD_SUPPORTYPBPR) {
          if(CR38 & 0x08) {
	     switch(CR38 & 0x30) {
	     case 0x10: pSiS->VBFlags |= TV_YPBPR525P; break;
	     case 0x20: pSiS->VBFlags |= TV_YPBPR750P; break;
	     case 0x30: pSiS->VBFlags |= TV_YPBPR1080I; break;
	     default:   pSiS->VBFlags |= TV_YPBPR525I;
	     }
	  } else        pSiS->VBFlags |= TV_YPBPR525I;
	  if(pSiS->SiS_SD_Flags & SiS_SD_SUPPORTYPBPRAR) {
             inSISIDXREG(SISCR,0x3B,CR39);
	     CR39 &= 0x03;
	     if(CR39 == 0x00)      pSiS->VBFlags |= TV_YPBPR43LB;
	     else if(CR39 == 0x01) pSiS->VBFlags |= TV_YPBPR169;
	     else if(CR39 == 0x03) pSiS->VBFlags |= TV_YPBPR43;
	  }
       }
    } else if((CR38 & 0x04) && (pSiS->VBFlags2 & VB2_CHRONTEL))
       pSiS->VBFlags |= (TV_CHSCART | TV_PAL);
    else if((CR38 & 0x08) && (pSiS->VBFlags2 & VB2_CHRONTEL))
       pSiS->VBFlags |= (TV_CHYPBPR525I | TV_NTSC);

    if(pSiS->VBFlags & (TV_SCART | TV_SVIDEO | TV_AVIDEO)) {
       if(pSiS->VGAEngine == SIS_300_VGA) {
	  /* Should be SR38, but this does not work. */
	  if(SR16 & 0x20)
	     pSiS->VBFlags |= TV_PAL;
          else
	     pSiS->VBFlags |= TV_NTSC;
       } else if(pSiS->Chipset == PCI_CHIP_SIS550) {
          inSISIDXREG(SISCR, 0x7a, CR79);
	  if(CR79 & 0x08) {
             inSISIDXREG(SISCR, 0x79, CR79);
	     CR79 >>= 5;
	  }
	  if(CR79 & 0x01) {
             pSiS->VBFlags |= TV_PAL;
	     if(CR38 & 0x40)      pSiS->VBFlags |= TV_PALM;
	     else if(CR38 & 0x80) pSiS->VBFlags |= TV_PALN;
 	  } else
	     pSiS->VBFlags |= TV_NTSC;
       } else if(pSiS->Chipset == PCI_CHIP_SIS650) {
	  inSISIDXREG(SISCR, 0x79, CR79);
	  if(CR79 & 0x20) {
             pSiS->VBFlags |= TV_PAL;
	     if(CR38 & 0x40)      pSiS->VBFlags |= TV_PALM;
	     else if(CR38 & 0x80) pSiS->VBFlags |= TV_PALN;
 	  } else
	     pSiS->VBFlags |= TV_NTSC;
       } else if(pSiS->NewCRLayout) {
          if(pSiS->ChipType<SIS_661){
              if(SR38 & 0x01) {
	          pSiS->VBFlags |= TV_PAL;
	      } else {
	          pSiS->VBFlags |= TV_NTSC;
	      }
	  }else{
              if(CR35 & 0x01) {
	          pSiS->VBFlags |= TV_PAL;
	      } else {
	          pSiS->VBFlags |= TV_NTSC;
	      }
	  }
	  if(pSiS->VBFlags & TV_PAL){
	      if(CR35 & 0x04)      pSiS->VBFlags |= TV_PALM;
	      else if(CR35 & 0x08) pSiS->VBFlags |= TV_PALN;
 	  }else if(pSiS->VBFlags & TV_NTSC){
	      if(CR35 & 0x02)      pSiS->VBFlags |= TV_NTSCJ;
	  }
       } else {	/* 315, 330 */
	  if(SR38 & 0x01) {
             pSiS->VBFlags |= TV_PAL;
	     if(CR38 & 0x40)      pSiS->VBFlags |= TV_PALM;
	     else if(CR38 & 0x80) pSiS->VBFlags |= TV_PALN;
 	  } else
	     pSiS->VBFlags |= TV_NTSC;
       }
    }

#ifdef TWDEBUG
    xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
    	"(vb.c: SISTVPreInit 2: VBFlags 0x%x)\n", pSiS->VBFlags);
#endif

    if((pSiS->VBFlags & (TV_SCART|TV_SVIDEO|TV_AVIDEO)) && !quiet) {
       xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "Detected default TV standard %s\n",
          (pSiS->VBFlags & TV_NTSC) ?
	     ((pSiS->VBFlags & TV_NTSCJ) ? "NTSCJ" : "NTSC") :
	         ((pSiS->VBFlags & TV_PALM) ? "PALM" :
		     ((pSiS->VBFlags & TV_PALN) ? "PALN" : "PAL")));
    }

    if((pSiS->VBFlags & TV_HIVISION) && !quiet) {
       xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "BIOS reports HiVision TV\n");
    }

    if((pSiS->VBFlags2 & VB2_CHRONTEL) && (pSiS->VBFlags & (TV_CHSCART|TV_CHYPBPR525I)) && !quiet) {
       xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "Chrontel: %s forced\n",
       	(pSiS->VBFlags & TV_CHSCART) ? "SCART (PAL)" : "YPbPr (480i)");
    }

    if((pSiS->VBFlags & TV_YPBPR) && !quiet) {
       xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "Detected YPbPr TV (by default %s)\n",
         (pSiS->VBFlags & TV_YPBPR525I) ? "480i" :
	     ((pSiS->VBFlags & TV_YPBPR525P) ? "480p" :
	        ((pSiS->VBFlags & TV_YPBPR750P) ? "720p" : "1080i")));
    }

/* K.T disable vertical blank end bit 9 and bit 10*/
	if((pSiS->VBFlags & (TV_AVIDEO|TV_SVIDEO)) && (pScrn->bitsPerPixel ==8 ))
	{
		inSISIDXREG(SISSR, 0x1C, temp);
		temp &= ~0x40;
		outSISIDXREG(SISSR, 0x1C, temp);
	}
}

/* Detect CRT2-VGA */
void SISCRT2PreInit(ScrnInfoPtr pScrn, Bool quiet)
{
    SISPtr pSiS = SISPTR(pScrn);
    UChar CR32;

    /* CRT2-VGA only supported on these bridges */
    if(!(pSiS->VBFlags2 & VB2_SISVGA2BRIDGE))
       return;

    inSISIDXREG(SISCR, 0x32, CR32);

    if(CR32 & 0x10) pSiS->VBFlags |= CRT2_VGA;

    /* See the comment in initextx.c/SiS_SenseVGA2DDC() */
    if(pSiS->SiS_Pr->DDCPortMixup) return;

#ifdef SISDUALHEAD
    if((!pSiS->DualHeadMode) || (!pSiS->SecondHead)) {
#endif

       if(pSiS->forcecrt2redetection) {
          pSiS->VBFlags &= ~CRT2_VGA;
       }

       /* We don't trust the normal sensing method for VGA2 since
        * it is performed by the BIOS during POST, and it is
        * impossible to sense VGA2 if the bridge is disabled.
        * Therefore, we try sensing VGA2 by DDC as well (if not
        * detected otherwise and only if there is no LCD panel
        * which is prone to be misdetected as a secondary VGA)
        */
       if(!(pSiS->nocrt2ddcdetection)) {
          if(!(pSiS->VBFlags & (CRT2_VGA | CRT2_LCD))) {
	     xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	         "%s secondary VGA, sensing via DDC\n",
	         pSiS->forcecrt2redetection ?
		      "Forced re-detection of" : "BIOS detected no");
             if(SiS_SenseVGA2DDC(pSiS->SiS_Pr, pSiS)) {
    	        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
	              "DDC error during secondary VGA detection\n");
	     } else {
	        inSISIDXREG(SISCR, 0x32, CR32);
	        if(CR32 & 0x10) {
	           pSiS->VBFlags |= CRT2_VGA;
	           pSiS->postVBCR32 |= 0x10;
		   xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		         "Detected secondary VGA connection\n");
	        } else {
	           xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		         "No secondary VGA connection detected\n");
	        }
	     }
          }
       }
#ifdef SISDUALHEAD
    }
#endif
}

static int
SISDoSense(ScrnInfoPtr pScrn, UShort type, UShort test)
{
    SISPtr pSiS = SISPTR(pScrn);
    int    temp, mytest, result, i, j;
#ifdef TWDEBUG
    xf86DrvMsg(0, X_INFO, "Sense: %x %x\n", type, test);
#endif

    for(j = 0; j < 10; j++) {
       result = 0;
       for(i = 0; i < 3; i++) {
          mytest = test;
          outSISIDXREG(SISPART4, 0x11, (type & 0x00ff));
          temp = (type >> 8) | (mytest & 0x00ff);
          setSISIDXREG(SISPART4, 0x10, 0xe0,temp);
          SiS_DDC2Delay(pSiS->SiS_Pr, 0x1500);
          mytest >>= 8;
          mytest &= 0x7f;
          inSISIDXREG(SISPART4, 0x03, temp);
          temp ^= 0x0e;
          temp &= mytest;
          if(temp == mytest) result++;
#if 1
	  outSISIDXREG(SISPART4, 0x11, 0x00);
	  andSISIDXREG(SISPART4, 0x10, 0xe0);
	  SiS_DDC2Delay(pSiS->SiS_Pr, 0x1000);
#endif
       }
       if((result == 0) || (result >= 2)) break;
    }
    return result;
}

#define GETROMWORD(w) (pSiS->BIOS[w] | (pSiS->BIOS[w+1] << 8))

/* Sense connected devices on 30x */
void
SISSense30x(ScrnInfoPtr pScrn, Bool quiet)
{
    SISPtr pSiS = SISPTR(pScrn);
    UChar  backupP4_0d, backupP2_00, backupP2_4d, backupSR_1e, biosflag=0;
    UShort svhs=0, svhs_c=0;
    UShort cvbs=0, cvbs_c=0;
    UShort vga2=0, vga2_c=0;
    int    myflag, result; /* , i; */
    ULong  offset;
    if(!(pSiS->VBFlags2 & VB2_SISBRIDGE)) return;

#ifdef TWDEBUG
    inSISIDXREG(SISCR, 0x32, backupP2_4d);
    xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
    	"(vb.c: SISSense30c 1: CR32=%02x, VBFlags 0x%x)\n", backupP2_4d, pSiS->VBFlags);
#endif

    if(pSiS->VBFlags2 & VB2_301) {
       svhs = 0x00b9; cvbs = 0x00b3; vga2 = 0x00d1;
       inSISIDXREG(SISPART4, 0x01, myflag);
       if(myflag & 0x04) {
	  svhs = 0x00dd; cvbs = 0x00ee; vga2 = 0x00fd;
       }
    } else if(pSiS->VBFlags2 & (VB2_301B | VB2_302B)) {
       svhs = 0x016b; cvbs = 0x0174; vga2 = 0x0190;
    } else if(pSiS->VBFlags2 & (VB2_301LV | VB2_302LV)) {
       svhs = 0x0200; cvbs = 0x0100;
    } else if(pSiS->VBFlags2 & (VB2_301C | VB2_302ELV | VB2_307T | VB2_307LV)) {
       svhs = 0x016b; cvbs = 0x0110; vga2 = 0x0190;
    } else return;


    vga2_c = 0x0e08; svhs_c = 0x0404; cvbs_c = 0x0804;
    if(pSiS->VBFlags2 & (VB2_301LV|VB2_302LV|VB2_302ELV|VB2_307LV)) {
       svhs_c = 0x0408; cvbs_c = 0x0808;
    }
    biosflag = 2;

    if(pSiS->Chipset == PCI_CHIP_SIS300) {
       inSISIDXREG(SISSR, 0x3b, myflag);
       if(!(myflag & 0x01)) vga2 = vga2_c = 0;
    }


    if(pSiS->SiS_Pr->UseROM) {
       if(pSiS->VGAEngine == SIS_300_VGA) {
	  if(pSiS->VBFlags2 & VB2_301) {
	     inSISIDXREG(SISPART4, 0x01, myflag);
	     if(!(myflag & 0x04)) {
		vga2 = GETROMWORD(0xf8); svhs = GETROMWORD(0xfa); cvbs = GETROMWORD(0xfc);
	     }
	  }
	  biosflag = pSiS->BIOS[0xfe];
       } else if((pSiS->Chipset == PCI_CHIP_SIS660) ||
	         (pSiS->Chipset == PCI_CHIP_SIS340) ||
	         (pSiS->Chipset == PCI_CHIP_SIS670) ||
	         (pSiS->Chipset == PCI_CHIP_SIS671)) {
	  if(pSiS->ROM661New) {
	     biosflag = 2;
             if(pSiS->ChipType>=SIS_761){
                 offset = GETROMWORD(0x92);
                 vga2 = GETROMWORD(offset);
             }else{
	         vga2 = GETROMWORD(0x63);
             }
	     if(pSiS->BIOS[0x6f] & 0x01) {
	        if(pSiS->VBFlags2 & VB2_SISUMC) vga2 = GETROMWORD(0x4d);
	     }
             if(pSiS->ChipType>=SIS_761){
                 svhs = cvbs = GETROMWORD(offset);
             }else{
	         svhs = cvbs = GETROMWORD(0x65);
             }
	     if(pSiS->BIOS[0x5d] & 0x04) biosflag |= 0x01;
	  }
       }
       /* No "else", some BIOSes carry wrong data */
    }

    if(pSiS->ChipType >= XGI_20) {
       if(pSiS->HaveXGIBIOS) {
          biosflag = pSiS->BIOS[0x58] & 0x03;
       } else {
          /* These boards have a s-video connector, but its
	   * pins are routed both the bridge's composite and
	   * svideo pins. This is for using the S-video plug
	   * for YPbPr output. Anyway, since a svideo connected
	   * TV would also been detected as a composite connected
	   * one, we don't check for composite if svideo is
	   * detected.
	   */
	   biosflag &= ~0x02;
       }
    }

    if(!(pSiS->VBFlags2 & VB2_SISVGA2BRIDGE)) {
       vga2 = vga2_c = 0;
    }

    inSISIDXREG(SISSR, 0x1e, backupSR_1e);
    orSISIDXREG(SISSR, 0x1e, 0x20);

    inSISIDXREG(SISPART4, 0x0d, backupP4_0d);
    if(pSiS->VBFlags2 & VB2_30xCLV) {
       setSISIDXREG(SISPART4, 0x0d, ~0x07, 0x01);
    } else if(pSiS->VBFlags2 & VB2_301) {
       setSISIDXREG(SISPART4, 0x0d, ~0x07, 0x05);
    } else {
       orSISIDXREG(SISPART4, 0x0d, 0x04);
    }
    SiS_DDC2Delay(pSiS->SiS_Pr, 0x2000);

    inSISIDXREG(SISPART2, 0x00, backupP2_00);
    outSISIDXREG(SISPART2, 0x00, ((backupP2_00 | 0x3c) & 0xfc));

    inSISIDXREG(SISPART2, 0x4d, backupP2_4d);
    if(pSiS->VBFlags2 & VB2_SISYPBPRBRIDGE) {
       outSISIDXREG(SISPART2, 0x4d, (backupP2_4d & ~0x10));
    }

    if(!(pSiS->VBFlags2 & VB2_30xCLV)) {
       SISDoSense(pScrn, 0, 0);
    }
	
    if(pSiS->VBFlags2 != VB2_307LV||pSiS->VBFlags2 != VB2_307T){
        andSISIDXREG(SISCR, 0x32, ~0x14);
        pSiS->postVBCR32 &= ~0x14;
        if(vga2_c || vga2) {
            if(SISDoSense(pScrn, vga2, vga2_c)) {
                if(biosflag & 0x01) {
                    if(!quiet) {
	                xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		        "SiS30x: Detected TV connected to SCART output\n");
	            }
	            pSiS->VBFlags |= TV_SCART;
	            orSISIDXREG(SISCR, 0x32, 0x04);
	            pSiS->postVBCR32 |= 0x04;
	        } else {
	            if(!quiet) {
	                xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		        "SiS30x: Detected secondary VGA connection\n");
	            }
	            pSiS->VBFlags |= VGA2_CONNECTED;
	            orSISIDXREG(SISCR, 0x32, 0x10);
	            pSiS->postVBCR32 |= 0x10;
	        }
	     }
        if(biosflag & 0x01) pSiS->SiS_SD_Flags |= SiS_SD_VBHASSCART;
        }
    }
    andSISIDXREG(SISCR, 0x32, 0x3f);
    pSiS->postVBCR32 &= 0x3f;

    if(pSiS->VBFlags2 & VB2_30xCLV) {
       orSISIDXREG(SISPART4, 0x0d, 0x04);
    }
	

    if((pSiS->VGAEngine == SIS_315_VGA) && (pSiS->VBFlags2 & VB2_SISYPBPRBRIDGE)) {
       if(pSiS->SenseYPbPr) {
	  outSISIDXREG(SISPART2, 0x4d, (backupP2_4d | 0x10));
	  SiS_DDC2Delay(pSiS->SiS_Pr, 0x2000);
	  /* New BIOS (2.x) uses vga2 sensing here for all bridges >301LV */
	  if((result = SISDoSense(pScrn, svhs, 0x0604))) {
	     if((result = SISDoSense(pScrn, cvbs, 0x0804))) {
		if(!quiet) {
		   xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
			"SiS30x: Detected TV connected to YPbPr component output\n");
		}
		orSISIDXREG(SISCR, 0x32, 0x80);
		pSiS->VBFlags |= TV_YPBPR;
		pSiS->postVBCR32 |= 0x80;
	     }
	  }
	  outSISIDXREG(SISPART2, 0x4d, backupP2_4d);
       }
    }

    andSISIDXREG(SISCR, 0x32, ~0x03);
    pSiS->postVBCR32 &= ~0x03;

    if(!(pSiS->VBFlags & TV_YPBPR)) {
       if((result = SISDoSense(pScrn, svhs, svhs_c))) {
	  if(!quiet) {
	     xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		  "SiS30x: Detected TV connected to SVIDEO output\n");
	  }
	  pSiS->VBFlags |= TV_SVIDEO;
	  orSISIDXREG(SISCR, 0x32, 0x02);
	  pSiS->postVBCR32 |= 0x02;
       }

       if((biosflag & 0x02) || (!result)) {
	  if(SISDoSense(pScrn, cvbs, cvbs_c)) {
	     if(!quiet) {
	        xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
	             "SiS30x: Detected TV connected to COMPOSITE output\n");
	     }
	     pSiS->VBFlags |= TV_AVIDEO;
	     orSISIDXREG(SISCR, 0x32, 0x01);
	     pSiS->postVBCR32 |= 0x01;
	  }
       }
    }
    SISDoSense(pScrn, 0, 0);

    outSISIDXREG(SISPART2, 0x00, backupP2_00);
    outSISIDXREG(SISPART4, 0x0d, backupP4_0d);
    outSISIDXREG(SISSR, 0x1e, backupSR_1e);

    if(pSiS->VBFlags2 & VB2_30xCLV) {
       inSISIDXREG(SISPART2, 0x00, biosflag);
       if(biosflag & 0x20) {
          for(myflag = 2; myflag > 0; myflag--) {
	     biosflag ^= 0x20;
	     outSISIDXREG(SISPART2, 0x00, biosflag);
	  }
       }
    }

    outSISIDXREG(SISPART2, 0x00, backupP2_00);

#ifdef TWDEBUG
    inSISIDXREG(SISCR, 0x32, backupP2_4d);
    xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
    	"(vb.c: SISSense30c 2: CR32=0x%02x, VBFlags 0x%x)\n", backupP2_4d, pSiS->VBFlags);
#endif
}

void
SISSenseChrontel(ScrnInfoPtr pScrn, Bool quiet)
{
    SISPtr  pSiS = SISPTR(pScrn);
    int     temp1=0, temp2, i;
    UChar test[3];

    if(pSiS->SiS_Pr->SiS_IF_DEF_CH70xx == 1) {

       /* Chrontel 700x */

       /* Read power status */
       temp1 = SiS_GetCH700x(pSiS->SiS_Pr, 0x0e);  /* Power status */
       if((temp1 & 0x03) != 0x03) {
	  /* Power all outputs */
	  SiS_SetCH700x(pSiS->SiS_Pr, 0x0e,0x0b);
	  SiS_DDC2Delay(pSiS->SiS_Pr, 0x96);
       }
       /* Sense connected TV devices */
       for(i = 0; i < 3; i++) {
	  SiS_SetCH700x(pSiS->SiS_Pr, 0x10, 0x01);
	  SiS_DDC2Delay(pSiS->SiS_Pr, 0x96);
	  SiS_SetCH700x(pSiS->SiS_Pr, 0x10, 0x00);
	  SiS_DDC2Delay(pSiS->SiS_Pr, 0x96);
	  temp1 = SiS_GetCH700x(pSiS->SiS_Pr, 0x10);
	  if(!(temp1 & 0x08))       test[i] = 0x02;
	  else if(!(temp1 & 0x02))  test[i] = 0x01;
	  else                      test[i] = 0;
	  SiS_DDC2Delay(pSiS->SiS_Pr, 0x96);
       }

       if(test[0] == test[1])      temp1 = test[0];
       else if(test[0] == test[2]) temp1 = test[0];
       else if(test[1] == test[2]) temp1 = test[1];
       else {
	  xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
	        "Chrontel: TV detection unreliable - test results varied\n");
	  temp1 = test[2];
       }

    } else if(pSiS->SiS_Pr->SiS_IF_DEF_CH70xx == 2) {

       /* Chrontel 701x */

       /* Backup Power register */
       temp1 = SiS_GetCH701x(pSiS->SiS_Pr, 0x49);

       /* Enable TV path */
       SiS_SetCH701x(pSiS->SiS_Pr, 0x49, 0x20);

       SiS_DDC2Delay(pSiS->SiS_Pr, 0x96);

       /* Sense connected TV devices */
       temp2 = SiS_GetCH701x(pSiS->SiS_Pr, 0x20);
       temp2 |= 0x01;
       SiS_SetCH701x(pSiS->SiS_Pr, 0x20, temp2);

       SiS_DDC2Delay(pSiS->SiS_Pr, 0x96);

       temp2 ^= 0x01;
       SiS_SetCH701x(pSiS->SiS_Pr, 0x20, temp2);

       SiS_DDC2Delay(pSiS->SiS_Pr, 0x96);

       temp2 = SiS_GetCH701x(pSiS->SiS_Pr, 0x20);

       /* Restore Power register */
       SiS_SetCH701x(pSiS->SiS_Pr, 0x49, temp1);

       temp1 = 0;
       if(temp2 & 0x02) temp1 |= 0x01;
       if(temp2 & 0x10) temp1 |= 0x01;
       if(temp2 & 0x04) temp1 |= 0x02;

       if( (temp1 & 0x01) && (temp1 & 0x02) ) temp1 = 0x04;

    }

    switch(temp1) {
       case 0x01:
          xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
	       "Chrontel: Detected TV connected to COMPOSITE output\n");
	  pSiS->VBFlags |= TV_AVIDEO;
	  orSISIDXREG(SISCR, 0x32, 0x01);
   	  andSISIDXREG(SISCR, 0x32, ~0x06);
	  pSiS->postVBCR32 |= 0x01;
	  pSiS->postVBCR32 &= ~0x06;
          break;
       case 0x02:
	  xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
	       "Chrontel: Detected TV connected to SVIDEO output\n");
	  pSiS->VBFlags |= TV_SVIDEO;
	  orSISIDXREG(SISCR, 0x32, 0x02);
	  andSISIDXREG(SISCR, 0x32, ~0x05);
	  pSiS->postVBCR32 |= 0x02;
	  pSiS->postVBCR32 &= ~0x05;
          break;
       case 0x04:
	  xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
	       "Chrontel: Detected TV connected to SCART or YPBPR output\n");
  	  if(pSiS->chtvtype == -1) {
	     if(!quiet) {
	        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	            "Chrontel: Use CHTVType option to select either SCART or YPBPR525I\n");
	        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	            "Chrontel: Using SCART by default\n");
	     }
	     pSiS->chtvtype = 1;
	  }
	  if(pSiS->chtvtype)
	     pSiS->VBFlags |= TV_CHSCART;
	  else
	     pSiS->VBFlags |= TV_CHYPBPR525I;
          break;
       default:
	  xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
	       "Chrontel: No TV detected.\n");
	  andSISIDXREG(SISCR, 0x32, ~0x07);
	  pSiS->postVBCR32 &= ~0x07;
       }
}

/* Redetect CRT2 devices. Calling this requires a reset
 * of the current display mode if TRUE is returned.
 */
Bool SISRedetectCRT2Type(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
    ULong  VBFlagsBackup = pSiS->VBFlags;
    Bool   backup1 = pSiS->forcecrt2redetection;
    Bool   backup2 = pSiS->nocrt2ddcdetection;

#ifdef SISDUALHEAD
    if(pSiS->DualHeadMode) return FALSE;
#endif

    pSiS->VBFlags &= ~(CRT2_DEFAULT   |
		       CRT2_ENABLE    |
		       TV_STANDARD    |
		       TV_INTERFACE   |
		       TV_YPBPRALL    |
		       TV_YPBPRAR     |
		       TV_CHSCART     |
		       TV_CHYPBPR525I |
		       CRT1_LCDA      |
		       DISPTYPE_CRT1);

    /* At first, re-do the sensing for TV and VGA2 */
    if(pSiS->VBFlags2 & VB2_SISBRIDGE) {
       SISSense30x(pScrn, TRUE);
    } else if(pSiS->VBFlags2 & VB2_CHRONTEL) {
       SiS_SetChrontelGPIO(pSiS->SiS_Pr, 0x9c);
       SISSenseChrontel(pScrn, TRUE);
       SiS_SetChrontelGPIO(pSiS->SiS_Pr, 0x00);
    }

    SISTVPreInit(pScrn, TRUE);

    pSiS->forcecrt2redetection = TRUE;
    pSiS->nocrt2ddcdetection = FALSE;

    /* We only re-detect LCD for the TMDS-SiS-bridges. LVDS
     * is practically never being hot-plugged (and even if,
     * there is no way of detecting this).
     */
    if((pSiS->VGAEngine == SIS_315_VGA) &&
       (pSiS->VBFlags2 & VB2_SISTMDSBRIDGE) &&
       (!(pSiS->VBFlags2 & VB2_30xBDH)) &&
       (pSiS->VESA != 1) &&
       (pSiS->SiS_Pr->SiS_CustomT != CUT_UNKNOWNLCD)) {
       SISLCDPreInit(pScrn, TRUE);
    } else {
       pSiS->VBFlags |= (pSiS->detectedCRT2Devices & CRT2_LCD);
    }

    /* Secondary VGA is only supported on these bridges: */
    if(pSiS->VBFlags2 & VB2_SISVGA2BRIDGE) {
       SISCRT2PreInit(pScrn, TRUE);
    }

    pSiS->forcecrt2redetection = backup1;
    pSiS->nocrt2ddcdetection = backup2;

    pSiS->SiS_SD_Flags &= ~SiS_SD_SUPPORTLCDA;
    if(SISDetermineLCDACap(pScrn)) {
       pSiS->SiS_SD_Flags |= SiS_SD_SUPPORTLCDA;
    }
    SISSaveDetectedDevices(pScrn);

    pSiS->VBFlags = VBFlagsBackup;

    /* If LCD disappeared, don't use it and don't advertise LCDA support. Duh! */
    if(!(pSiS->detectedCRT2Devices & CRT2_LCD)) {
       pSiS->SiS_SD_Flags &= ~(SiS_SD_SUPPORTLCDA);
       if(pSiS->VBFlags & CRT2_LCD) {
          /* If CRT2 was LCD, disable CRT2 and adapt display mode flags */
          pSiS->VBFlags &= ~(CRT2_LCD | DISPLAY_MODE);
	  /* Switch on CRT1 as an emergency measure */
	  pSiS->VBFlags |= (SINGLE_MODE | DISPTYPE_CRT1);
	  pSiS->CRT1off = 0;
       }
       /* If CRT1 was LCD, switch to CRT1-VGA. No need to adapt display mode flags. */
       pSiS->VBFlags &= ~(CRT1_LCDA);
       pSiS->VBFlags_backup = pSiS->VBFlags;
    }

    pSiS->VBFlagsInit = pSiS->VBFlags;

    /* Save new detection result registers to write them back in EnterVT() */
    inSISIDXREG(SISCR, 0x32, pSiS->myCR32);
    inSISIDXREG(SISCR, 0x36, pSiS->myCR36);
    inSISIDXREG(SISCR, 0x37, pSiS->myCR37);

    return TRUE;
}


/* Functions for adjusting various TV settings */

/* These are used by the PostSetMode() functions as well as
 * the display properties tool SiSCtrl.
 *
 * There is each a Set and a Get routine. The Set functions
 * take a value of the same range as the corresponding option.
 * The Get routines return a value of the same range (although
 * not necessarily the same value as previously set because
 * of the lower resolution of the respective setting compared
 * to the valid range).
 * The Get routines return -2 on error (eg. hardware does not
 * support this setting).
 * Note: The x and y positioning routines accept a position
 * RELATIVE to the default position. All other routines
 * take ABSOLUTE values.
 *
 * The Set functions will store the property regardless if TV is
 * currently used or not and if the hardware supports the property
 * or not. The Get routines will return this stored
 * value if TV is not currently used (because the register does
 * not contain the correct value then) or if the hardware supports
 * the respective property. This should make it easier for the
 * display property tool because it does not have to know the
 * hardware features.
 *
 * All the routines are dual head aware. It does not matter
 * if the function is called from the CRT1 or CRT2 session.
 * The values will be in pSiSEnt anyway, and read from there
 * if we're running dual head.
 */

void SiS_SetCHTVlumabandwidthcvbs(ScrnInfoPtr pScrn, int val)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   pSiS->chtvlumabandwidthcvbs = val;
#ifdef SISDUALHEAD
   if(pSiSEnt) pSiSEnt->chtvlumabandwidthcvbs = val;
#endif

   if(!(pSiS->VBFlags & CRT2_TV)) return;
   if(!(pSiS->VBFlags2 & VB2_CHRONTEL)) return;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   switch(pSiS->ChrontelType) {
       case CHRONTEL_700x:
           val /= 8;
           if((val == 0) || (val == 1)) {
	      SiS_SetCH70xxANDOR(pSiS->SiS_Pr, 0x03, val, 0xFE);
           }
	   break;
       case CHRONTEL_701x:
           val /= 4;
	   if((val >= 0) && (val <= 3)) {
	       SiS_SetCH70xxANDOR(pSiS->SiS_Pr, 0x02, val, 0xFC);
	   }
           break;
   }
}

int SiS_GetCHTVlumabandwidthcvbs(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   if(!((pSiS->VBFlags2 & VB2_CHRONTEL) && (pSiS->VBFlags & CRT2_TV))) {
#ifdef SISDUALHEAD
      if(pSiSEnt && pSiS->DualHeadMode)
           return (int)pSiSEnt->chtvlumabandwidthcvbs;
      else
#endif
           return (int)pSiS->chtvlumabandwidthcvbs;
   } else {
#ifdef UNLOCK_ALWAYS
      sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif
      switch(pSiS->ChrontelType) {
      case CHRONTEL_700x:
           return (int)((SiS_GetCH70xx(pSiS->SiS_Pr, 0x03) & 0x01) * 8);
      case CHRONTEL_701x:
	   return (int)((SiS_GetCH70xx(pSiS->SiS_Pr, 0x02) & 0x03) * 4);
      default:
           return (int)pSiS->chtvlumabandwidthcvbs;
      }
   }
}

void SiS_SetCHTVlumabandwidthsvideo(ScrnInfoPtr pScrn, int val)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   pSiS->chtvlumabandwidthsvideo = val;
#ifdef SISDUALHEAD
   if(pSiSEnt) pSiSEnt->chtvlumabandwidthsvideo = val;
#endif

   if(!(pSiS->VBFlags & CRT2_TV)) return;
   if(!(pSiS->VBFlags2 & VB2_CHRONTEL)) return;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   switch(pSiS->ChrontelType) {
       case CHRONTEL_700x:
           val /= 6;
           if((val >= 0) && (val <= 2)) {
	      SiS_SetCH70xxANDOR(pSiS->SiS_Pr, 0x03, (val << 1), 0xF9);
           }
	   break;
       case CHRONTEL_701x:
           val /= 4;
	   if((val >= 0) && (val <= 3)) {
	      SiS_SetCH70xxANDOR(pSiS->SiS_Pr, 0x02, (val << 2), 0xF3);
	   }
           break;
   }
}

int SiS_GetCHTVlumabandwidthsvideo(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   if(!((pSiS->VBFlags2 & VB2_CHRONTEL) && (pSiS->VBFlags & CRT2_TV))) {
#ifdef SISDUALHEAD
      if(pSiSEnt && pSiS->DualHeadMode)
           return (int)pSiSEnt->chtvlumabandwidthsvideo;
      else
#endif
           return (int)pSiS->chtvlumabandwidthsvideo;
   } else {
#ifdef UNLOCK_ALWAYS
      sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif
      switch(pSiS->ChrontelType) {
      case CHRONTEL_700x:
           return (int)(((SiS_GetCH70xx(pSiS->SiS_Pr, 0x03) & 0x06) >> 1) * 6);
      case CHRONTEL_701x:
	   return (int)(((SiS_GetCH70xx(pSiS->SiS_Pr, 0x02) & 0x0c) >> 2) * 4);
      default:
           return (int)pSiS->chtvlumabandwidthsvideo;
      }
   }
}

void SiS_SetCHTVlumaflickerfilter(ScrnInfoPtr pScrn, int val)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   pSiS->chtvlumaflickerfilter = val;
#ifdef SISDUALHEAD
   if(pSiSEnt) pSiSEnt->chtvlumaflickerfilter = val;
#endif

   if(!(pSiS->VBFlags & CRT2_TV)) return;
   if(!(pSiS->VBFlags2 & VB2_CHRONTEL)) return;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   switch(pSiS->ChrontelType) {
       case CHRONTEL_700x:
           val /= 6;
           if((val >= 0) && (val <= 2)) {
	      UShort reg = 0;
	      reg = SiS_GetCH70xx(pSiS->SiS_Pr, 0x01);
	      reg = (reg & 0xf0) | ((reg & 0x0c) >> 2) | (val << 2);
              SiS_SetCH70xx(pSiS->SiS_Pr, 0x01, reg);
           }
	   break;
       case CHRONTEL_701x:
           val /= 4;
	   if((val >= 0) && (val <= 3)) {
	      SiS_SetCH70xxANDOR(pSiS->SiS_Pr, 0x01, (val << 2), 0xF3);
	   }
           break;
   }
}

int SiS_GetCHTVlumaflickerfilter(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   if(!((pSiS->VBFlags2 & VB2_CHRONTEL) && (pSiS->VBFlags & CRT2_TV))) {
#ifdef SISDUALHEAD
      if(pSiSEnt && pSiS->DualHeadMode)
          return (int)pSiSEnt->chtvlumaflickerfilter;
      else
#endif
          return (int)pSiS->chtvlumaflickerfilter;
   } else {
#ifdef UNLOCK_ALWAYS
      sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif
      switch(pSiS->ChrontelType) {
      case CHRONTEL_700x:
           return (int)((SiS_GetCH70xx(pSiS->SiS_Pr, 0x01) & 0x03) * 6);
      case CHRONTEL_701x:
	   return (int)(((SiS_GetCH70xx(pSiS->SiS_Pr, 0x01) & 0x0c) >> 2) * 4);
      default:
           return (int)pSiS->chtvlumaflickerfilter;
      }
   }
}

void SiS_SetCHTVchromabandwidth(ScrnInfoPtr pScrn, int val)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   pSiS->chtvchromabandwidth = val;
#ifdef SISDUALHEAD
   if(pSiSEnt) pSiSEnt->chtvchromabandwidth = val;
#endif

   if(!(pSiS->VBFlags & CRT2_TV)) return;
   if(!(pSiS->VBFlags2 & VB2_CHRONTEL)) return;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   switch(pSiS->ChrontelType) {
       case CHRONTEL_700x:
           val /= 4;
           if((val >= 0) && (val <= 3)) {
              SiS_SetCH70xxANDOR(pSiS->SiS_Pr, 0x03, (val << 4), 0xCF);
           }
	   break;
       case CHRONTEL_701x:
           val /= 8;
	   if((val >= 0) && (val <= 1)) {
	      SiS_SetCH70xxANDOR(pSiS->SiS_Pr, 0x02, (val << 4), 0xEF);
	   }
           break;
   }
}

int SiS_GetCHTVchromabandwidth(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   if(!((pSiS->VBFlags2 & VB2_CHRONTEL) && (pSiS->VBFlags & CRT2_TV))) {
#ifdef SISDUALHEAD
      if(pSiSEnt && pSiS->DualHeadMode)
           return (int)pSiSEnt->chtvchromabandwidth;
      else
#endif
           return (int)pSiS->chtvchromabandwidth;
   } else {
#ifdef UNLOCK_ALWAYS
      sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif
      switch(pSiS->ChrontelType) {
      case CHRONTEL_700x:
           return (int)(((SiS_GetCH70xx(pSiS->SiS_Pr, 0x03) & 0x30) >> 4) * 4);
      case CHRONTEL_701x:
	   return (int)(((SiS_GetCH70xx(pSiS->SiS_Pr, 0x02) & 0x10) >> 4) * 8);
      default:
           return (int)pSiS->chtvchromabandwidth;
      }
   }
}

void SiS_SetCHTVchromaflickerfilter(ScrnInfoPtr pScrn, int val)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   pSiS->chtvchromaflickerfilter = val;
#ifdef SISDUALHEAD
   if(pSiSEnt) pSiSEnt->chtvchromaflickerfilter = val;
#endif

   if(!(pSiS->VBFlags & CRT2_TV)) return;
   if(!(pSiS->VBFlags2 & VB2_CHRONTEL)) return;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   switch(pSiS->ChrontelType) {
       case CHRONTEL_700x:
           val /= 6;
           if((val >= 0) && (val <= 2)) {
	      UShort reg = 0;
	      reg = SiS_GetCH70xx(pSiS->SiS_Pr, 0x01);
	      reg = (reg & 0xc0) | ((reg & 0x0c) >> 2) | ((reg & 0x03) << 2) | (val << 4);
              SiS_SetCH70xx(pSiS->SiS_Pr, 0x01, reg);
           }
	   break;
       case CHRONTEL_701x:
           val /= 4;
	   if((val >= 0) && (val <= 3)) {
	      SiS_SetCH70xxANDOR(pSiS->SiS_Pr, 0x01, (val << 4), 0xCF);
	   }
           break;
   }
}

int SiS_GetCHTVchromaflickerfilter(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   if(!((pSiS->VBFlags2 & VB2_CHRONTEL) && (pSiS->VBFlags & CRT2_TV))) {
#ifdef SISDUALHEAD
      if(pSiSEnt && pSiS->DualHeadMode)
           return (int)pSiSEnt->chtvchromaflickerfilter;
      else
#endif
           return (int)pSiS->chtvchromaflickerfilter;
   } else {
#ifdef UNLOCK_ALWAYS
      sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif
      switch(pSiS->ChrontelType) {
      case CHRONTEL_700x:
           return (int)(((SiS_GetCH70xx(pSiS->SiS_Pr, 0x01) & 0x30) >> 4) * 6);
      case CHRONTEL_701x:
	   return (int)(((SiS_GetCH70xx(pSiS->SiS_Pr, 0x01) & 0x30) >> 4) * 4);
      default:
           return (int)pSiS->chtvchromaflickerfilter;
      }
   }
}

void SiS_SetCHTVcvbscolor(ScrnInfoPtr pScrn, int val)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   pSiS->chtvcvbscolor = val ? 1 : 0;
#ifdef SISDUALHEAD
   if(pSiSEnt) pSiSEnt->chtvcvbscolor = pSiS->chtvcvbscolor;
#endif

   if(!(pSiS->VBFlags & CRT2_TV)) return;
   if(!(pSiS->VBFlags2 & VB2_CHRONTEL)) return;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   switch(pSiS->ChrontelType) {
       case CHRONTEL_700x:
           if(!val)  SiS_SetCH70xxANDOR(pSiS->SiS_Pr, 0x03, 0x40, 0x00);
           else      SiS_SetCH70xxANDOR(pSiS->SiS_Pr, 0x03, 0x00, ~0x40);
	   break;
       case CHRONTEL_701x:
           if(!val)  SiS_SetCH70xxANDOR(pSiS->SiS_Pr, 0x02, 0x00, ~0x20);
	   else      SiS_SetCH70xxANDOR(pSiS->SiS_Pr, 0x02, 0x20, 0x00);
           break;
   }
}

int SiS_GetCHTVcvbscolor(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   if(!((pSiS->VBFlags2 & VB2_CHRONTEL) && (pSiS->VBFlags & CRT2_TV))) {
#ifdef SISDUALHEAD
      if(pSiSEnt && pSiS->DualHeadMode)
           return (int)pSiSEnt->chtvcvbscolor;
      else
#endif
           return (int)pSiS->chtvcvbscolor;
   } else {
#ifdef UNLOCK_ALWAYS
      sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif
      switch(pSiS->ChrontelType) {
      case CHRONTEL_700x:
           return (int)(((SiS_GetCH70xx(pSiS->SiS_Pr, 0x03) & 0x40) >> 6) ^ 0x01);
      case CHRONTEL_701x:
	   return (int)(((SiS_GetCH70xx(pSiS->SiS_Pr, 0x02) & 0x20) >> 5) ^ 0x01);
      default:
           return (int)pSiS->chtvcvbscolor;
      }
   }
}

void SiS_SetCHTVtextenhance(ScrnInfoPtr pScrn, int val)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   pSiS->chtvtextenhance = val;
#ifdef SISDUALHEAD
   if(pSiSEnt) pSiSEnt->chtvtextenhance = val;
#endif

   if(!(pSiS->VBFlags & CRT2_TV)) return;
   if(!(pSiS->VBFlags2 & VB2_CHRONTEL)) return;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   switch(pSiS->ChrontelType) {
       case CHRONTEL_700x:
           val /= 6;
           if((val >= 0) && (val <= 2)) {
	      UShort reg = 0;
	      reg = SiS_GetCH70xx(pSiS->SiS_Pr, 0x01);
	      reg = (reg & 0xf0) | ((reg & 0x03) << 2) | val;
              SiS_SetCH70xx(pSiS->SiS_Pr, 0x01, reg);
           }
	   break;
       case CHRONTEL_701x:
           val /= 2;
	   if((val >= 0) && (val <= 7)) {
	      SiS_SetCH70xxANDOR(pSiS->SiS_Pr, 0x03, val, 0xF8);
	   }
           break;
   }
}

int SiS_GetCHTVtextenhance(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   if(!((pSiS->VBFlags2 & VB2_CHRONTEL) && (pSiS->VBFlags & CRT2_TV))) {
#ifdef SISDUALHEAD
      if(pSiSEnt && pSiS->DualHeadMode)
           return (int)pSiSEnt->chtvtextenhance;
      else
#endif
           return (int)pSiS->chtvtextenhance;
   } else {
#ifdef UNLOCK_ALWAYS
      sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif
      switch(pSiS->ChrontelType) {
      case CHRONTEL_700x:
	   return (int)(((SiS_GetCH70xx(pSiS->SiS_Pr, 0x01) & 0x0c) >> 2) * 6);
      case CHRONTEL_701x:
	   return (int)((SiS_GetCH70xx(pSiS->SiS_Pr, 0x03) & 0x07) * 2);
      default:
           return (int)pSiS->chtvtextenhance;
      }
   }
}

void SiS_SetCHTVcontrast(ScrnInfoPtr pScrn, int val)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   pSiS->chtvcontrast = val;
#ifdef SISDUALHEAD
   if(pSiSEnt) pSiSEnt->chtvcontrast = val;
#endif

   if(!(pSiS->VBFlags & CRT2_TV)) return;
   if(!(pSiS->VBFlags2 & VB2_CHRONTEL)) return;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   val /= 2;
   if((val >= 0) && (val <= 7)) {
       switch(pSiS->ChrontelType) {
       case CHRONTEL_700x:
              SiS_SetCH70xxANDOR(pSiS->SiS_Pr, 0x11, val, 0xF8);
	      break;
       case CHRONTEL_701x:
	      SiS_SetCH70xxANDOR(pSiS->SiS_Pr, 0x08, val, 0xF8);
              break;
       }
       SiS_DDC2Delay(pSiS->SiS_Pr, 1000);
   }
}

int SiS_GetCHTVcontrast(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   if(!((pSiS->VBFlags2 & VB2_CHRONTEL) && (pSiS->VBFlags & CRT2_TV))) {
#ifdef SISDUALHEAD
      if(pSiSEnt && pSiS->DualHeadMode)
           return (int)pSiSEnt->chtvcontrast;
      else
#endif
           return (int)pSiS->chtvcontrast;
   } else {
#ifdef UNLOCK_ALWAYS
      sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif
      switch(pSiS->ChrontelType) {
      case CHRONTEL_700x:
           return (int)((SiS_GetCH70xx(pSiS->SiS_Pr, 0x11) & 0x07) * 2);
      case CHRONTEL_701x:
	   return (int)((SiS_GetCH70xx(pSiS->SiS_Pr, 0x08) & 0x07) * 2);
      default:
           return (int)pSiS->chtvcontrast;
      }
   }
}

void SiS_SetSISTVedgeenhance(ScrnInfoPtr pScrn, int val)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   pSiS->sistvedgeenhance = val;
#ifdef SISDUALHEAD
   if(pSiSEnt) pSiSEnt->sistvedgeenhance = val;
#endif

   if(!(pSiS->VBFlags2 & VB2_301))  return;
   if(!(pSiS->VBFlags & CRT2_TV))   return;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   val /= 2;
   if((val >= 0) && (val <= 7)) {
      setSISIDXREG(SISPART2,0x3A, 0x1F, (val << 5));
   }
}

int SiS_GetSISTVedgeenhance(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
   int result = pSiS->sistvedgeenhance;
   UChar temp;
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;

   if(pSiSEnt && pSiS->DualHeadMode) result = pSiSEnt->sistvedgeenhance;
#endif

   if(!(pSiS->VBFlags2 & VB2_301))  return result;
   if(!(pSiS->VBFlags & CRT2_TV))   return result;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif
   inSISIDXREG(SISPART2, 0x3a, temp);
   return(int)(((temp & 0xe0) >> 5) * 2);
}

void SiS_SetSISTVantiflicker(ScrnInfoPtr pScrn, int val)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   pSiS->sistvantiflicker = val;
#ifdef SISDUALHEAD
   if(pSiSEnt) pSiSEnt->sistvantiflicker = val;
#endif

   if(!(pSiS->VBFlags & CRT2_TV))      return;
   if(!(pSiS->VBFlags2 & VB2_SISBRIDGE)) return;
   if(pSiS->VBFlags & TV_HIVISION)     return;
   if((pSiS->VBFlags & TV_YPBPR) &&
      (pSiS->VBFlags & (TV_YPBPR525P | TV_YPBPR625P | TV_YPBPR750P | TV_YPBPR1080I))) return;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   /* Valid values: 0=off, 1=low, 2=med, 3=high, 4=adaptive */
   if((val >= 0) && (val <= 4)) {
      setSISIDXREG(SISPART2,0x0A,0x8F, (val << 4));
   }
}

int SiS_GetSISTVantiflicker(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
   int result = pSiS->sistvantiflicker;
   UChar temp;
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;

   if(pSiSEnt && pSiS->DualHeadMode) result = pSiSEnt->sistvantiflicker;
#endif

   if(!(pSiS->VBFlags2 & VB2_SISBRIDGE)) return result;
   if(!(pSiS->VBFlags & CRT2_TV))        return result;
   if(pSiS->VBFlags & TV_HIVISION)       return result;
   if((pSiS->VBFlags & TV_YPBPR) &&
      (pSiS->VBFlags & (TV_YPBPR525P | TV_YPBPR625P | TV_YPBPR750P | TV_YPBPR1080I))) return result;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif
   inSISIDXREG(SISPART2, 0x0a, temp);
   return(int)((temp & 0x70) >> 4);
}

void SiS_SetSISTVsaturation(ScrnInfoPtr pScrn, int val)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   pSiS->sistvsaturation = val;
#ifdef SISDUALHEAD
   if(pSiSEnt) pSiSEnt->sistvsaturation = val;
#endif

   if(!(pSiS->VBFlags & CRT2_TV)) return;
   if(!(pSiS->VBFlags2 & VB2_SISBRIDGE)) return;
   if(pSiS->VBFlags2 & VB2_301) return;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   val /= 2;
   if((val >= 0) && (val <= 7)) {
      setSISIDXREG(SISPART4,0x21,0xF8, val);
   }
}

int SiS_GetSISTVsaturation(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
   int result = pSiS->sistvsaturation;
   UChar temp;
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;

   if(pSiSEnt && pSiS->DualHeadMode)  result = pSiSEnt->sistvsaturation;
#endif

   if(!(pSiS->VBFlags2 & VB2_SISBRIDGE)) return result;
   if(pSiS->VBFlags2 & VB2_301)          return result;
   if(!(pSiS->VBFlags & CRT2_TV))        return result;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif
   inSISIDXREG(SISPART4, 0x21, temp);
   return(int)((temp & 0x07) * 2);
}

void SiS_SetSISTVcolcalib(ScrnInfoPtr pScrn, int val, Bool coarse)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif
   int ccoarse, cfine, cbase = pSiS->sistvccbase;
   /* UChar temp; */

#ifdef SISDUALHEAD
   if(pSiSEnt && pSiS->DualHeadMode) cbase = pSiSEnt->sistvccbase;
#endif

   if(coarse) {
      pSiS->sistvcolcalibc = ccoarse = val;
      cfine = pSiS->sistvcolcalibf;
#ifdef SISDUALHEAD
      if(pSiSEnt) {
         pSiSEnt->sistvcolcalibc = val;
	 if(pSiS->DualHeadMode) cfine = pSiSEnt->sistvcolcalibf;
      }
#endif
   } else {
      pSiS->sistvcolcalibf = cfine = val;
      ccoarse = pSiS->sistvcolcalibc;
#ifdef SISDUALHEAD
      if(pSiSEnt) {
         pSiSEnt->sistvcolcalibf = val;
         if(pSiS->DualHeadMode) ccoarse = pSiSEnt->sistvcolcalibc;
      }
#endif
   }

   if(!(pSiS->VBFlags & CRT2_TV))               return;
   if(!(pSiS->VBFlags2 & VB2_SISBRIDGE))        return;
   if(pSiS->VBFlags & (TV_HIVISION | TV_YPBPR)) return;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   if((cfine >= -128) && (cfine <= 127) && (ccoarse >= -120) && (ccoarse <= 120)) {
      long finalcc = cbase + (((ccoarse * 256) + cfine) * 256);

#if 0
      inSISIDXREG(SISPART4,0x1f,temp);
      if(!(temp & 0x01)) {
         if(pSiS->VBFlags & TV_NTSC) finalcc += 0x21ed8620;
	 else if(pSiS->VBFlags & TV_PALM) finalcc += ?;
	 else if(pSiS->VBFlags & TV_PALM) finalcc += ?;
	 else finalcc += 0x2a05d300;
      }
#endif
      setSISIDXREG(SISPART2,0x31,0x80,((finalcc >> 24) & 0x7f));
      outSISIDXREG(SISPART2,0x32,((finalcc >> 16) & 0xff));
      outSISIDXREG(SISPART2,0x33,((finalcc >> 8) & 0xff));
      outSISIDXREG(SISPART2,0x34,(finalcc & 0xff));
   }
}

int SiS_GetSISTVcolcalib(ScrnInfoPtr pScrn, Bool coarse)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;

   if(pSiSEnt && pSiS->DualHeadMode)
      if(coarse)  return (int)pSiSEnt->sistvcolcalibc;
      else        return (int)pSiSEnt->sistvcolcalibf;
   else
#endif
   if(coarse)     return (int)pSiS->sistvcolcalibc;
   else           return (int)pSiS->sistvcolcalibf;
}

void SiS_SetSISTVcfilter(ScrnInfoPtr pScrn, int val)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   pSiS->sistvcfilter = val ? 1 : 0;
#ifdef SISDUALHEAD
   if(pSiSEnt) pSiSEnt->sistvcfilter = pSiS->sistvcfilter;
#endif

   if(!(pSiS->VBFlags & CRT2_TV))               return;
   if(!(pSiS->VBFlags2 & VB2_SISBRIDGE))        return;
   if(pSiS->VBFlags & (TV_HIVISION | TV_YPBPR)) return;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   setSISIDXREG(SISPART2,0x30,~0x10,((pSiS->sistvcfilter << 4) & 0x10));
}

int SiS_GetSISTVcfilter(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
   int result = pSiS->sistvcfilter;
   UChar temp;
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;

   if(pSiSEnt && pSiS->DualHeadMode) result = pSiSEnt->sistvcfilter;
#endif

   if(!(pSiS->VBFlags2 & VB2_SISBRIDGE))        return result;
   if(!(pSiS->VBFlags & CRT2_TV))               return result;
   if(pSiS->VBFlags & (TV_HIVISION | TV_YPBPR)) return result;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif
   inSISIDXREG(SISPART2, 0x30, temp);
   return (int)((temp & 0x10) ? 1 : 0);
}

void SiS_SetSISTVyfilter(ScrnInfoPtr pScrn, int val)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif
   UChar p35,p36,p37,p38,p48,p49,p4a,p30;
   int i,j;

   pSiS->sistvyfilter = val;
#ifdef SISDUALHEAD
   if(pSiSEnt) pSiSEnt->sistvyfilter = pSiS->sistvyfilter;
#endif

   if(!(pSiS->VBFlags & CRT2_TV))               return;
   if(!(pSiS->VBFlags2 & VB2_SISBRIDGE))        return;
   if(pSiS->VBFlags & (TV_HIVISION | TV_YPBPR)) return;

   p35 = pSiS->p2_35; p36 = pSiS->p2_36;
   p37 = pSiS->p2_37; p38 = pSiS->p2_38;
   p48 = pSiS->p2_48; p49 = pSiS->p2_49;
   p4a = pSiS->p2_4a; p30 = pSiS->p2_30;
#ifdef SISDUALHEAD
   if(pSiSEnt && pSiS->DualHeadMode) {
      p35 = pSiSEnt->p2_35; p36 = pSiSEnt->p2_36;
      p37 = pSiSEnt->p2_37; p38 = pSiSEnt->p2_38;
      p48 = pSiSEnt->p2_48; p49 = pSiSEnt->p2_49;
      p4a = pSiSEnt->p2_4a; p30 = pSiSEnt->p2_30;
   }
#endif
   p30 &= 0x20;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   switch(pSiS->sistvyfilter) {
   case 0:
      andSISIDXREG(SISPART2,0x30,0xdf);
      break;
   case 1:
      outSISIDXREG(SISPART2,0x35,p35);
      outSISIDXREG(SISPART2,0x36,p36);
      outSISIDXREG(SISPART2,0x37,p37);
      outSISIDXREG(SISPART2,0x38,p38);
      if(!(pSiS->VBFlags2 & VB2_301)) {
         outSISIDXREG(SISPART2,0x48,p48);
         outSISIDXREG(SISPART2,0x49,p49);
         outSISIDXREG(SISPART2,0x4a,p4a);
      }
      setSISIDXREG(SISPART2,0x30,0xdf,p30);
      break;
   case 2:
   case 3:
   case 4:
   case 5:
   case 6:
   case 7:
   case 8:
      if(!(pSiS->VBFlags & (TV_PALM | TV_PALN | TV_NTSCJ))) {
         int yindex301 = -1, yindex301B = -1;
	 UChar p3d4_34;

	 inSISIDXREG(SISCR,0x34,p3d4_34);

	 switch((p3d4_34 & 0x7f)) {
	 case 0x59:  /* 320x200 */
	 case 0x41:
	 case 0x4f:
	 case 0x50:  /* 320x240 */
	 case 0x56:
	 case 0x53:
	    yindex301  = (pSiS->VBFlags & TV_NTSC) ? 0 : 4;
	    break;
	 case 0x2f:  /* 640x400 */
	 case 0x5d:
	 case 0x5e:
	 case 0x2e:  /* 640x480 */
	 case 0x44:
	 case 0x62:
	    yindex301  = (pSiS->VBFlags & TV_NTSC) ? 1 : 5;
	    yindex301B = (pSiS->VBFlags & TV_NTSC) ? 0 : 4;
	    break;
	 case 0x31:   /* 720x480 */
	 case 0x33:
	 case 0x35:
	 case 0x32:   /* 720x576 */
	 case 0x34:
	 case 0x36:
	 case 0x5f:   /* 768x576 */
	 case 0x60:
	 case 0x61:
	    yindex301  = (pSiS->VBFlags & TV_NTSC) ? 2 : 6;
	    yindex301B = (pSiS->VBFlags & TV_NTSC) ? 1 : 5;
	    break;
	 case 0x51:   /* 400x300 */
	 case 0x57:
	 case 0x54:
	 case 0x30:   /* 800x600 */
	 case 0x47:
	 case 0x63:
	    yindex301  = (pSiS->VBFlags & TV_NTSC) ? 3 : 7;
	    yindex301B = (pSiS->VBFlags & TV_NTSC) ? 2 : 6;
	    break;
	 case 0x52:   /* 512x384 */
	 case 0x58:
	 case 0x5c:
	 case 0x38:   /* 1024x768 */
	 case 0x4a:
	 case 0x64:
	    yindex301B = (pSiS->VBFlags & TV_NTSC) ? 3 : 7;
	    break;
	 }
         if(pSiS->VBFlags2 & VB2_301) {
            if(yindex301 >= 0) {
	       for(i=0, j=0x35; i<=3; i++, j++) {
	          outSISIDXREG(SISPART2,j,(SiSTVFilter301[yindex301].filter[pSiS->sistvyfilter-2][i]));
	       }
	    }
         } else {
            if(yindex301B >= 0) {
	       for(i=0, j=0x35; i<=3; i++, j++) {
	          outSISIDXREG(SISPART2,j,(SiSTVFilter301B[yindex301B].filter[pSiS->sistvyfilter-2][i]));
	       }
	       for(i=4, j=0x48; i<=6; i++, j++) {
	          outSISIDXREG(SISPART2,j,(SiSTVFilter301B[yindex301B].filter[pSiS->sistvyfilter-2][i]));
	       }
	    }
         }
         orSISIDXREG(SISPART2,0x30,0x20);
      }
   }
}

int SiS_GetSISTVyfilter(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;

   if(pSiSEnt && pSiS->DualHeadMode)
      return (int)pSiSEnt->sistvyfilter;
   else
#endif
      return (int)pSiS->sistvyfilter;
}

void SiS_SetSIS6326TVantiflicker(ScrnInfoPtr pScrn, int val)
{
   SISPtr pSiS = SISPTR(pScrn);
   UChar tmp;

   pSiS->sistvantiflicker = val;

   if(!(pSiS->SiS6326Flags & SIS6326_TVDETECTED)) return;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   tmp = SiS6326GetTVReg(pScrn,0x00);
   if(!(tmp & 0x04)) return;

   /* Valid values: 0=off, 1=low, 2=med, 3=high, 4=adaptive */
   if(val >= 0 && val <= 4) {
      tmp &= 0x1f;
      tmp |= (val << 5);
      SiS6326SetTVReg(pScrn,0x00,tmp);
   }
}

int SiS_GetSIS6326TVantiflicker(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
   UChar tmp;

   if(!(pSiS->SiS6326Flags & SIS6326_TVDETECTED)) {
      return (int)pSiS->sistvantiflicker;
   }

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   tmp = SiS6326GetTVReg(pScrn,0x00);
   if(!(tmp & 0x04)) {
      return (int)pSiS->sistvantiflicker;
   } else {
      return (int)((tmp >> 5) & 0x07);
   }
}

void SiS_SetSIS6326TVenableyfilter(ScrnInfoPtr pScrn, int val)
{
   SISPtr pSiS = SISPTR(pScrn);
   UChar tmp;

   if(val) val = 1;
   pSiS->sis6326enableyfilter = val;

   if(!(pSiS->SiS6326Flags & SIS6326_TVDETECTED)) return;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   tmp = SiS6326GetTVReg(pScrn,0x00);
   if(!(tmp & 0x04)) return;

   tmp = SiS6326GetTVReg(pScrn,0x43);
   tmp &= ~0x10;
   tmp |= ((val & 0x01) << 4);
   SiS6326SetTVReg(pScrn,0x43,tmp);
}

int SiS_GetSIS6326TVenableyfilter(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
   UChar tmp;

   if(!(pSiS->SiS6326Flags & SIS6326_TVDETECTED)) {
      return (int)pSiS->sis6326enableyfilter;
   }

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   tmp = SiS6326GetTVReg(pScrn,0x00);
   if(!(tmp & 0x04)) {
      return (int)pSiS->sis6326enableyfilter;
   } else {
      tmp = SiS6326GetTVReg(pScrn,0x43);
      return (int)((tmp >> 4) & 0x01);
   }
}

void SiS_SetSIS6326TVyfilterstrong(ScrnInfoPtr pScrn, int val)
{
   SISPtr pSiS = SISPTR(pScrn);
   UChar tmp;

   if(val) val = 1;
   pSiS->sis6326yfilterstrong = val;

   if(!(pSiS->SiS6326Flags & SIS6326_TVDETECTED)) return;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   tmp = SiS6326GetTVReg(pScrn,0x00);
   if(!(tmp & 0x04)) return;

   tmp = SiS6326GetTVReg(pScrn,0x43);
   if(tmp & 0x10) {
      tmp &= ~0x40;
      tmp |= ((val & 0x01) << 6);
      SiS6326SetTVReg(pScrn,0x43,tmp);
   }
}

int SiS_GetSIS6326TVyfilterstrong(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
   UChar tmp;

   if(!(pSiS->SiS6326Flags & SIS6326_TVDETECTED)) {
      return (int)pSiS->sis6326yfilterstrong;
   }

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   tmp = SiS6326GetTVReg(pScrn,0x00);
   if(!(tmp & 0x04)) {
      return (int)pSiS->sis6326yfilterstrong;
   } else {
      tmp = SiS6326GetTVReg(pScrn,0x43);
      if(!(tmp & 0x10)) {
         return (int)pSiS->sis6326yfilterstrong;
      } else {
         return (int)((tmp >> 6) & 0x01);
      }
   }
}

void SiS_SetTVxposoffset(ScrnInfoPtr pScrn, int val)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   pSiS->tvxpos = val;
#ifdef SISDUALHEAD
   if(pSiSEnt) pSiSEnt->tvxpos = val;
#endif

   if(pSiS->VGAEngine == SIS_300_VGA || pSiS->VGAEngine == SIS_315_VGA) {

      if(pSiS->VBFlags & CRT2_TV) {

         if(pSiS->VBFlags2 & VB2_CHRONTEL) {

	    int x = pSiS->tvx;
#ifdef SISDUALHEAD
	    if(pSiSEnt && pSiS->DualHeadMode) x = pSiSEnt->tvx;
#endif
	    switch(pSiS->ChrontelType) {
	    case CHRONTEL_700x:
	       if((val >= -32) && (val <= 32)) {
		   x += val;
		   if(x < 0) x = 0;
		   SiS_SetCH700x(pSiS->SiS_Pr, 0x0a, (x & 0xff));
		   SiS_SetCH70xxANDOR(pSiS->SiS_Pr, 0x08, ((x & 0x0100) >> 7), 0xFD);
	       }
	       break;
	    case CHRONTEL_701x:
	       /* Not supported by hardware */
	       break;
	    }

	 } else if(pSiS->VBFlags2 & VB2_SISBRIDGE) {

	    if((val >= -32) && (val <= 32)) {

	        UChar p2_1f,p2_20,p2_2b,p2_42,p2_43;
		UShort temp;
		int mult;

		p2_1f = pSiS->p2_1f;
		p2_20 = pSiS->p2_20;
		p2_2b = pSiS->p2_2b;
		p2_42 = pSiS->p2_42;
		p2_43 = pSiS->p2_43;
#ifdef SISDUALHEAD
	        if(pSiSEnt && pSiS->DualHeadMode) {
		   p2_1f = pSiSEnt->p2_1f;
		   p2_20 = pSiSEnt->p2_20;
		   p2_2b = pSiSEnt->p2_2b;
		   p2_42 = pSiSEnt->p2_42;
		   p2_43 = pSiSEnt->p2_43;
		}
#endif
		mult = 2;
		if(pSiS->VBFlags & TV_YPBPR) {
		   if(pSiS->VBFlags & (TV_YPBPR1080I | TV_YPBPR750P)) {
		      unsigned char CR34;
		      mult = 4;
		      inSISIDXREG(SISCR, 0x34, CR34);
		      if(pSiS->VGAEngine == SIS_315_VGA && (pSiS->VBFlags & TV_YPBPR1080I)) {
		         if(CR34 == 0x1d || CR34 == 0x1e || CR34 == 0x1f) {
		            if(val < -26) val = -26;
		         }
		      } else if(pSiS->VBFlags & TV_YPBPR750P) {
#ifndef OLD1280720P
			 if(CR34 == 0x79 || CR34 == 0x75 || CR34 == 0x78) {
			    if(val < -17) val = -17;
			 }
#endif
		      }
		   }
		}

		temp = p2_1f | ((p2_20 & 0xf0) << 4);
		temp += (val * mult);
		p2_1f = temp & 0xff;
		p2_20 = (temp & 0xf00) >> 4;
		p2_2b = ((p2_2b & 0x0f) + (val * mult)) & 0x0f;
		temp = p2_43 | ((p2_42 & 0xf0) << 4);
		temp += (val * mult);
		p2_43 = temp & 0xff;
		p2_42 = (temp & 0xf00) >> 4;
		SISWaitRetraceCRT2(pScrn);
	        outSISIDXREG(SISPART2,0x1f,p2_1f);
		setSISIDXREG(SISPART2,0x20,0x0F,p2_20);
		setSISIDXREG(SISPART2,0x2b,0xF0,p2_2b);
		setSISIDXREG(SISPART2,0x42,0x0F,p2_42);
		outSISIDXREG(SISPART2,0x43,p2_43);
	     }
	 }
      }

   } else if(pSiS->Chipset == PCI_CHIP_SIS6326) {

      if(pSiS->SiS6326Flags & SIS6326_TVDETECTED) {

         UChar tmp;
	 UShort temp1, temp2, temp3;

         tmp = SiS6326GetTVReg(pScrn,0x00);
         if(tmp & 0x04) {

	    temp1 = pSiS->tvx1;
            temp2 = pSiS->tvx2;
            temp3 = pSiS->tvx3;
            if((val >= -16) && (val <= 16)) {
	       if(val > 0) {
	          temp1 += (val * 4);
	          temp2 += (val * 4);
	          while((temp1 > 0x0fff) || (temp2 > 0x0fff)) {
	             temp1 -= 4;
		     temp2 -= 4;
	          }
	       } else {
	          val = -val;
	          temp3 += (val * 4);
	          while(temp3 > 0x03ff) {
	     	     temp3 -= 4;
	          }
	       }
            }
            SiS6326SetTVReg(pScrn,0x3a,(temp1 & 0xff));
            tmp = SiS6326GetTVReg(pScrn,0x3c);
            tmp &= 0xf0;
            tmp |= ((temp1 & 0x0f00) >> 8);
            SiS6326SetTVReg(pScrn,0x3c,tmp);
            SiS6326SetTVReg(pScrn,0x26,(temp2 & 0xff));
            tmp = SiS6326GetTVReg(pScrn,0x27);
            tmp &= 0x0f;
            tmp |= ((temp2 & 0x0f00) >> 4);
            SiS6326SetTVReg(pScrn,0x27,tmp);
            SiS6326SetTVReg(pScrn,0x12,(temp3 & 0xff));
            tmp = SiS6326GetTVReg(pScrn,0x13);
            tmp &= ~0xC0;
            tmp |= ((temp3 & 0x0300) >> 2);
            SiS6326SetTVReg(pScrn,0x13,tmp);
	 }
      }
   }
}

int SiS_GetTVxposoffset(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;

   if(pSiSEnt && pSiS->DualHeadMode)
        return (int)pSiSEnt->tvxpos;
   else
#endif
        return (int)pSiS->tvxpos;
}

void SiS_SetTVyposoffset(ScrnInfoPtr pScrn, int val)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   pSiS->tvypos = val;
#ifdef SISDUALHEAD
   if(pSiSEnt) pSiSEnt->tvypos = val;
#endif

   if(pSiS->VGAEngine == SIS_300_VGA || pSiS->VGAEngine == SIS_315_VGA) {

      if(pSiS->VBFlags & CRT2_TV) {

         if(pSiS->VBFlags2 & VB2_CHRONTEL) {

	    int y = pSiS->tvy;
#ifdef SISDUALHEAD
	    if(pSiSEnt && pSiS->DualHeadMode) y = pSiSEnt->tvy;
#endif
	    switch(pSiS->ChrontelType) {
	    case CHRONTEL_700x:
	       if((val >= -32) && (val <= 32)) {
		   y -= val;
		   if(y < 0) y = 0;
		   SiS_SetCH700x(pSiS->SiS_Pr, 0x0b, (y & 0xff));
		   SiS_SetCH70xxANDOR(pSiS->SiS_Pr, 0x08, ((y & 0x0100) >> 8), 0xFE);
	       }
	       break;
	    case CHRONTEL_701x:
	       /* Not supported by hardware */
	       break;
	    }

	 } else if(pSiS->VBFlags2 & VB2_SISBRIDGE) {

	    if((val >= -32) && (val <= 32)) {
		char p2_01, p2_02;

		if( (pSiS->VBFlags & TV_HIVISION) ||
		    ((pSiS->VBFlags & TV_YPBPR) && (pSiS->VBFlags & (TV_YPBPR1080I|TV_YPBPR750P))) ) {
		   val *= 2;
		} else {
		   val /= 2;  /* 4 */
		}

		p2_01 = pSiS->p2_01;
		p2_02 = pSiS->p2_02;
#ifdef SISDUALHEAD
	        if(pSiSEnt && pSiS->DualHeadMode) {
		   p2_01 = pSiSEnt->p2_01;
		   p2_02 = pSiSEnt->p2_02;
		}
#endif
		p2_01 += val; /* val * 2 */
		p2_02 += val; /* val * 2 */
		if(!(pSiS->VBFlags & (TV_YPBPR | TV_HIVISION))) {
		   while((p2_01 <= 0) || (p2_02 <= 0)) {
		      p2_01 += 2;
		      p2_02 += 2;
		   }
		} else if((pSiS->VBFlags & TV_YPBPR) && (pSiS->VBFlags & TV_YPBPR1080I)) {
		   while(p2_01 <= 8) {
		      p2_01 += 2;
		      p2_02 += 2;
		   }
		} else if((pSiS->VBFlags & TV_YPBPR) && (pSiS->VBFlags & TV_YPBPR750P)) {
		   while(p2_01 <= 10) {
		      p2_01 += 2;
		      p2_02 += 2;
		   }
		}

		SISWaitRetraceCRT2(pScrn);
		outSISIDXREG(SISPART2,0x01,p2_01);
		outSISIDXREG(SISPART2,0x02,p2_02);
	     }
	 }

      }

   } else if(pSiS->Chipset == PCI_CHIP_SIS6326) {

      if(pSiS->SiS6326Flags & SIS6326_TVDETECTED) {

         UChar tmp;
	 int temp1, limit;

         tmp = SiS6326GetTVReg(pScrn,0x00);
         if(tmp & 0x04) {

	    if((val >= -16) && (val <= 16)) {
	      temp1 = (UShort)pSiS->tvy1;
	      limit = (pSiS->SiS6326Flags & SIS6326_TVPAL) ? 625 : 525;
	      if(val > 0) {
                temp1 += (val * 4);
	        if(temp1 > limit) temp1 -= limit;
	      } else {
	        val = -val;
	        temp1 -= (val * 2);
	        if(temp1 <= 0) temp1 += (limit -1);
	      }
	      SiS6326SetTVReg(pScrn,0x11,(temp1 & 0xff));
	      tmp = SiS6326GetTVReg(pScrn,0x13);
	      tmp &= ~0x30;
	      tmp |= ((temp1 & 0x300) >> 4);
	      SiS6326SetTVReg(pScrn,0x13,tmp);
	      if(temp1 == 1)                                 tmp = 0x10;
	      else {
	       if(pSiS->SiS6326Flags & SIS6326_TVPAL) {
	         if((temp1 <= 3) || (temp1 >= (limit - 2)))  tmp = 0x08;
	         else if(temp1 < 22)		 	     tmp = 0x02;
	         else 					     tmp = 0x04;
	       } else {
	         if((temp1 <= 5) || (temp1 >= (limit - 4)))  tmp = 0x08;
	         else if(temp1 < 19)			     tmp = 0x02;
	         else 					     tmp = 0x04;
	       }
	     }
	     SiS6326SetTVReg(pScrn,0x21,tmp);
           }
	 }
      }
   }
}

int SiS_GetTVyposoffset(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;

   if(pSiSEnt && pSiS->DualHeadMode)
        return (int)pSiSEnt->tvypos;
   else
#endif
        return (int)pSiS->tvypos;
}

void SiS_SetTVxscale(ScrnInfoPtr pScrn, int val)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   pSiS->tvxscale = val;
#ifdef SISDUALHEAD
   if(pSiSEnt) pSiSEnt->tvxscale = val;
#endif

   if(pSiS->VGAEngine == SIS_300_VGA || pSiS->VGAEngine == SIS_315_VGA) {

      if((pSiS->VBFlags & CRT2_TV) && (pSiS->VBFlags2 & VB2_SISBRIDGE)) {

	 if((val >= -16) && (val <= 16)) {

	    UChar p2_44,p2_45,p2_46;
	    int scalingfactor, mult;

	    p2_44 = pSiS->p2_44;
	    p2_45 = pSiS->p2_45 & 0x3f;
	    p2_46 = pSiS->p2_46 & 0x07;
#ifdef SISDUALHEAD
	    if(pSiSEnt && pSiS->DualHeadMode) {
	       p2_44 = pSiSEnt->p2_44;
	       p2_45 = pSiSEnt->p2_45 & 0x3f;
	       p2_46 = pSiSEnt->p2_46 & 0x07;
	    }
#endif
	    scalingfactor = (p2_46 << 13) | ((p2_45 & 0x1f) << 8) | p2_44;

	    mult = 64;
	    if(pSiS->VBFlags & TV_YPBPR) {
	       if(pSiS->VBFlags & TV_YPBPR1080I) {
	          mult = 190;
	       } else if(pSiS->VBFlags & TV_YPBPR750P) {
	          mult = 360;
	       }
	    } else if(pSiS->VBFlags & TV_HIVISION) {
	       mult = 190;
	    }

	    if(val < 0) {
	       p2_45 &= 0xdf;
	       scalingfactor += ((-val) * mult);
	       if(scalingfactor > 0xffff) scalingfactor = 0xffff;
	    } else if(val > 0) {
	       p2_45 &= 0xdf;
	       scalingfactor -= (val * mult);
	       if(scalingfactor < 1) scalingfactor = 1;
	    }

	    p2_44 = scalingfactor & 0xff;
	    p2_45 &= 0xe0;
	    p2_45 |= ((scalingfactor >> 8) & 0x1f);
	    p2_46 = ((scalingfactor >> 13) & 0x07);

	    SISWaitRetraceCRT2(pScrn);
	    outSISIDXREG(SISPART2,0x44,p2_44);
	    setSISIDXREG(SISPART2,0x45,0xC0,p2_45);
	    if(!(pSiS->VBFlags2 & VB2_301)) {
	       setSISIDXREG(SISPART2,0x46,0xF8,p2_46);
	    }

	 }

      }

   }
}

int SiS_GetTVxscale(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;

   if(pSiSEnt && pSiS->DualHeadMode)
        return (int)pSiSEnt->tvxscale;
   else
#endif
        return (int)pSiS->tvxscale;
}

void SiS_SetTVyscale(ScrnInfoPtr pScrn, int val)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   if(val < -4) val = -4;
   if(val > 3)  val = 3;

   pSiS->tvyscale = val;
#ifdef SISDUALHEAD
   if(pSiSEnt) pSiSEnt->tvyscale = val;
#endif

   if(pSiS->VGAEngine == SIS_300_VGA || pSiS->VGAEngine == SIS_315_VGA) {

      if((pSiS->VBFlags & CRT2_TV) && (pSiS->VBFlags2 & VB2_SISBRIDGE)) {

	 int srindex = -1, newvde, i = 0, j, vlimit, temp, vdediv;
	 int hdclk = 0;
	 UChar p3d4_34;
	 Bool found = FALSE;
	 Bool usentsc = FALSE;
	 Bool is750p = FALSE;
	 Bool is1080i = FALSE;
	 Bool skipmoveup = FALSE;

	 SiS_UnLockCRT2(pSiS->SiS_Pr);

	 if((pSiS->VBFlags & TV_YPBPR) && (pSiS->VBFlags & TV_YPBPR525P)) {
	    vlimit = 525 - 7;
	    vdediv = 1;
	    usentsc = TRUE;
	 } else if((pSiS->VBFlags & TV_YPBPR) && (pSiS->VBFlags & TV_YPBPR625P)) {
	    vlimit = 625 - 7;
	    vdediv = 1;
	 } else if((pSiS->VBFlags & TV_YPBPR) && (pSiS->VBFlags & TV_YPBPR750P)) {
	    vlimit = 750 - 7;
	    vdediv = 1;
	    is750p = TRUE;
	 } else if(((pSiS->VBFlags & TV_YPBPR) && (pSiS->VBFlags & TV_YPBPR1080I)) ||
	           (pSiS->VBFlags & TV_HIVISION)) {
	    vlimit = (1125 - 7) / 2;
	    vdediv = 2;
	    is1080i = TRUE;
	 } else {
	    if( ((pSiS->VBFlags & TV_YPBPR) && (pSiS->VBFlags & TV_YPBPR525I)) ||
	        ((!(pSiS->VBFlags & TV_YPBPR)) && (pSiS->VBFlags & (TV_NTSC | TV_PALM))) ) {
	       usentsc = TRUE;
	    }
	    vlimit = usentsc ? 259 : 309;
	    vdediv = 2;
	 }

	 inSISIDXREG(SISCR,0x34,p3d4_34);

	 switch((p3d4_34 & 0x7f)) {
	 case 0x50:   /* 320x240 */
	 case 0x56:
	 case 0x53:
	    hdclk = 1;
	    /* fall through */
	 case 0x2e:   /* 640x480 */
	 case 0x44:
	 case 0x62:
	    if(is1080i) {
	       srindex = 98;
	    } else if(is750p) {
	       srindex = 42;
	    } else {
	       srindex  = usentsc ? 0 : 21;
	    }
	    break;
	 case 0x31:   /* 720x480 */
	 case 0x33:
	 case 0x35:
	    if(is1080i) {
	       /* n/a */
	    } else if(is750p) {
	       srindex = 49;
	    } else {
	       srindex = usentsc ? 7 : 21;
	    }
	    break;
	 case 0x32:   /* 720x576 */
	 case 0x34:
	 case 0x36:
	 case 0x5f:   /* 768x576 */
	 case 0x60:
	 case 0x61:
	    if(is1080i) {
	       /* n/a */
	    } else if(is750p) {
	       srindex = 56;
	    } else {
	       srindex  = usentsc ? 147 : 28;
	    }
	    break;
	 case 0x70:   /* 800x480 */
	 case 0x7a:
	 case 0x76:
	    if(is1080i) {
	       srindex = 105;
	    } else if(is750p) {
	       srindex = 63;
	    } else {
	       srindex = usentsc ? 175 : 21;
	    }
	    break;
	 case 0x39:   /* 848x480 */
	 case 0x3b:
	 case 0x3e:
	 case 0x3f:   /* 856x480 */
	 case 0x42:
	 case 0x45:
	    if(is1080i) {
	       srindex = 105;
	    } else if(is750p) {
	       srindex = 63;
	    } else {
	       srindex = usentsc ? 217 : 210;
	    }
	    break;
	 case 0x51:   /* 400x300 - hdclk mode */
	 case 0x57:
	 case 0x54:
	    hdclk = 1;
	    /* fall through */
	 case 0x30:   /* 800x600 */
	 case 0x47:
	 case 0x63:
	    if(is1080i) {
	       srindex = 112;
	    } else if(is750p) {
	       srindex = 70;
	    } else {
	       srindex = usentsc ? 14 : 35;
	    }
	    break;
	 case 0x1d:	/* 960x540 */
	 case 0x1e:
	 case 0x1f:
	    if(is1080i) {
	       srindex = 196;
	       skipmoveup = TRUE;
	    }
	    break;
	 case 0x20:	/* 960x600 */
	 case 0x21:
	 case 0x22:
	    if(pSiS->VGAEngine == SIS_315_VGA && is1080i) {
	       srindex = 203;
	    }
	    break;
	 case 0x71:	/* 1024x576 */
	 case 0x74:
	 case 0x77:
	    if(is1080i) {
	       srindex = 119;
	    } else if(is750p) {
	       srindex = 77;
	    } else {
	       srindex  = usentsc ? 182 : 189;
	    }
	    break;
	 case 0x52:	/* 512x384 */
	 case 0x58:
	 case 0x5c:
	    hdclk = 1;
	    /* fall through */
	 case 0x38:	/* 1024x768 */
	 case 0x4a:
	 case 0x64:
	    if(is1080i) {
	       srindex = 126;
	    } else if(is750p) {
	       srindex = 84;
	    } else if(!usentsc) {
	       srindex = 154;
	    } else if(vdediv == 1) {
	       if(!hdclk) srindex = 168;
	    } else {
	       if(!hdclk) srindex = 161;
	    }
	    break;
	 case 0x79:	/* 1280x720 */
	 case 0x75:
	 case 0x78:
	    if(is1080i) {
	       srindex = 133;
	    } else if(is750p) {
	       srindex = 91;
	    }
	    break;
	 case 0x3a:	/* 1280x1024 */
	 case 0x4d:
	 case 0x65:
	    if(is1080i) {
	       srindex = 140;
	    }
	    break;
	 }

	 if(srindex < 0) return;

	 if(pSiS->tvyscale != 0) {
	    for(j = 0; j <= 1; j++) {
	       for(i = 0; i <= 6; i++) {
		  if(SiSTVVScale[srindex+i].sindex == pSiS->tvyscale) {
		     found = TRUE;
		     break;
		  }
	       }
	       if(found) break;
	       if(pSiS->tvyscale > 0) pSiS->tvyscale--;
	       else pSiS->tvyscale++;
	    }
	 }

#ifdef SISDUALHEAD
	 if(pSiSEnt) pSiSEnt->tvyscale = pSiS->tvyscale;
#endif

	 if(pSiS->tvyscale == 0) {
	    UChar p2_0a = pSiS->p2_0a;
	    UChar p2_2f = pSiS->p2_2f;
	    UChar p2_30 = pSiS->p2_30;
	    UChar p2_46 = pSiS->p2_46;
	    UChar p2_47 = pSiS->p2_47;
	    UChar p1scaling[9], p4scaling[9];
	    UChar *p2scaling;

	    for(i = 0; i < 9; i++) {
	        p1scaling[i] = pSiS->scalingp1[i];
		p4scaling[i] = pSiS->scalingp4[i];
	    }
	    p2scaling = &pSiS->scalingp2[0];

#ifdef SISDUALHEAD
	    if(pSiSEnt && pSiS->DualHeadMode) {
	       p2_0a = pSiSEnt->p2_0a;
	       p2_2f = pSiSEnt->p2_2f;
	       p2_30 = pSiSEnt->p2_30;
	       p2_46 = pSiSEnt->p2_46;
	       p2_47 = pSiSEnt->p2_47;
	       for(i = 0; i < 9; i++) {
		  p1scaling[i] = pSiSEnt->scalingp1[i];
		  p4scaling[i] = pSiSEnt->scalingp4[i];
	       }
	       p2scaling = &pSiSEnt->scalingp2[0];
	    }
#endif
            SISWaitRetraceCRT2(pScrn);
	    if(pSiS->VBFlags2 & VB2_SISTAP4SCALER) {
	       for(i = 0; i < 64; i++) {
	          outSISIDXREG(SISPART2,(0xc0 + i),p2scaling[i]);
	       }
	    }
	    for(i = 0; i < 9; i++) {
	       outSISIDXREG(SISPART1,SiSScalingP1Regs[i],p1scaling[i]);
	    }
	    for(i = 0; i < 9; i++) {
	       outSISIDXREG(SISPART4,SiSScalingP4Regs[i],p4scaling[i]);
	    }

	    setSISIDXREG(SISPART2,0x0a,0x7f,(p2_0a & 0x80));
	    outSISIDXREG(SISPART2,0x2f,p2_2f);
	    setSISIDXREG(SISPART2,0x30,0x3f,(p2_30 & 0xc0));
	    if(!(pSiS->VBFlags2 & VB2_301)) {
	       setSISIDXREG(SISPART2,0x46,0x9f,(p2_46 & 0x60));
	       outSISIDXREG(SISPART2,0x47,p2_47);
	    }

	 } else {

	    int realvde, myypos, watchdog = 32;
	    unsigned short temp1, temp2, vgahde, vgaht, vgavt;
	    int p1div = 1;
	    ULong calctemp;

	    srindex += i;
	    newvde = SiSTVVScale[srindex].ScaleVDE;
	    realvde = SiSTVVScale[srindex].RealVDE;

	    if(vdediv == 1) p1div = 2;

	    if(!skipmoveup) {
	       do {
	          inSISIDXREG(SISPART2,0x01,temp);
	          temp = vlimit - ((temp & 0x7f) / p1div);
	          if((temp - (((newvde / vdediv) - 2) + 9)) > 0) break;
	          myypos = pSiS->tvypos - 1;
#ifdef SISDUALHEAD
	          if(pSiSEnt && pSiS->DualHeadMode) myypos = pSiSEnt->tvypos - 1;
#endif
	          SiS_SetTVyposoffset(pScrn, myypos);
	       } while(watchdog--);
	    }

	    SISWaitRetraceCRT2(pScrn);

	    if(pSiS->VBFlags2 & VB2_SISTAP4SCALER) {
	       SiS_CalcXTapScaler(pSiS->SiS_Pr, realvde, newvde, 4, FALSE);
	    }

	    if(!(pSiS->VBFlags2 & VB2_301)) {
	       temp = (newvde / vdediv) - 3;
	       setSISIDXREG(SISPART2,0x46,0x9f,((temp & 0x0300) >> 3));
	       outSISIDXREG(SISPART2,0x47,(temp & 0xff));
	    }

	    inSISIDXREG(SISPART1,0x0a,temp1);
	    inSISIDXREG(SISPART1,0x0c,temp2);
	    vgahde = ((temp2 & 0xf0) << 4) | temp1;
	    if(pSiS->VGAEngine == SIS_300_VGA) {
	       vgahde -= 12;
	    } else {
	       vgahde -= 16;
	       if(hdclk) vgahde <<= 1;
	    }

	    vgaht = SiSTVVScale[srindex].reg[0];
	    temp1 = vgaht;
	    if((pSiS->VGAEngine == SIS_315_VGA) && hdclk) temp1 >>= 1;
	    temp1--;
	    outSISIDXREG(SISPART1,0x08,(temp1 & 0xff));
	    setSISIDXREG(SISPART1,0x09,0x0f,((temp1 >> 4) & 0xf0));

	    temp2 = (vgaht - vgahde) >> 2;
	    if(pSiS->VGAEngine == SIS_300_VGA) {
	       temp1 = vgahde + 12 + temp2;
	       temp2 = temp1 + (temp2 << 1);
	    } else {
	       temp1 = vgahde;
	       if(hdclk) {
		  temp1 >>= 1;
		  temp2 >>= 1;
	       }
	       temp2 >>= 1;
	       temp1 = temp1 + 16 + temp2;
	       temp2 = temp1 + temp2;
	    }
	    outSISIDXREG(SISPART1,0x0b,(temp1 & 0xff));
	    setSISIDXREG(SISPART1,0x0c,0xf0,((temp1 >> 8) & 0x0f));
	    outSISIDXREG(SISPART1,0x0d,(temp2 & 0xff));

	    vgavt = SiSTVVScale[srindex].reg[1];
	    temp1 = vgavt - 1;
	    if(pSiS->VGAEngine == SIS_315_VGA) temp1--;
	    outSISIDXREG(SISPART1,0x0e,(temp1 & 0xff));
	    setSISIDXREG(SISPART1,0x12,0xf8,((temp1 >> 8 ) & 0x07));
	    if((pSiS->VGAEngine == SIS_300_VGA) || (pSiS->ChipType >= SIS_661)) {
	       temp1 = (vgavt + SiSTVVScale[srindex].RealVDE) >> 1;
	       temp2 = ((vgavt - SiSTVVScale[srindex].RealVDE) >> 4) + temp1 + 1;
	    } else {
	       temp1 = (vgavt - SiSTVVScale[srindex].RealVDE) >> 2;
	       temp2 = (temp1 < 4) ? 4 : temp1;
	       temp1 += SiSTVVScale[srindex].RealVDE;
	       temp2 = (temp2 >> 2) + temp1 + 1;
	    }
	    outSISIDXREG(SISPART1,0x10,(temp1 & 0xff));
	    setSISIDXREG(SISPART1,0x11,0x8f,((temp1 >> 4) & 0x70));
	    setSISIDXREG(SISPART1,0x11,0xf0,(temp2 & 0x0f));

	    setSISIDXREG(SISPART2,0x0a,0x7f,((SiSTVVScale[srindex].reg[2] >> 8) & 0x80));
	    outSISIDXREG(SISPART2,0x2f,((newvde / vdediv) - 2));
	    setSISIDXREG(SISPART2,0x30,0x3f,((((newvde / vdediv) - 2) >> 2) & 0xc0));

	    outSISIDXREG(SISPART4,0x13,(SiSTVVScale[srindex].reg[2] & 0xff));
	    outSISIDXREG(SISPART4,0x14,(SiSTVVScale[srindex].reg[3] & 0xff));
	    setSISIDXREG(SISPART4,0x15,0x7f,((SiSTVVScale[srindex].reg[3] >> 1) & 0x80));

	    temp1 = vgaht - 1;
	    outSISIDXREG(SISPART4,0x16,(temp1 & 0xff));
	    setSISIDXREG(SISPART4,0x15,0x87,((temp1 >> 5) & 0x78));

	    temp1 = vgavt - 1;
	    outSISIDXREG(SISPART4,0x17,(temp1 & 0xff));
	    setSISIDXREG(SISPART4,0x15,0xf8,((temp1 >> 8) & 0x07));

	    outSISIDXREG(SISPART4,0x18,0x00);
	    setSISIDXREG(SISPART4,0x19,0xf0,0x00);

	    inSISIDXREG(SISPART4,0x0e,temp1);
	    if(is1080i) {
	       if(!(temp1 & 0xe0)) newvde >>= 1;
	    }

	    temp = 0x40;
	    if(realvde <= newvde) temp = 0;
	    else realvde -= newvde;

	    calctemp = (realvde * 256 * 1024) / newvde;
	    if((realvde * 256 * 1024) % newvde) calctemp++;
	    outSISIDXREG(SISPART4,0x1b,(calctemp & 0xff));
	    outSISIDXREG(SISPART4,0x1a,((calctemp >> 8) & 0xff));
	    setSISIDXREG(SISPART4,0x19,0x8f,(((calctemp >> 12) & 0x70) | temp));
	 }

      }

   }
}

int SiS_GetTVyscale(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;

   if(pSiSEnt && pSiS->DualHeadMode)
        return (int)pSiSEnt->tvyscale;
   else
#endif
        return (int)pSiS->tvyscale;
}

void SiS_SetSISCRT1SaturationGain(ScrnInfoPtr pScrn, int val)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   pSiS->siscrt1satgain = val;
#ifdef SISDUALHEAD
   if(pSiSEnt) pSiSEnt->siscrt1satgain = val;
#endif

   if(!(pSiS->SiS_SD3_Flags & SiS_SD3_CRT1SATGAIN)) return;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif

   if((val >= 0) && (val <= 7)) {
      setSISIDXREG(SISCR,0x53,0xE3, (val << 2));
   }
}

int SiS_GetSISCRT1SaturationGain(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
   int result = pSiS->siscrt1satgain;
   UChar temp;
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;

   if(pSiSEnt && pSiS->DualHeadMode)  result = pSiSEnt->siscrt1satgain;
#endif

   if(!(pSiS->SiS_SD3_Flags & SiS_SD3_CRT1SATGAIN)) return result;

#ifdef UNLOCK_ALWAYS
   sisSaveUnlockExtRegisterLock(pSiS, NULL, NULL);
#endif
   inSISIDXREG(SISCR, 0x53, temp);
   return (int)((temp >> 2) & 0x07);
}

void
SiSPostSetModeTVParms(ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

    /*  Apply TV settings given by options
           Do this even in DualHeadMode:
	   - if this is called by SetModeCRT1, CRT2 mode has been reset by SetModeCRT1
	   - if this is called by SetModeCRT2, CRT2 mode has changed (duh!)
	   -> Hence, in both cases, the settings must be re-applied.
     */

    if(pSiS->VBFlags & CRT2_TV) {
       int val;
       if(pSiS->VBFlags2 & VB2_CHRONTEL) {
	  int mychtvlumabandwidthcvbs = pSiS->chtvlumabandwidthcvbs;
	  int mychtvlumabandwidthsvideo = pSiS->chtvlumabandwidthsvideo;
	  int mychtvlumaflickerfilter = pSiS->chtvlumaflickerfilter;
	  int mychtvchromabandwidth = pSiS->chtvchromabandwidth;
	  int mychtvchromaflickerfilter = pSiS->chtvchromaflickerfilter;
	  int mychtvcvbscolor = pSiS->chtvcvbscolor;
	  int mychtvtextenhance = pSiS->chtvtextenhance;
	  int mychtvcontrast = pSiS->chtvcontrast;
	  int mytvxpos = pSiS->tvxpos;
	  int mytvypos = pSiS->tvypos;
#ifdef SISDUALHEAD
	  if(pSiSEnt && pSiS->DualHeadMode) {
	     mychtvlumabandwidthcvbs = pSiSEnt->chtvlumabandwidthcvbs;
	     mychtvlumabandwidthsvideo = pSiSEnt->chtvlumabandwidthsvideo;
	     mychtvlumaflickerfilter = pSiSEnt->chtvlumaflickerfilter;
	     mychtvchromabandwidth = pSiSEnt->chtvchromabandwidth;
	     mychtvchromaflickerfilter = pSiSEnt->chtvchromaflickerfilter;
	     mychtvcvbscolor = pSiSEnt->chtvcvbscolor;
	     mychtvtextenhance = pSiSEnt->chtvtextenhance;
	     mychtvcontrast = pSiSEnt->chtvcontrast;
	     mytvxpos = pSiSEnt->tvxpos;
	     mytvypos = pSiSEnt->tvypos;
	  }
#endif
	  if((val = mychtvlumabandwidthcvbs) != -1) {
	     SiS_SetCHTVlumabandwidthcvbs(pScrn, val);
	  }
	  if((val = mychtvlumabandwidthsvideo) != -1) {
	     SiS_SetCHTVlumabandwidthsvideo(pScrn, val);
	  }
	  if((val = mychtvlumaflickerfilter) != -1) {
	     SiS_SetCHTVlumaflickerfilter(pScrn, val);
	  }
	  if((val = mychtvchromabandwidth) != -1) {
	     SiS_SetCHTVchromabandwidth(pScrn, val);
	  }
	  if((val = mychtvchromaflickerfilter) != -1) {
	     SiS_SetCHTVchromaflickerfilter(pScrn, val);
	  }
	  if((val = mychtvcvbscolor) != -1) {
	     SiS_SetCHTVcvbscolor(pScrn, val);
	  }
	  if((val = mychtvtextenhance) != -1) {
	     SiS_SetCHTVtextenhance(pScrn, val);
	  }
	  if((val = mychtvcontrast) != -1) {
	     SiS_SetCHTVcontrast(pScrn, val);
	  }
	  /* Backup default TV position registers */
	  switch(pSiS->ChrontelType) {
	  case CHRONTEL_700x:
	     pSiS->tvx = SiS_GetCH700x(pSiS->SiS_Pr, 0x0a);
	     pSiS->tvx |= (((SiS_GetCH700x(pSiS->SiS_Pr, 0x08) & 0x02) >> 1) << 8);
	     pSiS->tvy = SiS_GetCH700x(pSiS->SiS_Pr, 0x0b);
	     pSiS->tvy |= ((SiS_GetCH700x(pSiS->SiS_Pr, 0x08) & 0x01) << 8);
#ifdef SISDUALHEAD
	     if(pSiSEnt) {
		pSiSEnt->tvx = pSiS->tvx;
		pSiSEnt->tvy = pSiS->tvy;
	     }
#endif
	     break;
	  case CHRONTEL_701x:
	     /* Not supported by hardware */
	     break;
	  }
	  if((val = mytvxpos) != 0) {
	     SiS_SetTVxposoffset(pScrn, val);
	  }
	  if((val = mytvypos) != 0) {
	     SiS_SetTVyposoffset(pScrn, val);
	  }
       }
       if(pSiS->VBFlags2 & VB2_301) {
          int mysistvedgeenhance = pSiS->sistvedgeenhance;
#ifdef SISDUALHEAD
          if(pSiSEnt && pSiS->DualHeadMode) {
	     mysistvedgeenhance = pSiSEnt->sistvedgeenhance;
	  }
#endif
          if((val = mysistvedgeenhance) != -1) {
	     SiS_SetSISTVedgeenhance(pScrn, val);
	  }
       }
       if(pSiS->VBFlags2 & VB2_SISBRIDGE) {
          int mysistvantiflicker = pSiS->sistvantiflicker;
	  int mysistvsaturation = pSiS->sistvsaturation;
	  int mysistvcolcalibf = pSiS->sistvcolcalibf;
	  int mysistvcolcalibc = pSiS->sistvcolcalibc;
	  int mysistvcfilter = pSiS->sistvcfilter;
	  int mysistvyfilter = pSiS->sistvyfilter;
	  int mytvxpos = pSiS->tvxpos;
	  int mytvypos = pSiS->tvypos;
	  int mytvxscale = pSiS->tvxscale;
	  int mytvyscale = pSiS->tvyscale;
	  int i;
	  ULong cbase;
	  UChar ctemp;
#ifdef SISDUALHEAD
          if(pSiSEnt && pSiS->DualHeadMode) {
	     mysistvantiflicker = pSiSEnt->sistvantiflicker;
	     mysistvsaturation = pSiSEnt->sistvsaturation;
	     mysistvcolcalibf = pSiSEnt->sistvcolcalibf;
	     mysistvcolcalibc = pSiSEnt->sistvcolcalibc;
	     mysistvcfilter = pSiSEnt->sistvcfilter;
	     mysistvyfilter = pSiSEnt->sistvyfilter;
	     mytvxpos = pSiSEnt->tvxpos;
	     mytvypos = pSiSEnt->tvypos;
	     mytvxscale = pSiSEnt->tvxscale;
	     mytvyscale = pSiSEnt->tvyscale;
	  }
#endif
          /* Backup default TV position, scale and colcalib registers */
	  inSISIDXREG(SISPART2,0x1f,pSiS->p2_1f);
	  inSISIDXREG(SISPART2,0x20,pSiS->p2_20);
	  inSISIDXREG(SISPART2,0x2b,pSiS->p2_2b);
	  inSISIDXREG(SISPART2,0x42,pSiS->p2_42);
	  inSISIDXREG(SISPART2,0x43,pSiS->p2_43);
	  inSISIDXREG(SISPART2,0x01,pSiS->p2_01);
	  inSISIDXREG(SISPART2,0x02,pSiS->p2_02);
	  inSISIDXREG(SISPART2,0x44,pSiS->p2_44);
	  inSISIDXREG(SISPART2,0x45,pSiS->p2_45);
	  if(!(pSiS->VBFlags2 & VB2_301)) {
	     inSISIDXREG(SISPART2,0x46,pSiS->p2_46);
	  } else {
	     pSiS->p2_46 = 0;
	  }
	  inSISIDXREG(SISPART2,0x0a,pSiS->p2_0a);
	  inSISIDXREG(SISPART2,0x31,cbase);
	  cbase = (cbase & 0x7f) << 8;
	  inSISIDXREG(SISPART2,0x32,ctemp);
	  cbase = (cbase | ctemp) << 8;
	  inSISIDXREG(SISPART2,0x33,ctemp);
	  cbase = (cbase | ctemp) << 8;
	  inSISIDXREG(SISPART2,0x34,ctemp);
	  pSiS->sistvccbase = (cbase | ctemp);
	  inSISIDXREG(SISPART2,0x35,pSiS->p2_35);
	  inSISIDXREG(SISPART2,0x36,pSiS->p2_36);
	  inSISIDXREG(SISPART2,0x37,pSiS->p2_37);
	  inSISIDXREG(SISPART2,0x38,pSiS->p2_38);
	  if(!(pSiS->VBFlags2 & VB2_301)) {
	     inSISIDXREG(SISPART2,0x47,pSiS->p2_47);
	     inSISIDXREG(SISPART2,0x48,pSiS->p2_48);
	     inSISIDXREG(SISPART2,0x49,pSiS->p2_49);
	     inSISIDXREG(SISPART2,0x4a,pSiS->p2_4a);
	  }
	  inSISIDXREG(SISPART2,0x2f,pSiS->p2_2f);
	  inSISIDXREG(SISPART2,0x30,pSiS->p2_30);
	  for(i=0; i<9; i++) {
	     inSISIDXREG(SISPART1,SiSScalingP1Regs[i],pSiS->scalingp1[i]);
	  }
	  for(i=0; i<9; i++) {
	     inSISIDXREG(SISPART4,SiSScalingP4Regs[i],pSiS->scalingp4[i]);
	  }
	  if(pSiS->VBFlags2 & VB2_SISTAP4SCALER) {
	     for(i=0; i<64; i++) {
	        inSISIDXREG(SISPART2,(0xc0 + i),pSiS->scalingp2[i]);
  	     }
	  }
#ifdef SISDUALHEAD
	  if(pSiSEnt) {
	     pSiSEnt->p2_1f = pSiS->p2_1f; pSiSEnt->p2_20 = pSiS->p2_20;
	     pSiSEnt->p2_42 = pSiS->p2_42; pSiSEnt->p2_43 = pSiS->p2_43;
	     pSiSEnt->p2_2b = pSiS->p2_2b;
	     pSiSEnt->p2_01 = pSiS->p2_01; pSiSEnt->p2_02 = pSiS->p2_02;
	     pSiSEnt->p2_44 = pSiS->p2_44; pSiSEnt->p2_45 = pSiS->p2_45;
	     pSiSEnt->p2_46 = pSiS->p2_46; pSiSEnt->p2_0a = pSiS->p2_0a;
	     pSiSEnt->sistvccbase = pSiS->sistvccbase;
	     pSiSEnt->p2_35 = pSiS->p2_35; pSiSEnt->p2_36 = pSiS->p2_36;
	     pSiSEnt->p2_37 = pSiS->p2_37; pSiSEnt->p2_38 = pSiS->p2_38;
	     pSiSEnt->p2_48 = pSiS->p2_48; pSiSEnt->p2_49 = pSiS->p2_49;
	     pSiSEnt->p2_4a = pSiS->p2_4a; pSiSEnt->p2_2f = pSiS->p2_2f;
	     pSiSEnt->p2_30 = pSiS->p2_30; pSiSEnt->p2_47 = pSiS->p2_47;
	     for(i=0; i<9; i++) {
	        pSiSEnt->scalingp1[i] = pSiS->scalingp1[i];
	     }
	     for(i=0; i<9; i++) {
	        pSiSEnt->scalingp4[i] = pSiS->scalingp4[i];
	     }
	     if(pSiS->VBFlags2 & VB2_SISTAP4SCALER) {
	        for(i=0; i<64; i++) {
	           pSiSEnt->scalingp2[i] = pSiS->scalingp2[i];
  	        }
	     }
	  }
#endif
          if((val = mysistvantiflicker) != -1) {
	     SiS_SetSISTVantiflicker(pScrn, val);
	  }
	  if((val = mysistvsaturation) != -1) {
	     SiS_SetSISTVsaturation(pScrn, val);
	  }
	  if((val = mysistvcfilter) != -1) {
	     SiS_SetSISTVcfilter(pScrn, val);
	  }
	  if((val = mysistvyfilter) != 1) {
	     SiS_SetSISTVyfilter(pScrn, val);
	  }
	  if((val = mysistvcolcalibc) != 0) {
	     SiS_SetSISTVcolcalib(pScrn, val, TRUE);
	  }
	  if((val = mysistvcolcalibf) != 0) {
	     SiS_SetSISTVcolcalib(pScrn, val, FALSE);
	  }
	  if((val = mytvxpos) != 0) {
	     SiS_SetTVxposoffset(pScrn, val);
	  }
	  if((val = mytvypos) != 0) {
	     SiS_SetTVyposoffset(pScrn, val);
	  }
	  if((val = mytvxscale) != 0) {
	     SiS_SetTVxscale(pScrn, val);
	  }
	  if((val = mytvyscale) != 0) {
	     SiS_SetTVyscale(pScrn, val);
	  }
       }
    }
}



