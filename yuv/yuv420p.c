/**
 * @file yuv420p.c  Reads yuv420p video source from file.
 *
 * Copyright (C) 2015 SeNSE
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


#define YUV_WIDTH     640
#define YUV_HEIGHT    480

const YUVFileName = "/path_to_file/filename.........";

const unsigned YUV_FPS = 25;


struct vidsrc_st 
{
	struct vidsrc *vs;  /* inheritance */

	FILE *file;
	pthread_t thread;
	bool run;
	struct vidsz frame_size;
	int32_t pixfmt;
	vidsrc_frame_h *frameh;
	void *arg;
	uint8_t *buffer;
	unsigned int  buffer_size;
};


static struct vidsrc *vidsrc;




/* fps: number of frames per second (integer)
 * delays for 1/fps seconds
*/
static void delay_for_preiod_of_time(unsigned fps)
{
//	struct timespec halt;
	unsigned long nanoseconds = 1000000/fps;

//	halt.tv_sec = 0;
//	halt.tv_nsec = (unsigned long)nanoseconds/f;;
//	nanosleep(&halt, NULL);

	usleep(nanoseconds);
}


static void call_frame_handler(struct vidsrc_st *st)
{
	struct vidframe frame;

	vidframe_init_buf(&frame, st->pixfmt, &st->frame_size, st->buffer);
	st->frameh(&frame, st->arg);
}


static int read_frame(struct vidsrc_st *st)
{
	size_t n;
	int err = 0;

	memset(st->buffer, 0, st->buffer_size);

	n = fread(st->buffer, sizeof(uint8_t), st->buffer_size, st->file);
	if (n != st->buffer_size) 
	{
		warning("YUV: failed to read full frame\n");

		if(feof(st->file))
			mem_deref(st);
			
		err = 1;
		return err;
	}

	call_frame_handler(st);

	return err;
}


static void close_yuv_source(struct vidsrc_st *st)
{
	fclose(st->file);
}


static void destructor(void *arg)
{
	struct vidsrc_st *st = arg;

	if (st->run) 
	{
		st->run = false;
		pthread_join(st->thread, NULL);
	}

	if (st->file)
		close_yuv_source(st);

	mem_deref(st->buffer);
	mem_deref(st->vs);
}


static void *read_thread(void *arg)
{
	struct vidsrc_st *st = arg;
	int err;

	while (st->run) 
	{
		err = read_frame(st);
		if (err) 
		{
			warning("v4l2: read_frame: %m\n", err);
		}
	
		delay_for_preiod_of_time(YUV_FPS);
	}

	return NULL;
}

static int open_yuv_source(struct vidsrc_st *st, const char *FileName)
{
	int err = 0;

	st->file = fopen(FileName, "rb");
	if(!st->file)
	{
		err = 1;
		return err;
	}
	
	fseek(st->file, 0, SEEK_SET);

	return err;
}


static int alloc(struct vidsrc_st **stp, struct vidsrc *vs,
		 struct media_ctx **ctx, struct vidsrc_prm *prm,
		 const struct vidsz *size, const char *fmt,
		 const char *FileName, vidsrc_frame_h *frameh,
		 vidsrc_error_h *errorh, void *arg)
{
	struct vidsrc_st *st;
	int err;

	(void)ctx;
	(void)prm;
	(void)fmt;
	(void)errorh;
	(void)Filename

	if (!stp || !size || !frameh)
		return EINVAL;
	
	if(size->w != YUV_WIDTH || size->h != YUV_HEIGHT)
	{
		warning("video size do not maches\n");
		return EINVAL;
	}

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->vs = mem_ref(vs);
	st->file = NULL;

	st->frame_size = *size;
	st->frameh = frameh;
	st->arg    = arg;

	st->pixfmt = VID_FMT_YUV420P;

	err = open_yuv_source(st, YUVFileName);
	if (err)
		goto out;

	st->buffer_size = st->frame_size.w * st->frame_size.h * 3 / 2;
	st->buffer = mem_zalloc(st->buffer_size, NULL);
	if (!st->buffer) 
	{
		err = ENOMEM;
		goto out;
	}

	st->run = true;
	err = pthread_create(&st->thread, NULL, read_thread, st);
	if (err) 
	{
		st->run = false;
		goto out;
	}

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int yuv_init(void)
{
	return vidsrc_register(&vidsrc, "YUV", alloc, NULL);
}


static int yuv_close(void)
{
	vidsrc = mem_deref(vidsrc);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(v4l2) = {
	"YUV",
	"vidsrc",
	yuv_init,
	yuv_close
};
