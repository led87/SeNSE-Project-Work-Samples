/**
 * @file h264.tl0d_packetize.c  Interface to H.264/SVC video codec for full SVC support - RFC 6190
 *
 * Copyright (C) 2015 SeNSE Project
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>

#include "h264_tl0d.h"
#include "h264_packetize.h"
#include "openh264_codec.h"



static uint16_t AU_start_seq;
static uint16_t AU_last_seq;
static int sequence_indicator = -2;



static int init_svc_naluheader(SVC_NALUHeader *svc_header, int size)
{
	int err = 0;
	int i;
		
	for(i = 0; i < size; i++)
	{
		svc_header[i].r = 1;
		svc_header[i].idr = 0;
		svc_header[i].priorityID = 0;
		svc_header[i].interLayerPred =0;
		svc_header[i].dependencyID = 0;
		svc_header[i].qualityID = 0;
		svc_header[i].temporalID = 0;
		svc_header[i].useRefBasePic = 0;
		svc_header[i].discardable = 0;
		svc_header[i].output = 0;
		svc_header[i].rr = 3;
		svc_header[i].length = 3;
	}

	return err;
}


static int init_tl0d(TL0D *tl0d, int size)
{
	int err = 0;
	int i;

	for(i = 0; i < size; i++)
	{
		tl0d[i].NALlength = 6;
		tl0d[i].type = 31;
		tl0d[i].nalu_size = 0;
		tl0d[i].TL0picIDx = 0;
		tl0d[i].fsn  = 0;
		tl0d[i].lsn  = 0;
	}
	
	return err;
}

static int h264_Info(H264Info **h264_info)
{
	H264Info *h264Inf;
	int err = 0;
	
	h264Inf = mem_zalloc(sizeof(*h264Inf), NULL);
	if(!h264Inf)
	{
		warning("faild to allocate memeory");
		err = ENOMEM;
		return err;
	}
	
	h264Inf->numNALUs = 0;
	h264Inf->numLayers = 0;
	
	memset(h264Inf->startCodeLength, 0, sizeof(KMaxNumberOfNALUs*sizeof(uint8_t)));
	memset(h264Inf->payloadSize, 0, sizeof(KMaxNumberOfNALUs*sizeof(uint32_t)));
	memset(h264Inf->NRI, 0, sizeof(KMaxNumberOfNALUs*sizeof(uint8_t)));
	memset(h264Inf->type, 0, sizeof(KMaxNumberOfNALUs*sizeof(uint8_t)));
	memset(h264Inf->accLayerSize, 0, sizeof(KMaxNumberOfLayers*sizeof(int32_t)));
	
	err = init_svc_naluheader(h264Inf->SVCheader, KMaxNumberOfNALUs);
	if(err)
	{
		//to do
	}
	
	err = init_tl0d(h264Inf->tl0d, KMaxNumberOfNALUs);
	if(err)
	{
		//to do
	}

	*h264_info = h264Inf;
	return err;
}

static inline int32_t FindNALUStartCodeLength(const uint8_t *p, int remLength)
{
	int i;
	int32_t StartCodeLength;
    // NAL unit start code. Ex. {0,0,1} or {0,0,0,1}
    for (i = 2; i < remLength; i++)
    {
        if (p[i] == 1 && p[i - 1] == 0 && p[i - 2] == 0)
        {
            StartCodeLength = i+1;
            return StartCodeLength;
        }
    }
    return -1;
}

static int32_t ParseSVCNALUHeader(H264Info *h264_info, const uint8_t *p)
{
	int err = 0;
	if(h264_info->type[h264_info->numNALUs]  == 5)
		h264_info->SVCheader[h264_info->numNALUs].idr = 1;
		
	
//	CONSTRUCT SVC HEADER - types 14, 20
// Extended NAL unit header (3 bytes).
// +---------------+---------------+---------------+
// |0|1|2|3|4|5|6|7|0|1|2|3|4|5|6|7|0|1|2|3|4|5|6|7|
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |R|I|   PRID    |N| DID |  QID  | TID |U|D|O| RR|
// +---------------+---------------+---------------+

// R    - Reserved for future extensions (MUST be 1). Receivers SHOULD ignore the value of R.
// I    - Is layer representation an IDR layer (1) or not (0).
// PRID - Priority identifier for the NAL unit.
// N    - Specifies whether inter-layer prediction may be used for decoding the coded slice (1) or not (0).
// DID  - Indicates the inter-layer coding dependency level of a layer representation.
// QID  - Indicates the quality level of an MGS layer representation.
// TID  - Indicates the temporal level of a layer representation.
// U    - Use only reference base pictures during the inter prediction process (1) or not (0).
// D    - Discardable flag.
// O    - Output_flag. Affects the decoded picture output process as defined in Annex C of [H.264].
// RR   - Reserved_three_2bits (MUST be '11'). Receivers SHOULD ignore the value of RR.

	if(h264_info->type[h264_info->numNALUs] == 14 || h264_info->type[h264_info->numNALUs] ==20)
	{
		uint32_t curByte = h264_info->startCodeLength[h264_info->numNALUs] + 1;
		
	    h264_info->SVCheader[h264_info->numNALUs].idr        	 = (p[curByte] >> 6) & 0x01;
        h264_info->SVCheader[h264_info->numNALUs].priorityID 	 = (p[curByte++] & 0x3F);

        h264_info->SVCheader[h264_info->numNALUs].interLayerPred = (p[curByte] >> 7) & 0x01;
        h264_info->SVCheader[h264_info->numNALUs].dependencyID   = (p[curByte] >> 4) & 0x07;
        h264_info->SVCheader[h264_info->numNALUs].qualityID      = (p[curByte++] & 0x0F);

        h264_info->SVCheader[h264_info->numNALUs].temporalID     = (p[curByte] >> 5) & 0x07;
        h264_info->SVCheader[h264_info->numNALUs].useRefBasePic  = (p[curByte] >> 4) & 0x01;
        h264_info->SVCheader[h264_info->numNALUs].discardable    = (p[curByte] >> 3) & 0x01;
        h264_info->SVCheader[h264_info->numNALUs].output         = (p[curByte] >> 2) & 0x01;

        if (h264_info->type[h264_info->numNALUs] == 14)
        {
            // inform the next NALU
            memcpy(&(h264_info->SVCheader[h264_info->numNALUs+1]), &(h264_info->SVCheader[h264_info->numNALUs]), sizeof(SVC_NALUHeader));
        }
    }

	return err;
	
}

static int32_t FindNALUType(H264Info *h264_info, const uint8_t *p)
{
	int err = 0;
	
	//gets the nal unit's type 
    h264_info->type[h264_info->numNALUs] = p[h264_info->startCodeLength[h264_info->numNALUs]] & 0x1f;
	
	if(h264_info->type[h264_info->numNALUs] == 0)
	{
		re_printf("Type is 0\n");
		return 1;
	}
	
	err = ParseSVCNALUHeader(h264_info, p);
	
	return err;
}

static void GetNRI(H264Info *h264_info, const uint8_t *p)
{
    //  NAL unit header (1 byte)
    //  ---------------------------------
    // |   start code    |F|NRI|  Type   |
    //  ---------------------------------

    // NRI (2 bits) - nal_ref_idc. '00' - the NAL unit is not used to reconstruct reference pictures.
    //                             >00  - the NAL unit is required to reconstruct reference pictures
    //                                    in the same layer, or contains a parameter set.

	//gets the headers nri of the nal unit pointed by p 
    const uint8_t type = p[h264_info->startCodeLength[h264_info->numNALUs]] & 0x1f;

    // NALU type of 5, 7 and 8 should have NRI to b011
    if( type == 5 || type == 7 || type == 8)
        h264_info->NRI[h264_info->numNALUs] = 0x60;
    else
        h264_info->NRI[h264_info->numNALUs] = p[h264_info->startCodeLength[h264_info->numNALUs]] & 0x60;
}
	
static void h264_tl0d_encode(H264Info *h264_info, uint8_t *TL0D_NalUnit, int i, uint16_t fseq, uint16_t lseq, uint8_t nalu_size, uint8_t sequence_id)
{
			memset(TL0D_NalUnit, 0, TL0D_SIZE);
			
			/*NAL Unit Header */
			//F = 0, from memset
			//NRI
			TL0D_NalUnit[0] |= h264_info->NRI[i] << 5;
			//Type
			TL0D_NalUnit[0] |= h264_info->tl0d[i].type;
			
			/*SVC Header */
			//R - I - PRID 
			TL0D_NalUnit[1] |= h264_info->SVCheader[i].r << 7;
			TL0D_NalUnit[1] |= h264_info->SVCheader[i].idr << 6;
			TL0D_NalUnit[1] |= h264_info->SVCheader[i].priorityID;			
			
			//N - DID - QID
			TL0D_NalUnit[2] |= h264_info->SVCheader[i].interLayerPred << 7;
			TL0D_NalUnit[2] |= h264_info->SVCheader[i].dependencyID << 4;
			TL0D_NalUnit[2] |= h264_info->SVCheader[i].qualityID;
			
			//TID - U - D - O - RR
			TL0D_NalUnit[3] |= h264_info->SVCheader[i].temporalID << 5;
			TL0D_NalUnit[3] |= h264_info->SVCheader[i].useRefBasePic << 4;
			TL0D_NalUnit[3] |= h264_info->SVCheader[i].discardable << 3;
			TL0D_NalUnit[3] |= h264_info->SVCheader[i].output << 2;
			TL0D_NalUnit[3] |= h264_info->SVCheader[i].rr << 0;
			
			/*NUM_ENH_NALUS*/
			TL0D_NalUnit[4] = nalu_size;
			TL0D_NalUnit[4] |= sequence_id << 7;
			
			/*TL0PICIDX */
			TL0D_NalUnit[5] |= h264_info->tl0d[i].TL0picIDx;
			/*fsn*/
			TL0D_NalUnit[6] |= (uint8_t)(fseq >> 8);
			TL0D_NalUnit[7] |= (uint8_t)(fseq);
			/*lsn*/
			TL0D_NalUnit[8] |= (uint8_t)(lseq >> 8);
			TL0D_NalUnit[9] |= (uint8_t)(lseq);
}


int h264_tl0d_packetize(struct mbuf *mb, size_t pktsize,
		   videnc_packet_h *pkth, void *arg)
{
	const uint8_t *start = mb->buf;
	const uint8_t *end   = start + mb->end;
	const uint8_t *pCurrentStartCode;
	H264Info *h264Info = NULL;

	bool foundLast = false;
	int i;
	int err = 0;
	int StartCodeLength = -1;
	
	uint8_t tl0 = 0;
	bool dup = false;
	bool idr = false;	
	
	get_tl0_pic_idx(&dup, &idr, &tl0, arg);
	
	if(end - start < 4)
	{
		re_printf("Error: not enough space ing buffer: end - star < 4\n");
		return 1;
	}

	if(!h264Info)
	{
		err = h264_Info(&h264Info);
		if(err)
		{
			re_printf("Error: h264_Info()\n");
			return err;
		}
	}

	pCurrentStartCode = h264_find_startcode(mb->buf, end);

	while (pCurrentStartCode < end) 
	{
		const uint8_t *pNextStartCode;
					
		StartCodeLength = FindNALUStartCodeLength(pCurrentStartCode, end - pCurrentStartCode + 1);
		
		h264Info->startCodeLength[h264Info->numNALUs] = StartCodeLength;
		
		pNextStartCode = h264_find_startcode(pCurrentStartCode + h264Info->startCodeLength[h264Info->numNALUs], end);
		
		h264Info->payloadSize[h264Info->numNALUs] = pNextStartCode - pCurrentStartCode - StartCodeLength;
		
		if(pNextStartCode == end) 
			foundLast = true;
		
		GetNRI(h264Info, pCurrentStartCode);
		
		err = FindNALUType(h264Info, pCurrentStartCode);
		if(err)
		{
			re_printf("Error: FindNALUType()\n");
		}
		
		if(foundLast)
		{
			h264Info->tl0d[h264Info->numNALUs].TL0picIDx = tl0;
			h264Info->numNALUs++;
			
			break;
		}
		
		h264Info->tl0d[h264Info->numNALUs].TL0picIDx = tl0;
		
		pCurrentStartCode = pNextStartCode;
		h264Info->numNALUs++;
	}

	pCurrentStartCode = h264_find_startcode(mb->buf, end);
	
	if(dup)
	{	//get_seq temporary defined in video.c
		get_seq(&AU_start_seq, arg);
		AU_last_seq = AU_start_seq + h264Info->numNALUs - 1;
		sequence_indicator = -2;
	}
	else
		sequence_indicator++;

	for(i = 0; i < h264Info->numNALUs; i++)
	{
		uint8_t STAP[STAP_A_SIZE];
		uint8_t TL0D_NalUnit[TL0D_SIZE];
			
		if(STAP_A_SIZE < h264Info->payloadSize[i] + TL0D_SIZE)
		{
			err = 1;
			return err;
		}
		
		memset(STAP, 0, sizeof(STAP));
		
		if(dup)
			h264_tl0d_encode(h264Info, TL0D_NalUnit, i, AU_start_seq, AU_last_seq, h264Info->numNALUs, 0);
		else
		{
			if(sequence_indicator < 0)
				h264_tl0d_encode(h264Info, TL0D_NalUnit, i, AU_start_seq, AU_last_seq, h264Info->numNALUs, 0);
			else
				h264_tl0d_encode(h264Info, TL0D_NalUnit, i, AU_start_seq, AU_last_seq, h264Info->numNALUs, sequence_indicator);
		}
		
		memcpy(STAP, &TL0D_NalUnit[1], TL0D_SIZE - 1);
		memcpy(STAP + TL0D_SIZE - 1, pCurrentStartCode + h264Info->startCodeLength[i], (int)(h264Info->payloadSize[i] + 1));
		
		if(idr)
		{
			set_tl0(dup, true, 0, AU_start_seq, AU_last_seq, arg);
			
			foundLast = (i == h264Info->numNALUs - 1) ? true : false;
			
			err |= h264_nal_send(true, true, foundLast, TL0D_NalUnit[0], STAP, TL0D_SIZE + h264Info->payloadSize[i], pktsize, pkth, arg);
			
			pCurrentStartCode += h264Info->startCodeLength[i] + h264Info->payloadSize[i];		
			idr = false;
		}
		else
		{
			set_tl0(dup, idr, 0, AU_start_seq, AU_last_seq, arg);
		
			foundLast = (i == h264Info->numNALUs - 1) ? true : false;
			
			err |= h264_nal_send(true, true, foundLast, TL0D_NalUnit[0], STAP, TL0D_SIZE + h264Info->payloadSize[i], pktsize, pkth, arg);
			
			pCurrentStartCode += h264Info->startCodeLength[i] + h264Info->payloadSize[i];
		}
	}
	
	h264Info->numNALUs = 0;
	mem_deref(h264Info);
	h264Info = NULL;
	
	return err;
}

static void h264_svc_header_decode(SVC_NALUHeader *SVCheader, const uint8_t * NalUnit, int pos)
{
			/*SVC Header */
			//R - I - PRID 
			SVCheader->r 			= (NalUnit[pos] & 0x80) >> 7;
			SVCheader->idr 			= (NalUnit[pos] & 0x40) >> 6;
			SVCheader->priorityID 	= (NalUnit[pos] & 0x3f) >> 0;			
			
			//N - DID - QID
			SVCheader->interLayerPred 	= (NalUnit[pos + 1] & 0x80) >> 7;
			SVCheader->dependencyID 	= (NalUnit[pos + 1] & 0x70) >> 4;
			SVCheader->qualityID 		= (NalUnit[pos + 1] & 0x0F) >> 0;
			
			//TID - U - D - O - RR
			SVCheader->temporalID 		= (NalUnit[pos + 2] & 0xE0) >> 5;
			SVCheader->useRefBasePic 	= (NalUnit[pos + 2] & 0x10) >> 4;
			SVCheader->discardable 		= (NalUnit[pos + 2] & 0x08) >> 3;
			SVCheader->output			= (NalUnit[pos + 2] & 0x04) >> 2;
			SVCheader->rr 				= (NalUnit[pos + 2] & 0x03) >> 0;	
}

void h264_tl0d_decode(TL0D *tl0d, const uint8_t * SVC_NalUnit, int pos)
{
	h264_svc_header_decode(&tl0d->SVCheader, SVC_NalUnit, pos);
	 
	tl0d->nalu_size =  SVC_NalUnit[pos + 3] & 0x7F;
	
	tl0d->TL0picIDx = SVC_NalUnit[pos + 4] & 0xFF;
	tl0d->fsn |= (SVC_NalUnit[pos + 5] & 0xFF) << 8;
	tl0d->fsn |= SVC_NalUnit[pos + 6] & 0xFF;
	tl0d->lsn |= (SVC_NalUnit[pos + 7] & 0xFF) << 8;
	tl0d->lsn |= SVC_NalUnit[pos + 8] & 0xFF;
}
