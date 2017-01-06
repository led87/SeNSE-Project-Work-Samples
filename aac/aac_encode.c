/**
 * @file aac/encode.c AAC Encode using libfaac 
 *
 * Copyright (C) 2014 - 2015  Project SeNSE
 */

#include <re.h>
#include <baresip.h>
#include <faac.h>
#include "aac.h"



struct auenc_state 
{
	faacEncHandle encoder;
	unsigned long nInputSamples;
 	unsigned long nMaxOutputBytes;
	struct aac_param parammeters;
};


static void destructor(void *arg)
{
	struct auenc_state *aes = arg;

	if (aes->encoder)
		faacEncClose(aes->encoder);   

}


int aac_encode_update(struct auenc_state **aesp, const struct aucodec *ac, struct auenc_param *param, const char *fmtp)
{
	struct auenc_state *aes;
	faacEncConfigurationPtr pConfiguration;
	int err = 0;

	(void)param;
	(void)fmtp;

	if (!aesp || !ac || !ac->ch)
		return EINVAL;

	

	if (!*aesp) 
	{
		aes = mem_zalloc(sizeof(*aes), destructor);
		if (!aes)
			return ENOMEM;   

		aes->parammeters.nSampleRate = ac->srate;
		aes->parammeters.nChannels = ac->ch;
		aes->nInputSamples = 0;
 		aes->nMaxOutputBytes = 0;

		//open faac encoder
		aes->encoder = faacEncOpen(aes->parammeters.nSampleRate, 
					   aes->parammeters.nChannels, 
					   &aes->nInputSamples, 
					   &aes->nMaxOutputBytes);

		if(aes->encoder == NULL)
		{
			warning("failed to call faacEncOpen()\n");
			err = 1;
			goto out;
		}
	}

	//get current encoding configuration
	pConfiguration = faacEncGetCurrentConfiguration(aes->encoder);

	pConfiguration->aacObjectType	= LOW;
	pConfiguration->allowMidside	= 1;
 	pConfiguration->mpegVersion	= MPEG4;
 	pConfiguration->useTns		= 0; 
	pConfiguration->inputFormat	= FAAC_INPUT_16BIT;
	pConfiguration->outputFormat	= 1;

	//set encoding configuretion
	err = !faacEncSetConfiguration(aes->encoder, pConfiguration);
	if(err)
	{
		warning("failed to set aac encoding configuration()\n");
		goto out;
	}


out:
	if(err)
	    mem_deref(aes);

	*aesp = aes;
	return err;
}


int aac_encode_frame(struct auenc_state *aes, uint8_t *buf, size_t *encoded_size, const int16_t *sampv, size_t sampc)
{
	int32_t n;

	if (!aes || !buf || !encoded_size || !sampv)
		return EINVAL;

	n = faacEncEncode(aes->encoder, 
			  (int32_t *) sampv, 
			  aes->nInputSamples,  
			  buf,
			  sampc);

	if (n < 0) 
	{
		warning("aac: encode error\n");
		return EPROTO;
	}

	*encoded_size = n;

	return 0;
}

