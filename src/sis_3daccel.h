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


/***** Packet Format *****/
#define SIS_3D_SPKC_HEADER		0x36800000L
#define SIS_3D_BUST_HEADER		0x76800000L




/***** Registers *****/
#define REG_3D_PSInit_340               0x8a84
#define REG_3D_FSAA_PrimType_340        0x8a8c
#define REG_3D_ClipTopBottom_340        0x8a98
#define REG_3D_TEnable0_340	0x8b00
#define REG_3D_TEnable1_340	0x8b04
#define REG_3D_BlendMode_340            0x8b28
#define REG_3D_AlphaBlendConstant_340   0x8b2c
#define REG_3D_Dst0Set_340              0x8b40
#define REG_3D_Dst0Addr_340             0x8b48
#define REG_3D_MRTCWMask0_340            0x8ba4
#define REG_3D_MRTCWMask1_340            0x8ba8
#define REG_3D_TexSet_340               0x8cdc
#define REG_3D_TexCoorSet_340           0x8ce4
#define REG_3D_TexColorBlendSet00_340   0x8d20
#define REG_3D_Tex0Set_340              0x8e00
#define REG_3D_PSSet1_771               0x8fc4

#define REG_3D_PSInitRead0_771          0x9064
#define REG_3D_FrontEnable0_340	0x9400
#define REG_3D_Stream01CopyType_340     0x9520
#define REG_3D_SwapColor_340            0x9540
#define REG_3D_VtxActiveL_340		0x9560
#define REG_3D_FTexCoordDim_340         0x9620
#define REG_GL_ScaleX_340               0x9880
#define REG_GL_LSet_340                 0x98a4

#define REG_3D_EngineId_671             0x8fc8
#define REG_3D_ParserFire_340           0x9f04
#define REG_3D_ListEnd_340              0x9f84


/***** Valuables *****/
#define VAL_FlatShadeA_340          0x00000800
#define VAL_FireMode_Vertex_340     0x00000004
#define VAL_LEnd_TSRCA_340          0x00000003
#define VAL_SolidFill_340           0x03000000
#define VAL_SolidCCWFill_340        0x0c000000
#define VAL_Dst_A8R8G8B8_340        0x00300000 
#define VAL_Dst_R5G6B5_340          0x00110000
#define VAL_Tex_A8R8G8B8_340        0x60000000
#define VAL_CBLSRC_One_340              0x01
#define VAL_CBLDST_InvSrc_Alpha_340     0x05
#define VAL_PSReName_NormalMode     0x08000000
#define VAL_PS_PixelNum64_340       0x00300000
#define VAL_Tex_MinNearest_340      0x00000000
#define VAL_Tex_MagNearest_340      0x00000000
#define VAL_TxL0InSys_340           0x00100000
#define SHT_TXW_340                 15
#define VAL_DType_TriStrip_340      0x00000009



#define SNAP4_CONSTANT  786432.0f     /* 2^18 + 2^19 */
#define SNAP6_CONSTANT  196608.0f     /* 2^16 + 2^17 */

#define INI_BlendMode_340           0x001ff000
#define NUM_MAX_PS_CONST340             32
#define PS_CNST_DWSIZE  4
#define NUM_MAX_PS_INST340     128  
#define PS_INST_DWSIZE  2

#define SHT_TnDiffuse_771            16 
#define SHT_TnSpecular_771         20
#define SHT_FrontTXNUM_340          16
#define SHT_TXNUM_340               12


#define MSK_XDisableClip_340        0xfc000000
#define MSK_RGXScrCoor_340          0x00000020
#define MSK_En_DisableCull_340      0x00000004
#define MSK_En_DisableReject_340    0x00000002
#define MSK_En_DisableZeroTest_340  0x00000001
#define MSK_DiffPresent_340         0x00000080
#define MSK_En_Blend_340            0x00000100
#define MSK_En_TexCache_340         0x00100000
#define MSK_En_TexL2Cache_340       0x00040000
#define MSK_En_AGPRqBurst_340       0x00000010
#define MSK_En_DRAM128b_342         0x00000080
#define MSK_En_PixelShader_340      0x00000008
#define MSK_En_ArbPreCharge_340     0x00000100
#define MSK_PS_ElementGamma_340     0xf0000000
#define MSK_TXNonPwdTwo_340         0x00080000
#define MSK_En_TexMap_340	    0x00080000
#define MSK_EGenTXInitR		    0x00000020



#define CMD_QUEUE_CHECK128_Null_3D \
{\
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	if (ttt % 16){\
		SiSWaitQueue(8); \
		SIS_WQINDEX(0) = (CARD32)(0x368f0000); \
		SIS_WQINDEX(1) = (CARD32)(0x368f0000); \
		ttt += 8;\
		ttt &= pSiS->cmdQueueSizeMask;\
		SiSSetSwWP(ttt); \
	}\
}

#define SiS3DClearTexCache \
{ \
	int i; \
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	unsigned long dwTEnable = 0x001e0080; \
	for (i = 0; i<4; i++) {\
	SiSWaitQueue(16); \
	SIS_WQINDEX(i*4) = (CARD32)(SIS_3D_SPKC_HEADER + REG_3D_TEnable1_340); \
	SIS_WQINDEX(i*4+1) = (CARD32)(dwTEnable); \
	SIS_WQINDEX(i*4+2) = (CARD32)(SIS_3D_SPKC_HEADER + REG_3D_TEnable1_340); \
	SIS_WQINDEX(i*4+3) = (CARD32)(dwTEnable); \
	SiSUpdateQueue \
	}\
	SiSSetSwWP(ttt); \
}


#define SiS3DEnableSet(dwEnable0, dwEnable1, dwEnable2) \
{ \
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(32); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_BUST_HEADER + REG_3D_TEnable0_340); \
	SIS_WQINDEX(1) = (CARD32)(0x62100006);\
	SIS_WQINDEX(2) = (CARD32)(dwEnable0);\
	SIS_WQINDEX(3) = (CARD32)(dwEnable1);\
	SiSUpdateQueue \
	\
	SIS_WQINDEX(4) = (CARD32)(dwEnable2); \
	SIS_WQINDEX(5) = (CARD32)(0x0L); \
	SIS_WQINDEX(6) = (CARD32)(0x0L); \
	SIS_WQINDEX(7) = (CARD32)(0x0L); \
	SiSUpdateQueue \
	SiSSetSwWP(ttt); \
}

#define SiS3DFrontEnableSet(dwFrontEnable2) \
{ \
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(32); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_BUST_HEADER + REG_3D_FrontEnable0_340); \
	SIS_WQINDEX(1) = (CARD32)(0x62100006);\
	SIS_WQINDEX(2) = (CARD32)(0x0L);	\
	SIS_WQINDEX(3) = (CARD32)(VAL_FlatShadeA_340);/* FrontEnable1 */	\
	SiSUpdateQueue \
	\
	SIS_WQINDEX(4) = (CARD32)(dwFrontEnable2); /* FrontEnable2 */\
	SIS_WQINDEX(5) = (CARD32)(MSK_RGXScrCoor_340 |MSK_En_DisableCull_340 |MSK_En_DisableReject_340 |MSK_En_DisableZeroTest_340); /* FrontEnable3 */\
	SIS_WQINDEX(6) = (CARD32)(MSK_XDisableClip_340);\
	SIS_WQINDEX(7) = (CARD32)(0x0L);\
	SiSUpdateQueue \
	SiSSetSwWP(ttt); \
}


#define SiS3DSetupDestination0Set(set, CWrMask) \
{ \
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(16); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_BUST_HEADER + REG_3D_Dst0Set_340); \
	SIS_WQINDEX(1) = (CARD32)(0x62100002);\
	SIS_WQINDEX(2) = (CARD32)(set); \
	SIS_WQINDEX(3) = (CARD32)(CWrMask);	\
	SiSUpdateQueue \
	SiSSetSwWP(ttt); \
}

#define SiS3DSetupDestination0Addr(Addr) \
{ \
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(16); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_SPKC_HEADER + REG_3D_Dst0Addr_340); \
	SIS_WQINDEX(1) = (CARD32)(Addr);\
	SIS_WQINDEX(2) = (CARD32)(0x368f0000); \
	SIS_WQINDEX(3) = (CARD32)(0x368f0000);	\
	SiSUpdateQueue \
	SiSSetSwWP(ttt); \
}



#define SiS3DSetupClippingRange(TB, LR) \
{ \
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(16); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_BUST_HEADER + REG_3D_ClipTopBottom_340); \
	SIS_WQINDEX(1) = (CARD32)(0x62100002);\
	SIS_WQINDEX(2) = (CARD32)(TB); \
	SIS_WQINDEX(3) = (CARD32)(LR);\
	SiSUpdateQueue \
	SiSSetSwWP(ttt); \
}

#define SiS3DSetupIdentityT1WVInvWV \
{\
	int i;\
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(16); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_SPKC_HEADER + REG_3D_ParserFire_340); \
	SIS_WQINDEX(1) = (CARD32)((0x0) << 24) | ((0x0)<<16) | 0x8; /* Packet type */\
	SIS_WQINDEX(2) = (CARD32)(0xb68a0000); /* 3D Packet command*/\
	SIS_WQINDEX(3) = (CARD32)(0x62100000 | 40);\
	SiSUpdateQueue \
	for (i=0; i<40; i++){\
		if(i%4 == 0){\
			SiSWaitQueue(16); \
			SIS_WQINDEX(i+4) = (CARD32)(HwIdentityMatrix_340[i]); \
			SIS_WQINDEX(i+5) = (CARD32)(HwIdentityMatrix_340[i+1]);\
			SIS_WQINDEX(i+6) = (CARD32)(HwIdentityMatrix_340[i+2]);\
			SIS_WQINDEX(i+7) = (CARD32)(HwIdentityMatrix_340[i+3]);\
			SiSUpdateQueue \
		}\
	}\
	SiSSetSwWP(ttt); \
}


#define SiS3DSetupIdentityT2 \
{\
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(32); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_BUST_HEADER + REG_GL_ScaleX_340); \
	SIS_WQINDEX(1) = (CARD32)(0x62100006);	\
	SIS_WQINDEX(2) = (CARD32)(1.0f);\
	SIS_WQINDEX(3) = (CARD32)(SNAP4_CONSTANT);\
	SiSUpdateQueue \
	\
	SIS_WQINDEX(4) = (CARD32)(1.0f); \
	SIS_WQINDEX(5) = (CARD32)(SNAP4_CONSTANT);	\
	SIS_WQINDEX(6) = (CARD32)(1.0f);\
	SIS_WQINDEX(7) = (CARD32)(0.0f);\
	SiSUpdateQueue \
	SiSSetSwWP(ttt); \
}

#define SiS3DSetupTextureSet(TexSet) \
{\
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(32); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_BUST_HEADER + REG_3D_TexSet_340); \
	SIS_WQINDEX(1) = (CARD32)(0x62100004);	\
	SIS_WQINDEX(2) = (CARD32)(TexSet);\
	SIS_WQINDEX(3) = (CARD32)(0L);\
	SiSUpdateQueue \
	\
	SIS_WQINDEX(4) = (CARD32)(0L);\
	SIS_WQINDEX(5) = (CARD32)(0L);\
	SIS_WQINDEX(6) = (CARD32)(0x368f0000); /*alignment */\
	SIS_WQINDEX(7) = (CARD32)(0x368f0000); /*alignment */\
	SiSUpdateQueue \
	SiSSetSwWP(ttt); \
}

#define SiS3DSetupAlphaBlend(BlendMode, BlendCst) \
{\
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(16); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_SPKC_HEADER + REG_3D_BlendMode_340); \
	SIS_WQINDEX(1) = (CARD32)(BlendMode);	\
	SIS_WQINDEX(2) = (CARD32)(SIS_3D_SPKC_HEADER + REG_3D_AlphaBlendConstant_340);\
	SIS_WQINDEX(3) = (CARD32)(BlendCst);\
	SiSUpdateQueue \
	SiSSetSwWP(ttt); \
}

#define SiS3DSetupShaderMRT(PsSet, DstNum) \
{\
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(32); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_SPKC_HEADER + REG_3D_PSInit_340); \
	SIS_WQINDEX(1) = (CARD32)(PsSet);	\
	SIS_WQINDEX(2) = (CARD32)(SIS_3D_BUST_HEADER + REG_3D_MRTCWMask0_340);\
	SIS_WQINDEX(3) = (CARD32)(0x62100004);\
	SiSUpdateQueue \
	\
	SIS_WQINDEX(4) = (CARD32)(MRTCWMask[DstNum][0]);\
	SIS_WQINDEX(5) = (CARD32)(MRTCWMask[DstNum][1]);\
	SIS_WQINDEX(6) = (CARD32)(0L); /*alignment */\
	SIS_WQINDEX(7) = (CARD32)(0L); /*alignment */\
	SiSUpdateQueue \
	SiSSetSwWP(ttt); \
}

#define SiS3DSetupInitRead0(PsSet1, InitRead) \
{\
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(16); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_SPKC_HEADER + REG_3D_PSSet1_771); \
	SIS_WQINDEX(1) = (CARD32)(PsSet1);	\
	SIS_WQINDEX(2) = (CARD32)(SIS_3D_SPKC_HEADER + REG_3D_PSInitRead0_771);\
	SIS_WQINDEX(3) = (CARD32)(InitRead);\
	SiSUpdateQueue \
	SiSSetSwWP(ttt); \
}

#define SiS3DSetupPixelCst(count, cnst) \
{\
	int i; \
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(16); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_SPKC_HEADER + REG_3D_ParserFire_340); \
	SIS_WQINDEX(1) = (CARD32)((0x10) << 24) | ((0x0)<<16) | 0x8; /* Packet type */\
	SIS_WQINDEX(2) = (CARD32)(0xb68a0000); /* 3D Packet command*/\
	SIS_WQINDEX(3) = (CARD32)(0x62100000 | count);\
	SiSUpdateQueue \
	\
	SiSWaitQueue(count*4); \
	for (i=0; i<count; i++)	SIS_WQINDEX(i+4) = (CARD32)(cnst[i]); \
	ttt += count*4;\
	ttt &= pSiS->cmdQueueSizeMask;\
	SiSSetSwWP(ttt); \
	CMD_QUEUE_CHECK128_Null_3D \
}

#define SiS3DSetupPixelInst(count, inst) \
{\
	int i;\
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(16); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_SPKC_HEADER + REG_3D_ParserFire_340); \
	SIS_WQINDEX(1) = (CARD32)((0x11) << 24) | ((0x0)<<16) | 0x8; /* Packet type */\
	SIS_WQINDEX(2) = (CARD32)(0xb68a0000); /* 3D Packet command*/\
	SIS_WQINDEX(3) = (CARD32)(0x62100000 | count);\
	SiSUpdateQueue \
	\
	SiSWaitQueue(count*4); \
	for (i=0; i<count; i++)	SIS_WQINDEX(i+4) = (CARD32)(inst[i]); \
	ttt += count*4;\
	ttt &= pSiS->cmdQueueSizeMask;\
	SiSSetSwWP(ttt); \
	CMD_QUEUE_CHECK128_Null_3D \
}


#define SiS3DSetupBackCoordinate(coord) \
{\
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(16); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_SPKC_HEADER + REG_3D_TexCoorSet_340); \
	SIS_WQINDEX(1) = (CARD32)(coord);\
	SIS_WQINDEX(2) = (CARD32)(0x368f0000);\
	SIS_WQINDEX(3) = (CARD32)(0x368f0000);\
	SiSUpdateQueue \
	SiSSetSwWP(ttt); \
}


#define SiS3DTexture0Setting(set0, set1, depth, size, adder) \
{\
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(64); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_BUST_HEADER + REG_3D_Tex0Set_340); \
	SIS_WQINDEX(1) = (CARD32)(0x62100004); \
	SIS_WQINDEX(2) = (CARD32)(set0);\
	/*After SiS771, driver can support video stretchblt when video source in AGP. v3.78.03*/ \
	SIS_WQINDEX(3) = (CARD32)(set1 & ~0x100000);\
	SiSUpdateQueue \
	\
	SIS_WQINDEX(4) = (CARD32)(depth); \
	SIS_WQINDEX(5) = (CARD32)(size); \
 	SIS_WQINDEX(6) = (CARD32)(0x368f0000);\
	SIS_WQINDEX(7) = (CARD32)(0x368f0000);\
	SiSUpdateQueue \
	\
	SIS_WQINDEX(8) = (CARD32)(SIS_3D_SPKC_HEADER + REG_3D_ParserFire_340); \
	SIS_WQINDEX(9) = (CARD32)(((0x12) << 24) | (0<<16) | 0x8);/* Packet type */ \
	SIS_WQINDEX(10) = (CARD32)(0xb68a0000);\
	SIS_WQINDEX(11) = (CARD32)(0x62100004);\
	SiSUpdateQueue \
	\
	SIS_WQINDEX(12) = (CARD32)(adder); \
	SIS_WQINDEX(13) = (CARD32)(0L); \
	SIS_WQINDEX(14) = (CARD32)(0L);\
	SIS_WQINDEX(15) = (CARD32)(0L);\
	SiSUpdateQueue \
	SiSSetSwWP(ttt);\
}


#define SiS3DSetupTexture0Pitch(pitch) \
{\
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(32); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_SPKC_HEADER + REG_3D_ParserFire_340); \
	SIS_WQINDEX(1) = (CARD32)(((0x14) << 24) | (0<<16) | 0x8);/* Packet type */ \
	SIS_WQINDEX(2) = (CARD32)(0xb68a0000);\
	SIS_WQINDEX(3) = (CARD32)(0x62100004);\
	SiSUpdateQueue \
	\
	SIS_WQINDEX(4) = (CARD32)(pitch); \
	SIS_WQINDEX(5) = (CARD32)(0L); \
	SIS_WQINDEX(6) = (CARD32)(0L);\
	SIS_WQINDEX(7) = (CARD32)(0L);\
	SiSUpdateQueue \
	SiSSetSwWP(ttt);\
}

#define SiS3DSetupDimLight(TexCoordDim) \
{\
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(32); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_BUST_HEADER + REG_3D_FTexCoordDim_340); \
	SIS_WQINDEX(1) = (CARD32)(0x62100004);\
	SIS_WQINDEX(2) = (CARD32)(TexCoordDim);\
	SIS_WQINDEX(3) = (CARD32)(0L);/* no wrap*/\
	SiSUpdateQueue \
	SIS_WQINDEX(4) = (CARD32)(0L);/* no wrap*/\
	SIS_WQINDEX(5) = (CARD32)(0x000007ff);\
	SIS_WQINDEX(6) = (CARD32)(SIS_3D_SPKC_HEADER + REG_GL_LSet_340);\
	SIS_WQINDEX(7) = (CARD32)(MSK_DiffPresent_340);\
	SiSUpdateQueue \
	SiSSetSwWP(ttt); \
}

#define SiS3DSetupVertexVector(TexCrdIdx) \
{\
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(32); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_BUST_HEADER + REG_3D_Stream01CopyType_340); \
	SIS_WQINDEX(1) = (CARD32)(0x62100006);\
	SIS_WQINDEX(2) = (CARD32)(0x00008040 | FVF_TexCoord_771[TexCrdIdx][0]);\
	SIS_WQINDEX(3) = (CARD32)(FVF_TexCoord_771[TexCrdIdx][1]);\
	SiSUpdateQueue \
	SIS_WQINDEX(4) = (CARD32)(FVF_TexCoord_771[TexCrdIdx][2]);\
	SIS_WQINDEX(5) = (CARD32)(FVF_TexCoord_771[TexCrdIdx][3]);\
	SIS_WQINDEX(6) = (CARD32)(FVF_TexCoord_771[TexCrdIdx][4]);\
	SIS_WQINDEX(7) = (CARD32)(FVF_TexCoord_771[TexCrdIdx][5]);\
	SiSUpdateQueue \
	SiSSetSwWP(ttt); \
}

#define SiS3DPrimitiveSet \
{\
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(16); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_SPKC_HEADER + REG_3D_FSAA_PrimType_340);\
	SIS_WQINDEX(1) = (CARD32)(VAL_DType_TriStrip_340);\
	SIS_WQINDEX(2) = (CARD32)(0x368f0000);\
	SIS_WQINDEX(3) = (CARD32)(0x368f0000);\
	SiSUpdateQueue \
	SiSSetSwWP(ttt); \
}


#define SiS3DSetupStream(TexCoorNum, TexCoorExtNum, ExtDWNum)\
{\
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(32); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_BUST_HEADER + REG_3D_VtxActiveL_340);\
	SIS_WQINDEX(1) = (CARD32)(0x62100006);\
	SIS_WQINDEX(2) = (CARD32)(VertexActBitsL_771[TexCoorNum] | (VertexActBitsLEx_771[TexCoorExtNum]<<(3+TexCoorNum*2)) );\
	SIS_WQINDEX(3) = (CARD32)(0L);\
	SiSUpdateQueue \
	SIS_WQINDEX(4) = (CARD32)(0L);\
	SIS_WQINDEX(5) = (CARD32)(0L);\
	SIS_WQINDEX(6) = (CARD32)( ((TexCoorExtNum + TexCoorNum)<<16) | (3 + TexCoorNum*2 + ExtDWNum) );\
	SIS_WQINDEX(7) = (CARD32)(4L);\
	SiSUpdateQueue \
	SiSSetSwWP(ttt); \
}


#define SiS3DSetupSwapColor \
{\
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(16); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_SPKC_HEADER + REG_3D_SwapColor_340); \
	SIS_WQINDEX(1) = (CARD32)(0L);\
	SIS_WQINDEX(2) = (CARD32)(0x368f0000);\
	SIS_WQINDEX(3) = (CARD32)(0x368f0000);\
	SiSUpdateQueue \
	SiSSetSwWP(ttt); \
}

#define SiS3DSetupVertexData(vertex) \
{\
	int i; \
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(160); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_SPKC_HEADER + REG_3D_ParserFire_340); \
	SIS_WQINDEX(1) = (CARD32)(VAL_FireMode_Vertex_340);\
	SIS_WQINDEX(2) = (CARD32)(0xb68a0000);\
	SIS_WQINDEX(3) = (CARD32)(0x62100000 | 36);\
	for(i=0; i<4; i++){\
		SIS_WQINDEX(4+i*9) = (CARD32)(vertex[i].sx);\
		SIS_WQINDEX(5+i*9) = (CARD32)(vertex[i].sy);\
		SIS_WQINDEX(6+i*9) = (CARD32)(vertex[i].sz);\
		SIS_WQINDEX(7+i*9) = (CARD32)(vertex[i].tu);\
		SIS_WQINDEX(8+i*9) = (CARD32)(vertex[i].tv);\
		SIS_WQINDEX(9+i*9) = (CARD32)(vertex[i].u);\
		SIS_WQINDEX(10+i*9) = (CARD32)(vertex[i].v);\
		SIS_WQINDEX(11+i*9) = (CARD32)(vertex[i].m);\
		SIS_WQINDEX(12+i*9) = (CARD32)(vertex[i].n);\
	}\
	ttt += 160;\
	ttt &= pSiS->cmdQueueSizeMask;\
	SiSSetSwWP(ttt); \
}

#define SiS3DListEnd(EngineId, Stamp) \
{\
	CARD32 ttt = SiSGetSwWP(); \
	pointer tt = (char *)pSiS->cmdQueueBase + ttt; \
	SiSWaitQueue(16); \
	SIS_WQINDEX(0) = (CARD32)(SIS_3D_SPKC_HEADER + REG_3D_ListEnd_340); \
	SIS_WQINDEX(1) = (CARD32)(VAL_LEnd_TSRCA_340);\
	SIS_WQINDEX(2) = (CARD32)(SIS_3D_SPKC_HEADER + EngineId);\
	SIS_WQINDEX(3) = (CARD32)(Stamp);\
	SiSUpdateQueue \
	SiSSetHwWP(ttt); \
}

