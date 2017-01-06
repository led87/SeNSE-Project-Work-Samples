#
# module.mk
#
# Copyright (C) 2014 - 2015 SeNSE
#
#

MOD		:= openh264
$(MOD)_SRCS	+= openh264_codec.c h264_packetize.c openh264_encode.c openh264_decode.c h264_tl0d_packetize.c
$(MOD)_LFLAGS	+= -lopenh264

include mk/mod.mk
