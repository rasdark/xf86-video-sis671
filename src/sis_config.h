/* $XFree86$ */
/* $XdotOrg$ */
/*
 * Configurable compile-time options
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
 * Author:   Thomas Winischhofer <thomas@winischhofer.net>
 *
 */

#undef SISDUALHEAD
#undef SISMERGED
#undef SISXINERAMA
#undef SIS_ARGB_CURSOR
#undef SISVRAMQ
#undef INCL_YUV_BLIT_ADAPTOR
#undef SIS_USE_XAA
#undef SIS_USE_EXA

/* Configurable stuff: ------------------------------------- */

#define SISDUALHEAD		/* Include Dual Head support  */

#define SISMERGED		/* Include Merged-FB support */

#undef SISXINERAMA
#ifdef SISMERGED
#define SISXINERAMA		/* Include SiS Pseudo-Xinerama support for MergedFB mode */
#endif

#if 1
#define SIS_ARGB_CURSOR		/* Include support for color hardware cursors */
#endif

#if 1
#define SISVRAMQ		/* Use VRAM queue mode support on 315+ series */
#endif

#undef INCL_YUV_BLIT_ADAPTOR
#ifdef SISVRAMQ
#if 1
#define INCL_YUV_BLIT_ADAPTOR	/* Include support for YUV->RGB blit adaptors (VRAM queue mode only) */
#endif
#endif

#if 1
#undef SIS_USE_XAA		/* Don't include support for XAA, current xorg servers don't support it */
#endif

#ifdef SISVRAMQ
#ifdef XORG_VERSION_CURRENT
#if defined(SIS_HAVE_EXA) || (defined(XF86EXA) && (XF86EXA != 0))
#if 1
#define SIS_USE_EXA		/* Include support for EXA */
#endif
#endif
#endif
#endif

/* End of configurable stuff --------------------------------- */


