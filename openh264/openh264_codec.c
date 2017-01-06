/**
 * @file openh264_codec.c  Video codecs using OpenH264 video codec Verionn 1.4.0
 *
 * Copyright (C) 2014 - 2015 SeNSE
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>

#include "h264_packetize.h"
#include "openh264_codec.h"



static uint32_t packetization_mode(const char *fmtp)
{
	struct pl pl, mode;

	if (!fmtp)
		return 0;

	pl_set_str(&pl, fmtp);

	if (fmt_param_get(&pl, "packetization-mode", &mode))
		return pl_u32(&mode);

	return 0;
}


static int h264_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
			 bool offer, void *arg)
{
	struct vidcodec *vc = arg;
	const uint8_t profile_idc = 0x42; /* baseline profile */
	const uint8_t profile_iop = 0x80;
	(void)offer;

	if (!mb || !fmt || !vc)
		return 0;

	return mbuf_printf(mb, "a=fmtp:%s"
			   " packetization-mode=0"
			   ";profile-level-id=%02x%02x%02x"
			   "\r\n",
			   fmt->id, profile_idc, profile_iop, h264_level_idc);
}


static bool h264_fmtp_cmp(const char *fmtp1, const char *fmtp2, void *data)
{
	(void)data;

	return packetization_mode(fmtp1) == packetization_mode(fmtp2);
}


static struct vidcodec openh264 = {
	.name = "H264",
	.variant = "packetization-mode=0",
	.encupdh = openh264_encoder_update,
	.ench = openh264_encode,
	.decupdh = openh264_decoder_update,
	.dech = openh264_decode,
	.fmtp_ench = h264_fmtp_enc,
	.fmtp_cmph = h264_fmtp_cmp,
};


static int module_init(void)
{
	vidcodec_register(&openh264);
	return 0;
}


static int module_close(void)
{
	vidcodec_unregister(&openh264);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(openh264_codec) = {
	"openh264",
	"codec",
	module_init,
	module_close
};
