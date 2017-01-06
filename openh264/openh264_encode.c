/**
 * @file openh264_encode.c  Video codec using OpenH264 codec Version 1.4.0
 *
 * Copyright (C) 2014 - 2015 SENSE
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <stdbool.h>
#include <string.h>

#include "h264_packetize.h"
#include "h264_tl0d.h"
#include "openh264_codec.h"

/* OpenH264: */
#include <wels/codec_api.h>
#include <wels/codec_app_def.h>

#define	MIN_BUFFER_SIZE      16384
#define SPATIAL_LAYER_NUM    1
#define TEMPORAL_LAYER_NUM   3
#define MAXIMUM_NAL_SIZE     1500
#include <pthread.h>

enum { DEFAULT_GOP_SIZE = 120 };



struct videnc_state 
{	
	ISVCEncoder	*encoder;
	SEncParamExt 	 param;
	SSourcePicture  *SourcPict;
	SFrameBSInfo 	 BitStreamInfo;

	int64_t 	 pts;
	uint32_t	 enc_input_size;
	uint8_t		*enc_frame_whole;
	uint32_t	 enc_frame_size;
	uint32_t	 enc_processed;
	int		 ilayer;

	struct  vidsz encsize;
	struct  videnc_param encprm;

	struct mbuf *mb;
	size_t  sz_max; /* todo: figure out proper buffer size */

	struct 
	{
		uint32_t packetization_mode;
		uint32_t profile_idc;
		uint32_t profile_iop;
		uint32_t level_idc;
		uint32_t max_fs;
		uint32_t max_smbps;
	}h264;
};


static void destructor(void *arg)
{
	struct videnc_state *st = arg;
	
	if (st->encoder) 
		WelsDestroySVCEncoder(st->encoder);

	if (st->SourcPict)
		mem_deref(st->SourcPict);
		
	mem_deref(st->mb);
}


static void param_handler(const struct pl *name, const struct pl *val, void *arg)
{
	struct videnc_state *st = arg;

	(void)decode_sdpparam_h264(st, name, val);
}


int decode_sdpparam_h264(struct videnc_state *st, const struct pl *name,
			 const struct pl *val)
{

	if (0 == pl_strcasecmp(name, "packetization-mode")) 
	{
		st->h264.packetization_mode = pl_u32(val);

		if (st->h264.packetization_mode != 0) 
		{
			warning("avcodec: illegal packetization-mode %u\n",
				st->h264.packetization_mode);
			return EPROTO;
		}
	}
	else if (0 == pl_strcasecmp(name, "profile-level-id")) 
	{
		struct pl prof = *val;
		if (prof.l != 6) 
		{
			warning("avcodec: invalid profile-level-id (%r)\n", val);
			return EPROTO;
		}

		prof.l = 2;
		st->h264.profile_idc = pl_x32(&prof); prof.p += 2;
		st->h264.profile_iop = pl_x32(&prof); prof.p += 2;
		st->h264.level_idc   = pl_x32(&prof);
	}
	else if (0 == pl_strcasecmp(name, "max-fs")) 
	{
		st->h264.max_fs = pl_u32(val);
	}
	else if (0 == pl_strcasecmp(name, "max-smbps")) 
	{
		st->h264.max_smbps = pl_u32(val);
	}

	return 0;
}


/* Init encoder parameters
	//this should be initialized in video.c when calling video_encoder_set
	//encparam->full_frame = true; 
 * encoder should have been created before calling this function
*/
static int openh264_set_encoder_params(struct videnc_state *st, const struct videnc_param *encparam, const struct vidsz *encsize)
{
	int i;

	st->param = (SEncParamExt){ 0 };
	

	(*st->encoder)->GetDefaultParams(st->encoder, &st->param);
	st->param.iUsageType			 = CAMERA_VIDEO_REAL_TIME;
	//width and height of picture in luminance samples 
	//the maximum of all layers if multiple spatial layers presents
	st->param.iPicWidth			 = encsize->w;
	st->param.iPicHeight 			 = encsize->h;
	st->param.iTargetBitrate 		 = encparam->bitrate;
	st->param.iRCMode 			 = RC_BITRATE_MODE;
	st->param.fMaxFrameRate			 = encparam->fps *0.1f;

	// set the number of temporal layers
	st->param.iTemporalLayerNum		 = TEMPORAL_LAYER_NUM;
	// set the number of spatial layers
	st->param.iSpatialLayerNum 		 = SPATIAL_LAYER_NUM;
	// type of profile id defined in EProfileIdc
	for(i = 0; i < st->param.iSpatialLayerNum; i++)
	{
		SSpatialLayerConfig *Layer = &st->param.sSpatialLayers[i];

		// width of picture in luminance samples of the specific layer
		Layer->iVideoWidth	 = st->param.iPicWidth >> (st->param.iSpatialLayerNum -i -1);
		// width of picture in luminance samples of the specific layer 
		Layer->iVideoHeight	 = st->param.iPicHeight >> (st->param.iSpatialLayerNum - i- 1);
		// frame rate specified for a layer
		Layer->fFrameRate	 = st->param.fMaxFrameRate;
		// target bitrate for a spatial layer, in unit of bps
		Layer->iSpatialBitrate	 = st->param.iTargetBitrate;
		// value of profile IDC: PRO_UNKNOWN for auto-detection
		Layer->uiProfileIdc	 = PRO_UNKNOWN;
		// value of profile IDC: 0 for auto-detection
		Layer->uiLevelIdc	 = 0;
		// value of level IDC: 0 for auto-detection
		Layer->iDLayerQp	 = 0; 
		/* slice configuration for a layer */
		//since uiMaxNalSize != 0 then uiSliceMod = SM_DYN_SLICE
		Layer->sSliceCfg.uiSliceMode 		   = SM_DYN_SLICE ; //SM_SINGLE_SLICE; 
		Layer->sSliceCfg.sSliceArgument.uiSliceNum = 1;
	}


	//possible modes LOW, MEDIUM, HIGH: decreasing order for spead, increasing for quality
	st->param.iComplexityMode		 = MEDIUM_COMPLEXITY;/*HIGH_COMPLEXITY;*/
	/* I-Frame interval in frames */
	st->param.uiIntraPeriod			 = DEFAULT_GOP_SIZE; 
	st->param.eSpsPpsIdStrategy		 = CONSTANT_ID;
	// false: not use Prefix NAL; true: use Prefix NAL
	st->param.bPrefixNalAddingCtrl 		 = true;
	//false:not use SSEI; true: use SSEI
	st->param.bEnableSSEI			 = true;
	//false: use SVC syntax for higher layers, true: use Simulcast AVC -- coming soon
	st->param.bSimulcastAVC			 = false;
	// 0:CAVLC  1:CABAC
	st->param.iEntropyCodingModeFlag	 = 0; 

//re_printf("function:openh264_set_encoder_params() ------> Assigning Values 2\n");
	/* RC Control */
	// false: don't skip frame even if VBV buffer overflow 
	// true: allow skipping frames to keep the bitrate within limits
	st->param.bEnableFrameSkip		 = true;
	st->param.iMaxBitrate			 = UNSPECIFIED_BIT_RATE;
	//the maximum QP encoder supports
	//st->param.iMaxQp			 = ;
	//the minimum QP encoder supports
	//st->param.iMinQp			 = ;
	// the maximum NAL size, should be not 0 for dynamic slice mode
	st->param.uiMaxNalSize			 = MAXIMUM_NAL_SIZE;


	/* LTR (Long Term Reference) settings */
	//< true: on, false: off
	st->param.bEnableLongTermReference 	 = false;//true;
	// the LTR marked period that is used in feedback
	st->param.iLtrMarkPeriod 		 = 30;
	

	/*multi-thread settings */
	// 0: auto(dynamic imp. internal encoder) 
	// 1: multiple threads imp. disabled; 
	// lager than 1: count number of threads;
	st->param.iMultipleThreadIdc 		 = 0;
	
	/* Deblocking Loop filter */
	// 0: on, 1: off, 2: on except for slice boundaries
	st->param.iLoopFilterDisableIdc		 = 0;
	// AlphaOffset: valid range [-6, 6], default 0
	st->param.iLoopFilterAlphaC0Offset 	 = 0;
	// BetaOffset: valid range [-6, 6], default 0
	st->param.iLoopFilterBetaOffset		 = 0;


	/*preprocessing features */
	// denoise control
	st->param.bEnableDenoise 		 = false;
	// background detection control: VAA_BACKGROUND_DETECTION, BGD cmd
	st->param.bEnableBackgroundDetection 	 = true;
	// adaptive quantization control
	st->param.bEnableAdaptiveQuant 		 = true;
	// enable frame cropping flag: TRUE always in application
	st->param.bEnableFrameCroppingFlag	 = true;
	st->param.bEnableSceneChangeDetect 	 = true;

	//get the total target bit rate of all the layers
	st->param.iTargetBitrate *= st->param.iSpatialLayerNum;
	
	return 0;
}


static int openh264_encoder_open(struct videnc_state *st, const struct videnc_param *prm, const struct vidsz *size)
{
	int err = 0;
	
	if (st->encoder)
	{
		debug("openh264_encoder: re-opening encoder\n");
		return EINVAL;
	}
	/* allocate encoder */
	err = WelsCreateSVCEncoder(&st->encoder);
	if (err)
	{
		warning("openh264_encoder: openh264_encoder_open() failed to create encoder\n");
		goto out;
	}
	
	//set parameters for encoder	
	err = openh264_set_encoder_params(st, prm, size);
	if(err)
	{
		warning("openh264_encoder: openh264_encoder_open() failed to set parameters\n");
		goto out;
	}
	
	st->enc_input_size = (size->w * size->h * 3) >> 1;

	/* Initialize encoder */
	err = (*st->encoder)->InitializeExt(st->encoder, &st->param);
	if (err) //err != cmResultSuccess which is defined to 0 in 
	{
		warning("openh264_encoder: openh264_encoder_open() Initialization failed\n");
		goto out;
	}
	
	st->encsize = *size;

	if(!st->SourcPict)
	{
		warning("openh264_encoder: openh264_encoder_open() Picture not Allocated\n");
		goto out;
	}

	/* Initialize memory for input picture allocated in openh264_update_encoder */
	st->SourcPict->iColorFormat	= videoFormatI420;
	st->SourcPict->uiTimeStamp	= 0;
	st->SourcPict->iPicWidth	= st->param.iPicWidth;
	st->SourcPict->iPicHeight	= st->param.iPicHeight;
	st->SourcPict->iStride[0]	= st->SourcPict->iPicWidth;
	st->SourcPict->iStride[1]	= st->SourcPict->iStride[0]>>1;
	st->SourcPict->iStride[2]	= st->SourcPict->iStride[1];

 out:
	if (err) 
	{
		if (st->encoder) 
		{
			WelsDestroySVCEncoder(st->encoder);
			st->encoder = NULL;
		}
		
		if (st->SourcPict)
			mem_deref(st->SourcPict);
	}
	
	return err;
}


int openh264_encoder_update(struct videnc_state **vesp, const struct vidcodec *vc, struct videnc_param *prm, const char *fmtp)
{
	struct videnc_state *st;
	int err = 0;

	if (!vesp || !vc || !prm)
		return EINVAL;
	
	st = *vesp;
	//allocate appropriate structures if not allready allocated 
	if (!st)
	{
		st = mem_zalloc(sizeof(*st), destructor);
		if (!st)
			return ENOMEM;
			
		st->SourcPict = mem_zalloc(sizeof(*st->SourcPict), NULL);
		if (!st->SourcPict)
		{
			err = ENOMEM;
			goto out;
		}
		
		st->mb  = mbuf_alloc(MIN_BUFFER_SIZE * 20);
		if (!st->mb)
		{
			err = ENOMEM;
			goto out;
		}
	}
	//else close the encoder and initialize to NULL if params have changed
	else
	{
		if(st->encprm.bitrate != prm->bitrate || st->encprm.pktsize != prm->pktsize ||
			st->encprm.fps != prm->fps)
		{
			WelsDestroySVCEncoder(st->encoder);
			st->encoder = NULL;
		} 
	}
	//set parameters
	st->encprm = *prm;
	st->sz_max = st->mb->size;

	if (str_isset(fmtp)) 
	{
		struct pl sdp_fmtp;

		pl_set_str(&sdp_fmtp, fmtp);
		fmt_param_apply(&sdp_fmtp, param_handler, st);
	}

	debug("openh264_codec: video encoder %s: %d fps, %d bit/s, pktsize=%u\n",
	      vc->name, prm->fps, prm->bitrate, prm->pktsize);
	      
 out:
	if (err)
		mem_deref(st);
	else{
		*vesp = st;
}
	return err;
}

/*
*Input:
*	Pointer NalUnit points at the begnning of a nal unit start code
*Output:
*	Returns the length of the nal units start code or zero if not found
*/
static inline int find_start_code_length(uint8_t *NalUnit)
{
	if(NalUnit[0] == 0x00 && NalUnit[1] == 0x00 && NalUnit[2] == 0x01)
				return 3;
	if(NalUnit[0] == 0x00 && NalUnit[1] == 0x00 && NalUnit[2] == 0x00 && NalUnit[3] == 0x01)
				return 4;
	return 0;
}

/* 
 * Prints bitstream info for debuging purposes 
*/
static void openh264_BitStreamInfo(SFrameBSInfo *BitStreamInfo)
{
	int i;

	re_printf("BITSTREAM INFO - ONE ACCESS UNIT\n");
	re_printf("iTemporalId:= %d\n", BitStreamInfo->iTemporalId);
	re_printf("iSubSeqId:= %d  -------  Referes to D.2.11 sub-sequence information SEI\n",  BitStreamInfo->iSubSeqId);
	re_printf("uiTimeStamp:= %lld\n", BitStreamInfo->uiTimeStamp);
	
	switch(BitStreamInfo->eFrameType)
	{
		case videoFrameTypeInvalid:
						re_printf("eFrameType:= %s\n", "videoFrameTypeInvalid");
						break;
		case videoFrameTypeIDR:
						re_printf("eFrameType:= %s\n", "videoFrameTypeIDR");
						break;
		case videoFrameTypeI:
						re_printf("eFrameType:= %s\n", "videoFrameTypeI");
						break;
		case videoFrameTypeP:
						re_printf("eFrameType:= %s\n", "videoFrameTypeP");
						break;
		case videoFrameTypeSkip:
						re_printf("eFrameType:= %s\n", "videoFrameTypeSkip");
						break;
		case videoFrameTypeIPMixed:
						re_printf("eFrameType:= %s\n", "videoFrameTypeIPMixed");
						break;
		default :
						re_printf("eFrameType:= %s\n", "InvalidFrameType");
	}
	
	re_printf("iFrameSizeInBytes:= %d\n", BitStreamInfo->iFrameSizeInBytes);
	re_printf("uiTimeStamp:= %lu\n", BitStreamInfo->uiTimeStamp);
	re_printf("iLayerNum:= %d  -------  Number of svc layers, if IDR frame then one more layer is present \n", BitStreamInfo->iLayerNum);
		           		
	for (i=0; i < BitStreamInfo->iLayerNum; i++) 
	{
		int inal;
		unsigned char* bitstream;
		SLayerBSInfo *Layer = &BitStreamInfo->sLayerInfo[i];

		re_printf("\nsLayerInfo:= %d  -------  Layer in svc hierarchy\n", i);
		re_printf("\nuiSpatialId:= %u\n", Layer->uiSpatialId);
		re_printf("uiQualityId:= %u\n", Layer->uiQualityId);
		re_printf("uiTemporalId:= %u\n", Layer->uiTemporalId);
		re_printf("uiLayerType:= %u\n", Layer->uiLayerType);
		re_printf("iNalCount:= %d  -------  Number of nalus in this layer\n", Layer->iNalCount);

		bitstream = Layer->pBsBuf;
		for(inal=0; inal<Layer->iNalCount; inal++)
		{
			uint8_t NalType;
			int nalLength, startCodeLength;

			nalLength = Layer->pNalLengthInByte[inal];
			//bitstream points at the nal start code,
			startCodeLength = find_start_code_length(bitstream);
			//bitstream points at nalus header 
			bitstream += startCodeLength;

			//get the nal unit type from its header 
			NalType = *bitstream & 0x1f;
			re_printf("NalLength[%d]:= %d, NalType:= %u \n", inal, nalLength, NalType);
			//check if it is a SEI nal uint
			if(NalType == 0x06)
			{
				re_printf("FOUND SEI\n");
				//to do: if SEI present, calculate SEI payload Type
			}

			//bitstream points at next nalus start code 
			bitstream += nalLength - startCodeLength;
		}
	}

	re_printf("\n\n");
}

//For TL0 Mechanism
void update_tl0_pic_idx(void *arg1, void *arg2)
{
	SFrameBSInfo *BitStreamInfo = arg1;
	int i;
	
	switch(BitStreamInfo->eFrameType)
	{
		case videoFrameTypeInvalid:
						break;
		case videoFrameTypeIDR:
						
						set_tl0(true, true, 0, 0, 0, arg2);
						return;
						
		case videoFrameTypeI:
						break;
		case videoFrameTypeP:
						break;
		case videoFrameTypeSkip:
						break;
		case videoFrameTypeIPMixed:
						break;
		default :		break;
	}
	
	for (i=0; i < BitStreamInfo->iLayerNum; i++) 
	{
		SLayerBSInfo *Layer = &BitStreamInfo->sLayerInfo[i];
		
		if( Layer->uiTemporalId == 0)
		{
			set_tl0(true, false, 1, 0, 0, arg2);
			return;
		}
	}
	
	set_tl0(false, false, 0, 0, 0,arg2);
	return;
}

int openh264_encode(struct videnc_state *st, bool update, const struct vidframe *frame, videnc_packet_h *pkth, void *arg)
{
	int i, err, ret;
	SLayerBSInfo* pLayerBsInfo;
	unsigned int payload_size = 0;

	if (!st || !frame || !pkth || frame->fmt != VID_FMT_YUV420P)
			return EINVAL;

	if (!st->encoder || !vidsz_cmp(&st->encsize, &frame->size)) 
	{
		err = openh264_encoder_open(st, &st->encprm, &frame->size);
		if (err) 
		{
			warning("openh264_encode: openh264_encoder_open failed %m\n", err);
			return err;
		}
	}
	
	//copy frame to SSourcePicture type in videnc_state structure
	for (i=0; i<4; i++) 
	{
		st->SourcPict->pData[i]   = frame->data[i];
		st->SourcPict->iStride[i] = frame->linesize[i];
	}
	st->SourcPict->uiTimeStamp = st->pts++;
	
	if (update) 
	{
		re_printf("openh264_encode: encoder picture update\n");
		(*st->encoder)->ForceIntraFrame(st->encoder, true);
	}

	mbuf_rewind(st->mb);
	st->BitStreamInfo = (SFrameBSInfo){ 0 };
	
	//encode frame
	ret = (*st->encoder)->EncodeFrame(st->encoder, st->SourcPict, &st->BitStreamInfo);
	if (ret) //if ret != cmResultSuccess, where cmResultSuccess == 0
	{
		debug("openh264: Frame encoding failed\n");
		return EBADMSG;
	}

	if (st->BitStreamInfo.eFrameType == videoFrameTypeSkip)
	{
		debug("openh264: frame skiped\n");
		return 0;
	}

	st->ilayer = 0;
	st->enc_frame_size = 0;
	st->enc_processed = 0;

	if (true) 
	{
		//Normal frames have one single layer, IDR frames have two layers: 
		//the first layer contains the SPS/PPS.
		st->ilayer = 0;
		pLayerBsInfo = &st->BitStreamInfo.sLayerInfo[st->ilayer];
		if (!pLayerBsInfo) 
			return 0;
					
		for (i = st->ilayer;  i < st->BitStreamInfo.iLayerNum; i++) 
		{
			int j;
			payload_size = 0;
			pLayerBsInfo = &st->BitStreamInfo.sLayerInfo[i];
			for (j=0; j < pLayerBsInfo->iNalCount; j++)
				payload_size += pLayerBsInfo->pNalLengthInByte[j];

			err = mbuf_write_mem(st->mb, pLayerBsInfo->pBsBuf, payload_size);
			if(err)
			{
				warning("openh264_encode: can not copy to memory: %m\n", err);
				return err;
			}
		}

		//openh264_BitStreamInfo(&st->BitStreamInfo);
		
		//For TL0 Mechanism
		update_tl0_pic_idx(&st->BitStreamInfo, arg);
		
		err = h264_tl0d_packetize(st->mb, st->encprm.pktsize, pkth, arg);
	}
	return err;
}
