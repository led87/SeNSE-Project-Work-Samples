#
# module.mk
#
# Copyright (C) 2014 - 2015 Project SeNSE 
# 

MOD		:= aac
$(MOD)_SRCS	+= aac_decode.c aac_encode.c aac.c
$(MOD)_LFLAGS	+= -lfaac -lfaad

include mk/mod.mk
