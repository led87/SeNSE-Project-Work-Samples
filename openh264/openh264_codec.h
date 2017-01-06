/**
 * @file openh264_codec.h Video codecs using OpenH264  v1.4.0 -- internal API
 *
 * Copyright (C) 2014 -2015 SeNSE
 */	



extern const uint8_t h264_level_idc;


/*
 * Encode
 */

struct videnc_state;

int openh264_encoder_update(struct videnc_state **vesp, const struct vidcodec *vc, 
							struct videnc_param *prm, const char *fmtp);
int openh264_encode(struct videnc_state *st, bool update, const struct vidframe *frame,
					videnc_packet_h *pkth, void *arg);


/*
 * Decode
 */

struct viddec_state;

int openh264_decoder_update(struct viddec_state **vdsp, const struct vidcodec *vc, const char *fmtp);
int openh264_decode(struct viddec_state *st, struct vidframe *frame, bool eof, uint16_t seq, struct mbuf *src);
int h264_parse_nal_units(struct viddec_state *st, struct mbuf *src);

int decode_sdpparam_h264(struct videnc_state *st, const struct pl *name, const struct pl *val);
int h264_packetize(struct mbuf *mb, size_t pktsize, videnc_packet_h *pkth, void *arg);


int h264_nal_send(bool first, bool last, bool marker, uint32_t ihdr, const uint8_t *buf,
				  size_t size, size_t maxsz, videnc_packet_h *pkth, void *arg);

//For TL0 Mechanism
void update_tl0_pic_idx(void *arg1, void *arg2);
