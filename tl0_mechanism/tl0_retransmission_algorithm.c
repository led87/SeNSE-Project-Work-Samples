/**
 * @file tl0_retransmission_algorithm.c  
 * 
 * This file implements a mechanism for detecting, requesting, and retransmitting. the base layer of H264/SVC bitstream RFC 6190.
 *
 * Copyright (C) 2014 - 2015 SENSE
 */

#include <string.h>
#include <time.h>
#include <re.h>
#include <baresip.h>
#include "core.h"

#define NON_TL0_VALUE 255
#define MAX_SEQ_INDEX 255
#define MAX_RTP_SEQUENCE 65536
#define MAX_PACKET_TOLERANCE 50
#define MAX_NACK_TOLERANCE 5

static struct list tl0l = LIST_INIT;
static int packet_count = 0;

struct enh_status
{
	uint8_t nalu_size;
	uint8_t received_nalus;
	bool got_enh;
	bool skip;
};

struct TL0_info
{
	struct le le;
	
	uint8_t TL0;
	uint32_t first_seq;
	uint32_t last_seq;
	uint16_t num_nalus;
	int32_t seq_index[MAX_SEQ_INDEX];
	int nack_index[MAX_SEQ_INDEX];
	bool tl0_completed;
	bool has_enhancement;
	
	struct enh_status enh_layers[3];
};

void list_destructor(void)
{
	list_flush(&tl0l);
}

static uint8_t get_tl0_from_tl0d(uint8_t v)
{
	return (v & 0xFF);
}

static uint8_t h264_hdr_decode(uint8_t v)
{
	return (v>>0 & 0x1f);
}

static uint32_t get_tl0(struct mbuf *mb)
{
	uint8_t type;
	uint8_t tl0;
	
	type = h264_hdr_decode(mb->buf[mb->pos]);	
	
	if(type == 31)
	{
		tl0 = get_tl0_from_tl0d(mb->buf[mb->pos + 5]);
		return tl0;
	}
	
	return NON_TL0_VALUE;
}

static uint32_t calc_scanning_length(struct TL0_info *inf, struct le *le)
{
	uint32_t scanning_length = 0;
	
	if(inf->has_enhancement || le->next)
		scanning_length = inf->num_nalus;
	else
	{
		int i;
		
		for(i = inf->num_nalus - 1; (i >= 0) && (inf->seq_index[i] == -1); i--){}
		
		scanning_length = i;
	}
	
	return scanning_length;
}

static uint16_t get_fseq_from_tl0d(struct mbuf *mb)
{
	uint16_t v;
	
	memset(&v, 0, sizeof(v));
	
	v |= (mb->buf[mb->pos + 6] & 0xFF) << 8;
	v |= mb->buf[mb->pos + 7] & 0xFF;
	
	return v;
}

static uint16_t get_lseq_from_tl0d(struct mbuf *mb)
{
	uint16_t v;
	
	memset(&v, 0, sizeof(v));
	
	v |= (mb->buf[mb->pos + 8] & 0xFF) << 8;
	v |= mb->buf[mb->pos + 9] & 0xFF;
	
	return v;
}

static uint8_t get_num_of_nalus(struct mbuf *mb)
{
	uint8_t v = 0;
	
	v = (mb->buf[mb->pos + 4] & 0x7F);
	
	return v;
}

static uint8_t get_sequence_id(struct mbuf *mb)
{
	uint8_t v = 0;
	
	v = (mb->buf[mb->pos + 4] >> 7);
	
	return v;
}

static uint8_t get_temporal_from_tl0d(struct mbuf *mb)
{
	uint8_t v;
	
	memset(&v, 0, sizeof(v));
	
	v = (mb->buf[mb->pos + 3] & 0xE0) >> 5;
	
	return v;
}

static int init_tl0(struct TL0_info **inf, uint8_t tl0, uint16_t start_seq, uint16_t last_seq)
{
	int err = 0;
	
	struct TL0_info *tl0_info;
	
	tl0_info = mem_zalloc(sizeof(*tl0_info), NULL);
	
	tl0_info->TL0 = tl0;
	tl0_info->first_seq = start_seq;
	tl0_info->last_seq = last_seq;
	tl0_info->tl0_completed = false;	
	tl0_info->has_enhancement = false;
	tl0_info->num_nalus = last_seq > start_seq ? (last_seq - start_seq + 1) : (MAX_RTP_SEQUENCE - start_seq + last_seq + 1);
	
	memset(tl0_info->seq_index, -1, sizeof(tl0_info->seq_index));
	memset(tl0_info->nack_index, 0, sizeof(tl0_info->nack_index));
	
	tl0_info->enh_layers[0].nalu_size = 0;
	tl0_info->enh_layers[0].received_nalus = 0;
	tl0_info->enh_layers[0].got_enh = false;
	tl0_info->enh_layers[0].skip = false;
	
	tl0_info->enh_layers[1].nalu_size = 0;
	tl0_info->enh_layers[1].received_nalus = 0;
	tl0_info->enh_layers[1].got_enh = false;
	
	tl0_info->enh_layers[2].nalu_size = 0;
	tl0_info->enh_layers[2].received_nalus = 0;
	tl0_info->enh_layers[2].got_enh = false;
	tl0_info->enh_layers[2].skip = false;
	
	*inf = tl0_info;
	
	return err;
}

static int update_tl0(struct TL0_info *inf, uint16_t seq)
{
	int err = 0;
	uint16_t i;
	int position;
	
	position = (seq >= inf->first_seq) ? (seq - inf->first_seq) : (MAX_RTP_SEQUENCE - inf->first_seq + seq);
	inf->seq_index[position] = seq;
	
	for(i = 0; i < inf->num_nalus; i++)
	{
		if(inf->seq_index[i] == -1)
			break;
	}
	
	if(i == inf->num_nalus)
		inf->tl0_completed = true;
		
	return err;
}

static bool check_if_already(struct TL0_info *inf, uint16_t seq)
{
	int position;
	
	position = (seq >= inf->first_seq) ? (seq - inf->first_seq) : (MAX_RTP_SEQUENCE - inf->first_seq + seq);
	
	if(inf->seq_index[position] == seq)		
		return true;
		
	return false;
}

static bool check_cycle(struct TL0_info *inf, uint16_t first_seq, uint16_t last_seq)
{
	if(inf->first_seq == first_seq && inf->last_seq == last_seq)
		return false;
	
	return true;
}

static struct TL0_info *tl0_find(uint8_t tl0, uint16_t start_seq, uint16_t last_seq)
{
	struct le *le;
	
	for (le=tl0l.head; le; le=le->next)
	{
		struct TL0_info *tl0_inf = le->data;
		
		if (tl0_inf->TL0 == tl0)
		{			
			if(check_cycle(tl0_inf, start_seq, last_seq))
			{				
				list_unlink(&tl0_inf->le);
				mem_deref(tl0_inf);
				
				return NULL;
			}
			
			return tl0_inf;
		}
	}
	
	return NULL;
}

static int request_missing_tl0_packets(struct stream *s)
{
	struct TL0_info *inf;
	struct le *le;
	uint32_t i;
	uint32_t scanning_length;
	uint16_t fsn;
	uint16_t blp;
	int counter;
	bool has_fsn = false;
	
	for(le = tl0l.head; le; le = le->next)
	{
		counter = fsn = blp = scanning_length = 0;
		inf = le->data;
		
		if(!inf->tl0_completed)
		{
			scanning_length = calc_scanning_length(inf, le);
			
			for(i = 0; i < scanning_length; i++)
			{
				if(inf->seq_index[i] == -1)
				{						
					if(inf->nack_index[i] == 0 || inf->nack_index[i] == MAX_NACK_TOLERANCE)
					{
						inf->nack_index[i] = 1;
						
						if(!has_fsn)
						{
							fsn = inf->first_seq + i;
							has_fsn = true;
						}
						else
						{
							if(counter == 17)
							{
								rtcp_send_nack( s->rtp, fsn, blp);
								
								fsn = inf->first_seq + 1;
								counter = 0;
								blp = 0;
							}
							else
							{
								counter++;
								blp |= (1 << (16 - counter));
							}
						}
					}
					else
					{
						inf->nack_index[i]++;
					}
				}
			}
		
			if(has_fsn)
				rtcp_send_nack( s->rtp, fsn, blp);
		}
	}
	
	return 0;
}

static int send_nalu_to_decoder(struct mbuf *mb, struct rtp_header hdr)
{
	uint8_t temporal_id;	
	
	temporal_id = get_temporal_from_tl0d(mb);
	
	if(temporal_id > 0)
	{
		uint8_t tl0;
		uint16_t first_seq;
		uint16_t last_seq;
		struct TL0_info *inf;

		tl0 = get_tl0(mb);
		first_seq = get_fseq_from_tl0d(mb);
		last_seq = get_lseq_from_tl0d(mb);
		
		inf = tl0_find(tl0, first_seq, last_seq);
		
		if(temporal_id == 1)
		{
			if(hdr.m && (inf->enh_layers[1].nalu_size == inf->enh_layers[1].received_nalus))
			{
				inf->enh_layers[1].got_enh = true;
					
				return 0;
			}
			else if(hdr.m && (inf->enh_layers[1].nalu_size != inf->enh_layers[1].received_nalus))
			{
				inf->enh_layers[2].skip = true;
				
				return 1;
			}
			
			if(!inf->enh_layers[0].got_enh && !inf->enh_layers[0].skip)
			{
				inf->enh_layers[0].skip = true;
				return -1;
			}	
		}
		else
		{
			uint8_t sequence_id;
			
			sequence_id = get_sequence_id(mb);
			if(sequence_id)
			{
				if(hdr.m && (inf->enh_layers[2].nalu_size == inf->enh_layers[2].received_nalus))
				{
					if(inf->enh_layers[1].got_enh)
					{
						inf->enh_layers[2].got_enh = true;
						return 0;
					}
					
					inf->enh_layers[2].skip = true;
					return 1;
				}
				else if(hdr.m && (inf->enh_layers[2].nalu_size != inf->enh_layers[2].received_nalus))
				{
					inf->enh_layers[2].skip = true;
					return 1;
				}
				else
				{
					if(inf->enh_layers[1].got_enh)
						return 0;
					
					inf->enh_layers[2].skip = true;
					return 1;
				}
			}
			else
			{
				if(hdr.m && (inf->enh_layers[0].nalu_size == inf->enh_layers[0].received_nalus))
				{
					inf->enh_layers[0].got_enh = true;
					
					return 0;
				}
				else if(hdr.m && (inf->enh_layers[0].nalu_size != inf->enh_layers[0].received_nalus))
				{
					inf->enh_layers[0].skip = true;
					return 1;
				}
			}
		}
	}
	else
	{
		uint8_t tl0;
		uint16_t first_seq;
		uint16_t last_seq;
		struct TL0_info *inf;
		struct TL0_info *prev_inf;

		tl0 = get_tl0(mb);
		first_seq = get_fseq_from_tl0d(mb);
		last_seq = get_lseq_from_tl0d(mb);
		
		inf = tl0_find(tl0, first_seq, last_seq);
		
		if(inf->le.prev)
		{
			prev_inf = inf->le.prev->data;
			
			if(!prev_inf->enh_layers[2].got_enh && prev_inf->enh_layers[2].nalu_size != 0 && prev_inf->enh_layers[2].received_nalus != 0 && !prev_inf->enh_layers[2].skip)
			{
				prev_inf->enh_layers[2].skip = true;
				return -1;
			}
			
			if(!prev_inf->enh_layers[1].got_enh && prev_inf->enh_layers[1].nalu_size != 0 && prev_inf->enh_layers[1].received_nalus != 0 && !prev_inf->enh_layers[1].skip)
			{
				prev_inf->enh_layers[1].skip = true;
				return -1;
			}
			
			if(!prev_inf->enh_layers[0].got_enh && prev_inf->enh_layers[0].nalu_size != 0 && prev_inf->enh_layers[0].received_nalus != 0 && !prev_inf->enh_layers[0].skip)
			{
				prev_inf->enh_layers[0].skip = true;
				return -1;
			}
			
			return 0;			
		}
	}
	
	return 0;
}

void rtp_recv_tl0(const struct sa *src, const struct rtp_header *hdr,
		     struct mbuf *mb, void *arg)
{
	struct stream *s = arg;
	bool flush = false;
	int err;

	if (!mbuf_get_left(mb))
		return;

	if (!(sdp_media_ldir(s->sdp) & SDP_RECVONLY))
		return;

	metric_add_packet(&s->metric_rx, mbuf_get_left(mb));

	if (hdr->ssrc != s->ssrc_rx) {
		if (s->ssrc_rx) {
			flush = true;
			info("stream: %s: SSRC changed %x -> %x"
			     " (%u bytes from %J)\n",
			     sdp_media_name(s->sdp), s->ssrc_rx, hdr->ssrc,
			     mbuf_get_left(mb), src);
		}
		s->ssrc_rx = hdr->ssrc;
	}

	if(isVideo(s))
	{
		uint8_t tl0;
		uint16_t first_seq;
		uint16_t last_seq;
		uint8_t temporal_id;
		struct TL0_info *inf;
		
		tl0 = get_tl0(mb);
		
		if(s->requested_fir && tl0 != 0)
		{
			list_flush(&tl0l);
			jbuf_flush(s->jbuf);
			
			if(packet_count == MAX_PACKET_TOLERANCE)
			{
				stream_send_fir(s, true);
				packet_count = 0;
				
				return;
			}
			else
			{
				packet_count++;
				
				return;
			}
		}
		else if(s->requested_fir && tl0 == 0)
		{
			list_flush(&tl0l);
			jbuf_flush(s->jbuf);
			s->requested_fir = false;
			packet_count = 0;
		}
		
		//Get the first sequence number for the TL0 AU
		first_seq = get_fseq_from_tl0d(mb);
		
		//Get the last sequence number for the TL0 AU
		last_seq = get_lseq_from_tl0d(mb);
		
		//Get Temporal ID from TL0D
		temporal_id = get_temporal_from_tl0d(mb);
		
		//Find TL0_info from tl0l list
		inf = tl0_find(tl0, first_seq, last_seq);
		
		if(!inf)
		{
			err = init_tl0(&inf, tl0, first_seq, last_seq);
				
			if(err)
			{
				//TODO Error handling
				re_printf("RECEIVER: Error init_tl0()\n");
				return;
			}
			
			list_append(&tl0l, &inf->le, inf);
		}
		
		if(temporal_id > 0)
		{
			uint8_t nalu_size;
			
			inf->has_enhancement = true;
			
			nalu_size = get_num_of_nalus(mb);
			
			if(temporal_id == 1)
			{
				inf->enh_layers[1].nalu_size = nalu_size;
				inf->enh_layers[1].received_nalus++;
			}
			else
			{
				uint8_t sequence_id;
				
				sequence_id = get_sequence_id(mb);
				
				if(sequence_id)
				{
					if(inf->enh_layers[2].skip)
						return;
						
					inf->enh_layers[2].nalu_size = nalu_size;
					inf->enh_layers[2].received_nalus++;
				}
				else
				{
					if(inf->enh_layers[0].skip)
						return;
						
					inf->enh_layers[0].nalu_size = nalu_size;
					inf->enh_layers[0].received_nalus++;
				}
			}
		}
		else
		{
			if(check_if_already(inf, hdr->seq))
			{
				struct rtp_header hdr2;
				void *mb2 = NULL;
				int ret = 0;
			
				err = request_missing_tl0_packets(s);
				if(err)
				{
					//TODO error handling
					re_printf("Error: request_missing_tl0_packets\n");
					return;
				}
				
				if (jbuf_get(s->jbuf, &hdr2, &mb2))
				{

					if (!s->jbuf_started)
						return;

					memset(&hdr2, 0, sizeof(hdr2));
				}
				else
				{
					s->jbuf_started = true;
					
					if( (ret = send_nalu_to_decoder(mb2, hdr2)) == 1)
					{
						hdr2.cc = 1;
						s->rtph(&hdr2, mb2, s->arg);
						
						mem_deref(mb2);
						
						return;
					}
					else if(ret == -1)
					{
						
						hdr2.cc = 1;
						s->rtph(&hdr2, mb2, s->arg);
						
						hdr2.cc = 0;
						s->rtph(&hdr2, mb2, s->arg);
						
						mem_deref(mb2);
						
						return;
					}
				}
				
				s->jbuf_started = true;
					
				s->rtph(&hdr2, mb2, s->arg);

				mem_deref(mb2);	
				
				return;
			}
			
			if(!inf->tl0_completed)
			{
				err = update_tl0(inf, hdr->seq);
				if(err)
				{
					//TODO error handling
					re_printf("Error: update_tl0\n");
					return;
				}
			}
		}
		
		err = request_missing_tl0_packets(s);
		if(err)
		{
			//TODO error handling
			re_printf("Error: request_missing_tl0_packets\n");
			return;
		}
		
		if (s->jbuf)
		{
			struct rtp_header hdr2;
			void *mb2 = NULL;
			int ret = 0;

			/* Put frame in Jitter Buffer */
			if (flush)
				jbuf_flush(s->jbuf);
			
			err = jbuf_put(s->jbuf, hdr, mb);
			if (err)
			{
				warning("%s: dropping %u bytes from %J (%m)\n",
					 sdp_media_name(s->sdp), mb->end,
					 src, err);
				s->metric_rx.n_err++;
			}

			if (jbuf_get(s->jbuf, &hdr2, &mb2))
			{

				if (!s->jbuf_started)
					return;

				memset(&hdr2, 0, sizeof(hdr2));
			}
			else
			{
				s->jbuf_started = true;
				
				if( (ret = send_nalu_to_decoder(mb2, hdr2)) == 1)
				{
					hdr2.cc = 1;
					s->rtph(&hdr2, mb2, s->arg);
					
					mem_deref(mb2);
					
					return;
				}
				else if(ret == -1)
				{
					
					hdr2.cc = 1;
					s->rtph(&hdr2, mb2, s->arg);
					
					hdr2.cc = 0;
					s->rtph(&hdr2, mb2, s->arg);
					
					mem_deref(mb2);
					
					return;
				}
			}
			
			s->jbuf_started = true;
				
			s->rtph(&hdr2, mb2, s->arg);

			mem_deref(mb2);	
		}
		else
		{
			s->rtph(hdr, mb, s->arg);
		}
	}
	else
	{
		if (s->jbuf)
		{
			struct rtp_header hdr2;
			void *mb2 = NULL;

			/* Put frame in Jitter Buffer */
			if (flush)
				jbuf_flush(s->jbuf);

			err = jbuf_put(s->jbuf, hdr, mb);
			if (err)
			{
				warning("%s: dropping %u bytes from %J (%m)\n",
					 sdp_media_name(s->sdp), mb->end,
					 src, err);
				s->metric_rx.n_err++;
			}

			if (jbuf_get(s->jbuf, &hdr2, &mb2))
			{

				if (!s->jbuf_started)
					return;

				memset(&hdr2, 0, sizeof(hdr2));
			}

			s->jbuf_started = true;

			s->rtph(&hdr2, mb2, s->arg);

			mem_deref(mb2);
		}
		else
		{
			s->rtph(hdr, mb, s->arg);
		}
	}
	
	return;
}
