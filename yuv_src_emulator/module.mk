#
# module.mk
#
# Copyright (C) 2014 - 2015 Project SeNSE 
#

MOD		:= YUV
$(MOD)_SRCS	+= yuv420p_src.c

include mk/mod.mk
