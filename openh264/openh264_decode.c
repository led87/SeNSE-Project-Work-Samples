/**
 * @file openh264_decode.c  Video codec inteerface using OpenH264 codec Version 1.4.0
 *
 * Copyright (C) 2014 - 2015 SeNSE
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



struct viddec_state 
{
	ISVCDecoder *decoder;
	SDecodingParam DecodingParam;
	struct mbuf *mb;
	bool got_keyframe;
};

static void destructor(void *arg)
{
	struct viddec_state *st = arg;

	mem_deref(st->mb);
	if (st->decoder)
	{
		(*st->decoder)->Uninitialize(st->decoder);
		WelsDestroyDecoder(st->decoder);
		st->decoder = NULL;
	}
}


static int openh264_init_open_decoder(struct viddec_state *st)
{
	int err = 0;
	static EVideoFormatType videoFormat = videoFormatI420;


	err = WelsCreateDecoder(&st->decoder);
	if (err)
	{
		warning("openh264_decoder: openh264_init_open_decoder() failed to create decoder\n");
		return err;
	}

	memset(&st->DecodingParam, 0, sizeof(st->DecodingParam));
	st->DecodingParam.eOutputColorFormat  = videoFormatI420;
	st->DecodingParam.uiTargetDqLayer = UCHAR_MAX;
	st->DecodingParam.eEcActiveIdc = ERROR_CON_FRAME_COPY;
	st->DecodingParam.bParseOnly = false;
	st->DecodingParam.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;

	err = (*st->decoder)->Initialize(st->decoder, &st->DecodingParam);
	if (err)
	{
		warning("openh264_decoder: openh264_init_open_decoder() failed to initialize OpenH264 decoder\n");
		goto out;
	}

	err = (*st->decoder)->SetOption(st->decoder, DECODER_OPTION_DATAFORMAT, &videoFormat);
	if (err)
	{
		warning("openh264_decoder: openh264_init_open_decoder() failed to set data format option on OpenH264 decoder\n");
		goto out;
	}


out: 
	if(err)
		if (st->decoder)
		{
			(*st->decoder)->Uninitialize(st->decoder);
			WelsDestroyDecoder(st->decoder);
			st->decoder = NULL;
		}

		return err;
}


int openh264_decoder_update(struct viddec_state **vdsp, const struct vidcodec *vc,
		  const char *fmtp)
{
	struct viddec_state *st;
	int err = 0;

	if (!vdsp || !vc)
		return EINVAL;

	if (*vdsp)
		return 0;

	(void)fmtp;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;


	/* if further memory is required then mbuf_write_mem allocates it automatically */
	st->mb = mbuf_alloc(1024);
	if (!st->mb) 
	{
		err = ENOMEM;
		goto out;
	}

	st->decoder = NULL;

	err = openh264_init_open_decoder(st);
	if (err) 
	{
		warning("openh264: %s: could not init decoder\n", vc->name);
		goto out;
	}

	debug("openh264: video decoder %s (%s)\n", vc->name, fmtp);

 out:
	if (err)
		mem_deref(st);
	else
		*vdsp = st;

	return err;
}


/*
 * TODO: check input/output size
 */
static int openh264_decoder_decode(struct viddec_state *st, struct vidframe *frame,
		    bool eof, struct mbuf *src)
{
	int i, err;
	uint8_t * pFrameData[3] = {NULL};
	SBufferInfo sDstBufInfo;

	/* assemble packets in "mbuf" until a full access unit arrives*/
	err = mbuf_write_mem(st->mb, mbuf_buf(src), mbuf_get_left(src));
	
	if (err)
		return err;
	
	if (!eof)
		return 0;

	st->mb->pos = 0;

	if (!st->got_keyframe) 
	{
		err = EPROTO;
		goto out;
	}

	memset (&sDstBufInfo, 0, sizeof (SBufferInfo));

	/* Decode */
	err = (*st->decoder)->DecodeFrame2(st->decoder, st->mb->buf, (int)mbuf_get_left(st->mb), pFrameData, &sDstBufInfo);
	if(err)
		goto out;
		
	if (sDstBufInfo.iBufferStatus == 1) 
	{
		for (i=0; i<3; i++) 
		{
			int j = i>0 ? 1 : 0;
			frame->data[i]     = pFrameData[i];
			frame->linesize[i] = sDstBufInfo.UsrData.sSystemBuffer.iStride[j];
		}

		frame->size.w = sDstBufInfo.UsrData.sSystemBuffer.iWidth;
		frame->size.h = sDstBufInfo.UsrData.sSystemBuffer.iHeight;
		frame->fmt    = VID_FMT_YUV420P;
	}


 out:
	if (eof)
		mbuf_rewind(st->mb);
	
	//For TL0 Mechanism
	if(err)
	{
		st->got_keyframe = false;
		mbuf_rewind(st->mb);
	}

	return err;
}

/*adds the start code and nal header at the begening of each nal unit
  or fragmented nal units
*/
int h264_parse_nal_units(struct viddec_state *st, struct mbuf *src)
{
	struct h264_hdr h264_hdr;
	const uint8_t nal_seq[3] = {0, 0, 1};
	int err;
	
	err = h264_hdr_decode(&h264_hdr, src);	
	if (err)
		return err;
	
	if (h264_hdr.f)
		return EBADMSG;
	
	/* handle NAL types */
	if (1 <= h264_hdr.type && h264_hdr.type <= 23) 
	{

		if (!st->got_keyframe) 
		{
			switch (h264_hdr.type) 
			{

				case H264_NAL_PPS:
				case H264_NAL_SPS:
						st->got_keyframe = true;
						break;
			}
		}

		/* prepend H.264 NAL start sequence */
		mbuf_write_mem(st->mb, nal_seq, 3);

		/* encode NAL header back to buffer */
		err = h264_hdr_encode(&h264_hdr, st->mb);
	}
	else if (H264_NAL_FU_A == h264_hdr.type) 
	{
		struct fu fu;
		
		err = fu_hdr_decode(&fu, src);
		if (err)
			return err;
		h264_hdr.type = fu.type;

		if (fu.s) 
		{
			/* prepend H.264 NAL start sequence */
			mbuf_write_mem(st->mb, nal_seq, 3);
			/* encode NAL header back to buffer */
			err = h264_hdr_encode(&h264_hdr, st->mb);
		}
	}
	else if (31 == h264_hdr.type) 
	{
		TL0D tl0d;
		struct h264_hdr h264_hdr_STAP_A;

		memset(&tl0d, 0, sizeof(tl0d));
		h264_tl0d_decode(&tl0d, src->buf, src->pos);
		
		//advance reading position to skip PACSI header
		src->pos = src->pos + TL0D_SIZE -1;

		err = h264_hdr_decode(&h264_hdr_STAP_A, src);
		if (err)
			return err;
		
		if (1 <= h264_hdr_STAP_A.type && h264_hdr_STAP_A.type <= 23) 
		{
			
			if (!st->got_keyframe) 
			{
				switch (h264_hdr_STAP_A.type) 
				{

					case H264_NAL_PPS:
					case H264_NAL_SPS:
							st->got_keyframe = true;
							break;
				}
			}
			
			/* prepend H.264 NAL start sequence */
			mbuf_write_mem(st->mb, nal_seq, 3);
			/* encode NAL header back to buffer */
			err = h264_hdr_encode(&h264_hdr_STAP_A, st->mb);
		}
	}
	else 
	{
		re_printf("openh264_decoder: unknown NAL type %u\n", h264_hdr.type);
		return EBADMSG;
	}

	return err;
}


int openh264_decode(struct viddec_state *st, struct vidframe *frame,
		bool eof, uint16_t seq, struct mbuf *src)
{
	int err;
	
	//For TL0 Mechanism
	if(!src && seq == 0)
	{
		mbuf_rewind(st->mb);
		return 0;
	}

	if (!src)
		return 0;
	
	err = h264_parse_nal_units(st, src);

	if(err)
		return err;
	
	return openh264_decoder_decode(st, frame, eof, src);
}

