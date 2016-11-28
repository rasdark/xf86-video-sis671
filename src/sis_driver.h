/* $XFree86$ */
/* $XdotOrg$ */
/*
 * Global data and definitions
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
 * Author:     	Thomas Winischhofer <thomas@winischhofer.net>
 *
 */

/* VESA */
/*     The following is included because there are BIOSes out there that
 *     report incomplete mode lists. These are 630 BIOS versions <2.01.2x
 *     -) VBE 3.0 on SiS300 and 315 series do not support 24 fpp modes
 *     -) Only SiS315 series support 1920x1440x32
 */

static const UShort VESAModeIndices[] = {
   /*   x    y     8      16    (24)    32   */
       320, 200, 0x138, 0x10e, 0x000, 0x000,
       320, 240, 0x132, 0x135, 0x000, 0x000,
       400, 300, 0x133, 0x136, 0x000, 0x000,
       512, 384, 0x134, 0x137, 0x000, 0x000,
       640, 400, 0x100, 0x139, 0x000, 0x000,
       640, 480, 0x101, 0x111, 0x000, 0x13a,
       800, 600, 0x103, 0x114, 0x000, 0x13b,
      1024, 768, 0x105, 0x117, 0x000, 0x13c,
      1280,1024, 0x107, 0x11a, 0x000, 0x13d,
      1600,1200, 0x130, 0x131, 0x000, 0x13e,
      1920,1440, 0x13f, 0x140, 0x000, 0x141,
      9999,9999, 0,     0,     0,     0
};

/* For calculating refresh rate index (CR33) */
static const struct _sis_vrate {
    CARD16 idx;
    CARD16 xres;
    CARD16 yres;
    CARD16 refresh;
    Bool SiS730valid32bpp;
} sisx_vrate[] = {
	{1,  320,  200,  60,  TRUE}, {1,  320,  200,  70,  TRUE},
	{1,  320,  240,  60,  TRUE},
	{1,  400,  300,  60,  TRUE},
        {1,  512,  384,  60,  TRUE},
	{1,  640,  400,  60,  TRUE}, {1,  640,  400,  72,  TRUE},
	{1,  640,  480,  60,  TRUE}, {2,  640,  480,  72,  TRUE}, {3,  640,  480,  75,  TRUE},
	{4,  640,  480,  85,  TRUE}, {5,  640,  480, 100,  TRUE}, {6,  640,  480, 120,  TRUE},
	{7,  640,  480, 160, FALSE}, {8,  640,  480, 200, FALSE},
	{1,  720,  480,  60,  TRUE},
	{1,  720,  576,  60,  TRUE},
	{1,  768,  576,  60,  TRUE},
	{1,  800,  480,  60,  TRUE}, {2,  800,  480,  75,  TRUE}, {3,  800,  480,  85,  TRUE},
	{1,  800,  600,  56,  TRUE}, {2,  800,  600,  60,  TRUE}, {3,  800,  600,  72,  TRUE},
	{4,  800,  600,  75,  TRUE}, {5,  800,  600,  85,  TRUE}, {6,  800,  600, 105,  TRUE},
	{7,  800,  600, 120, FALSE}, {8,  800,  600, 160, FALSE},
	{1,  848,  480,  39,  TRUE}, {2,  848,  480,  60,  TRUE},
	{1,  856,  480,  39,  TRUE}, {2,  856,  480,  60,  TRUE},
	{1,  960,  540,  60,  TRUE},
	{1,  960,  600,  60,  TRUE},
	{1, 1024,  576,  60,  TRUE}, {2, 1024,  576,  75,  TRUE}, {3, 1024,  576,  85,  TRUE},
	{1, 1024,  600,  60,  TRUE},
	{1, 1024,  768,  43,  TRUE}, {2, 1024,  768,  60,  TRUE}, {3, 1024,  768,  70, FALSE},
	{4, 1024,  768,  75, FALSE}, {5, 1024,  768,  85,  TRUE}, {6, 1024,  768, 100,  TRUE},
	{7, 1024,  768, 120,  TRUE},
	{1, 1152,  768,  60,  TRUE},
	{1, 1152,  864,  60,  TRUE}, {2, 1152,  864,  75,  TRUE}, {3, 1152,  864,  84, FALSE},
	{1, 1280,  720,  60,  TRUE}, {2, 1280,  720,  75, FALSE}, {3, 1280,  720,  85,  TRUE},
	{1, 1280,  768,  60,  TRUE}, {2, 1280,  768,  75,  TRUE}, {3, 1280,  768,  85,  TRUE},
	{1, 1280,  800,  60,  TRUE}, {2, 1280,  800,  75,  TRUE}, {3, 1280,  800,  85,  TRUE},
	{1, 1280,  854,  60,  TRUE}, {2, 1280,  854,  75,  TRUE}, {3, 1280,  854,  85,  TRUE},
	{1, 1280,  960,  60,  TRUE}, {2, 1280,  960,  85,  TRUE},
	{1, 1280, 1024,  43, FALSE}, {2, 1280, 1024,  60,  TRUE}, {3, 1280, 1024,  75, FALSE},
	{4, 1280, 1024,  85,  TRUE},
	{1, 1360,  768,  60,  TRUE},
	{1, 1400, 1050,  60,  TRUE}, {2, 1400, 1050,  75,  TRUE},
	{1, 1440,  900,  60,  TRUE}, {2, 1440,  900,  75,  TRUE}, {3, 1440,  900,  85,  TRUE},
	{1, 1600, 1200,  60,  TRUE}, {2, 1600, 1200,  65,  TRUE}, {3, 1600, 1200,  70,  TRUE},
	{4, 1600, 1200,  75,  TRUE}, {5, 1600, 1200,  85,  TRUE}, {6, 1600, 1200, 100,  TRUE},
	{7, 1600, 1200, 120,  TRUE},
	{1, 1680, 1050,  60,  TRUE},
	{1, 1920, 1080,  30,  TRUE},
	{1, 1920, 1440,  60,  TRUE}, {2, 1920, 1440,  65,  TRUE}, {3, 1920, 1440,  70,  TRUE},
	{4, 1920, 1440,  75,  TRUE}, {5, 1920, 1440,  85,  TRUE}, {6, 1920, 1440, 100,  TRUE},
	{1, 2048, 1536,  60,  TRUE}, {2, 2048, 1536,  65,  TRUE}, {3, 2048, 1536,  70,  TRUE},
	{4, 2048, 1536,  75,  TRUE}, {5, 2048, 1536,  85,  TRUE},
	{0,    0,    0,   0, FALSE}
};

/*     Some 300-series laptops have a badly designed BIOS and make it
 *     impossible to detect the correct panel delay compensation. This
 *     table used to detect such machines by their PCI subsystem IDs;
 *     however, I don't know how reliable this method is. (With Asus
 *     machines, it is to general, ASUS uses the same ID for different
 *     boxes)
 */
static const pdctable mypdctable[] = {
        { 0x1071, 0x7522, 32, "Mitac", "7521T" },
	{ 0,      0,       0, ""     , ""      }
};

/*     These machines require setting/clearing a GPIO bit for enabling/
 *     disabling communication with the Chrontel TV encoder
 */
static const chswtable mychswtable[] = {
        { 0x1631, 0x1002, "Mitachi", "0x1002" },
	{ 0x1071, 0x7521, "Mitac"  , "7521P"  },
	{ 0,      0,      ""       , ""       }
};

/*     These machines require special timing/handling
 */
const customttable SiS_customttable[] = {
	{ SIS_630, "2.00.07", "09/27/2002-13:38:25",
	  0x3240A8,
	  { 0x220, 0x227, 0x228, 0x229, 0x0ee },
	  {  0x01,  0xe3,  0x9a,  0x6a,  0xef },
	  0x1039, 0x6300,
	  "Barco", "iQ R200L/300/400", CUT_BARCO1366, "BARCO_1366"
	},
	{ SIS_630, "2.00.07", "09/27/2002-13:38:25",
	  0x323FBD,
	  { 0x220, 0x227, 0x228, 0x229, 0x0ee },
	  {  0x00,  0x5a,  0x64,  0x41,  0xef },
	  0x1039, 0x6300,
	  "Barco", "iQ G200L/300/400/500", CUT_BARCO1024, "BARCO_1024"
	},
	{ SIS_650, "", "",
	  0,
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x0e11, 0x083c,
	  "Inventec (Compaq)", "3017cl/3045US", CUT_COMPAQ12802, "COMPAQ_1280"
	},
	{ SIS_650, "", "",
	  0,	/* Special 1024x768 / dual link */
	  { 0x00c, 0, 0, 0, 0 },
	  { 'e'  , 0, 0, 0, 0 },
	  0x1558, 0x0287,
	  "Clevo", "L285/L287 (Version 1)", CUT_CLEVO1024, "CLEVO_L28X_1"
	},
	{ SIS_650, "", "",
	  0,	/* Special 1024x768 / single link */
	  { 0x00c, 0, 0, 0, 0 },
	  { 'y'  , 0, 0, 0, 0 },
	  0x1558, 0x0287,
	  "Clevo", "L285/L287 (Version 2)", CUT_CLEVO10242, "CLEVO_L28X_2"
	},
	{ SIS_650, "", "",
	  0,	/* Special 1400x1050 */
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x1558, 0x0400,  /* possibly 401 and 402 as well; not panelsize specific? */
	  "Clevo", "D400S/D410S/D400H/D410H", CUT_CLEVO1400, "CLEVO_D4X0"
	},
	{ SIS_650, "", "",
	  0,	/* Shift LCD in LCD-via-CRT1 mode */
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x1558, 0x2263,
	  "Clevo", "D22ES/D27ES", CUT_UNIWILL1024, "CLEVO_D2X0ES"
	},
	{ SIS_650, "", "",
	  0,	/* Shift LCD in LCD-via-CRT1 mode */
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x1734, 0x101f,
	  "Uniwill", "N243S9", CUT_UNIWILL1024, "UNIWILL_N243S9"
	},
	{ SIS_650, "", "",
	  0,	/* Shift LCD in LCD-via-CRT1 mode */
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x1584, 0x5103,
	  "Uniwill", "N35BS1", CUT_UNIWILL10242, "UNIWILL_N35BS1"
	},
	{ SIS_650, "1.09.2c", "",  /* Other versions, too? */
	  0,	/* Shift LCD in LCD-via-CRT1 mode */
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x1019, 0x0f05,
	  "ECS", "A928", CUT_UNIWILL1024, "ECS_A928"
	},
	{ SIS_740, "1.11.27a", "",
	  0,
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x1043, 0x1612,
	  "Asus", "L3000D/L3500D", CUT_ASUSL3000D, "ASUS_L3X00"
	},
	{ SIS_650, "1.10.9k", "",
	  0,	/* For EMI */
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x1025, 0x0028,
	  "Acer", "Aspire 1700", CUT_ACER1280, "ACER_ASPIRE1700"
	},
	{ SIS_650, "1.10.7w", "",
	  0,	/* For EMI */
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x14c0, 0x0012,
	  "Compal", "??? (V1)", CUT_COMPAL1400_1, "COMPAL_1400_1"
	},
	{ SIS_650, "1.10.7x", "", /* New BIOS on its way (from BG.) */
	  0,	/* For EMI */
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x14c0, 0x0012,
	  "Compal", "??? (V2)", CUT_COMPAL1400_2, "COMPAL_1400_2"
	},
	{ SIS_650, "1.10.8o", "",
	  0,	/* For EMI (unknown) */
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x1043, 0x1612,
	  "Asus", "A2H (V1)", CUT_ASUSA2H_1, "ASUS_A2H_1"
	},
	{ SIS_650, "1.10.8q", "",
	  0,	/* For EMI */
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x1043, 0x1612,
	  "Asus", "A2H (V2)", CUT_ASUSA2H_2, "ASUS_A2H_2"
	},
#if 0
	{ SIS_550, "1.02.0z", "",
	  0x317f37,	/* 320x240 LCD panel */
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0, 0,
	  "AAEON", "AOP-8060", CUT_AOP8060, "AAEON_AOP_8060"
	},
#endif
	{ SIS_550, "1.02.00", "08/14/2002-10:02:21",
	  0x312201,
	  { 0x9560, 0x9600, 0x9768, 0x9c1a, 0 },
	  { 0x76,   0x84,   0xa7,   0x14,   0 },
	  0, 0,
	  "ICOP", "Vortex86-607x", CUT_ICOP550, "ICOP550"
	},
	{ SIS_550, "1.03.00", "06/25/2003-17:17:42",
	  0x2c0600,
	  { 0x7850, 0x78dc, 0x79e0, 0x7c9c, 0 },
	  { 0x84,   0x84,   0xa7,   0x90,   0 },
	  0, 0,
	  "ICOP", "Vortex86-607x (2)", CUT_ICOP550_2, "ICOP550_2"
	},
	{ 4321, "", "",			/* never autodetected */
	  0,
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0, 0,
	  "Generic", "LVDS/Parallel 848x480", CUT_PANEL848, "PANEL848x480"
	},
	{ 4322, "", "",			/* never autodetected */
	  0,
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0, 0,
	  "Generic", "LVDS/Parallel 856x480", CUT_PANEL856, "PANEL856x480"
	},
	{ 0, "", "",
	  0,
	  { 0, 0, 0, 0 },
	  { 0, 0, 0, 0 },
	  0, 0,
	  "", "", CUT_NONE, ""
	}
};

/*     Our TV modes for the 6326. The data in these structures
 *     is mainly correct, but since we use our private CR and
 *     clock values anyway, small errors do no matter.
 */
static DisplayModeRec SiS6326PAL800x600Mode = {
	NULL, NULL,     /* prev, next */
	"PAL800x600",   /* identifier of this mode */
	MODE_OK,        /* mode status */
	M_T_BUILTIN,    /* mode type */
	36000,		/* Clock frequency */
	800,		/* HDisplay */
	848,		/* HSyncStart */
	912,		/* HSyncEnd */
	1008,		/* HTotal */
	0,		/* HSkew */
	600,		/* VDisplay */
	600,		/* VSyncStart */
	602,		/* VSyncEnd */
	625,		/* VTotal */
	0,		/* VScan */
	V_PHSYNC | V_PVSYNC,	/* Flags */
	-1,		/* ClockIndex */
	36000,		/* SynthClock */
	800,		/* CRTC HDisplay */
	808,            /* CRTC HBlankStart */
	848,            /* CRTC HSyncStart */
	912,            /* CRTC HSyncEnd */
	1008,           /* CRTC HBlankEnd */
	1008,           /* CRTC HTotal */
	0,              /* CRTC HSkew */
	600,		/* CRTC VDisplay */
	600,		/* CRTC VBlankStart */
	600,		/* CRTC VSyncStart */
	602,		/* CRTC VSyncEnd */
	625,		/* CRTC VBlankEnd */
	625,		/* CRTC VTotal */
	FALSE,		/* CrtcHAdjusted */
	FALSE,		/* CrtcVAdjusted */
	0,		/* PrivSize */
	NULL,		/* Private */
	0.0,		/* HSync */
	0.0		/* VRefresh */
};

/*     Due to the scaling method this mode uses, the vertical data here
 *     does not match the CR data. But this does not matter, we use our
 *     private CR data anyway.
 */
static DisplayModeRec SiS6326PAL800x600UMode = {
	NULL,           /* prev */
	&SiS6326PAL800x600Mode, /* next */
	"PAL800x600U",  /* identifier of this mode */
	MODE_OK,        /* mode status */
	M_T_BUILTIN,    /* mode type */
	37120,		/* Clock frequency */
	800,		/* HDisplay */
	872,		/* HSyncStart */
	984,		/* HSyncEnd */
	1088,		/* HTotal */
	0,		/* HSkew */
	600,		/* VDisplay (548 due to scaling) */
	600,		/* VSyncStart (584) */
	602,		/* VSyncEnd (586) */
	625,		/* VTotal */
	0,		/* VScan */
	V_PHSYNC | V_PVSYNC,	/* Flags */
	-1,		/* ClockIndex */
	37120,		/* SynthClock */
	800,		/* CRTC HDisplay */
	808,            /* CRTC HBlankStart */
	872,            /* CRTC HSyncStart */
	984,            /* CRTC HSyncEnd */
	1024,           /* CRTC HBlankEnd */
	1088,           /* CRTC HTotal */
	0,              /* CRTC HSkew */
	600,		/* CRTC VDisplay (548 due to scaling) */
	600,		/* CRTC VBlankStart (600) */
	600,		/* CRTC VSyncStart (584) */
	602,		/* CRTC VSyncEnd (586) */
	625,		/* CRTC VBlankEnd */
	625,		/* CRTC VTotal */
	FALSE,		/* CrtcHAdjusted */
	FALSE,		/* CrtcVAdjusted */
	0,		/* PrivSize */
	NULL,		/* Private */
	0.0,		/* HSync */
	0.0		/* VRefresh */
};

static DisplayModeRec SiS6326PAL720x540Mode = {
	NULL,      	/* prev */
	&SiS6326PAL800x600UMode, /* next */
	"PAL720x540",   /* identifier of this mode */
	MODE_OK,        /* mode status */
	M_T_BUILTIN,    /* mode type */
	36000,		/* Clock frequency */
	720,		/* HDisplay */
	816,		/* HSyncStart */
	920,		/* HSyncEnd */
	1008,		/* HTotal */
	0,		/* HSkew */
	540,		/* VDisplay */
	578,		/* VSyncStart */
	580,		/* VSyncEnd */
	625,		/* VTotal */
	0,		/* VScan */
	V_PHSYNC | V_PVSYNC,	/* Flags */
	-1,		/* ClockIndex */
	36000,		/* SynthClock */
	720,		/* CRTC HDisplay */
	736,            /* CRTC HBlankStart */
	816,            /* CRTC HSyncStart */
	920,            /* CRTC HSyncEnd */
	1008,           /* CRTC HBlankEnd */
	1008,           /* CRTC HTotal */
	0,              /* CRTC HSkew */
	540,		/* CRTC VDisplay */
	577,		/* CRTC VBlankStart */
	578,		/* CRTC VSyncStart */
	580,		/* CRTC VSyncEnd */
	625,		/* CRTC VBlankEnd */
	625,		/* CRTC VTotal */
	FALSE,		/* CrtcHAdjusted */
	FALSE,		/* CrtcVAdjusted */
	0,		/* PrivSize */
	NULL,		/* Private */
	0.0,		/* HSync */
	0.0		/* VRefresh */
};

static DisplayModeRec SiS6326PAL640x480Mode = {
	NULL,      	/* prev */
	&SiS6326PAL720x540Mode, /* next */
	"PAL640x480",   /* identifier of this mode */
	MODE_OK,        /* mode status */
	M_T_BUILTIN,    /* mode type */
	36000,		/* Clock frequency */
	640,		/* HDisplay */
	768,		/* HSyncStart */
	920,		/* HSyncEnd */
	1008,		/* HTotal */
	0,		/* HSkew */
	480,		/* VDisplay */
	532,		/* VSyncStart */
	534,		/* VSyncEnd */
	625,		/* VTotal */
	0,		/* VScan */
	V_NHSYNC | V_NVSYNC,	/* Flags */
	-1,		/* ClockIndex */
	36000,		/* SynthClock */
	640,		/* CRTC HDisplay */
	648,            /* CRTC HBlankStart */
	768,            /* CRTC HSyncStart */
	920,            /* CRTC HSyncEnd */
	944,            /* CRTC HBlankEnd */
	1008,           /* CRTC HTotal */
	0,              /* CRTC HSkew */
	480,		/* CRTC VDisplay */
	481,		/* CRTC VBlankStart */
	532,		/* CRTC VSyncStart */
	534,		/* CRTC VSyncEnd */
	561,		/* CRTC VBlankEnd */
	625,		/* CRTC VTotal */
	FALSE,		/* CrtcHAdjusted */
	FALSE,		/* CrtcVAdjusted */
	0,		/* PrivSize */
	NULL,		/* Private */
	0.0,		/* HSync */
	0.0		/* VRefresh */
};

static DisplayModeRec SiS6326NTSC640x480Mode = {
	NULL, NULL,	/* prev, next */
	"NTSC640x480",  /* identifier of this mode */
	MODE_OK,        /* mode status */
	M_T_BUILTIN,    /* mode type */
	27000,		/* Clock frequency */
	640,		/* HDisplay */
	664,		/* HSyncStart */
	760,		/* HSyncEnd */
	800,		/* HTotal */
	0,		/* HSkew */
	480,		/* VDisplay */
	489,		/* VSyncStart */
	491,		/* VSyncEnd */
	525,		/* VTotal */
	0,		/* VScan */
	V_NHSYNC | V_NVSYNC,	/* Flags */
	-1,		/* ClockIndex */
	27000,		/* SynthClock */
	640,		/* CRTC HDisplay */
	648,            /* CRTC HBlankStart */
	664,            /* CRTC HSyncStart */
	760,            /* CRTC HSyncEnd */
	792,            /* CRTC HBlankEnd */
	800,            /* CRTC HTotal */
	0,              /* CRTC HSkew */
	480,		/* CRTC VDisplay */
	488,		/* CRTC VBlankStart */
	489,		/* CRTC VSyncStart */
	491,		/* CRTC VSyncEnd */
	517,		/* CRTC VBlankEnd */
	525,		/* CRTC VTotal */
	FALSE,		/* CrtcHAdjusted */
	FALSE,		/* CrtcVAdjusted */
	0,		/* PrivSize */
	NULL,		/* Private */
	0.0,		/* HSync */
	0.0		/* VRefresh */
};

/*     Due to the scaling method this mode uses, the vertical data here
 *     does not match the CR data. But this does not matter, we use our
 *     private CR data anyway.
 */
static DisplayModeRec SiS6326NTSC640x480UMode = {
	NULL, 		/* prev */
	&SiS6326NTSC640x480Mode, /* next */
	"NTSC640x480U", /* identifier of this mode */
	MODE_OK,        /* mode status */
	M_T_BUILTIN,    /* mode type */
	32215,		/* Clock frequency */
	640,		/* HDisplay */
	696,		/* HSyncStart */
	840,		/* HSyncEnd */
	856,		/* HTotal */
	0,		/* HSkew */
	480,		/* VDisplay (439 due to scaling) */
	489,		/* VSyncStart (473) */
	491,		/* VSyncEnd (475) */
	525,		/* VTotal */
	0,		/* VScan */
	V_NHSYNC | V_NVSYNC,	/* Flags */
	-1,		/* ClockIndex */
	32215,		/* SynthClock */
	640,		/* CRTC HDisplay */
	656,            /* CRTC HBlankStart */
	696,            /* CRTC HSyncStart */
	840,            /* CRTC HSyncEnd */
	856,            /* CRTC HBlankEnd */
	856,            /* CRTC HTotal */
	0,              /* CRTC HSkew */
	480,		/* CRTC VDisplay */
	488,		/* CRTC VBlankStart */
	489,		/* CRTC VSyncStart */
	491,		/* CRTC VSyncEnd */
	517,		/* CRTC VBlankEnd */
	525,		/* CRTC VTotal */
	FALSE,		/* CrtcHAdjusted */
	FALSE,		/* CrtcVAdjusted */
	0,		/* PrivSize */
	NULL,		/* Private */
	0.0,		/* HSync */
	0.0		/* VRefresh */
};


static DisplayModeRec SiS6326NTSC640x400Mode = {
	NULL, 	     	/* prev */
	&SiS6326NTSC640x480UMode, /* next */
	"NTSC640x400",  /* identifier of this mode */
	MODE_OK,        /* mode status */
	M_T_BUILTIN,    /* mode type */
	27000,		/* Clock frequency */
	640,		/* HDisplay */
	664,		/* HSyncStart */
	760,		/* HSyncEnd */
	800,		/* HTotal */
	0,		/* HSkew */
	400,		/* VDisplay */
	459,		/* VSyncStart */
	461,		/* VSyncEnd */
	525,		/* VTotal */
	0,		/* VScan */
	V_NHSYNC | V_NVSYNC,	/* Flags */
	-1,		/* ClockIndex */
	27000,		/* SynthClock */
	640,		/* CRTC HDisplay */
	648,            /* CRTC HBlankStart */
	664,            /* CRTC HSyncStart */
	760,            /* CRTC HSyncEnd */
	792,            /* CRTC HBlankEnd */
	800,            /* CRTC HTotal */
	0,              /* CRTC HSkew */
	400,		/* CRTC VDisplay */
	407,		/* CRTC VBlankStart */
	459,		/* CRTC VSyncStart */
	461,		/* CRTC VSyncEnd */
	490,		/* CRTC VBlankEnd */
	525,		/* CRTC VTotal */
	FALSE,		/* CrtcHAdjusted */
	FALSE,		/* CrtcVAdjusted */
	0,		/* PrivSize */
	NULL,		/* Private */
	0.0,		/* HSync */
	0.0		/* VRefresh */
};

/*     Built-in hi-res modes for the 6326.
 *     For some reason, our default mode lines and the
 *     clock calculation functions in sis_dac.c do no
 *     good job on higher clocks. It seems, the hardware
 *     needs some tricks so make mode with higher clock
 *     rates than ca. 120MHz work. I didn't bother trying
 *     to find out what exactly is going wrong, so I
 *     implemented two special modes instead for 1280x1024
 *     and 1600x1200. These two are automatically added
 *     to the list if they are supported with the current
 *     depth.
 *     The data in the strucures below is a proximation,
 *     in sis_vga.c the register contents are fetched from
 *     fixed tables anyway.
 */
static DisplayModeRec SiS6326SIS1280x1024_75Mode = {
	NULL, 	       	/* prev */
	NULL,           /* next */
	"SIS1280x1024-75",  /* identifier of this mode */
	MODE_OK,        /* mode status */
	M_T_BUILTIN,    /* mode type */
	135000,		/* Clock frequency */
	1280,		/* HDisplay */
	1296,		/* HSyncStart */
	1440,		/* HSyncEnd */
	1688,		/* HTotal */
	0,		/* HSkew */
	1024,		/* VDisplay */
	1025,		/* VSyncStart */
	1028,		/* VSyncEnd */
	1066,		/* VTotal */
	0,		/* VScan */
	V_PHSYNC | V_PVSYNC,	/* Flags */
	-1,		/* ClockIndex */
	135000,		/* SynthClock */
	1280,		/* CRTC HDisplay */
	1280,           /* CRTC HBlankStart */
	1296,           /* CRTC HSyncStart */
	1440,           /* CRTC HSyncEnd */
	1680,           /* CRTC HBlankEnd */
	1688,           /* CRTC HTotal */
	0,              /* CRTC HSkew */
	1024,		/* CRTC VDisplay */
	1024,		/* CRTC VBlankStart */
	1025,		/* CRTC VSyncStart */
	1028,		/* CRTC VSyncEnd */
	1065,		/* CRTC VBlankEnd */
	1066,		/* CRTC VTotal */
	FALSE,		/* CrtcHAdjusted */
	FALSE,		/* CrtcVAdjusted */
	0,		/* PrivSize */
	NULL,		/* Private */
	0.0,		/* HSync */
	0.0		/* VRefresh */
};

static DisplayModeRec SiS6326SIS1600x1200_60Mode = {
	NULL, 	       	/* prev */
	NULL,           /* next */
	"SIS1600x1200-60",  /* identifier of this mode */
	MODE_OK,        /* mode status */
	M_T_BUILTIN,    /* mode type */
	162000,		/* Clock frequency */
	1600,		/* HDisplay */
	1664,		/* HSyncStart */
	1856,		/* HSyncEnd */
	2160,		/* HTotal */
	0,		/* HSkew */
	1200,		/* VDisplay */
	1201,		/* VSyncStart */
	1204,		/* VSyncEnd */
	1250,		/* VTotal */
	0,		/* VScan */
	V_PHSYNC | V_PVSYNC,	/* Flags */
	-1,		/* ClockIndex */
	162000,		/* SynthClock */
	1600,		/* CRTC HDisplay */
	1600,           /* CRTC HBlankStart */
	1664,           /* CRTC HSyncStart */
	1856,           /* CRTC HSyncEnd */
	2152,            /* CRTC HBlankEnd */
	2160,            /* CRTC HTotal */
	0,              /* CRTC HSkew */
	1200,		/* CRTC VDisplay */
	1200,		/* CRTC VBlankStart */
	1201,		/* CRTC VSyncStart */
	1204,		/* CRTC VSyncEnd */
	1249,		/* CRTC VBlankEnd */
	1250,		/* CRTC VTotal */
	FALSE,		/* CrtcHAdjusted */
	FALSE,		/* CrtcVAdjusted */
	0,		/* PrivSize */
	NULL,		/* Private */
	0.0,		/* HSync */
	0.0		/* VRefresh */
};

/* For communication with the SiS Linux framebuffer driver (sisfb) */

/* ioctl for identifying and giving some info (esp. memory heap start) */
#define SISFB_GET_INFO_SIZE	0x8004f300
#define SISFB_GET_INFO		0x8000f301  /* Must be patched with result from ..._SIZE at D[29:16] */
/* deprecated ioctl number (for older versions of sisfb) */
#define SISFB_GET_INFO_OLD	0x80046ef8

/* ioctls for tv parameters (position) */
#define SISFB_SET_TVPOSOFFSET	0x4004f304

/* lock sisfb from register access */
#define SISFB_SET_LOCK		0x4004f306

/* Magic value for USB device */
#ifndef SISFB_USB_MAGIC
#define SISFB_USB_MAGIC		0x55aa2011
#endif

/* Structure argument for SISFB_GET_INFO ioctl  */
typedef struct _SISFB_INFO sisfb_info, *psisfb_info;

struct _SISFB_INFO {
	CARD32 	sisfb_id;		/* for identifying sisfb */
#ifndef SISFB_ID
#define SISFB_ID	  0x53495346	/* Identify myself with 'SISF' */
#endif
 	CARD32 	chip_id;		/* PCI ID of detected chip */
	CARD32	memory;			/* video memory in KB which sisfb manages */
	CARD32	heapstart;		/* heap start (= sisfb "mem" argument) in KB */
	CARD8 	fbvidmode;		/* current sisfb mode */

	CARD8 	sisfb_version;
	CARD8	sisfb_revision;
	CARD8 	sisfb_patchlevel;

	CARD8 	sisfb_caps;		/* sisfb's capabilities */

	CARD32 	sisfb_tqlen;		/* turbo queue length (in KB) */

	CARD32 	sisfb_pcibus;		/* The card's PCI bus ID. For USB, bus = SISFB_USB_MAGIC */
	CARD32 	sisfb_pcislot;		/* alias usbbus */
	CARD32 	sisfb_pcifunc;		/* alias usbdev */

	CARD8 	sisfb_lcdpdc;

	CARD8	sisfb_lcda;

	CARD32	sisfb_vbflags;
	CARD32	sisfb_currentvbflags;

	CARD32 	sisfb_scalelcd;
	CARD32 	sisfb_specialtiming;

	CARD8 	sisfb_haveemi;
	CARD8 	sisfb_emi30,sisfb_emi31,sisfb_emi32,sisfb_emi33;
	CARD8 	sisfb_haveemilcd;

	CARD8 	sisfb_lcdpdca;

	CARD16  sisfb_tvxpos, sisfb_tvypos;	/* Warning: Values + 32 ! */

	CARD32	sisfb_heapsize;			/* heap size (in KB) */
	CARD32	sisfb_videooffset;		/* Offset of viewport in video memory (in bytes) */

	CARD32	sisfb_curfstn;			/* currently running FSTN/DSTN mode */
	CARD32	sisfb_curdstn;

	CARD16	sisfb_pci_vendor;		/* PCI vendor (SiS or XGI) */

	CARD32	sisfb_vbflags2;

	CARD8	sisfb_can_post;			/* sisfb can POST this card */
	CARD8	sisfb_card_posted;		/* card is POSTED */
	CARD8	sisfb_was_boot_device;		/* This card was the boot video device (ie is primary) */

	CARD8 reserved[183];			/* for future use */
};

/* Mandatory functions */
static void SISIdentify(int flags);
static Bool SISProbe(DriverPtr drv, int flags);
static Bool SISPreInit(ScrnInfoPtr pScrn, int flags);
static Bool SISScreenInit(ScreenPtr pScreen, int argc, char **argv);
static Bool SISEnterVT(ScrnInfoPtr pScrn);
static void SISLeaveVT(ScrnInfoPtr pScrn);
static Bool SISCloseScreen(ScreenPtr pScreen);
static Bool SISSaveScreen(ScreenPtr pScreen, int mode);
static Bool SISSwitchMode(ScrnInfoPtr pScrn, DisplayModePtr mode);
static void SISNewAdjustFrame(ScrnInfoPtr pScrn, int x, int y);
static Bool SISPMEvent(ScrnInfoPtr pScrn, pmEvent event, Bool undo);/*APM-ACPI, adding by Ivans.*/

#if XSERVER_LIBPCIACCESS
static Bool SIS_pci_probe(DriverPtr driver, int entity_num, struct pci_device *device, intptr_t match_data);
#endif
/* ACPI Device Switch functions */
static Bool SISHotkeySwitchCRT1Status(ScrnInfoPtr pScrn,int onoff);/*hotkey pressing: switch CRT1 on/off*/
static Bool SISHotkeySwitchCRT2Status(ScrnInfoPtr pScrn,ULong newvbflags ,ULong newvbflags3);/*LCD on/off*/
static Bool SISHotkeySwitchMode(ScrnInfoPtr pScrn, Bool adjust);/*Resolution optimal function*/

/* Optional functions */
#ifdef SISDUALHEAD
static Bool	SISSaveScreenDH(ScreenPtr pScreen, int mode);
#endif
static void     SISFreeScreen(ScrnInfoPtr pScrn);
static ModeStatus SISValidMode(ScrnInfoPtr pScrn, DisplayModePtr mode,
				Bool verbose, int flags);
#ifdef SIS_HAVE_RR_FUNC
#ifdef SIS_HAVE_DRIVER_FUNC
static Bool SISDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op, pointer p);
#else
static Bool SISDriverFunc(ScrnInfoPtr pScrn, xorgRRFuncFlags op, xorgRRRotationPtr p);
#endif
#ifdef SISISXORG6899901
static Bool SiS_GetModeMM(ScrnInfoPtr pScrn, DisplayModePtr mode, int virtX, int virtY,
					int *mmWidth, int *mmHeight);
#endif
#endif

/* Internally used functions */
#ifdef SIS_NEED_MAP_IOP
static Bool	SISMapIOPMem(ScrnInfoPtr pScrn);
static Bool	SISUnmapIOPMem(ScrnInfoPtr pScrn);
#endif
void		SISAdjustFrame(ScrnInfoPtr pScrn, int x, int y);
UChar		SISSearchCRT1Rate(ScrnInfoPtr pScrn, DisplayModePtr mode);
UShort		SiS_CheckModeCRT1(ScrnInfoPtr pScrn, DisplayModePtr mode,
				 unsigned int VBFlags, unsigned int VBFlags3, Bool hcm);
UShort		SiS_CheckModeCRT2(ScrnInfoPtr pScrn, DisplayModePtr mode,
				 unsigned int VBFlags, unsigned int VBFlags3, Bool hcm);
void		SiSPrintModes(ScrnInfoPtr pScrn, Bool printfreq);
Bool		SiSBridgeIsInSlaveMode(ScrnInfoPtr pScrn);
UChar		SiS_GetSetBIOSScratch(ScrnInfoPtr pScrn, UShort offset, UChar value);
Bool		SISDetermineLCDACap(ScrnInfoPtr pScrn);
void		SISSaveDetectedDevices(ScrnInfoPtr pScrn);
void		SISErrorLog(ScrnInfoPtr pScrn, const char *format, ...);
void		SiSFindAspect(ScrnInfoPtr pScrn, xf86MonPtr pMonitor, int crtnum, Bool q);
DisplayModePtr	SiSDuplicateMode(DisplayModePtr source);
Bool		SiSMakeOwnModeList(ScrnInfoPtr pScrn, Bool acceptcustommodes,
				Bool includelcdmodes, Bool isfordvi, Bool *havecustommodes,
				Bool fakecrt2modes, Bool IsForCRT2);
xf86MonPtr	SiSInternalDDC(ScrnInfoPtr pScrn, int crtno);
Bool		SiSFixupHVRanges(ScrnInfoPtr pScrn, int mfbcrt, Bool quiet);
void		SiSClearModesPrivate(DisplayModePtr modelist);
DisplayModePtr	SiSSearchMode(ScrnInfoPtr pScrn, DisplayModePtr modelist,
			DisplayModePtr mode, Bool *needreset, Bool *needrecalcxine);
void		SISAdjustFrameHW_CRT1(ScrnInfoPtr pScrn, int x, int y);
void		SISAdjustFrameHW_CRT2(ScrnInfoPtr pScrn, int x, int y);
#if defined(SISDUALHEAD) || defined(SISMERGED)
int		SiSRemoveUnsuitableModes(ScrnInfoPtr pScrn, DisplayModePtr initial,
				const char *reason, Bool quiet);
#endif
#ifdef SISGAMMARAMP
void		SISCalculateGammaRamp(ScreenPtr pScreen, ScrnInfoPtr pScrn);
#endif

extern void	RecalcScreenPitch(ScrnInfoPtr pScrn);

/* Our very own vgaHW functions (sis_vga.c) */
extern void 	SiSVGASave(ScrnInfoPtr pScrn, SISRegPtr save, int flags);
extern void 	SiSVGARestore(ScrnInfoPtr pScrn, SISRegPtr restore, int flags);
extern void 	SiSVGASaveFonts(ScrnInfoPtr pScrn);
extern void 	SiSVGARestoreFonts(ScrnInfoPtr pScrn);
extern void 	SISVGALock(SISPtr pSiS);
extern void 	SiSVGAUnlock(SISPtr pSiS);
extern void 	SiSVGAProtect(ScrnInfoPtr pScrn, Bool on);
extern Bool 	SiSVGAMapMem(ScrnInfoPtr pScrn);
extern void 	SiSVGAUnmapMem(ScrnInfoPtr pScrn);
extern Bool 	SiSVGASaveScreen(ScreenPtr pScreen, int mode);

/* shadow, randr, randr-rotation */
extern void 	SISPointerMoved(ScrnInfoPtr pScrn, int x, int y);
extern void 	SISRefreshArea(ScrnInfoPtr pScrn, int num, BoxPtr pbox);
extern void 	SISRefreshAreaReflect(ScrnInfoPtr pScrn, int num, BoxPtr pbox);
extern void 	SISRefreshArea8(ScrnInfoPtr pScrn, int num, BoxPtr pbox);
extern void 	SISRefreshArea16(ScrnInfoPtr pScrn, int num, BoxPtr pbox);
extern void 	SISRefreshArea24(ScrnInfoPtr pScrn, int num, BoxPtr pbox);
extern void 	SISRefreshArea32(ScrnInfoPtr pScrn, int num, BoxPtr pbox);

/* vb */
extern void 	SISCRT1PreInit(ScrnInfoPtr pScrn);
extern void 	SISLCDPreInit(ScrnInfoPtr pScrn, Bool quiet);
extern void 	SISTVPreInit(ScrnInfoPtr pScrn, Bool quiet);
extern void 	SISCRT2PreInit(ScrnInfoPtr pScrn, Bool quiet);
extern void 	SISSense30x(ScrnInfoPtr pScrn, Bool quiet);
extern void 	SISSenseChrontel(ScrnInfoPtr pScrn, Bool quiet);
extern void     SiSSetupPseudoPanel(ScrnInfoPtr pScrn);
extern void	SiSPostSetModeTVParms(ScrnInfoPtr pScrn);

extern unsigned int    SiS_DetectVGA1(ScrnInfoPtr pScrn);/*only probe VGA1.(Ivans Lee)*/
	

/* utility */
extern void	SiSCtrlExtInit(ScrnInfoPtr pScrn);
extern void	SiSCtrlExtUnregister(SISPtr pSiS, int index);

/* init.c, init301.c ----- (use their data types!) */
extern unsigned short	SiS_GetModeID(int VGAEngine, unsigned int VBFlags, int HDisplay, int VDisplay,
				int Depth, BOOLEAN FSTN, int LCDwith, int LCDheight);
extern unsigned short	SiS_GetModeID_LCD(int VGAEngine, unsigned int VBFlags, int HDisplay,
				int VDisplay, int Depth, BOOLEAN FSTN, unsigned short CustomT,
				int LCDwith, int LCDheight, unsigned int VBFlags2);
extern unsigned short	SiS_GetModeID_TV(int VGAEngine, unsigned int VBFlags, int HDisplay,
				int VDisplay, int Depth, unsigned int VBFlags2);
extern unsigned short	SiS_GetModeID_VGA2(int VGAEngine, unsigned int VBFlags, int HDisplay,
				int VDisplay, int Depth, unsigned int VBFlags2);
extern int		SiSTranslateToVESA(ScrnInfoPtr pScrn, int modenumber);
extern int		SiSTranslateToOldMode(int modenumber);
extern BOOLEAN		SiSDetermineROMLayout661(struct SiS_Private *SiS_Pr);
extern BOOLEAN		SiSBIOSSetMode(struct SiS_Private *SiS_Pr,
                               ScrnInfoPtr pScrn, DisplayModePtr mode, BOOLEAN IsCustom);
extern BOOLEAN		SiSSetMode(struct SiS_Private *SiS_Pr,
                           ScrnInfoPtr pScrn, unsigned short ModeNo, BOOLEAN dosetpitch);
extern BOOLEAN		SiSBIOSSetModeCRT1(struct SiS_Private *SiS_Pr,
				   ScrnInfoPtr pScrn, DisplayModePtr mode, BOOLEAN IsCustom);
extern BOOLEAN		SiSBIOSSetModeCRT2(struct SiS_Private *SiS_Pr,
				   ScrnInfoPtr pScrn, DisplayModePtr mode, BOOLEAN IsCustom);
extern DisplayModePtr	SiSBuildBuiltInModeList(ScrnInfoPtr pScrn, BOOLEAN includelcdmodes,
					      BOOLEAN isfordvi, BOOLEAN fakecrt2modes, BOOLEAN IsForCRT2);
extern void		SiS_Chrontel701xBLOn(struct SiS_Private *SiS_Pr);
extern void		SiS_Chrontel701xBLOff(struct SiS_Private *SiS_Pr);
extern void		SiS_SiS30xBLOn(struct SiS_Private *SiS_Pr);
extern void		SiS_SiS30xBLOff(struct SiS_Private *SiS_Pr);
extern void		SiS_SetPitchCRT1(struct SiS_Private *SiS_Pr, ScrnInfoPtr pScrn);
extern void		SiS_SetPitchCRT2(struct SiS_Private *SiS_Pr, ScrnInfoPtr pScrn);

/* MergedFB */
#ifdef SISMERGED
extern void		SiSMFBInitMergedFB(ScrnInfoPtr pScrn);
extern void		SiSMFBHandleModesCRT2(ScrnInfoPtr pScrn, ClockRangePtr clockRanges);
extern void 		SiSMFBMakeModeList(ScrnInfoPtr pScrn);
extern void		SiSMFBCorrectVirtualAndLayout(ScrnInfoPtr pScrn);
extern Bool		SiSMFBRebuildModelist(ScrnInfoPtr pScrn, ClockRangePtr clockRanges);
extern Bool		SiSMFBRevalidateModelist(ScrnInfoPtr pScrn, ClockRangePtr clockRanges);
extern void		SiSMFBSetDpi(ScrnInfoPtr pScrn1, ScrnInfoPtr pScrn2, SiSScrn2Rel srel);
extern void		SISMFBPointerMoved(ScrnInfoPtr pScrn, int x, int y);
extern void		SISMFBAdjustFrame(ScrnInfoPtr pScrn, int x, int y);
#ifdef SISXINERAMA
extern void		SiSXineramaExtensionInit(ScrnInfoPtr pScrn);
extern Bool 		SiSnoPanoramiXExtension;
#endif
#endif

