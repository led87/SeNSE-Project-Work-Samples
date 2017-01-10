/**
 * @file aac/aac_decode.c AAC Decoder using libfaad2
 *
 * Copyright (C) 2014 - 2015 Project SeNSE 
 */

#include <re.h>
#include <baresip.h>
#include <string.h>
#include <faad.h>
#include "aac.h"



struct audec_state 
{
	faacDecHandle Decoder; 
	unsigned long nSamples;
	unsigned SampleSize;
	struct aac_param parammeters;
	int init;
};


static void destructor(void *arg)
{
	struct audec_state *ads = arg;	

	if (ads->Decoder)
		faacDecClose(ads->Decoder);

}


int aac_decode_update(struct audec_state **adsp, const struct aucodec *ac, const char *fmtp)
{

	struct audec_state *ads;
	faacDecConfigurationPtr pConfiguration;
	int err = 0;

	(void)fmtp;


	if (!adsp || !ac || !ac->ch)
		return EINVAL;

	ads = *adsp;

	if (ads)
		return 0;

	ads = mem_zalloc(sizeof(*ads), destructor);
	if (!ads)
		return ENOMEM;

	ads->init 			= 0;
	ads->parammeters.nSampleRate 	= ac->srate;
	ads->parammeters.nChannels 	= ac->ch;

	ads->Decoder = faacDecOpen();
	if (!ads->Decoder) 
	{
		warning("failed to open aac audio decoder \n");
		err = ENOMEM;
		goto out;
	}


	pConfiguration = faacDecGetCurrentConfiguration(ads->Decoder);

	if(pConfiguration)
	{
		pConfiguration->outputFormat 	= FAAD_FMT_16BIT;
		ads->SampleSize 		= 2;
		pConfiguration->defSampleRate 	= ac->srate;
		pConfiguration->defObjectType 	= LC;
	}

	faacDecSetConfiguration(ads->Decoder, pConfiguration);


 out:
	if (err)
		mem_deref(ads);
	else
		*adsp = ads;

	return err;
}


int aac_decode_frame(struct audec_state *ads, int16_t *sampv, size_t *sampc, const uint8_t *buf, size_t buf_len)
{
	faacDecFrameInfo frameInfo;
	void *out;

	if (!ads || !sampv || !sampc || !buf)
		return EINVAL;

	if(!buf_len)
		return 0;


	if(!ads->init)
	{
		unsigned long srate;
		unsigned char channels;
		int r;
		r = faacDecInit(ads->Decoder, (unsigned char *)buf, buf_len, &srate, &channels);
		if(r < 0)
		{
			warning("faad: codec init failed: %s\n", faacDecGetErrorMessage(frameInfo.error));
			return -1;
		}
		ads->parammeters.nSampleRate  = srate;
		ads->parammeters.nChannels = channels;
		ads->init = 1;
	}

	out = faacDecDecode(ads->Decoder, &frameInfo, (unsigned char *)buf, buf_len);
	if (frameInfo.error > 0) 
	{
		warning("faad: frame decoding failed: %s\n", faacDecGetErrorMessage(frameInfo.error));
		return -1;
	}

	memcpy(sampv, out, frameInfo.samples * ads->SampleSize);

	if (sampc)
		*sampc = frameInfo.samples;

	return (buf_len < (unsigned)frameInfo.bytesconsumed) ? buf_len : (unsigned)frameInfo.bytesconsumed;

}
