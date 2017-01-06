/**
 * @file aac.c  AAC Audio Codec Using libfaac/libfaad2
 *
 * Copyright (C) 2014 - 2015 Project SeNSE 
 */

#include <re.h>
#include <baresip.h>
#include <faac.h>
#include "aac.h"




static struct aucodec aac = {
	.name      = "aac",
	.srate     = 44100,
	.ch        = 2,
	.encupdh   = aac_encode_update,
	.ench      = aac_encode_frame,
	.decupdh   = aac_decode_update,
	.dech      = aac_decode_frame,
};


static int module_init(void)
{
	aucodec_register(&aac);

	return 0;
}


static int module_close(void)
{
	aucodec_unregister(&aac);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(aac) = {
	"aac",
	"audio codec",
	module_init,
	module_close,
};
