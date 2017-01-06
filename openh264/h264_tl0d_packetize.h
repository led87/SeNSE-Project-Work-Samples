/**
 * @file h264_tl0d_packetize.h  Interface to H.264/SVC video codec for full SVC support - RFC 6190
 *
 * Copyright (C) 2015 SeNSE Project
 */


#include <stdint.h>

#define KMaxNumberOfNALUs 128
#define KMaxNumberOfSEINALUs 2
#define KMaxNumberOfLayers 16

#define STAP_A_SIZE 2000
#define TL0D_SIZE 10

//NAL types 14, 15, 20.
typedef struct SVC_NALUHeader
{
	uint8_t       r;
	uint8_t       idr;
	uint8_t       priorityID;
	uint8_t       interLayerPred;
	uint8_t       dependencyID;
	uint8_t       qualityID;
	uint8_t       temporalID;
	uint8_t       useRefBasePic;
	uint8_t       discardable;
	uint8_t       output;
	uint8_t       rr;
	uint8_t       length;
}SVC_NALUHeader;


typedef struct TL0D
{
	uint32_t        NALlength;
	uint8_t         type;
	uint8_t			nalu_size;
	uint8_t         TL0picIDx;
	uint16_t        fsn;
	uint16_t        lsn;
	SVC_NALUHeader	SVCheader;
}TL0D;


typedef struct H264Info
{
	uint16_t             numNALUs;
	uint8_t              numLayers;
	uint8_t              startCodeLength[KMaxNumberOfNALUs];
	uint32_t             payloadSize[KMaxNumberOfNALUs];
	uint8_t              NRI[KMaxNumberOfNALUs];
	uint8_t              type[KMaxNumberOfNALUs];
	SVC_NALUHeader 		 SVCheader[KMaxNumberOfNALUs];
	TL0D     			 tl0d[KMaxNumberOfNALUs];
	int32_t              accLayerSize[KMaxNumberOfLayers];
}H264Info;



int h264_tl0d_packetize(struct mbuf *mb, size_t pktsize,
		   videnc_packet_h *pkth, void *arg);

void h264_tl0d_decode(TL0D *tl0d, const uint8_t * NalUnit, int pos);
