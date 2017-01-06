/**
 * @file aac.h Local AAC Interface
 *
 * Copyright (C) 2014 - 2015 Project SeNSE
 */


struct aac_param 
{
	unsigned long nSampleRate; 
	unsigned long nChannels;
};


/* Encode */
int aac_encode_update(struct auenc_state **aesp, const struct aucodec *ac, struct auenc_param *prm, const char *fmtp);
int aac_encode_frame(struct auenc_state *aes, uint8_t *buf, size_t *len, const int16_t *sampv, size_t sampc);


/* Decode */
int aac_decode_update(struct audec_state **adsp, const struct aucodec *ac, const char *fmtp);
int aac_decode_frame(struct audec_state *ads, int16_t *sampv, size_t *sampc, const uint8_t *buf, size_t len);
