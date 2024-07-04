/* $XFree86$ */
/* $XdotOrg$ */
/*
 * SiS driver MergedFB code
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
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "sis.h"

#ifdef SISMERGED

#include "dixstruct.h"
#include "globals.h"

#ifdef SISXINERAMA
#include "resource.h"
#include "windowstr.h"
#endif

#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) >= 5
#include <inputstr.h> /* for inputInfo */
#endif

/*
 * LookupWindow was removed with video abi 11.
 */
#if (GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 4)
#ifndef DixGetAttrAccess
#define DixGetAttrAccess (1<<4)
#endif
#endif

#if (GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 2)
static inline int
dixLookupWindow(WindowPtr *pWin, XID id, ClientPtr client, Mask access)
{
       *pWin = LookupWindow(id, client);
       if (!*pWin)
       return BadWindow;
       return Success;
}
#endif

void		SiSMFBInitMergedFB(ScrnInfoPtr pScrn);
void		SiSMFBHandleModesCRT2(ScrnInfoPtr pScrn, ClockRangePtr clockRanges);
void		SiSMFBMakeModeList(ScrnInfoPtr pScrn);
void		SiSMFBCorrectVirtualAndLayout(ScrnInfoPtr pScrn);
void		SiSMFBSetDpi(ScrnInfoPtr pScrn1, ScrnInfoPtr pScrn2, SiSScrn2Rel srel);
void		SISMFBPointerMoved(int scrnIndex, int x, int y);
void		SISMFBAdjustFrame(int scrnIndex, int x, int y, int flags);

Bool		SiSMFBRebuildModelist(ScrnInfoPtr pScrn, ClockRangePtr clockRanges);
Bool		SiSMFBRevalidateModelist(ScrnInfoPtr pScrn, ClockRangePtr clockRanges);

#ifdef SISXINERAMA
void		SiSXineramaExtensionInit(ScrnInfoPtr pScrn);

Bool 			SiSnoPanoramiXExtension = TRUE;
static int		SiSXineramaNumScreens = 0;
static SiSXineramaData	*SiSXineramadataPtr = NULL;
static int		SiSXineramaGeneration;
#endif

extern void		SISErrorLog(ScrnInfoPtr pScrn, const char *format, ...);
extern int		SiSMemBandWidth(ScrnInfoPtr pScrn, Bool IsForCRT2, Bool quiet);
extern DisplayModePtr	SiSDuplicateMode(DisplayModePtr source);
extern xf86MonPtr	SiSInternalDDC(ScrnInfoPtr pScrn, int crtno);
extern float		SiSCalcVRate(DisplayModePtr mode);
extern void		SiSFindAspect(ScrnInfoPtr pScrn, xf86MonPtr pMonitor, int crtnum,
				Bool quiet);
extern Bool		SiSMakeOwnModeList(ScrnInfoPtr pScrn, Bool acceptcustommodes,
				Bool includelcdmodes, Bool isfordvi, Bool *havecustommodes,
				Bool fakecrt2modes, Bool IsForCRT2);
extern int		SiSRemoveUnsuitableModes(ScrnInfoPtr pScrn, DisplayModePtr initial,
				const char *reason, Bool quiet);
extern Bool		SiSFixupHVRanges(ScrnInfoPtr pScrn, int mfbcrt, Bool quiet);
extern void		SiSClearModesPrivate(DisplayModePtr modelist);
extern void		SiSPrintModes(ScrnInfoPtr pScrn, Bool printfreq);
extern void		SISAdjustFrameHW_CRT1(ScrnInfoPtr pScrn, int x, int y);
extern void		SISAdjustFrameHW_CRT2(ScrnInfoPtr pScrn, int x, int y);

#define SISSWAP(x, y) {		\
	int temp = x;		\
	x = y;			\
	y = temp;		\
	}

/* Helper function for CRT2 monitor vrefresh/hsync options
 * (Code based on code from mga driver)
 */
static int
SiSStrToRanges(range *r, char *s, int max)
{
   float num = 0.0;
   int rangenum = 0;
   Bool gotdash = FALSE;
   Bool nextdash = FALSE;
   char *strnum = NULL;
   do {
      switch(*s) {
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
      case '.':
         if(strnum == NULL) {
            strnum = s;
            gotdash = nextdash;
            nextdash = FALSE;
         }
         break;
      case '-':
      case ' ':
      case 0:
         if(strnum == NULL) break;
         sscanf(strnum, "%f", &num);
	 strnum = NULL;
         if(gotdash) {
            r[rangenum - 1].hi = num;
         } else {
            r[rangenum].lo = num;
            r[rangenum].hi = num;
            rangenum++;
         }
         if(*s == '-') nextdash = (rangenum != 0);
	 else if(rangenum >= max) return rangenum;
         break;
      default:
         return 0;
      }

   } while(*(s++) != 0);

   return rangenum;
}

/* Copy and link two modes (i, j) for mergedfb mode
 * (Code formerly based on code from mga driver)
 *
 * - Copy mode i, merge j to copy of i, link the result to dest
 * - Link i and j in private record.
 * - If dest is NULL, return value is copy of i linked to itself.
 * - For mergedfb auto-config, we only check the dimension
 *   against virtualX/Y, if they were user-provided.
 * - No special treatment required for CRTxxOffs.
 * - Provide fake dotclock in order to distinguish between similar
 *   looking MetaModes (for RandR and VidMode extensions)
 * - Set unique VRefresh of dest mode for RandR
 */
static DisplayModePtr
SiSCopyModeNLink(ScrnInfoPtr pScrn, DisplayModePtr dest,
		 DisplayModePtr i, DisplayModePtr j,
		 SiSScrn2Rel srel, Bool quiet)
{
    SISPtr pSiS = SISPTR(pScrn);
    DisplayModePtr mode;
    int dx = 0, dy = 0;
    char namebuffer[32], namebuf1[64], namebuf2[64];
    char printbuffer[256];

    if(!((mode = malloc(sizeof(DisplayModeRec)))))
       return dest;

    memcpy(mode, i, sizeof(DisplayModeRec));

    if(!((mode->Private = malloc(sizeof(SiSMergedDisplayModeRec))))) {
       free(mode);
       return dest;
    }

    ((SiSMergedDisplayModePtr)mode->Private)->CRT1 = i;
    ((SiSMergedDisplayModePtr)mode->Private)->CRT2 = j;
    ((SiSMergedDisplayModePtr)mode->Private)->CRT2Position = srel;

    mode->PrivSize = 0;

    switch(srel) {
    case sisLeftOf:
    case sisRightOf:
       if(!(pScrn->display->virtualX)) {
          dx = i->HDisplay + j->HDisplay;
       } else {
          dx = min(pScrn->virtualX, i->HDisplay + j->HDisplay);
       }
       dx -= mode->HDisplay;
       if(!(pScrn->display->virtualY)) {
          dy = max(i->VDisplay, j->VDisplay);
       } else {
          dy = min(pScrn->virtualY, max(i->VDisplay, j->VDisplay));
       }
       dy -= mode->VDisplay;
       break;
    case sisAbove:
    case sisBelow:
       if(!(pScrn->display->virtualY)) {
          dy = i->VDisplay + j->VDisplay;
       } else {
          dy = min(pScrn->virtualY, i->VDisplay + j->VDisplay);
       }
       dy -= mode->VDisplay;
       if(!(pScrn->display->virtualX)) {
          dx = max(i->HDisplay, j->HDisplay);
       } else {
          dx = min(pScrn->virtualX, max(i->HDisplay, j->HDisplay));
       }
       dx -= mode->HDisplay;
       break;
    case sisClone:
       if(!(pScrn->display->virtualX)) {
          dx = max(i->HDisplay, j->HDisplay);
       } else {
          dx = min(pScrn->virtualX, max(i->HDisplay, j->HDisplay));
       }
       dx -= mode->HDisplay;
       if(!(pScrn->display->virtualY)) {
          dy = max(i->VDisplay, j->VDisplay);
       } else {
	  dy = min(pScrn->virtualY, max(i->VDisplay, j->VDisplay));
       }
       dy -= mode->VDisplay;
       break;
    }
    mode->HDisplay += dx;
    mode->HSyncStart += dx;
    mode->HSyncEnd += dx;
    mode->HTotal += dx;
    mode->VDisplay += dy;
    mode->VSyncStart += dy;
    mode->VSyncEnd += dy;
    mode->VTotal += dy;

    mode->type = M_T_DEFAULT;
#ifdef M_T_USERDEF
    /* Set up as user defined (ie fake that the mode has been named in the
     * Modes-list in the screen section; corrects cycling with CTRL-ALT-[-+]
     * when source mode has not been listed there.)
     */
    mode->type |= M_T_USERDEF;
#endif

    /* Set the VRefresh field (in order to make RandR use it for the rates). We
     * simply set this to the refresh rate for the CRT1 mode (since CRT2 will
     * mostly be LCD or TV anyway).
     */
    mode->VRefresh = SiSCalcVRate(i);

    if( ((mode->HDisplay * (pScrn->bitsPerPixel >> 3) * mode->VDisplay) > pSiS->maxxfbmem) ||
	(mode->HDisplay > 4088) ||
	(mode->VDisplay > 4096) ) {

       if(!quiet) {
	  xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		"Skipped \"%s\" (%dx%d), not enough video RAM or beyond hardware specs\n",
		mode->name, mode->HDisplay, mode->VDisplay);
       }
       free(mode->Private);
       free(mode);

       return dest;
    }

    /* Find out whether we have at least one non-clone mode
     * (in order to find out whether we enable pseudo-xinerama)
     * and whether we have at least one mode which is not of
     * "!" notation, ie follows the given CRT2Position. We need
     * this for the Xinerama layout calculation.
     */
#ifdef SISXINERAMA
    if(srel != sisClone) {
       pSiS->AtLeastOneNonClone = TRUE;
    }
#endif

    /* Now see if the resulting mode would be discarded as a "size" by the
     * RandR extension and bump its clock in steps if 1000 in case it does.
     */
    if(dest) {
       DisplayModePtr t = dest->next; /* Start with first mode (dest is last) */
       do {
          if((t->HDisplay == mode->HDisplay) &&
	     (t->VDisplay == mode->VDisplay) &&
	     ((int)(t->VRefresh + .5) == (int)(mode->VRefresh + .5))) {
	     mode->VRefresh += 1000.0;
	  }
	  t = t->next;
       } while((t) && (t != dest->next));
    }

    /* Provide a fake but unique DotClock in order to trick the vidmode
     * extension to allow selecting among a number of modes whose merged result
     * looks identical but consists of different modes for CRT1 and CRT2
     */
    mode->Clock = (int)(mode->VRefresh * 1000.0);

    /* Generate a mode name */
    sprintf(namebuffer, "%dx%d", mode->HDisplay, mode->VDisplay);
    if((mode->name = malloc(strlen(namebuffer) + 1))) {
       strcpy(mode->name, namebuffer);
    }

    if(!quiet) {
       Bool printname1 = TRUE, printname2 = TRUE;

       sprintf(printbuffer, (srel == sisClone) ? "Cloned " : "Merged ");

       sprintf(namebuf1, "%dx%d", i->HDisplay, i->VDisplay);
       if((strcmp(namebuf1, i->name) == 0) ||
          (strlen(i->name) > 90))
          printname1 = FALSE;

       sprintf(namebuf2, "%dx%d", j->HDisplay, j->VDisplay);
       if((strcmp(namebuf2, j->name) == 0) ||
          (strlen(j->name) > 90))
          printname2 = FALSE;

       if(printname1) {
          strcat(printbuffer, "\"");
          strcat(printbuffer, i->name);
          strcat(printbuffer, "\" (");
       }

       strcat(printbuffer, namebuf1);

       if(printname1) {
          strcat(printbuffer, ")");
       }

       strcat(printbuffer, " and ");

       if(printname2) {
          strcat(printbuffer, "\"");
          strcat(printbuffer, j->name);
          strcat(printbuffer, "\" (");
       }

       strcat(printbuffer, namebuf2);

       if(printname2) {
          strcat(printbuffer, ")");
       }

       strcat(printbuffer, " to ");

       sprintf(namebuf1, "%dx%d (%d)\n", mode->HDisplay, mode->VDisplay, (int)mode->VRefresh);

       strcat(printbuffer, namebuf1);

       xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s", printbuffer);
    }

    mode->next = mode;
    mode->prev = mode;

    if(dest) {
       mode->next = dest->next; 	/* Insert node after "dest" */
       dest->next->prev = mode;
       mode->prev = dest;
       dest->next = mode;
    }

    return mode;
}

/* Helper function to find a mode from a given name
 * (Code base taken from mga driver)
 */
static DisplayModePtr
SiSGetModeFromName(char* str, DisplayModePtr i)
{
    DisplayModePtr c = i;

    if(!i)
       return NULL;

    do {
       if(strcmp(str, c->name) == 0) return c;
       c = c->next;
    } while(c && c != i);

    return NULL;
}

static DisplayModePtr
SiSFindWidestTallestMode(DisplayModePtr i, Bool tallest)
{
    DisplayModePtr c = i, d = NULL;
    int max = 0;

    if(!i)
       return NULL;

    do {
       if(tallest) {
          if(c->VDisplay > max) {
	     max = c->VDisplay;
	     d = c;
          }
       } else {
          if(c->HDisplay > max) {
	     max = c->HDisplay;
	     d = c;
          }
       }
       c = c->next;
    } while(c != i);

    return d;
}

static void
SiSFindWidestTallestCommonMode(DisplayModePtr i, DisplayModePtr j, Bool tallest,
				DisplayModePtr *a, DisplayModePtr *b)
{
    DisplayModePtr c = i, d;
    int max = 0;
    Bool foundone;

    (*a) = (*b) = NULL;

    if(!i || !j)
       return;

    do {
       d = j;
       foundone = FALSE;
       do {
	  if( (c->HDisplay == d->HDisplay) &&
	      (c->VDisplay == d->VDisplay) ) {
	     foundone = TRUE;
	     break;
	  }
	  d = d->next;
       } while(d != j);
       if(foundone) {
	  if(tallest) {
	     if(c->VDisplay > max) {
		max = c->VDisplay;
		(*a) = c;
		(*b) = d;
	     }
	  } else {
	     if(c->HDisplay > max) {
		max = c->HDisplay;
		(*a) = c;
		(*b) = d;
	     }
	  }
       }
       c = c->next;
    } while(c != i);
}

static DisplayModePtr
SiSGenerateModeListFromLargestModes(ScrnInfoPtr pScrn,
		    DisplayModePtr i, DisplayModePtr j,
		    SiSScrn2Rel srel, Bool quiet)
{
#ifdef SISXINERAMA
    SISPtr pSiS = SISPTR(pScrn);
#endif
    DisplayModePtr mode1 = NULL;
    DisplayModePtr mode2 = NULL;
    DisplayModePtr mode3 = NULL;
    DisplayModePtr mode4 = NULL;
    DisplayModePtr result = NULL;

#ifdef SISXINERAMA
    pSiS->AtLeastOneNonClone = FALSE;
#endif

    /* Now build a default list of MetaModes.
     * - Non-clone: If the user enabled NonRectangular, we use the
     * largest mode for each CRT1 and CRT2. If not, we use the largest
     * common mode for CRT1 and CRT2 (if available). Additionally, and
     * regardless if the above, we produce a clone mode consisting of
     * the largest common mode (if available) in order to use DGA.
     * - Clone: If the (global) CRT2Position is Clone, we use the
     * largest common mode if available, otherwise the first two modes
     * in each list.
     */

    switch(srel) {
    case sisLeftOf:
    case sisRightOf:
       mode1 = SiSFindWidestTallestMode(i, FALSE);
       mode2 = SiSFindWidestTallestMode(j, FALSE);
       SiSFindWidestTallestCommonMode(i, j, FALSE, &mode3, &mode4);
       break;
    case sisAbove:
    case sisBelow:
       mode1 = SiSFindWidestTallestMode(i, TRUE);
       mode2 = SiSFindWidestTallestMode(j, TRUE);
       SiSFindWidestTallestCommonMode(i, j, TRUE, &mode3, &mode4);
       break;
    case sisClone:
       SiSFindWidestTallestCommonMode(i, j, FALSE, &mode3, &mode4);
       if(mode3 && mode4) {
	  mode1 = mode3;
	  mode2 = mode4;
       } else {
	  mode1 = i;
	  mode2 = j;
       }
    }

    if(srel != sisClone) {
       if(mode3 && mode4 && !pSiS->NonRect) {
	  mode1 = mode3;
	  mode2 = mode2;
       }
    }

    if(mode1 && mode2) {
       result = SiSCopyModeNLink(pScrn, result, mode1, mode2, srel, quiet);
    }

    if(srel != sisClone) {
       if(mode3 && mode4) {
	  result = SiSCopyModeNLink(pScrn, result, mode3, mode4, sisClone, quiet);
       }
    }

    return result;
}

/* Generate the merged-fb modelist from given metamodes
 */
static void
SiSMetaModeParseError(ScrnInfoPtr pScrn, char *src, char *curr, char *lastcurr, Bool quiet)
{
    if(!quiet) {
       char backup = *curr;
       *curr = 0;
       xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		"Error parsing Metamodes at character no %d (near or in \"%s\")\n",
		curr - src,
		lastcurr);
       *curr = backup;
    }
}


static void
SiSMetaModeBad(ScrnInfoPtr pScrn, int crtnum, char *modename,
			char *metaname, char *metaend, Bool quiet)
{
    char backup;

    if(!quiet) {
       while((metaend != metaname) && (*metaend == ' ' || *metaend == ';')) {
          metaend--;
       }
       metaend++;
       backup = *metaend;
       *metaend = 0;
       xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		"\"%s\" is not a supported mode for CRT%d, skipping \"%s\"\n",
		modename, crtnum, metaname);
       *metaend = backup;
    }
}

static void
SiSRemoveTrailingSpace(char *string)
{
   int idx = strlen(string);

   while(idx) {
      if(string[idx] == ' ') string[idx] = 0;
      idx--;
   };
}

static DisplayModePtr
SiSParseMetaModes(ScrnInfoPtr pScrn, char *src,
			DisplayModePtr i, DisplayModePtr j,
			SiSScrn2Rel srel, Bool quiet)
{
#ifdef SISXINERAMA
    SISPtr pSiS = SISPTR(pScrn);
#endif
    char *curr, *currend, *first, *second, *lastcurr;
    int len;
    SiSScrn2Rel connect;
    Bool isend = FALSE;
    char dstcrt1[256], dstcrt2[256];
    DisplayModePtr mode1, mode2 = NULL, result = NULL;

#ifdef SISXINERAMA
    pSiS->AtLeastOneNonClone = FALSE;
#endif

    curr = src;

    do {

       first = dstcrt1;
       second = dstcrt2;
       connect = srel;

       while(*curr == ' ' || 				/* remove leading spaces and other garbage */
	     *curr == ';' ||
	     *curr == ',' ||
	     *curr == '-' ||
	     *curr == '+') curr++;

       lastcurr = curr;

       if(*curr == 0) break;


       currend = strpbrk(curr, " -,+;");		/* Find delimiter. If delimiting char */
       if(!currend) {					/* not found, assume end of string as */
	  currend = curr;				/* delimiter. */
	  while(*currend) currend++;
	  isend = TRUE;
       }


       if((currend - curr) == 0) {			/* Must have at least one mode here */
	  SiSMetaModeParseError(pScrn, src, currend, lastcurr, quiet);
	  break;
       }

       len = min((currend - curr), 255);
       strncpy(first, curr, len);			/* Copy first mode name */
       first[len] = 0;
       curr = currend;

       if(isend) {					/* If no more coming, this is a clone mode */

	  strcpy(second, first);
	  connect = sisClone;

       } else {						/* Otherwise check what's coming next: */

	  while(*curr == ' ') curr++;			/* Check the following non-space char */

	  if(*curr == '-' ||
	     *curr == '+' ||
	     *curr == ',') {				/* If it's a mode delimiter, so be it: */

	     if(*curr == '+') {
		connect = sisClone;
	     }
	     curr++;
	     while(*curr == ' ') curr++;		/*    Skip leading spaces */

	     currend = strpbrk(curr, " ;");		/*    Find head delimiter. If delimiting char */
	     if(!currend) {				/*    not found, assume end of string as */
		currend = curr;				/*    delimiter. */
		while(*currend) currend++;
		isend = TRUE;
	     }

	     if((currend - curr) == 0) {

		SiSMetaModeParseError(pScrn, src, currend, lastcurr, quiet);

		strcpy(second, first);
		connect = sisClone;

	     } else {

		len = min((currend - curr), 255);
		strncpy(second, curr, len);		/*    Copy second mode name */
		second[len] = 0;

	     }

	  } else {					/* Otherwise, it's a clone mode */

	     currend = curr - 1;

	     strcpy(second, first);
	     connect = sisClone;

	  }

       }

       curr = currend;

       SiSRemoveTrailingSpace(first);
       SiSRemoveTrailingSpace(second);

       mode1 = SiSGetModeFromName(dstcrt1, i);
       if(!mode1) {
          SiSMetaModeBad(pScrn, 1, dstcrt1, lastcurr, currend, quiet);
       } else {
	  mode2 = SiSGetModeFromName(dstcrt2, j);
          if(!mode2 && !quiet) {
             SiSMetaModeBad(pScrn, 2, dstcrt2, lastcurr, currend, quiet);
	  }
       }

       if(mode1 && mode2) {
	  result = SiSCopyModeNLink(pScrn, result, mode1, mode2, connect, quiet);
       }

    } while (!isend);

    return result;
}

static DisplayModePtr
SiSGenerateModeList(ScrnInfoPtr pScrn, char* str,
		    DisplayModePtr i, DisplayModePtr j,
		    SiSScrn2Rel srel, Bool quiet)
{
   SISPtr pSiS = SISPTR(pScrn);
   DisplayModePtr result;

   if((str != NULL) &&
      (result = SiSParseMetaModes(pScrn, str, i, j, srel, quiet))) {
      return result;
   } else {
      if(!quiet) {
         xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	   "%s, linking %s modes by default\n",
	   (str != NULL) ? "Bad MetaModes" : "No MetaModes given",
	   (srel == sisClone) ? "largest common" :
	      (pSiS->NonRect ?
		(((srel == sisLeftOf) || (srel == sisRightOf)) ? "widest" :  "tallest")
		:
		(((srel == sisLeftOf) || (srel == sisRightOf)) ? "widest common" :  "tallest common")) );
      }
      return SiSGenerateModeListFromLargestModes(pScrn, i, j, srel, quiet);
   }
}

static void
SiSRecalcDefaultVirtualSize(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
    DisplayModePtr mode, bmode;
    int maxh, maxv;
    static const char *str = "MergedFB: Virtual %s %d\n";
    static const char *errstr = "Virtual %s to small for given CRT2Position offset\n";

    mode = bmode = pScrn->modes;
    maxh = maxv = 0;
    do {
       if(mode->HDisplay > maxh) maxh = mode->HDisplay;
       if(mode->VDisplay > maxv) maxv = mode->VDisplay;
       mode = mode->next;
    } while(mode != bmode);

    maxh += pSiS->CRT1XOffs + pSiS->CRT2XOffs;
    maxv += pSiS->CRT1YOffs + pSiS->CRT2YOffs;

    if(!(pScrn->display->virtualX)) {
       if(maxh > 4088) {
	  xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		"Virtual width with CRT2Position offset beyond hardware specs\n");
	  pSiS->CRT1XOffs = pSiS->CRT2XOffs = 0;
	  maxh -= (pSiS->CRT1XOffs + pSiS->CRT2XOffs);
       }
       pScrn->virtualX = maxh;
       pScrn->displayWidth = maxh;
       xf86DrvMsg(pScrn->scrnIndex, X_PROBED, str, "width", maxh);
    } else {
       if(maxh < pScrn->display->virtualX) {
	  xf86DrvMsg(pScrn->scrnIndex, X_ERROR, errstr, "width");
	  pSiS->CRT1XOffs = pSiS->CRT2XOffs = 0;
       }
    }

    if(!(pScrn->display->virtualY)) {
       pScrn->virtualY = maxv;
       xf86DrvMsg(pScrn->scrnIndex, X_PROBED, str, "height", maxv);
    } else {
       if(maxv < pScrn->display->virtualY) {
	  xf86DrvMsg(pScrn->scrnIndex, X_ERROR, errstr, "height");
	  pSiS->CRT1YOffs = pSiS->CRT2YOffs = 0;
       }
    }
}

static void
SiSMFBHandleCRT2DDCAndRanges(ScrnInfoPtr pScrn, Bool quiet)
{
    SISPtr pSiS = SISPTR(pScrn);
    xf86MonPtr pMonitor = NULL;
    static const char *ddcsstr = "CRT%d DDC monitor info: *******************************************\n";
    static const char *ddcestr = "End of CRT%d DDC monitor info *************************************\n";

    if(pSiS->CRT2HSync) {
       pSiS->CRT2pScrn->monitor->nHsync =
		SiSStrToRanges(pSiS->CRT2pScrn->monitor->hsync, pSiS->CRT2HSync, MAX_HSYNC);
    }

    if(pSiS->CRT2VRefresh) {
       pSiS->CRT2pScrn->monitor->nVrefresh =
		SiSStrToRanges(pSiS->CRT2pScrn->monitor->vrefresh, pSiS->CRT2VRefresh, MAX_VREFRESH);
    }

    pSiS->CRT2pScrn->monitor->DDC = NULL;

    if((pMonitor = SiSInternalDDC(pSiS->CRT2pScrn, 1))) {

       if(!quiet) {
	  xf86DrvMsg(pScrn->scrnIndex, X_PROBED, ddcsstr, 2);
	  xf86PrintEDID(pMonitor);
       }

       pSiS->CRT2pScrn->monitor->DDC = pMonitor;

       /* Now try to find out aspect ratio */
       SiSFindAspect(pScrn, pMonitor, 2, FALSE);

       if(!quiet) {
	  xf86DrvMsg(pScrn->scrnIndex, X_PROBED, ddcestr, 2);
       }

       /* Use DDC data if no ranges in config file */
       if(!pSiS->CRT2HSync) {
	  pSiS->CRT2pScrn->monitor->nHsync = 0;
       }
       if(!pSiS->CRT2VRefresh) {
	  pSiS->CRT2pScrn->monitor->nVrefresh = 0;
       }

    } else if(!quiet) {

       xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		"Failed to read DDC data for CRT2\n");

    }
}

void
SiSMFBInitMergedFB(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);
    static const char *crt2monname = "CRT2";

    if(pSiS->MergedFB) {

       pSiS->CRT2pScrn->monitor = malloc(sizeof(MonRec));

       if(pSiS->CRT2pScrn->monitor) {

	  DisplayModePtr tempm = NULL, currentm = NULL, newm = NULL;

	  /* Make a copy of CRT1's monitor, but clear DDC */
	  memcpy(pSiS->CRT2pScrn->monitor, pScrn->monitor, sizeof(MonRec));
	  pSiS->CRT2pScrn->monitor->id = (char *)crt2monname;
	  pSiS->CRT2pScrn->monitor->DDC = NULL;

	  /* Copy CRT1's monitor->modes. This is only done
	   * to copy over the user-provided modelines and
	   * for the case that the internal modes shall not
	   * be used. (Usually, the default modes will be
	   * replaced by our own later.)
	   */
	  pSiS->CRT2pScrn->monitor->Modes = NULL;
	  tempm = pScrn->monitor->Modes;
	  while(tempm) {
	     if(!(newm = SiSDuplicateMode(tempm)))
	        break;

	     if(!pSiS->CRT2pScrn->monitor->Modes)
		pSiS->CRT2pScrn->monitor->Modes = newm;

	     if(currentm) {
		currentm->next = newm;
		newm->prev = currentm;
	     }
	     currentm = newm;
	     tempm = tempm->next;
	  }

	  /* Read DDC and set up horizsync/vrefresh ranges */
	  SiSMFBHandleCRT2DDCAndRanges(pScrn, FALSE);

       } else {

	  SISErrorLog(pScrn, "Failed to allocate memory for CRT2 monitor, MergedFB mode disabled.\n");
	  free(pSiS->CRT2pScrn);
	  pSiS->CRT2pScrn = NULL;
	  pSiS->MergedFB = FALSE;

       }
    }
}

static void
SiSFreeCRT2Structs(SISPtr pSiS)
{
    if(pSiS->CRT2pScrn) {
       if(pSiS->CRT2pScrn->modes) {
	  while(pSiS->CRT2pScrn->modes)
	     xf86DeleteMode(&pSiS->CRT2pScrn->modes, pSiS->CRT2pScrn->modes);
       }
       if(pSiS->CRT2pScrn->monitor) {
	  if(pSiS->CRT2pScrn->monitor->Modes) {
	     while(pSiS->CRT2pScrn->monitor->Modes)
		xf86DeleteMode(&pSiS->CRT2pScrn->monitor->Modes, pSiS->CRT2pScrn->monitor->Modes);
	  }
	  pSiS->CRT2pScrn->monitor->DDC = NULL;
	  free(pSiS->CRT2pScrn->monitor);
       }
       free(pSiS->CRT2pScrn);
       pSiS->CRT2pScrn = NULL;
    }
}

static void
SiSSetupModeListParmsCRT2(SISPtr pSiS, unsigned int VBFlags, unsigned int VBFlags3,
		Bool *acceptcustommodes, Bool *includelcdmodes, Bool *isfordvi,
		Bool *fakecrt2modes)
{
    (*acceptcustommodes) = TRUE;
    (*includelcdmodes)   = TRUE;
    (*isfordvi)          = FALSE;
    (*fakecrt2modes)     = FALSE;

    if(pSiS->VBFlags2 & VB2_SISTMDSBRIDGE) {
       if(!(pSiS->VBFlags2 & VB2_30xBDH)) {
          if(!(VBFlags & (CRT2_LCD|CRT2_VGA))) (*includelcdmodes) = FALSE;
	  if(VBFlags & CRT2_LCD)               (*isfordvi)        = TRUE;
	  /* See above for a remark on handling CRT2 = TV */
       } else {
	  if(VBFlags & (CRT2_LCD|CRT2_TV)) {
	     (*includelcdmodes)   = FALSE;
	     (*acceptcustommodes) = FALSE;
	     (*fakecrt2modes)     = TRUE;
	  }
       }
    } else {
       (*includelcdmodes)   = FALSE;
       (*acceptcustommodes) = FALSE;
       if(VBFlags & (CRT2_LCD|CRT2_TV)) {
          (*fakecrt2modes)  = TRUE;
       }
    }
}

static void
SiSSetupClockRangesCRT2(ScrnInfoPtr pScrn, ClockRangePtr clockRanges, Bool quiet)
{
    SISPtr pSiS = SISPTR(pScrn);

    clockRanges->next = NULL;
    clockRanges->minClock = pSiS->MinClock;
    clockRanges->maxClock = SiSMemBandWidth(pSiS->CRT2pScrn, TRUE, quiet);
    clockRanges->clockIndex = -1;
    clockRanges->interlaceAllowed = FALSE;
    clockRanges->doubleScanAllowed = FALSE;
    if(pSiS->VGAEngine == SIS_315_VGA) {
       clockRanges->doubleScanAllowed = TRUE;
    }
}

void
SiSMFBHandleModesCRT2(ScrnInfoPtr pScrn, ClockRangePtr clockRanges)
{
    SISPtr pSiS = SISPTR(pScrn);
    Bool acceptcustommodes;
    Bool includelcdmodes;
    Bool isfordvi;
    Bool fakecrt2modes;
    int i;

    static const char *crtsetupstr = "*************************** CRT%d setup ***************************\n";
    static const char *modesforstr = "Modes for CRT%d: **************************************************\n";
    static const char *mergeddisstr = "MergedFB mode disabled";

    if(pSiS->MergedFB) {

       xf86DrvMsg(pScrn->scrnIndex, X_INFO, crtsetupstr, 2);

       SiSSetupClockRangesCRT2(pScrn, clockRanges, FALSE);

       xf86DrvMsg(pScrn->scrnIndex, X_DEFAULT, "Min pixel clock for CRT2 is %d MHz\n",
                clockRanges->minClock / 1000);

       xf86DrvMsg(pScrn->scrnIndex, X_DEFAULT, "Max pixel clock for CRT2 is %d MHz\n",
                clockRanges->maxClock / 1000);

       SiSSetupModeListParmsCRT2(pSiS, pSiS->VBFlags, pSiS->VBFlags3,
			&acceptcustommodes, &includelcdmodes, &isfordvi,
			&fakecrt2modes);

       pSiS->HaveCustomModes2 = FALSE;

       if((pSiS->VGAEngine != SIS_315_VGA) || (!(pSiS->VBFlags2 & VB2_SISVGA2BRIDGE))) {
          pSiS->SiS_Pr->SiS_UseWideCRT2 = 0;
       }

       if(!SiSMakeOwnModeList(pSiS->CRT2pScrn, acceptcustommodes, includelcdmodes,
				isfordvi, &pSiS->HaveCustomModes2, FALSE /* fakecrt2modes */, TRUE )) {

	  SISErrorLog(pScrn, "Building list of built-in modes for CRT2 failed, %s\n", mergeddisstr);
	  SiSFreeCRT2Structs(pSiS);
	  pSiS->MergedFB = FALSE;

       } else {

	  xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		 "Replaced %s mode list for CRT2 with built-in modes\n",
		 pSiS->HaveCustomModes2 ? "default" : "entire");

	  if((pSiS->VGAEngine == SIS_315_VGA) && (pSiS->VBFlags2 & VB2_SISVGA2BRIDGE)) {
	     xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		 "Using %s widescreen modes for CRT2 VGA devices\n",
		 pSiS->SiS_Pr->SiS_UseWideCRT2 ? "real" : "fake");
	  }

       }

    }

    if(pSiS->MergedFB) {

       pointer backupddc = pSiS->CRT2pScrn->monitor->DDC;

       /* Suppress bogus DDC warning */
       if(SiSFixupHVRanges(pSiS->CRT2pScrn, 2, FALSE)) {
          pSiS->CRT2pScrn->monitor->DDC = NULL;
       }

#if !defined(XORG_VERSION_CURRENT) && (XF86_VERSION_CURRENT >= XF86_VERSION_NUMERIC(4,5,0,0,0))
       /* XFree86 4.5+ thinks it's smart to automatically
        * add EDID modes to the monitor mode list - and
        * it even does this in *Validate*Modes()!
        * We do not like this if we discarded all default
        * and user modes because they aren't suppored. Hence,
        * we clear the DDC pointer in that case (and live
        * with the disadvantage that we don't get any
        * DDC warnings.)
        */
       if(!pSiS->HaveCustomModes2) {
          pScrn->monitor->DDC = NULL;
       }
#endif

       pSiS->CheckForCRT2 = TRUE;
       i = xf86ValidateModes(pSiS->CRT2pScrn, pSiS->CRT2pScrn->monitor->Modes,
			pSiS->CRT2pScrn->display->modes, clockRanges,
			NULL, 256, 4088,
			pSiS->CRT2pScrn->bitsPerPixel * 8, 128, 4096,
			pScrn->display->virtualX ? pScrn->virtualX : 0,
			pScrn->display->virtualY ? pScrn->virtualY : 0,
			pSiS->maxxfbmem,
			LOOKUP_BEST_REFRESH);
       pSiS->CheckForCRT2 = FALSE;

       pSiS->CRT2pScrn->monitor->DDC = backupddc;

       if(i == -1) {
	  SISErrorLog(pScrn, "xf86ValidateModes() error, %s\n", mergeddisstr);
	  SiSFreeCRT2Structs(pSiS);
	  pSiS->MergedFB = FALSE;
       }

    }

    if(pSiS->MergedFB) {

       SiSRemoveUnsuitableModes(pScrn, pSiS->CRT2pScrn->modes, "MergedFB", FALSE);

       xf86PruneDriverModes(pSiS->CRT2pScrn);

       if(i == 0 || pSiS->CRT2pScrn->modes == NULL) {
	  SISErrorLog(pScrn, "No valid modes found for CRT2; %s\n", mergeddisstr);
	  SiSFreeCRT2Structs(pSiS);
	  pSiS->MergedFB = FALSE;
       }

    }

    if(pSiS->MergedFB) {

       xf86SetCrtcForModes(pSiS->CRT2pScrn, INTERLACE_HALVE_V);

       /* Clear the modes' Private field */
       SiSClearModesPrivate(pSiS->CRT2pScrn->modes);

       xf86DrvMsg(pScrn->scrnIndex, X_INFO, modesforstr, 2);

       if(pSiS->VBFlags & (CRT2_LCD | CRT2_TV)) {
	  SiSPrintModes(pSiS->CRT2pScrn, (pSiS->VBFlags2 & VB2_SISVGA2BRIDGE) ? TRUE : FALSE);
       } else {
	  xf86PrintModes(pSiS->CRT2pScrn);
       }

    }
}

void
SiSMFBMakeModeList(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);

    if(pSiS->MergedFB) {

       pSiS->CRT1Modes = pScrn->modes;
       pSiS->CRT1CurrentMode = pScrn->currentMode;

       xf86DrvMsg(pScrn->scrnIndex, X_INFO, "MergedFB: Generating mode list\n");

       pScrn->modes = SiSGenerateModeList(pScrn, pSiS->MetaModes,
					  pSiS->CRT1Modes, pSiS->CRT2pScrn->modes,
					  pSiS->CRT2Position, FALSE);

       if(!pScrn->modes) {

	  SISErrorLog(pScrn, "Failed to parse MetaModes or no modes found. MergedFB mode disabled.\n");
	  SiSFreeCRT2Structs(pSiS);
	  pScrn->modes = pSiS->CRT1Modes;
	  pSiS->CRT1Modes = NULL;
	  pSiS->MergedFB = FALSE;

       }

    }
}


void
SiSMFBCorrectVirtualAndLayout(ScrnInfoPtr pScrn)
{
    SISPtr pSiS = SISPTR(pScrn);

    if(pSiS->MergedFB) {

       /* If no virtual dimension was given by the user,
	* calculate a sane one now. Adapts pScrn->virtualX,
	* pScrn->virtualY and pScrn->displayWidth.
	*/
       SiSRecalcDefaultVirtualSize(pScrn);

       /* We get the last from GenerateModeList(), skip to first */
       pScrn->modes = pScrn->modes->next;
       pScrn->currentMode = pScrn->modes;

       /* Update CurrentLayout */
       pSiS->CurrentLayout.mode = pScrn->currentMode;
       pSiS->CurrentLayout.displayWidth = pScrn->displayWidth;
       pSiS->CurrentLayout.displayHeight = pScrn->virtualY;

    }
}

static void
SiSMFBCalcDPI(ScrnInfoPtr pScrn1, ScrnInfoPtr pScrn2, SiSScrn2Rel srel, Bool quiet)
{
    SISPtr pSiS = SISPTR(pScrn1);
    MessageType from = X_DEFAULT;
    xf86MonPtr DDC1 = (xf86MonPtr)(pScrn1->monitor->DDC);
    xf86MonPtr DDC2 = (xf86MonPtr)(pScrn2->monitor->DDC);
    int ddcWidthmm = 0, ddcHeightmm = 0;
    const char *dsstr = "MergedFB: Display dimensions: %dx%d mm\n";

    /* This sets the DPI for MergedFB mode. The problem is that
     * this can never be exact, because the output devices may
     * have different dimensions. This function tries to compromise
     * through a few assumptions, and it just calculates an average
     * DPI value for both monitors.
     */

    /* Copy user-given DisplaySize (which should regard BOTH monitors!) */
    pScrn1->widthmm = pScrn1->monitor->widthmm;
    pScrn1->heightmm = pScrn1->monitor->heightmm;

    if(monitorResolution > 0) {

       /* Set command line given values (overrules given options) */
       pScrn1->xDpi = monitorResolution;
       pScrn1->yDpi = monitorResolution;
       from = X_CMDLINE;

    } else if(pSiS->MergedFBXDPI) {

       /* Set option-wise given values (overrules DisplaySize config option) */
       pScrn1->xDpi = pSiS->MergedFBXDPI;
       pScrn1->yDpi = pSiS->MergedFBYDPI;
       from = X_CONFIG;

    } else if(pScrn1->widthmm > 0 || pScrn1->heightmm > 0) {

       /* Set values calculated from given DisplaySize */
       from = X_CONFIG;
       if(pScrn1->widthmm > 0) {
	  pScrn1->xDpi = (int)((double)pScrn1->virtualX * 25.4 / pScrn1->widthmm);
       }
       if(pScrn1->heightmm > 0) {
	  pScrn1->yDpi = (int)((double)pScrn1->virtualY * 25.4 / pScrn1->heightmm);
       }
       if(!quiet) {
          xf86DrvMsg(pScrn1->scrnIndex, from, dsstr, pScrn1->widthmm, pScrn1->heightmm);
       }

    } else if(ddcWidthmm && ddcHeightmm) {

       /* Set values from DDC-provided display size */

       /* Get DDC display size; if only either CRT1 or CRT2 provided these,
	* assume equal dimensions for both, otherwise add dimensions
	*/
       if( (DDC1 && (DDC1->features.hsize > 0 && DDC1->features.vsize > 0)) &&
	   (DDC2 && (DDC2->features.hsize > 0 && DDC2->features.vsize > 0)) ) {
	  ddcWidthmm = max(DDC1->features.hsize, DDC2->features.hsize) * 10;
	  ddcHeightmm = max(DDC1->features.vsize, DDC2->features.vsize) * 10;
	  switch(srel) {
	  case sisLeftOf:
	  case sisRightOf:
	     ddcWidthmm = (DDC1->features.hsize + DDC2->features.hsize) * 10;
	     break;
	  case sisAbove:
	  case sisBelow:
	     ddcHeightmm = (DDC1->features.vsize + DDC2->features.vsize) * 10;
	  default:
	     break;
	  }
       } else if(DDC1 && (DDC1->features.hsize > 0 && DDC1->features.vsize > 0)) {
	  ddcWidthmm = DDC1->features.hsize * 10;
	  ddcHeightmm = DDC1->features.vsize * 10;
	  switch(srel) {
	  case sisLeftOf:
	  case sisRightOf:
	     ddcWidthmm *= 2;
	     break;
	  case sisAbove:
	  case sisBelow:
	     ddcHeightmm *= 2;
	  default:
	     break;
          }
       } else if(DDC2 && (DDC2->features.hsize > 0 && DDC2->features.vsize > 0) ) {
	  ddcWidthmm = DDC2->features.hsize * 10;
	  ddcHeightmm = DDC2->features.vsize * 10;
	  switch(srel) {
	  case sisLeftOf:
	  case sisRightOf:
	     ddcWidthmm *= 2;
	     break;
	  case sisAbove:
	  case sisBelow:
	     ddcHeightmm *= 2;
	  default:
	     break;
	  }
       }

       from = X_PROBED;

       if(!quiet) {
          xf86DrvMsg(pScrn1->scrnIndex, from, dsstr, ddcWidthmm, ddcHeightmm);
       }

       pScrn1->widthmm = ddcWidthmm;
       pScrn1->heightmm = ddcHeightmm;
       if(pScrn1->widthmm > 0) {
	  pScrn1->xDpi = (int)((double)pScrn1->virtualX * 25.4 / pScrn1->widthmm);
       }
       if(pScrn1->heightmm > 0) {
	  pScrn1->yDpi = (int)((double)pScrn1->virtualY * 25.4 / pScrn1->heightmm);
       }

    } else {

       pScrn1->xDpi = pScrn1->yDpi = DEFAULT_DPI;

    }

    /* Sanity check */
    if(pScrn1->xDpi > 0 && pScrn1->yDpi <= 0)
       pScrn1->yDpi = pScrn1->xDpi;
    if(pScrn1->yDpi > 0 && pScrn1->xDpi <= 0)
       pScrn1->xDpi = pScrn1->yDpi;

    pScrn2->xDpi = pScrn1->xDpi;
    pScrn2->yDpi = pScrn1->yDpi;

    if(!quiet) {
       xf86DrvMsg(pScrn1->scrnIndex, from, "MergedFB: DPI set to (%d, %d)\n",
		pScrn1->xDpi, pScrn1->yDpi);
    }
}

void
SiSMFBSetDpi(ScrnInfoPtr pScrn1, ScrnInfoPtr pScrn2, SiSScrn2Rel srel)
{
    SISPtr pSiS = SISPTR(pScrn1);

    SiSMFBCalcDPI(pScrn1, pScrn2, srel, FALSE);

    pSiS->MergedDPISRel = srel;
    pSiS->SiSMergedDPIVX = pScrn1->virtualX;
    pSiS->SiSMergedDPIVY = pScrn1->virtualY;
}

#if defined(RANDR) && !defined(SIS_HAVE_RR_GET_MODE_MM)
void
SiSMFBResetDpi(ScrnInfoPtr pScrn, Bool force)
{
    SISPtr pSiS = SISPTR(pScrn);
    ScreenPtr pScreen = screenInfo.screens[pScrn->scrnIndex];
    SiSScrn2Rel srel = ((SiSMergedDisplayModePtr)pSiS->CurrentLayout.mode->Private)->CRT2Position;

    /* This does the same calculation for the DPI as
     * the initial run. This means that an eventually
     * given -dpi command line switch will lead to
     * constant dpi values, regardless of the virtual
     * screen size.
     * I consider this consequent. If this is undesired,
     * one should use the DisplaySize parameter in the
     * config file instead of the command line switch.
     * The DPI will be calculated then.
     */

    if(force						||
       (pSiS->MergedDPISRel != srel)			||
       (pSiS->SiSMergedDPIVX != pScrn->virtualX)	||
       (pSiS->SiSMergedDPIVY != pScrn->virtualY)
						) {

       SiSMFBCalcDPI(pScrn, pSiS->CRT2pScrn, srel, TRUE);

       pScreen->mmWidth = (pScrn->virtualX * 254 + pScrn->xDpi * 5) / (pScrn->xDpi * 10);
       pScreen->mmHeight = (pScrn->virtualY * 254 + pScrn->yDpi * 5) / (pScrn->yDpi * 10);

       pSiS->MergedDPISRel = srel;
       pSiS->SiSMergedDPIVX = pScrn->virtualX;
       pSiS->SiSMergedDPIVY = pScrn->virtualY;

    }
}
#endif

#ifdef SIS_HAVE_RR_GET_MODE_MM
void
SiSMFBCalcDPIPerMode(ScrnInfoPtr pScrn, DisplayModePtr mode,
			int virtX, int virtY,
			int *mmWidth, int *mmHeight)
{
    SISPtr pSiS = SISPTR(pScrn);
    int width = virtX, height = virtY;

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
        * Exception: We need to take care of clone modes. They totally
        * screw up the DPI (100 100 becomes 50 100 if switching from
        * modes aside from each other to a clone mode).
        *
        * This might need some enhancement when growing the screen
        * is supported in MergedFB mode (ie "ReserveLargeVirtual").
        */
       if(mode && (SiSMergedDisplayModePtr)mode->Private) {
          if(((SiSMergedDisplayModePtr)mode->Private)->CRT2Position == sisClone) {
             switch(pSiS->CRT2Position) {
             case sisLeftOf:
             case sisRightOf:
                *mmWidth /= 2;
                break;
             case sisAbove:
             case sisBelow:
                *mmHeight /= 2;
                break;
             default:
                break;
             }
          }
       } else if(mode) {
          ErrorF("Internal error: Apparent Metamode lacks private!\n");
       }

    }
}
#endif


static Bool
InRegion(int x, int y, region r)
{
    return (r.x0 <= x) && (x <= r.x1) && (r.y0 <= y) && (y <= r.y1);
}


void
SISMFBPointerMoved(int scrnIndex, int x, int y)
{
    ScrnInfoPtr	pScrn1 = xf86Screens[scrnIndex];
    SISPtr	pSiS = SISPTR(pScrn1);
    ScrnInfoPtr	pScrn2 = pSiS->CRT2pScrn;
    region	out, in1, in2, f2, f1;
    int		deltax, deltay;
    int		temp1, temp2;
    int		old1x0, old1y0, old2x0, old2y0;
    int		CRT1XOffs = 0, CRT1YOffs = 0, CRT2XOffs = 0, CRT2YOffs = 0;
    int		MBXNR1XMAX = 65536, MBXNR1YMAX = 65536, MBXNR2XMAX = 65536, MBXNR2YMAX = 65536;
    int		CRT1HDisplay, CRT1VDisplay, CRT2HDisplay, CRT2VDisplay;
    int		HVirt = pScrn1->virtualX;
    int		VVirt = pScrn1->virtualY;
    int		sigstate;
    Bool	doit = FALSE, HaveNonRect = FALSE, HaveOffsRegions = FALSE;
    SiSScrn2Rel	srel = ((SiSMergedDisplayModePtr)pSiS->CurrentLayout.mode->Private)->CRT2Position;

    /* Beware: This is executed asynchronously. */

    if(pSiS->DGAactive) {
       return;
       /* DGA: There is no cursor and no panning while DGA is active. */
       /* If it were, we would need to do: */
       /* HVirt = pSiS->CurrentLayout.displayWidth;
          VVirt = pSiS->CurrentLayout.displayHeight;
          BOUND(x, pSiS->CurrentLayout.DGAViewportX, HVirt);
          BOUND(y, pSiS->CurrentLayout.DGAViewportY, VVirt); */
    } else {
       CRT1XOffs = pSiS->CRT1XOffs;
       CRT1YOffs = pSiS->CRT1YOffs;
       CRT2XOffs = pSiS->CRT2XOffs;
       CRT2YOffs = pSiS->CRT2YOffs;
       HaveNonRect = pSiS->HaveNonRect;
       HaveOffsRegions = pSiS->HaveOffsRegions;
    }


    /* Check if the pointer is inside our dead areas */
    /* Only do this if the layout is somewhat in sync with
     * the current display modes. We don't want regions
     * which are visible but not accessible with the
     * pointer.
     */
#ifdef SISXINERAMA
    if(!SiSnoPanoramiXExtension							&&
       (pSiS->MouseRestrictions)						&&
       (srel != sisClone)) {
       if(HaveNonRect) {
	  if(InRegion(x, y, pSiS->NonRectDead)) {
	     switch(srel) {
	     case sisLeftOf:
	     case sisRightOf: y = pSiS->NonRectDead.y0 - 1;
			      doit = TRUE;
			      break;
	     case sisAbove:
	     case sisBelow:   x = pSiS->NonRectDead.x0 - 1;
			      doit = TRUE;
	     default:	      break;
	     }
	  }
       }
       if(HaveOffsRegions) {
	  if(InRegion(x, y, pSiS->OffDead1)) {
	     switch(srel) {
	     case sisLeftOf:
	     case sisRightOf: y = pSiS->OffDead1.y1;
			      doit = TRUE;
			      break;
	     case sisAbove:
	     case sisBelow:   x = pSiS->OffDead1.x1;
			      doit = TRUE;
	     default:	      break;
	     }
	  } else if(InRegion(x, y, pSiS->OffDead2)) {
	     switch(srel) {
	     case sisLeftOf:
	     case sisRightOf: y = pSiS->OffDead2.y0 - 1;
			      doit = TRUE;
			      break;
	     case sisAbove:
	     case sisBelow:   x = pSiS->OffDead2.x0 - 1;
			      doit = TRUE;
	     default:	      break;
	     }
	  }
       }
       if(doit) {
	  UpdateCurrentTime();
	  sigstate = xf86BlockSIGIO();
	  miPointerAbsoluteCursor(x, y, currentTime.milliseconds);
	  xf86UnblockSIGIO(sigstate);
	  return;
       }
    }
#endif

    f1.x0 = old1x0 = pSiS->CRT1frameX0;
    f1.x1 = pSiS->CRT1frameX1;
    f1.y0 = old1y0 = pSiS->CRT1frameY0;
    f1.y1 = pSiS->CRT1frameY1;
    f2.x0 = old2x0 = pScrn2->frameX0;
    f2.x1 = pScrn2->frameX1;
    f2.y0 = old2y0 = pScrn2->frameY0;
    f2.y1 = pScrn2->frameY1;

    /* Define the outer region. Crossing this causes all frames to move */
    out.x0 = pScrn1->frameX0;
    out.x1 = pScrn1->frameX1;
    out.y0 = pScrn1->frameY0;
    out.y1 = pScrn1->frameY1;

    /*
     * Define the inner sliding window. Being outside both frames but
     * inside the outer clipping window will slide corresponding frame
     */
    in1 = out;
    in2 = out;
    switch(srel) {
    case sisLeftOf:
       in1.x0 = f1.x0;
       in2.x1 = f2.x1;
       break;
    case sisRightOf:
       in1.x1 = f1.x1;
       in2.x0 = f2.x0;
       break;
    case sisBelow:
       in1.y1 = f1.y1;
       in2.y0 = f2.y0;
       break;
    case sisAbove:
       in1.y0 = f1.y0;
       in2.y1 = f2.y1;
       break;
    case sisClone:
       break;
    }

    deltay = 0;
    deltax = 0;

    if(InRegion(x, y, out)) {	/* inside outer region */

       if(InRegion(x, y, in1) && !InRegion(x, y, f1)) {
	  REBOUND(f1.x0, f1.x1, x);
	  REBOUND(f1.y0, f1.y1, y);
	  deltax = 1;
       }
       if(InRegion(x, y, in2) && !InRegion(x, y, f2)) {
	  REBOUND(f2.x0, f2.x1, x);
	  REBOUND(f2.y0, f2.y1, y);
	  deltax = 1;
       }

    } else {			/* outside outer region */

       if(out.x0 > x) {
	  deltax = x - out.x0;
       }
       if(out.x1 < x) {
	  deltax = x - out.x1;
       }
       if(deltax) {
	  pScrn1->frameX0 += deltax;
	  pScrn1->frameX1 += deltax;
	  f1.x0 += deltax;
	  f1.x1 += deltax;
	  f2.x0 += deltax;
	  f2.x1 += deltax;
       }

       if(out.y0 > y) {
	  deltay = y - out.y0;
       }
       if(out.y1 < y) {
	  deltay = y - out.y1;
       }
       if(deltay) {
	  pScrn1->frameY0 += deltay;
	  pScrn1->frameY1 += deltay;
	  f1.y0 += deltay;
	  f1.y1 += deltay;
	  f2.y0 += deltay;
	  f2.y1 += deltay;
       }

       switch(srel) {
       case sisLeftOf:
	  if(x >= f1.x0) { REBOUND(f1.y0, f1.y1, y); }
	  if(x <= f2.x1) { REBOUND(f2.y0, f2.y1, y); }
	  break;
       case sisRightOf:
	  if(x <= f1.x1) { REBOUND(f1.y0, f1.y1, y); }
	  if(x >= f2.x0) { REBOUND(f2.y0, f2.y1, y); }
	  break;
       case sisBelow:
	  if(y <= f1.y1) { REBOUND(f1.x0, f1.x1, x); }
	  if(y >= f2.y0) { REBOUND(f2.x0, f2.x1, x); }
	  break;
       case sisAbove:
	  if(y >= f1.y0) { REBOUND(f1.x0, f1.x1, x); }
	  if(y <= f2.y1) { REBOUND(f2.x0, f2.x1, x); }
	  break;
       case sisClone:
	  break;
       }

    }

    if(deltax || deltay) {
       pSiS->CRT1frameX0 = f1.x0;
       pSiS->CRT1frameY0 = f1.y0;
       pScrn2->frameX0 = f2.x0;
       pScrn2->frameY0 = f2.y0;

       CRT1HDisplay = CDMPTR->CRT1->HDisplay;
       CRT1VDisplay = CDMPTR->CRT1->VDisplay;
       CRT2HDisplay = CDMPTR->CRT2->HDisplay;
       CRT2VDisplay = CDMPTR->CRT2->VDisplay;

       switch(srel) {
       case sisLeftOf:
       case sisRightOf:
	  if(CRT1YOffs || CRT2YOffs || HaveNonRect) {
	     if(srel == sisLeftOf) {
		if(pSiS->NonRectDead.x0 == 0) MBXNR2YMAX = pSiS->MBXNRYMAX;
		else			      MBXNR1YMAX = pSiS->MBXNRYMAX;
	     } else {
		if(pSiS->NonRectDead.x0 == 0) MBXNR1YMAX = pSiS->MBXNRYMAX;
		else			      MBXNR2YMAX = pSiS->MBXNRYMAX;
	     }
	     if(pSiS->CRT1frameY0 != old1y0) {
		if(pSiS->CRT1frameY0 < CRT1YOffs)
		   pSiS->CRT1frameY0 = CRT1YOffs;

		temp1 = pSiS->CRT1frameY0 + CRT1VDisplay;
		temp2 = min((VVirt - CRT2YOffs), (CRT1YOffs + MBXNR1YMAX));
		if(temp1 > temp2)
		   pSiS->CRT1frameY0 -= (temp1 - temp2);
	     }
	     if(pScrn2->frameY0 != old2y0) {
		if(pScrn2->frameY0 < CRT2YOffs)
		   pScrn2->frameY0 = CRT2YOffs;

		temp1 = pScrn2->frameY0 + CRT2VDisplay;
		temp2 = min((VVirt - CRT1YOffs), (CRT2YOffs + MBXNR2YMAX));
		if(temp1 > temp2)
		   pScrn2->frameY0 -= (temp1 - temp2);
	     }
	  }
	  break;
       case sisBelow:
       case sisAbove:
	  if(CRT1XOffs || CRT2XOffs || HaveNonRect) {
	     if(srel == sisAbove) {
		if(pSiS->NonRectDead.y0 == 0) MBXNR2XMAX = pSiS->MBXNRXMAX;
		else			      MBXNR1XMAX = pSiS->MBXNRXMAX;
	     } else {
		if(pSiS->NonRectDead.y0 == 0) MBXNR1XMAX = pSiS->MBXNRXMAX;
		else			      MBXNR2XMAX = pSiS->MBXNRXMAX;
	     }
	     if(pSiS->CRT1frameX0 != old1x0) {
		if(pSiS->CRT1frameX0 < CRT1XOffs)
		   pSiS->CRT1frameX0 = CRT1XOffs;

		temp1 = pSiS->CRT1frameX0 + CRT1HDisplay;
		temp2 = min((HVirt - CRT2XOffs), (CRT1XOffs + MBXNR1XMAX));
		if(temp1 > temp2)
		   pSiS->CRT1frameX0 -= (temp1 - temp2);
	     }
	     if(pScrn2->frameX0 != old2x0) {
		if(pScrn2->frameX0 < CRT2XOffs)
		   pScrn2->frameX0 = CRT2XOffs;

		temp1 = pScrn2->frameX0 + CRT2HDisplay;
		temp2 = min((HVirt - CRT1XOffs), (CRT2XOffs + MBXNR2XMAX));
		if(temp1 > temp2)
		   pScrn2->frameX0 -= (temp1 - temp2);
	     }
	  }
	  break;
       default:
	  break;
       }

       pSiS->CRT1frameX1 = pSiS->CRT1frameX0 + CRT1HDisplay - 1;
       pSiS->CRT1frameY1 = pSiS->CRT1frameY0 + CRT1VDisplay - 1;
       pScrn2->frameX1   = pScrn2->frameX0   + CRT2HDisplay - 1;
       pScrn2->frameY1   = pScrn2->frameY0   + CRT2VDisplay - 1;

       /* No need to update pScrn1->frame?1, done above */

       /* Need to go the official way to avoid hw access and
        * to update Xv's overlays
        */
       (pScrn1->AdjustFrame)(pScrn1, pScrn1->frameX0, pScrn1->frameY0);
    }
}

void
SISMFBAdjustFrame(int scrnIndex, int x, int y, int flags)
{
    ScrnInfoPtr pScrn1 = xf86Screens[scrnIndex];
    SISPtr pSiS = SISPTR(pScrn1);
    ScrnInfoPtr pScrn2 = pSiS->CRT2pScrn;
    int HTotal = pSiS->CurrentLayout.mode->HDisplay;
    int VTotal = pSiS->CurrentLayout.mode->VDisplay;
    int HMax = HTotal;
    int VMax = VTotal;
    int HVirt = pScrn1->virtualX;
    int VVirt = pScrn1->virtualY;
    int x1 = x, x2 = x;
    int y1 = y, y2 = y;
    int CRT1XOffs = 0, CRT1YOffs = 0, CRT2XOffs = 0, CRT2YOffs = 0;
    int MBXNR1XMAX = 65536, MBXNR1YMAX = 65536, MBXNR2XMAX = 65536, MBXNR2YMAX = 65536;
    int CRT1HDisplay, CRT1VDisplay, CRT2HDisplay, CRT2VDisplay;
    SiSScrn2Rel srel = SDMPTR(pScrn1)->CRT2Position;

    if(pSiS->DGAactive) {

       HVirt = pSiS->CurrentLayout.displayWidth;
       VVirt = pSiS->CurrentLayout.displayHeight;

    } else {

       CRT1XOffs = pSiS->CRT1XOffs;
       CRT1YOffs = pSiS->CRT1YOffs;
       CRT2XOffs = pSiS->CRT2XOffs;
       CRT2YOffs = pSiS->CRT2YOffs;

       if((srel != sisClone) && pSiS->HaveNonRect) {
	  switch(srel) {
	  case sisLeftOf:
	     if(pSiS->NonRectDead.x0 == 0) MBXNR2YMAX = pSiS->MBXNRYMAX;
	     else			   MBXNR1YMAX = pSiS->MBXNRYMAX;
	     break;
	  case sisRightOf:
	     if(pSiS->NonRectDead.x0 == 0) MBXNR1YMAX = pSiS->MBXNRYMAX;
	     else			   MBXNR2YMAX = pSiS->MBXNRYMAX;
	     break;
	  case sisAbove:
	     if(pSiS->NonRectDead.y0 == 0) MBXNR2XMAX = pSiS->MBXNRXMAX;
	     else			   MBXNR1XMAX = pSiS->MBXNRXMAX;
	     break;
	  case sisBelow:
	     if(pSiS->NonRectDead.y0 == 0) MBXNR1XMAX = pSiS->MBXNRXMAX;
	     else			   MBXNR2XMAX = pSiS->MBXNRXMAX;
	     break;
	  default:
	     break;
	  }
       }


    }

    BOUND(x, 0, HVirt - HTotal);
    BOUND(y, 0, VVirt - VTotal);

    if(SDMPTR(pScrn1)->CRT2Position != sisClone) {
       BOUND(x1, CRT1XOffs, min(HVirt, MBXNR1XMAX + CRT1XOffs) - min(HTotal, MBXNR1XMAX) - CRT2XOffs);
       BOUND(y1, CRT1YOffs, min(VVirt, MBXNR1YMAX + CRT1YOffs) - min(VTotal, MBXNR1YMAX) - CRT2YOffs);
       BOUND(x2, CRT2XOffs, min(HVirt, MBXNR2XMAX + CRT2XOffs) - min(HTotal, MBXNR2XMAX) - CRT1XOffs);
       BOUND(y2, CRT2YOffs, min(VVirt, MBXNR2YMAX + CRT2YOffs) - min(VTotal, MBXNR2YMAX) - CRT1YOffs);
    }

    CRT1HDisplay = CDMPTR->CRT1->HDisplay;
    CRT1VDisplay = CDMPTR->CRT1->VDisplay;
    CRT2HDisplay = CDMPTR->CRT2->HDisplay;
    CRT2VDisplay = CDMPTR->CRT2->VDisplay;

    switch(srel) {
    case sisLeftOf:
       pScrn2->frameX0 = x2;
       BOUND(pScrn2->frameY0,   y2, y2 + min(VMax, MBXNR2YMAX) - CRT2VDisplay);
       pSiS->CRT1frameX0 = x1 + CRT2HDisplay;
       BOUND(pSiS->CRT1frameY0, y1, y1 + min(VMax, MBXNR1YMAX) - CRT1VDisplay);
       break;
    case sisRightOf:
       pSiS->CRT1frameX0 = x1;
       BOUND(pSiS->CRT1frameY0, y1, y1 + min(VMax, MBXNR1YMAX) - CRT1VDisplay);
       pScrn2->frameX0 = x2 + CRT1HDisplay;
       BOUND(pScrn2->frameY0,   y2, y2 + min(VMax, MBXNR2YMAX) - CRT2VDisplay);
       break;
    case sisAbove:
       BOUND(pScrn2->frameX0,   x2, x2 + min(HMax, MBXNR2XMAX) - CRT2HDisplay);
       pScrn2->frameY0 = y2;
       BOUND(pSiS->CRT1frameX0, x1, x1 + min(HMax, MBXNR1XMAX) - CRT1HDisplay);
       pSiS->CRT1frameY0 = y1 + CRT2VDisplay;
        break;
    case sisBelow:
       BOUND(pSiS->CRT1frameX0, x1, x1 + min(HMax, MBXNR1XMAX) - CRT1HDisplay);
       pSiS->CRT1frameY0 = y1;
       BOUND(pScrn2->frameX0,   x2, x2 + min(HMax, MBXNR2XMAX) - CRT2HDisplay);
       pScrn2->frameY0 = y2 + CRT1VDisplay;
       break;
    case sisClone:
       BOUND(pSiS->CRT1frameX0, x,  x + HMax - CRT1HDisplay);
       BOUND(pSiS->CRT1frameY0, y,  y + VMax - CRT1VDisplay);
       BOUND(pScrn2->frameX0,   x,  x + HMax - CRT2HDisplay);
       BOUND(pScrn2->frameY0,   y,  y + VMax - CRT2VDisplay);
       break;
    }

    BOUND(pSiS->CRT1frameX0, 0, HVirt - CRT1HDisplay);
    BOUND(pSiS->CRT1frameY0, 0, VVirt - CRT1VDisplay);
    BOUND(pScrn2->frameX0,   0, HVirt - CRT2HDisplay);
    BOUND(pScrn2->frameY0,   0, VVirt - CRT2VDisplay);

    pScrn1->frameX0 = x;
    pScrn1->frameY0 = y;

    pSiS->CRT1frameX1 = pSiS->CRT1frameX0 + CRT1HDisplay - 1;
    pSiS->CRT1frameY1 = pSiS->CRT1frameY0 + CRT1VDisplay - 1;
    pScrn2->frameX1   = pScrn2->frameX0   + CRT2HDisplay - 1;
    pScrn2->frameY1   = pScrn2->frameY0   + CRT2VDisplay - 1;

    pScrn1->frameX1 = pScrn1->frameX0 - 1;
    pScrn1->frameY1 = pScrn1->frameY0 - 1;
    pScrn1->frameX1 += pSiS->CurrentLayout.mode->HDisplay;
    pScrn1->frameY1 += pSiS->CurrentLayout.mode->VDisplay;

    if(SDMPTR(pScrn1)->CRT2Position != sisClone) {
       pScrn1->frameX1 += CRT1XOffs + CRT2XOffs;
       pScrn1->frameY1 += CRT1YOffs + CRT2YOffs;
    }

    SISAdjustFrameHW_CRT1(pScrn1, pSiS->CRT1frameX0, pSiS->CRT1frameY0);
    SISAdjustFrameHW_CRT2(pScrn1, pScrn2->frameX0, pScrn2->frameY0);
}

/* Pseudo-Xinerama extension for MergedFB mode */
#ifdef SISXINERAMA

#define SIS_XINERAMA_MAJOR_VERSION  1
#define SIS_XINERAMA_MINOR_VERSION  2

/* ---------- stuff to be made public ------------ */

/* If server's xinerama is pre-1.2, we need to defined these */

#ifndef X_XineramaSelectInput
#define X_XineramaSelectInput 6
#endif

#ifndef sz_xXineramaSelectInputReq
typedef struct {
	CARD8   reqType;
	CARD8   panoramiXReqType;
	CARD16  length B16;
	CARD32  window B32;		/* window requesting notification */
	CARD16  enable B16;
	CARD16  pad2 B16;
} xXineramaSelectInputReq;
#define sz_xXineramaSelectInputReq 12
#endif

#ifndef XineramaLayoutChangeNotifyMask
#define XineramaLayoutChangeNotifyMask  (1L << 0)
#endif

#ifndef XineramaLayoutChangeNotify
#define XineramaLayoutChangeNotify	0
#endif

/*
 * Each window has a list of clients requesting
 * XineramaNotify events. Each client has a resource
 * for each window it selects XineramaNotify input for,
 * this resource is used to delete the XineramaNotifyRec
 * entry from the per-window queue.
 */

#ifndef sz_xXineramaLayoutChangeNotifyEvent
typedef struct {
	CARD8 type;			/* always evBase + LayoutChangeNotify */
	CARD8 pad1;
	CARD16 sequenceNumber B16;
	CARD32 window B32;		/* window requesting notification */
} xXineramaLayoutChangeNotifyEvent;
#define sz_xXineramaLayoutChangeNotifyEvent 8
#endif

/* ------- end of public stuff --------- */

typedef struct _SiSXineramaEvent *SiSXineramaEventPtr;

typedef struct _SiSXineramaEvent {
    SiSXineramaEventPtr  next;
    ClientPtr	client;
    WindowPtr	window;
    XID		clientResource;
    int		mask;
} SiSXineramaEventRec;

static RESTYPE ClientType, EventType;
static int SiSXineramaEventbase;
static int SiSXineramaClientsListening;

static int
SiSTellChanged(WindowPtr pWin, pointer value)
{
    SiSXineramaEventPtr			*pHead, pXineramaEvent;
    ClientPtr				client;
    xXineramaLayoutChangeNotifyEvent	se;

    dixLookupResourceByType((pointer) &pHead, pWin->drawable.id, EventType, NullClient, DixUnknownAccess);
    if(!pHead) {
       return WT_WALKCHILDREN;
    }

    se.type = XineramaLayoutChangeNotify + SiSXineramaEventbase;
    se.window = pWin->drawable.id;

    for(pXineramaEvent = *pHead; pXineramaEvent; pXineramaEvent = pXineramaEvent->next) {
       client = pXineramaEvent->client;
       if(client == serverClient || client->clientGone)
	  continue;
       se.sequenceNumber = client->sequence;
       if(pXineramaEvent->mask & XineramaLayoutChangeNotifyMask) {
	  WriteEventsToClient(client, 1, (xEvent *)&se);
       }
    }

    return WT_WALKCHILDREN;
}

void
SiSUpdateXineramaScreenInfo(ScrnInfoPtr pScrn1)
{
    SISPtr pSiS = SISPTR(pScrn1);
    ScreenPtr pScreen = screenInfo.screens[pScrn1->scrnIndex];
    int crt1scrnnum, crt2scrnnum;
    int x1 = 0, x2 = 0, y1 = 0, y2 = 0, h1 = 0, h2 = 0, w1 = 0, w2 = 0;
    int currH1 = 0, currH2 = 0, currV1 = 0, currV2 = 0;
    int virtualX, virtualY, realvirtX, realvirtY;
    DisplayModePtr currentMode, firstMode;
    Bool infochanged = FALSE;
    Bool usenonrect = pSiS->NonRect;
    SiSScrn2Rel currSRel, srel = pSiS->CRT2Position;
    const char *rectxine = "\t... setting up rectangular Xinerama layout\n";

    pSiS->MBXNRXMAX = pSiS->MBXNRYMAX = 65536;
    pSiS->HaveNonRect = pSiS->HaveOffsRegions = FALSE;

    if(!pSiS->MergedFB		||
       SiSnoPanoramiXExtension	||
       !SiSXineramadataPtr	||
       !pScrn1->modes)
       return;

    /* Note: Usage of RandR may lead to virtual X and Y dimensions
     * actually smaller than our MetaModes. To avoid this, we calculate
     * the maxCRT fields here (and not somewhere else, like in CopyNLink)
     *
     * *** Note: RandR is disabled if one of CRTxxOffs is non-zero.
     */

    /* "Real" virtual: Virtual without the Offset */
    realvirtX = pScrn1->virtualX - pSiS->CRT1XOffs - pSiS->CRT2XOffs;
    realvirtY = pScrn1->virtualY - pSiS->CRT1YOffs - pSiS->CRT2YOffs;

    currentMode = pSiS->CurrentLayout.mode;

    /* Get the current display mode's dimensions */
    currH1 = ((SiSMergedDisplayModePtr)currentMode->Private)->CRT1->HDisplay;
    currV1 = ((SiSMergedDisplayModePtr)currentMode->Private)->CRT1->VDisplay;
    currH2 = ((SiSMergedDisplayModePtr)currentMode->Private)->CRT2->HDisplay;
    currV2 = ((SiSMergedDisplayModePtr)currentMode->Private)->CRT2->VDisplay;
    currSRel = ((SiSMergedDisplayModePtr)currentMode->Private)->CRT2Position;

    /* Determine whether or not to recalculate the layout.
     * We provide Xinerama 1.2 which allows clients to listen to
     * XineramaLayoutChanged events. Since this is new and it will
     * take time until window managers know of and use this technique,
     * we check whether or not any client (which is supposed to be a
     * window manager) actually listens to this event and only behave
     * Xinerama 1.2 compliant (ie recalc our layout on any relevant
     * display mode changes) if that is the case.
     * For Xinerama 1.1 aware clients (ie: no client is listening
     * to our 1.2 events), there is no mechanism for informing them
     * about layout changes. Modern window managers, however, seem
     * to re-query the Xinerama extension upon RandR events. Still,
     * simple display mode changes without a screen resize/rotation
     * change are not noticed by clients.
     * So, if there is nobody listening to our 1.2 events, there is
     * no point in updating our layout unless a RandR event is
     * triggered at the same time (which is the case if size or
     * rotation is changed).
     *
     * Note that the dead (inaccessible) areas are re-calculated in
     * any case.
     */

    if( (pSiS->SiSXineramaVX != pScrn1->virtualX)	||
        (pSiS->SiSXineramaVY != pScrn1->virtualY)
							) {


	  pSiS->maxCRT1_X1 = pSiS->maxCRT1_X2 = pSiS->maxCRT1_Y1 = pSiS->maxCRT1_Y2 = 0;
	  pSiS->maxCRT2_X1 = pSiS->maxCRT2_X2 = pSiS->maxCRT2_Y1 = pSiS->maxCRT2_Y2 = 0;
	  pSiS->maxClone_X1 = pSiS->maxClone_X2 = pSiS->maxClone_Y1 =  pSiS->maxClone_Y2 = 0;

	  currentMode = firstMode = pScrn1->modes;

	  do {

	     DisplayModePtr p = currentMode->next;
	     DisplayModePtr i = ((SiSMergedDisplayModePtr)currentMode->Private)->CRT1;
	     DisplayModePtr j = ((SiSMergedDisplayModePtr)currentMode->Private)->CRT2;
	     SiSScrn2Rel srelc = ((SiSMergedDisplayModePtr)currentMode->Private)->CRT2Position;
	     int limitX, limitY;

	     limitX = realvirtX;
	     limitY = realvirtY;

	     if((currentMode->HDisplay <= limitX) &&
		(currentMode->VDisplay <= limitY) &&
		(i->HDisplay <= limitX) &&
		(j->HDisplay <= limitX) &&
		(i->VDisplay <= limitY) &&
		(j->VDisplay <= limitY)) {

		int ih = i->HDisplay, iv = i->VDisplay;
		int jh = j->HDisplay, jv = j->VDisplay;

		if(srelc != sisClone) {

		   /* What we do here is essentially calculating maximum values
		    * of modes used [LEFT | RIGHT | UP | DOWN] and - despite the
		    * variable naming - not what's displayed on the specific CRTC
		    * channels.
		    */

		   if(pSiS->maxCRT1_X1 == ih) {
		      if(pSiS->maxCRT1_X2 < jh) {
			 pSiS->maxCRT1_X2 = jh;   /* Widest CRT2 mode displayed with widest CRT1 mode */
		      }
		   } else if(pSiS->maxCRT1_X1 < ih) {
		      pSiS->maxCRT1_X1 = ih;      /* Widest CRT1 mode */
		      pSiS->maxCRT1_X2 = jh;
		   }

		   if(pSiS->maxCRT2_X2 == jh) {
		      if(pSiS->maxCRT2_X1 < ih) {
			 pSiS->maxCRT2_X1 = ih;   /* Widest CRT1 mode displayed with widest CRT2 mode */
		      }
		   } else if(pSiS->maxCRT2_X2 < jh) {
		      pSiS->maxCRT2_X2 = jh;      /* Widest CRT2 mode */
		      pSiS->maxCRT2_X1 = ih;
		   }

		   if(pSiS->maxCRT1_Y1 == iv) {   /* Same as above, but tallest instead of widest */
		      if(pSiS->maxCRT1_Y2 < jv) {
			 pSiS->maxCRT1_Y2 = jv;
		      }
		   } else if(pSiS->maxCRT1_Y1 < iv) {
		      pSiS->maxCRT1_Y1 = iv;
		      pSiS->maxCRT1_Y2 = jv;
		   }

		   if(pSiS->maxCRT2_Y2 == jv) {
		      if(pSiS->maxCRT2_Y1 < iv) {
			 pSiS->maxCRT2_Y1 = iv;
		      }
		   } else if(pSiS->maxCRT2_Y2 < jv) {
		      pSiS->maxCRT2_Y2 = jv;
		      pSiS->maxCRT2_Y1 = iv;
		   }

		} else {

		   if(pSiS->maxClone_X1 < ih) {
		      pSiS->maxClone_X1 = ih;
		   }
		   if(pSiS->maxClone_X2 < jh) {
		      pSiS->maxClone_X2 = jh;
		   }
		   if(pSiS->maxClone_Y1 < iv) {
		      pSiS->maxClone_Y1 = iv;
		   }
		   if(pSiS->maxClone_Y2 < jv) {
		      pSiS->maxClone_Y2 = jv;
		   }

		}
	     }

	     currentMode = p;

	  } while((currentMode) && (currentMode != firstMode));

       infochanged = TRUE;

       pSiS->SiSXineramaVX = pScrn1->virtualX;
       pSiS->SiSXineramaVY = pScrn1->virtualY;


       pSiS->XineSRel = srel;

    }

    srel = pSiS->XineSRel;

    /* Determine if we should set up dead areas (ie make a non-rectangular layout) */
    if((usenonrect) && (srel != sisClone) && pSiS->maxCRT1_X1) {

       switch(srel) {
       case sisLeftOf:
       case sisRightOf:
	  if((pSiS->maxCRT1_Y1 != realvirtY) && (pSiS->maxCRT2_Y2 != realvirtY)) {
	     usenonrect = FALSE;
	  }
	  break;
       case sisAbove:
       case sisBelow:
	  if((pSiS->maxCRT1_X1 != realvirtX) && (pSiS->maxCRT2_X2 != realvirtX)) {
	     usenonrect = FALSE;
	  }
       default:
	  break;
       }

       if(infochanged && !usenonrect) {
	  xf86DrvMsgVerb(pScrn1->scrnIndex, X_INFO, pSiS->XineVerb,
			"Current screen size does not match maximum display modes...\n");
	  xf86DrvMsgVerb(pScrn1->scrnIndex, X_INFO, pSiS->XineVerb, "%s", rectxine);
       }

    } else if(infochanged && usenonrect) {

       usenonrect = FALSE;
       xf86DrvMsgVerb(pScrn1->scrnIndex, X_INFO, pSiS->XineVerb,
		"Only clone modes available for this screen size...\n");
       xf86DrvMsgVerb(pScrn1->scrnIndex, X_INFO, pSiS->XineVerb, "%s", rectxine);

    }

    if(pSiS->maxCRT1_X1) {		/* We have at least one non-clone mode */

       /* Use the virtuals from pScrn here; we need absolute
        * numbers, not the "relative" ones in realVirtual.
        */
       virtualX = pScrn1->virtualX;
       virtualY = pScrn1->virtualY;

       switch(srel) {

       case sisRightOf:
	  x1 = 0;
	  w1 = max(pSiS->maxCRT1_X1, virtualX - pSiS->maxCRT1_X2);
	  if(w1 > virtualX) w1 = virtualX;

	  x2 = min(pSiS->maxCRT2_X1, virtualX - pSiS->maxCRT2_X2);
	  if(x2 < 0) x2 = 0;

	  /* Avoid overlapping xinerama screens: If the current mode
	   * fills entire virtual, we use the current mode dimensions
	   * as screen limits instead of the data calculated above.
	   * (Can use virtualX instead if realVirtualX here, it is the
	   * same.)
	   */
	  if((x1 + w1) > x2) {
	     if(currH1 + currH2 == virtualX) {
		w1 = x2 = currH1;
	     }
	  }

	  w2 = virtualX - x2;

	  y1 = pSiS->CRT1YOffs;
	  h1 = realvirtY;

	  y2 = pSiS->CRT2YOffs;
	  h2 = realvirtY;

	  break;

       case sisLeftOf:
	  x1 = min(pSiS->maxCRT1_X2, virtualX - pSiS->maxCRT1_X1);
	  if(x1 < 0) x1 = 0;

	  x2 = 0;
	  w2 = max(pSiS->maxCRT2_X2, virtualX - pSiS->maxCRT2_X1);
	  if(w2 > virtualX) w2 = virtualX;

	  if((x2 + w2) > x1) {
	     if(currH1 + currH2 == virtualX) {
		w2 = x1 = currH2;
	     }
	  }

	  w1 = virtualX - x1;

	  y1 = pSiS->CRT1YOffs;
	  h1 = realvirtY;

	  y2 = pSiS->CRT2YOffs;
	  h2 = realvirtY;

	  break;

       case sisBelow:
	  x1 = pSiS->CRT1XOffs;
	  w1 = realvirtX;

	  x2 = pSiS->CRT2XOffs;
	  w2 = realvirtX;

	  y1 = 0;
	  h1 = max(pSiS->maxCRT1_Y1, virtualY - pSiS->maxCRT1_Y2);
	  if(h1 > virtualY) h1 = virtualY;

	  y2 = min(pSiS->maxCRT2_Y1, virtualY - pSiS->maxCRT2_Y2);
	  if(y2 < 0) y2 = 0;

	  if((y1 + h1) > y2) {
	     if(currV1 + currV2 == virtualY) {
		h1 = y2 = currV1;
	     }
	  }

	  h2 = virtualY - y2;

	  break;

       case sisAbove:
	  x1 = pSiS->CRT1XOffs;
	  w1 = realvirtX;

	  x2 = pSiS->CRT2XOffs;
	  w2 = realvirtX;

	  y1 = min(pSiS->maxCRT1_Y2, virtualY - pSiS->maxCRT1_Y1);
	  if(y1 < 0) y1 = 0;

	  y2 = 0;
	  h2 = max(pSiS->maxCRT2_Y2, virtualY - pSiS->maxCRT2_Y1);
	  if(h2 > virtualY) h2 = virtualY;

	  if((y2 + h2) > y1) {
	     if(currV1 + currV2 == virtualY) {
		h2 = y1 = currV2;
	     }
	  }

	  h1 = virtualY - y1;

	  break;

       default:
	  break;
       }

       /* Calculate dead areas */
       switch(srel) {

       case sisLeftOf:
       case sisRightOf:
	  if(usenonrect) {
	     if(pSiS->maxCRT1_Y1 != realvirtY) {
	        h1 = pSiS->MBXNRYMAX = pSiS->maxCRT1_Y1;
	        pSiS->NonRectDead.x0 = x1;
	        pSiS->NonRectDead.x1 = x1 + w1 - 1;
	        pSiS->NonRectDead.y0 = y1 + h1;
	        pSiS->NonRectDead.y1 = virtualY - 1;
	        pSiS->HaveNonRect = TRUE;
	     } else if(pSiS->maxCRT2_Y2 != realvirtY) {
	        h2 = pSiS->MBXNRYMAX = pSiS->maxCRT2_Y2;
	        pSiS->NonRectDead.x0 = x2;
	        pSiS->NonRectDead.x1 = x2 + w2 - 1;
	        pSiS->NonRectDead.y0 = y2 + h2;
	        pSiS->NonRectDead.y1 = virtualY - 1;
	        pSiS->HaveNonRect = TRUE;
	     }
	  }

	  if(pSiS->CRT1YOffs) {
	     pSiS->OffDead1.x0 = x1;
	     pSiS->OffDead1.x1 = x1 + w1 - 1;
	     pSiS->OffDead1.y0 = 0;
	     pSiS->OffDead1.y1 = y1 - 1;
	     pSiS->OffDead2.x0 = x2;
	     pSiS->OffDead2.x1 = x2 + w2 - 1;
	     pSiS->OffDead2.y0 = y2 + h2;
	     pSiS->OffDead2.y1 = virtualY - 1;
	     pSiS->HaveOffsRegions = TRUE;
	  } else if(pSiS->CRT2YOffs) {
	     pSiS->OffDead1.x0 = x2;
	     pSiS->OffDead1.x1 = x2 + w2 - 1;
	     pSiS->OffDead1.y0 = 0;
	     pSiS->OffDead1.y1 = y2 - 1;
	     pSiS->OffDead2.x0 = x1;
	     pSiS->OffDead2.x1 = x1 + w1 - 1;
	     pSiS->OffDead2.y0 = y1 + h1;
	     pSiS->OffDead2.y1 = virtualY - 1;
	     pSiS->HaveOffsRegions = TRUE;
	  }
	  break;

       case sisAbove:
       case sisBelow:
	  if(usenonrect) {
	     if(pSiS->maxCRT1_X1 != realvirtX) {
		w1 = pSiS->MBXNRXMAX = pSiS->maxCRT1_X1;
		pSiS->NonRectDead.x0 = x1 + w1;
		pSiS->NonRectDead.x1 = virtualX - 1;
		pSiS->NonRectDead.y0 = y1;
		pSiS->NonRectDead.y1 = y1 + h1 - 1;
		pSiS->HaveNonRect = TRUE;
	     } else if(pSiS->maxCRT2_X2 != realvirtX) {
		w2 = pSiS->MBXNRXMAX = pSiS->maxCRT2_X2;
		pSiS->NonRectDead.x0 = x2 + w2;
		pSiS->NonRectDead.x1 = virtualX - 1;
		pSiS->NonRectDead.y0 = y2;
		pSiS->NonRectDead.y1 = y2 + h2 - 1;
		pSiS->HaveNonRect = TRUE;
	     }
	  }

	  if(pSiS->CRT1XOffs) {
	     pSiS->OffDead1.x0 = x2 + w2;
	     pSiS->OffDead1.x1 = virtualX - 1;
	     pSiS->OffDead1.y0 = y2;
	     pSiS->OffDead1.y1 = y2 + h2 - 1;
	     pSiS->OffDead2.x0 = 0;
	     pSiS->OffDead2.x1 = x1 - 1;
	     pSiS->OffDead2.y0 = y1;
	     pSiS->OffDead2.y1 = y1 + h1 - 1;
	     pSiS->HaveOffsRegions = TRUE;
	  } else if(pSiS->CRT2XOffs) {
	     pSiS->OffDead1.x0 = x1 + w1;
	     pSiS->OffDead1.x1 = virtualX - 1;
	     pSiS->OffDead1.y0 = y1;
	     pSiS->OffDead1.y1 = y1 + h1 - 1;
	     pSiS->OffDead2.x0 = 0;
	     pSiS->OffDead2.x1 = x2 - 1;
	     pSiS->OffDead2.y0 = y2;
	     pSiS->OffDead2.y1 = y2 + h2 - 1;
	     pSiS->HaveOffsRegions = TRUE;
	  }
       default:
	  break;
       }

    } else {	/* Only clone-modes left */

       x1 = x2 = 0;
       y1 = y2 = 0;
       w1 = w2 = max(pSiS->maxClone_X1, pSiS->maxClone_X2);
       h1 = h2 = max(pSiS->maxClone_Y1, pSiS->maxClone_Y2);

    }

    /* Now let's think about the screen number: */

    crt1scrnnum = -1;
    switch(srel) {
    case sisRightOf:	/* MFBScr0LR: TRUE = Left is 0, FALSE = Right is 0 */
       if(pSiS->MFBScr0LR != -1) {
          crt1scrnnum = (pSiS->MFBScr0LR) ? 0 : 1;
       }
       break;
    case sisLeftOf:
       if(pSiS->MFBScr0LR != -1) {
          crt1scrnnum = (pSiS->MFBScr0LR) ? 1 : 0;
       }
       break;
    case sisBelow:	/* MFBScr0TB: TRUE = Top is 0, FALSE = Bottom is 0 */
       if(pSiS->MFBScr0TB != -1) {
          crt1scrnnum = (pSiS->MFBScr0TB) ? 0 : 1;
       }
       break;
    case sisAbove:
       if(pSiS->MFBScr0TB != -1) {
          crt1scrnnum = (pSiS->MFBScr0TB) ? 1 : 0;
       }
    default:
       break;
    }

    if(crt1scrnnum == -1) {

       crt1scrnnum = 0;

       if(pSiS->CRT2IsScrn0) {
          crt1scrnnum = 1;
       }

    }

    crt2scrnnum = crt1scrnnum ^ 1;

    if((SiSXineramadataPtr[crt1scrnnum].x == x1) &&
       (SiSXineramadataPtr[crt1scrnnum].y == y1) &&
       (SiSXineramadataPtr[crt1scrnnum].width == w1) &&
       (SiSXineramadataPtr[crt1scrnnum].height == h1) &&
       (SiSXineramadataPtr[crt2scrnnum].x == x2) &&
       (SiSXineramadataPtr[crt2scrnnum].y == y2) &&
       (SiSXineramadataPtr[crt2scrnnum].width == w2) &&
       (SiSXineramadataPtr[crt2scrnnum].height == h2)) {
       infochanged = FALSE;
    } else {
       SiSXineramadataPtr[crt1scrnnum].x = x1;
       SiSXineramadataPtr[crt1scrnnum].y = y1;
       SiSXineramadataPtr[crt1scrnnum].width = w1;
       SiSXineramadataPtr[crt1scrnnum].height = h1;
       SiSXineramadataPtr[crt2scrnnum].x = x2;
       SiSXineramadataPtr[crt2scrnnum].y = y2;
       SiSXineramadataPtr[crt2scrnnum].width = w2;
       SiSXineramadataPtr[crt2scrnnum].height = h2;
    }

    if(infochanged) {

       /* Send XineramaLayoutChanged events */
       WalkTree(pScreen, SiSTellChanged, (pointer)pScreen);

       xf86DrvMsgVerb(pScrn1->scrnIndex, X_INFO, pSiS->XineVerb,
	  "Pseudo-Xinerama: Screen %d (%d,%d)-(%d,%d)\n",
	  crt1scrnnum, x1, y1, w1+x1-1, h1+y1-1);
       xf86DrvMsgVerb(pScrn1->scrnIndex, X_INFO, pSiS->XineVerb,
	  "Pseudo-Xinerama: Screen %d (%d,%d)-(%d,%d)\n",
	  crt2scrnnum, x2, y2, w2+x2-1, h2+y2-1);

       if(pSiS->HaveNonRect) {
	  xf86DrvMsgVerb(pScrn1->scrnIndex, X_INFO, pSiS->XineVerb,
		"Pseudo-Xinerama: Inaccessible area (%d,%d)-(%d,%d)\n",
		pSiS->NonRectDead.x0, pSiS->NonRectDead.y0,
		pSiS->NonRectDead.x1, pSiS->NonRectDead.y1);
       }

       if(pSiS->HaveOffsRegions) {
	  xf86DrvMsgVerb(pScrn1->scrnIndex, X_INFO, pSiS->XineVerb,
		"Pseudo-Xinerama: Inaccessible offset area (%d,%d)-(%d,%d)\n",
		pSiS->OffDead1.x0, pSiS->OffDead1.y0,
		pSiS->OffDead1.x1, pSiS->OffDead1.y1);
	  xf86DrvMsgVerb(pScrn1->scrnIndex, X_INFO, pSiS->XineVerb,
		"Pseudo-Xinerama: Inaccessible offset area (%d,%d)-(%d,%d)\n",
		pSiS->OffDead2.x0, pSiS->OffDead2.y0,
		pSiS->OffDead2.x1, pSiS->OffDead2.y1);
       }

    }
}

/* Proc */

static int
SiSProcXineramaQueryVersion(ClientPtr client)
{
    xPanoramiXQueryVersionReply	  rep;
    register int		  n;

    REQUEST_SIZE_MATCH(xPanoramiXQueryVersionReq);
    rep.type = X_Reply;
    rep.length = 0;
    rep.sequenceNumber = client->sequence;
    rep.majorVersion = SIS_XINERAMA_MAJOR_VERSION;
    rep.minorVersion = SIS_XINERAMA_MINOR_VERSION;
    if(client->swapped) {
       _swaps(&rep.sequenceNumber, n);
       _swapl(&rep.length, n);
       _swaps(&rep.majorVersion, n);
       _swaps(&rep.minorVersion, n);
    }
    WriteToClient(client, sizeof(xPanoramiXQueryVersionReply), (char *)&rep);
    return (client->noClientException);
}

static int
SiSProcXineramaGetState(ClientPtr client)
{
    REQUEST(xPanoramiXGetStateReq);
    WindowPtr			pWin;
    xPanoramiXGetStateReply	rep;
    register int		n;
    int                         rc;

    REQUEST_SIZE_MATCH(xPanoramiXGetStateReq);
    rc = dixLookupWindow(&pWin, stuff->window, client, DixGetAttrAccess);
    if (rc != Success)
       return rc;

    rep.type = X_Reply;
    rep.length = 0;
    rep.sequenceNumber = client->sequence;
    rep.state = !SiSnoPanoramiXExtension;
    if(client->swapped) {
       _swaps (&rep.sequenceNumber, n);
       _swapl (&rep.length, n);
       //_swaps (&rep.state, n); // XXX???
    }
    WriteToClient(client, sizeof(xPanoramiXGetStateReply), (char *)&rep);
    return client->noClientException;
}

static int
SiSProcXineramaGetScreenCount(ClientPtr client)
{
    REQUEST(xPanoramiXGetScreenCountReq);
    WindowPtr				pWin;
    xPanoramiXGetScreenCountReply	rep;
    register int			n;

    REQUEST_SIZE_MATCH(xPanoramiXGetScreenCountReq);
    if(Success != dixLookupWindow(&pWin, stuff->window, client, DixReadAccess))
      return BadWindow;

    rep.type = X_Reply;
    rep.length = 0;
    rep.sequenceNumber = client->sequence;
    rep.ScreenCount = SiSXineramaNumScreens;
    if(client->swapped) {
       _swaps(&rep.sequenceNumber, n);
       _swapl(&rep.length, n);
       //_swaps(&rep.ScreenCount, n); // XXX???
    }
    WriteToClient(client, sizeof(xPanoramiXGetScreenCountReply), (char *)&rep);
    return client->noClientException;
}

static int
SiSProcXineramaGetScreenSize(ClientPtr client)
{
    REQUEST(xPanoramiXGetScreenSizeReq);
    WindowPtr				pWin;
    xPanoramiXGetScreenSizeReply	rep;
    register int			n;

    REQUEST_SIZE_MATCH(xPanoramiXGetScreenSizeReq);
    if(Success != dixLookupWindow(&pWin, stuff->window, client, DixReadAccess))
      return BadWindow;

    rep.type = X_Reply;
    rep.length = 0;
    rep.sequenceNumber = client->sequence;
    rep.width  = SiSXineramadataPtr[stuff->screen].width;
    rep.height = SiSXineramadataPtr[stuff->screen].height;
    if(client->swapped) {
       _swaps(&rep.sequenceNumber, n);
       _swapl(&rep.length, n);
       _swapl(&rep.width, n);
       _swapl(&rep.height, n);
    }
    WriteToClient(client, sizeof(xPanoramiXGetScreenSizeReply), (char *)&rep);
    return client->noClientException;
}

static int
SiSProcXineramaIsActive(ClientPtr client)
{
    xXineramaIsActiveReply	rep;

    REQUEST_SIZE_MATCH(xXineramaIsActiveReq);

    rep.type = X_Reply;
    rep.length = 0;
    rep.sequenceNumber = client->sequence;
    rep.state = !SiSnoPanoramiXExtension;
    if(client->swapped) {
       register int n;
       _swaps(&rep.sequenceNumber, n);
       _swapl(&rep.length, n);
       _swapl(&rep.state, n);
    }
    WriteToClient(client, sizeof(xXineramaIsActiveReply), (char *) &rep);
    return client->noClientException;
}

static int
SiSProcXineramaQueryScreens(ClientPtr client)
{
    xXineramaQueryScreensReply	rep;

    REQUEST_SIZE_MATCH(xXineramaQueryScreensReq);

    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    rep.number = (SiSnoPanoramiXExtension) ? 0 : SiSXineramaNumScreens;
    rep.length = rep.number * sz_XineramaScreenInfo >> 2;
    if(client->swapped) {
       register int n;
       _swaps(&rep.sequenceNumber, n);
       _swapl(&rep.length, n);
       _swapl(&rep.number, n);
    }
    WriteToClient(client, sizeof(xXineramaQueryScreensReply), (char *)&rep);

    if(!SiSnoPanoramiXExtension) {
       xXineramaScreenInfo scratch;
       int i;

       for(i = 0; i < SiSXineramaNumScreens; i++) {
	  scratch.x_org  = SiSXineramadataPtr[i].x;
	  scratch.y_org  = SiSXineramadataPtr[i].y;
	  scratch.width  = SiSXineramadataPtr[i].width;
	  scratch.height = SiSXineramadataPtr[i].height;
	  if(client->swapped) {
	     register int n;
	     _swaps(&scratch.x_org, n);
	     _swaps(&scratch.y_org, n);
	     _swaps(&scratch.width, n);
	     _swaps(&scratch.height, n);
	  }
	  WriteToClient(client, sz_XineramaScreenInfo, (char *)&scratch);
       }
    }

    return client->noClientException;
}

static int
SiSProcXineramaSelectInput(ClientPtr client)
{
    REQUEST(xXineramaSelectInputReq);
    WindowPtr pWin;
    SiSXineramaEventPtr pXineramaEvent, pNewXineramaEvent, *pHead;
    XID clientResource;
    int lookup_ret;

    REQUEST_SIZE_MATCH(xXineramaSelectInputReq);
    int rc = dixLookupWindow(&pWin, stuff->window, client, DixWriteAccess);
    if (rc != Success)
      return BadWindow;

    #if NEW_XORG_VERSION == 1 /*New Xorg Version >= 1.4 */
        lookup_ret = dixLookupResourceByType((pointer) &pHead,
                                                pWin->drawable.id, EventType,
                                                client, DixWriteAccess);
        pHead = (lookup_ret == Success ? pHead : NULL);
    #else
      pHead = (SiSXineramaEventPtr *)SecurityLookupIDByType(client,
                                                 pWin->drawable.id, EventType,
                                                 SecurityWriteAccess);
    #endif

    if(stuff->enable & (XineramaLayoutChangeNotifyMask)) {

       /* Check for existing entry */
       if(pHead) {
	  for(pXineramaEvent = *pHead; pXineramaEvent; pXineramaEvent = pXineramaEvent->next) {
	     if(pXineramaEvent->client == client) {
		return Success;
	     }
	  }
       }

       /* Build a new entry */
       if(!(pNewXineramaEvent = (SiSXineramaEventPtr)malloc(sizeof(SiSXineramaEventRec)))) {
	  return BadAlloc;
       }
       pNewXineramaEvent->next = 0;
       pNewXineramaEvent->client = client;
       pNewXineramaEvent->window = pWin;
       pNewXineramaEvent->mask = stuff->enable;

       /*
	* Add a resource that will be deleted when
	* the client goes away
	*/
       clientResource = FakeClientID(client->index);
       pNewXineramaEvent->clientResource = clientResource;
       if(!AddResource(clientResource, ClientType, (pointer)pNewXineramaEvent)) {
	  return BadAlloc;
       }

       /*
	* Create a resource to contain a pointer to the list
	* of clients selecting input. This must be indirect as
	* the list may be arbitrarily rearranged which cannot be
	* done through the resource database.
	*/
       if(!pHead) {
	  pHead = (SiSXineramaEventPtr *)malloc(sizeof(SiSXineramaEventPtr));
	  if(!pHead || !AddResource(pWin->drawable.id, EventType, (pointer)pHead)) {
	     FreeResource(clientResource, RT_NONE);
	     return BadAlloc;
	  }
	  *pHead = NULL;
       }
       pNewXineramaEvent->next = *pHead;
       *pHead = pNewXineramaEvent;

       SiSXineramaClientsListening++;

    } else if(stuff->enable == xFalse) {

       /* Delete the interest */
       if(pHead) {
	  pNewXineramaEvent = NULL;
	  for(pXineramaEvent = *pHead; pXineramaEvent; pXineramaEvent = pXineramaEvent->next) {
	     if(pXineramaEvent->client == client)
		break;
	     pNewXineramaEvent = pXineramaEvent;
	  }
	  if(pXineramaEvent) {
	     FreeResource(pXineramaEvent->clientResource, ClientType);
	     if(pNewXineramaEvent) {
		pNewXineramaEvent->next = pXineramaEvent->next;
	     } else {
		*pHead = pXineramaEvent->next;
	     }
	     free(pXineramaEvent);
	     SiSXineramaClientsListening--;
	  }
       }

    } else {

	client->errorValue = stuff->enable;
	return BadValue;

    }

    return Success;
}


static int
SiSProcXineramaDispatch(ClientPtr client)
{
    REQUEST(xReq);
    switch (stuff->data) {
    case X_PanoramiXQueryVersion:
       return SiSProcXineramaQueryVersion(client);
    case X_PanoramiXGetState:
       return SiSProcXineramaGetState(client);
    case X_PanoramiXGetScreenCount:
       return SiSProcXineramaGetScreenCount(client);
    case X_PanoramiXGetScreenSize:
       return SiSProcXineramaGetScreenSize(client);
    case X_XineramaIsActive:
       return SiSProcXineramaIsActive(client);
    case X_XineramaQueryScreens:
       return SiSProcXineramaQueryScreens(client);
    case X_XineramaSelectInput:
       return SiSProcXineramaSelectInput(client);
    }
    return BadRequest;
}

/* SProc */

static int
SiSSProcXineramaQueryVersion (ClientPtr client)
{
    REQUEST(xPanoramiXQueryVersionReq);
    register int n;
    _swaps(&stuff->length,n);
    REQUEST_SIZE_MATCH (xPanoramiXQueryVersionReq);
    return SiSProcXineramaQueryVersion(client);
}

static int
SiSSProcXineramaGetState(ClientPtr client)
{
    REQUEST(xPanoramiXGetStateReq);
    register int n;
    _swaps (&stuff->length, n);
    REQUEST_SIZE_MATCH(xPanoramiXGetStateReq);
    return SiSProcXineramaGetState(client);
}

static int
SiSSProcXineramaGetScreenCount(ClientPtr client)
{
    REQUEST(xPanoramiXGetScreenCountReq);
    register int n;
    _swaps (&stuff->length, n);
    REQUEST_SIZE_MATCH(xPanoramiXGetScreenCountReq);
    return SiSProcXineramaGetScreenCount(client);
}

static int
SiSSProcXineramaGetScreenSize(ClientPtr client)
{
    REQUEST(xPanoramiXGetScreenSizeReq);
    register int n;
    _swaps (&stuff->length, n);
    REQUEST_SIZE_MATCH(xPanoramiXGetScreenSizeReq);
    return SiSProcXineramaGetScreenSize(client);
}

static int
SiSSProcXineramaIsActive(ClientPtr client)
{
    REQUEST(xXineramaIsActiveReq);
    register int n;
    _swaps (&stuff->length, n);
    REQUEST_SIZE_MATCH(xXineramaIsActiveReq);
    return SiSProcXineramaIsActive(client);
}

static int
SiSSProcXineramaQueryScreens(ClientPtr client)
{
    REQUEST(xXineramaQueryScreensReq);
    register int n;
    _swaps (&stuff->length, n);
    REQUEST_SIZE_MATCH(xXineramaQueryScreensReq);
    return SiSProcXineramaQueryScreens(client);
}

static int
SiSSProcXineramaSelectInput(ClientPtr client)
{
    REQUEST(xXineramaSelectInputReq);
    register int n;
    _swaps(&stuff->length, n);
    _swapl(&stuff->window, n);
    return SiSProcXineramaSelectInput(client);
}

static int
SiSSProcXineramaDispatch(ClientPtr client)
{
    REQUEST(xReq);
    switch (stuff->data) {
    case X_PanoramiXQueryVersion:
       return SiSSProcXineramaQueryVersion(client);
    case X_PanoramiXGetState:
       return SiSSProcXineramaGetState(client);
    case X_PanoramiXGetScreenCount:
       return SiSSProcXineramaGetScreenCount(client);
    case X_PanoramiXGetScreenSize:
       return SiSSProcXineramaGetScreenSize(client);
    case X_XineramaIsActive:
       return SiSSProcXineramaIsActive(client);
    case X_XineramaQueryScreens:
       return SiSSProcXineramaQueryScreens(client);
    case X_XineramaSelectInput:
       return SiSSProcXineramaSelectInput(client);
    }
    return BadRequest;
}

static void
SiSXineramaResetProc(ExtensionEntry* extEntry)
{
    /* Called by CloseDownExtensions() */
    if(SiSXineramadataPtr) {
       free(SiSXineramadataPtr);
       SiSXineramadataPtr = NULL;
    }
}

static int
SiSXineramaFreeClient(pointer data, XID id)
{
    SiSXineramaEventPtr pXineramaEvent = (SiSXineramaEventPtr)data;
    SiSXineramaEventPtr *pHead, pCur, pPrev;
    WindowPtr pWin = pXineramaEvent->window;

    dixLookupResourceByType((pointer) &pHead, pWin->drawable.id, EventType, NullClient, DixUnknownAccess);
    if(pHead) {
       pPrev = NULL;
       for(pCur = *pHead; pCur && pCur != pXineramaEvent; pCur = pCur->next) {
	  pPrev = pCur;
       }
       if(pCur) {
	  if(pPrev) pPrev->next = pXineramaEvent->next;
	  else      *pHead = pXineramaEvent->next;
       }
    }
    free((pointer)pXineramaEvent);
    return 1;
}

static int
SiSXineramaFreeEvents(pointer data, XID id)
{
    SiSXineramaEventPtr *pHead, pCur, pNext;

    pHead = (SiSXineramaEventPtr *)data;
    for(pCur = *pHead; pCur; pCur = pNext) {
       pNext = pCur->next;
       FreeResource(pCur->clientResource, ClientType);
       free((pointer)pCur);
    }
    free((pointer)pHead);
    return 1;
}

static void
SXineramaLayoutChangeNotifyEvent(xXineramaLayoutChangeNotifyEvent *from,
				 xXineramaLayoutChangeNotifyEvent *to)
{
    to->type = from->type;
    cpswapl(from->window, to->window);
}

void
SiSXineramaExtensionInit(ScrnInfoPtr pScrn)
{
    SISPtr	pSiS = SISPTR(pScrn);
    Bool	success = FALSE;
    const char	*sispx = "SiS Pseudo-Xinerama";

    if(!SiSXineramadataPtr) {

       if(!pSiS->MergedFB) {
	  SiSnoPanoramiXExtension = TRUE;
	  pSiS->MouseRestrictions = FALSE;
	  return;
       }

#ifdef PANORAMIX
       if(!noPanoramiXExtension) {
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	     "Xinerama active, not initializing %s\n", sispx);
	  SiSnoPanoramiXExtension = TRUE;
	  pSiS->MouseRestrictions = FALSE;
	  return;
       }
#endif

       if(SiSnoPanoramiXExtension) {
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	      "%s disabled\n", sispx);
	  pSiS->MouseRestrictions = FALSE;
	  return;
       }

       if(pSiS->CRT2Position == sisClone) {
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	     "Running MergedFB in Clone mode, %s disabled\n", sispx);
	  SiSnoPanoramiXExtension = TRUE;
	  pSiS->MouseRestrictions = FALSE;
	  return;
       }

       SiSXineramaNumScreens = 2;

       while(SiSXineramaGeneration != serverGeneration) {

	  ClientType = CreateNewResourceType(SiSXineramaFreeClient, "XineramaClient");
	  if(!ClientType)
	     break;

	  EventType = CreateNewResourceType(SiSXineramaFreeEvents, "XineramaEvents");
	  if(!EventType)
	     break;

	  pSiS->XineramaExtEntry = AddExtension(PANORAMIX_PROTOCOL_NAME, 1, 0,
					SiSProcXineramaDispatch,
					SiSSProcXineramaDispatch,
					SiSXineramaResetProc,
					StandardMinorOpcode);

	  if(!pSiS->XineramaExtEntry) break;

	  if(!(SiSXineramadataPtr = (SiSXineramaData *)
	        calloc(SiSXineramaNumScreens, sizeof(SiSXineramaData)))) break;

	  SiSXineramaEventbase = pSiS->XineramaExtEntry->eventBase;
	  EventSwapVector[SiSXineramaEventbase + XineramaLayoutChangeNotify] =
			(EventSwapPtr)SXineramaLayoutChangeNotifyEvent;

	  SiSXineramaGeneration = serverGeneration;
	  success = TRUE;
       }

       if(!success) {
	  SISErrorLog(pScrn, "Failed to initialize %s extension\n", sispx);
	  SiSnoPanoramiXExtension = TRUE;
	  pSiS->MouseRestrictions = FALSE;
	  return;
       }

       xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	  "%s extension initialized\n", sispx);

       pSiS->SiSXineramaVX = 0;
       pSiS->SiSXineramaVY = 0;
       SiSXineramaClientsListening = 0;

       pSiS->XineVerb = 3;
    }

    SiSUpdateXineramaScreenInfo(pScrn);

}
#endif  /* End of PseudoXinerama */

#else /* SISMERGED */

int i;	/* Suppress compiler warning */

#endif

