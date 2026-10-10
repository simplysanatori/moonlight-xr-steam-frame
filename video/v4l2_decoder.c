// Moonlight XR Native - V4L2 stateful (M2M) H.264/HEVC decoder for the
// Steam Frame's Iris decoder. See v4l2_decoder.h for the API contract.
//
// Copyright (C) 2026 Moonlight XR Native contributors.
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License version 3 as published by
// the Free Software Foundation. It is distributed WITHOUT ANY WARRANTY; see
// LICENSE.txt for the full license text.

#define _GNU_SOURCE
#include "v4l2_decoder.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <linux/videodev2.h>
#include <libdrm/drm_fourcc.h>

#define OUT_TYPE V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
#define CAP_TYPE V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
#define MAX_OUT 16
#define MAX_CAP 32
#define PTS_RING 256

enum cap_state { CAP_FREE = 0, CAP_DRIVER, CAP_APP };

typedef struct {
	void *map;
	size_t length;
	bool queued;
} out_buf;

typedef struct {
	enum cap_state state;
	int fd[MLXR_VDEC_MAX_PLANES];
	uint64_t inode[MLXR_VDEC_MAX_PLANES];
	uint32_t length[MLXR_VDEC_MAX_PLANES];
} cap_buf;

typedef struct {
	uint64_t seq;
	int64_t pts;
	uint64_t t_submit_ns;
} pts_entry;

struct mlxr_vdec {
	mlxr_vdec_config cfg;
	int fd;
	uint32_t out_fourcc;

	// OUTPUT side: submit thread only (out_mtx also taken by reset/close).
	pthread_mutex_t out_mtx;
	out_buf out[MAX_OUT];
	uint32_t n_out;
	uint64_t next_seq;

	// CAPTURE side: receive thread + release from any thread.
	pthread_mutex_t cap_mtx;
	cap_buf cap[MAX_CAP];
	uint32_t n_cap;
	bool cap_streaming;
	bool change_pending;   // SOURCE_CHANGE seen, draining to LAST
	bool change_ready;     // drained, waiting for apply_format_change()
	uint32_t generation;
	uint32_t coded_w, coded_h, vis_w, vis_h;
	uint32_t cap_fourcc, cap_planes;
	uint32_t bpl[MLXR_VDEC_MAX_PLANES], sizeimage[MLXR_VDEC_MAX_PLANES];
	uint32_t colorspace, ycbcr_enc, quantization;

	pthread_mutex_t pts_mtx;
	pts_entry pts[PTS_RING];

	pthread_mutex_t stats_mtx;
	mlxr_vdec_stats st;
	uint64_t dequeued_aus; // AUs that produced output or an error
};

static uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void vlog(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("[vdec] ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

// ioctl with EINTR retry. On failure logs the ioctl name and errno unless the
// errno is one the caller expects (quiet_errno), and returns -errno.
static int xioctl_(int fd, unsigned long req, void *arg, const char *name, int quiet_errno)
{
	int r;
	do {
		r = ioctl(fd, req, arg);
	} while (r < 0 && errno == EINTR);
	if (r < 0) {
		int e = errno;
		if (e != quiet_errno)
			vlog("%s failed: %s (errno %d)", name, strerror(e), e);
		return -e;
	}
	return 0;
}
#define XIOCTL(fd, req, arg) xioctl_(fd, req, arg, #req, 0)
#define XIOCTL_Q(fd, req, arg, quiet) xioctl_(fd, req, arg, #req, quiet)

const char *mlxr_vdec_status_str(int s)
{
	switch (s) {
	case MLXR_VDEC_OK: return "ok";
	case MLXR_VDEC_AGAIN: return "again";
	case MLXR_VDEC_FORMAT_CHANGE: return "format-change";
	case MLXR_VDEC_E_DECODE: return "decode-error";
	case MLXR_VDEC_E_BUSY: return "busy";
	case MLXR_VDEC_E_TOO_BIG: return "au-too-big";
	case MLXR_VDEC_E_IO: return "io-error";
	case MLXR_VDEC_E_FATAL: return "fatal";
	case MLXR_VDEC_E_INVALID: return "invalid";
	}
	return "?";
}

static const char *fourcc_str(uint32_t f, char buf[5])
{
	memcpy(buf, &f, 4);
	buf[4] = 0;
	return buf;
}

void mlxr_vdec_config_defaults(mlxr_vdec_config *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->device = "/dev/video-dec0";
	cfg->codec = MLXR_CODEC_HEVC;
	cfg->width_hint = 1920;
	cfg->height_hint = 1080;
	cfg->capture_buffers = 12;
	cfg->output_buffers = 6;
}

// ---------------------------------------------------------------- OUTPUT side

static int setup_output(mlxr_vdec *d)
{
	struct v4l2_format f = { .type = OUT_TYPE };
	f.fmt.pix_mp.pixelformat = d->out_fourcc;
	f.fmt.pix_mp.width = d->cfg.width_hint;
	f.fmt.pix_mp.height = d->cfg.height_hint;
	f.fmt.pix_mp.num_planes = 1;
	if (XIOCTL(d->fd, VIDIOC_S_FMT, &f) < 0)
		return MLXR_VDEC_E_IO;
	if (d->cfg.verbose) {
		char b[5];
		vlog("OUTPUT %s %ux%u sizeimage %u", fourcc_str(f.fmt.pix_mp.pixelformat, b),
		     f.fmt.pix_mp.width, f.fmt.pix_mp.height, f.fmt.pix_mp.plane_fmt[0].sizeimage);
	}

	struct v4l2_requestbuffers rb = { .count = d->cfg.output_buffers, .type = OUT_TYPE,
					  .memory = V4L2_MEMORY_MMAP };
	if (XIOCTL(d->fd, VIDIOC_REQBUFS, &rb) < 0)
		return MLXR_VDEC_E_IO;
	if (rb.count == 0 || rb.count > MAX_OUT) {
		vlog("REQBUFS OUTPUT returned %u buffers", rb.count);
		return MLXR_VDEC_E_FATAL;
	}
	d->n_out = rb.count;
	for (uint32_t i = 0; i < d->n_out; i++) {
		struct v4l2_plane pl[VIDEO_MAX_PLANES] = { 0 };
		struct v4l2_buffer b = { .type = OUT_TYPE, .memory = V4L2_MEMORY_MMAP, .index = i,
					 .m.planes = pl, .length = VIDEO_MAX_PLANES };
		if (XIOCTL(d->fd, VIDIOC_QUERYBUF, &b) < 0)
			return MLXR_VDEC_E_IO;
		void *m = mmap(NULL, pl[0].length, PROT_READ | PROT_WRITE, MAP_SHARED, d->fd,
			       pl[0].m.mem_offset);
		if (m == MAP_FAILED) {
			vlog("mmap OUTPUT %u failed: %s (errno %d)", i, strerror(errno), errno);
			return MLXR_VDEC_E_IO;
		}
		d->out[i].map = m;
		d->out[i].length = pl[0].length;
		d->out[i].queued = false;
	}
	return MLXR_VDEC_OK;
}

// Reclaim OUTPUT buffers the driver has consumed (non-blocking).
static int reclaim_output(mlxr_vdec *d)
{
	for (;;) {
		struct v4l2_plane pl[VIDEO_MAX_PLANES] = { 0 };
		struct v4l2_buffer b = { .type = OUT_TYPE, .memory = V4L2_MEMORY_MMAP,
					 .m.planes = pl, .length = VIDEO_MAX_PLANES };
		int r = XIOCTL_Q(d->fd, VIDIOC_DQBUF, &b, EAGAIN);
		if (r == -EAGAIN)
			return 0;
		if (r < 0)
			return r;
		if (b.index < d->n_out)
			d->out[b.index].queued = false;
	}
}

static int free_output_index(mlxr_vdec *d)
{
	for (uint32_t i = 0; i < d->n_out; i++)
		if (!d->out[i].queued)
			return (int)i;
	return -1;
}

int mlxr_vdec_submit(mlxr_vdec *d, const void *au, uint32_t size, int64_t pts)
{
	if (!d || !au || !size)
		return MLXR_VDEC_E_INVALID;
	pthread_mutex_lock(&d->out_mtx);
	int ret = MLXR_VDEC_OK;
	if (reclaim_output(d) < 0) {
		ret = MLXR_VDEC_E_IO;
		goto out;
	}
	int idx = free_output_index(d);
	// All OUTPUT buffers busy: the decoder is behind. Wait up to 5 ms.
	for (int tries = 0; idx < 0 && tries < 20; tries++) {
		struct timespec nap = { 0, 250000 };
		nanosleep(&nap, NULL);
		if (reclaim_output(d) < 0) {
			ret = MLXR_VDEC_E_IO;
			goto out;
		}
		idx = free_output_index(d);
	}
	if (idx < 0) {
		vlog("submit: no free OUTPUT buffer, AU of %u bytes dropped", size);
		ret = MLXR_VDEC_E_BUSY;
		goto out;
	}
	out_buf *ob = &d->out[idx];
	if (size > ob->length) {
		vlog("submit: AU of %u bytes exceeds the %zu-byte OUTPUT buffer", size, ob->length);
		ret = MLXR_VDEC_E_TOO_BIG;
		goto out;
	}
	memcpy(ob->map, au, size);

	uint64_t seq = d->next_seq++;
	uint64_t t = now_ns();
	pthread_mutex_lock(&d->pts_mtx);
	d->pts[seq % PTS_RING] = (pts_entry){ .seq = seq, .pts = pts, .t_submit_ns = t };
	pthread_mutex_unlock(&d->pts_mtx);

	struct v4l2_plane pl[1] = { { .bytesused = size, .length = (uint32_t)ob->length } };
	struct v4l2_buffer b = { .type = OUT_TYPE, .memory = V4L2_MEMORY_MMAP, .index = (uint32_t)idx,
				 .m.planes = pl, .length = 1 };
	// The timestamp carries our sequence number; the driver copies it to the
	// CAPTURE buffer it decodes this AU into.
	b.timestamp.tv_sec = (time_t)(seq / 1000000);
	b.timestamp.tv_usec = (suseconds_t)(seq % 1000000);
	if (XIOCTL(d->fd, VIDIOC_QBUF, &b) < 0) {
		ret = MLXR_VDEC_E_IO;
		goto out;
	}
	ob->queued = true;
	pthread_mutex_lock(&d->stats_mtx);
	d->st.aus_submitted++;
	uint64_t inflight = d->st.aus_submitted - d->dequeued_aus;
	if (inflight > d->st.in_flight_max)
		d->st.in_flight_max = (uint32_t)inflight;
	pthread_mutex_unlock(&d->stats_mtx);
out:
	if (ret < 0) {
		pthread_mutex_lock(&d->stats_mtx);
		d->st.aus_dropped++;
		pthread_mutex_unlock(&d->stats_mtx);
	}
	pthread_mutex_unlock(&d->out_mtx);
	return ret;
}

int mlxr_vdec_reset(mlxr_vdec *d)
{
	if (!d)
		return MLXR_VDEC_E_INVALID;
	pthread_mutex_lock(&d->out_mtx);
	int type = OUT_TYPE, ret = MLXR_VDEC_OK;
	if (XIOCTL(d->fd, VIDIOC_STREAMOFF, &type) < 0)
		ret = MLXR_VDEC_E_IO;
	for (uint32_t i = 0; i < d->n_out; i++)
		d->out[i].queued = false;
	if (ret == MLXR_VDEC_OK && XIOCTL(d->fd, VIDIOC_STREAMON, &type) < 0)
		ret = MLXR_VDEC_E_FATAL;
	pthread_mutex_lock(&d->stats_mtx);
	d->dequeued_aus = d->st.aus_submitted; // the dropped AUs will never come out
	pthread_mutex_unlock(&d->stats_mtx);
	pthread_mutex_unlock(&d->out_mtx);
	return ret;
}

// --------------------------------------------------------------- CAPTURE side

static void free_capture_fds(mlxr_vdec *d)
{
	for (uint32_t i = 0; i < d->n_cap; i++) {
		for (int p = 0; p < MLXR_VDEC_MAX_PLANES; p++) {
			if (d->cap[i].fd[p] >= 0 && (p == 0 || d->cap[i].fd[p] != d->cap[i].fd[0]))
				close(d->cap[i].fd[p]);
			d->cap[i].fd[p] = -1;
		}
		d->cap[i].state = CAP_FREE;
	}
}

// Read the CAPTURE format the driver settled on after SOURCE_CHANGE.
static int read_capture_format(mlxr_vdec *d, struct v4l2_format *f, uint32_t *vis_w, uint32_t *vis_h)
{
	memset(f, 0, sizeof(*f));
	f->type = CAP_TYPE;
	if (XIOCTL(d->fd, VIDIOC_G_FMT, f) < 0)
		return MLXR_VDEC_E_IO;
	*vis_w = f->fmt.pix_mp.width;
	*vis_h = f->fmt.pix_mp.height;
	struct v4l2_selection sel = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
				      .target = V4L2_SEL_TGT_COMPOSE };
	if (XIOCTL_Q(d->fd, VIDIOC_G_SELECTION, &sel, EINVAL) == 0 && sel.r.width && sel.r.height &&
	    sel.r.width <= f->fmt.pix_mp.width && sel.r.height <= f->fmt.pix_mp.height) {
		*vis_w = sel.r.width;
		*vis_h = sel.r.height;
	}
	return MLXR_VDEC_OK;
}

static bool format_matches(const mlxr_vdec *d, const struct v4l2_format *f, uint32_t vw, uint32_t vh)
{
	const struct v4l2_pix_format_mplane *p = &f->fmt.pix_mp;
	if (p->pixelformat != d->cap_fourcc || p->width != d->coded_w || p->height != d->coded_h ||
	    p->num_planes != d->cap_planes || vw != d->vis_w || vh != d->vis_h)
		return false;
	for (uint32_t i = 0; i < p->num_planes && i < MLXR_VDEC_MAX_PLANES; i++)
		if (p->plane_fmt[i].bytesperline != d->bpl[i] ||
		    p->plane_fmt[i].sizeimage > d->sizeimage[i])
			return false;
	return true;
}

static int queue_capture(mlxr_vdec *d, uint32_t i)
{
	struct v4l2_plane pl[VIDEO_MAX_PLANES] = { 0 };
	for (uint32_t p = 0; p < d->cap_planes; p++)
		pl[p].length = d->cap[i].length[p];
	struct v4l2_buffer b = { .type = CAP_TYPE, .memory = V4L2_MEMORY_MMAP, .index = i,
				 .m.planes = pl, .length = d->cap_planes };
	if (XIOCTL(d->fd, VIDIOC_QBUF, &b) < 0)
		return MLXR_VDEC_E_IO;
	d->cap[i].state = CAP_DRIVER;
	return MLXR_VDEC_OK;
}

static int stream_capture(mlxr_vdec *d, bool on)
{
	int type = CAP_TYPE;
	if (on) {
		if (XIOCTL(d->fd, VIDIOC_STREAMON, &type) < 0)
			return MLXR_VDEC_E_IO;
	} else {
		if (XIOCTL(d->fd, VIDIOC_STREAMOFF, &type) < 0)
			return MLXR_VDEC_E_IO;
	}
	d->cap_streaming = on;
	return MLXR_VDEC_OK;
}

// Allocate CAPTURE at the current driver format, export every buffer once,
// queue them and stream on. Called with cap_mtx held.
static int setup_capture(mlxr_vdec *d)
{
	struct v4l2_format f;
	uint32_t vw, vh;
	int r = read_capture_format(d, &f, &vw, &vh);
	if (r)
		return r;
	// Ask for linear NV12 explicitly (the driver's default today, but the
	// list also has Q08C first).
	if (f.fmt.pix_mp.pixelformat != V4L2_PIX_FMT_NV12) {
		f.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
		if (XIOCTL(d->fd, VIDIOC_S_FMT, &f) < 0)
			return MLXR_VDEC_E_IO;
		r = read_capture_format(d, &f, &vw, &vh);
		if (r)
			return r;
	}
	struct v4l2_pix_format_mplane *p = &f.fmt.pix_mp;
	if (p->pixelformat != V4L2_PIX_FMT_NV12 || p->num_planes < 1 ||
	    p->num_planes > MLXR_VDEC_MAX_PLANES) {
		char b[5];
		vlog("unexpected CAPTURE format %s with %u planes", fourcc_str(p->pixelformat, b),
		     p->num_planes);
		return MLXR_VDEC_E_FATAL;
	}
	d->cap_fourcc = p->pixelformat;
	d->cap_planes = p->num_planes;
	d->coded_w = p->width;
	d->coded_h = p->height;
	d->vis_w = vw;
	d->vis_h = vh;
	for (uint32_t i = 0; i < p->num_planes; i++) {
		d->bpl[i] = p->plane_fmt[i].bytesperline;
		d->sizeimage[i] = p->plane_fmt[i].sizeimage;
	}
	d->colorspace = p->colorspace;
	d->ycbcr_enc = p->ycbcr_enc;
	d->quantization = p->quantization;
	// A coded height that cannot hold the picture would make the driver DMA
	// past the end of the buffers (SteamOS #2869): refuse it.
	if (d->coded_w < vw || d->coded_h < vh ||
	    (p->num_planes == 1 && d->sizeimage[0] < d->bpl[0] * d->coded_h * 3 / 2)) {
		vlog("CAPTURE format %ux%u bpl %u size %u too small for %ux%u", d->coded_w,
		     d->coded_h, d->bpl[0], d->sizeimage[0], vw, vh);
		return MLXR_VDEC_E_FATAL;
	}

	struct v4l2_requestbuffers rb = { .count = d->cfg.capture_buffers, .type = CAP_TYPE,
					  .memory = V4L2_MEMORY_MMAP };
	if (XIOCTL(d->fd, VIDIOC_REQBUFS, &rb) < 0)
		return MLXR_VDEC_E_IO;
	if (rb.count == 0 || rb.count > MAX_CAP) {
		vlog("REQBUFS CAPTURE returned %u buffers", rb.count);
		return MLXR_VDEC_E_FATAL;
	}
	d->n_cap = rb.count;
	d->generation++;
	for (uint32_t i = 0; i < d->n_cap; i++) {
		cap_buf *cb = &d->cap[i];
		cb->state = CAP_FREE;
		for (int k = 0; k < MLXR_VDEC_MAX_PLANES; k++)
			cb->fd[k] = -1;
		struct v4l2_plane pl[VIDEO_MAX_PLANES] = { 0 };
		struct v4l2_buffer b = { .type = CAP_TYPE, .memory = V4L2_MEMORY_MMAP, .index = i,
					 .m.planes = pl, .length = VIDEO_MAX_PLANES };
		if (XIOCTL(d->fd, VIDIOC_QUERYBUF, &b) < 0)
			return MLXR_VDEC_E_IO;
		for (uint32_t k = 0; k < d->cap_planes; k++) {
			struct v4l2_exportbuffer e = { .type = CAP_TYPE, .index = i, .plane = k,
						       .flags = O_RDWR | O_CLOEXEC };
			if (XIOCTL(d->fd, VIDIOC_EXPBUF, &e) < 0)
				return MLXR_VDEC_E_IO;
			struct stat sb;
			if (fstat(e.fd, &sb) < 0) {
				vlog("fstat dma-buf failed: %s (errno %d)", strerror(errno), errno);
				close(e.fd);
				return MLXR_VDEC_E_IO;
			}
			cb->fd[k] = e.fd;
			cb->inode[k] = (uint64_t)sb.st_ino;
			cb->length[k] = pl[k].length;
			pthread_mutex_lock(&d->stats_mtx);
			d->st.expbufs++;
			pthread_mutex_unlock(&d->stats_mtx);
		}
		if ((r = queue_capture(d, i)))
			return r;
	}
	pthread_mutex_lock(&d->stats_mtx);
	d->st.capture_allocs++;
	d->st.capture_buffers += d->n_cap;
	pthread_mutex_unlock(&d->stats_mtx);
	if ((r = stream_capture(d, true)))
		return r;
	if (d->cfg.verbose) {
		vlog("CAPTURE gen %u: NV12 %ux%u (visible %ux%u) planes %u bpl %u size %u, %u buffers, "
		     "colorspace %u enc %u quant %u",
		     d->generation, d->coded_w, d->coded_h, d->vis_w, d->vis_h, d->cap_planes, d->bpl[0],
		     d->sizeimage[0], d->n_cap, d->colorspace, d->ycbcr_enc, d->quantization);
	}
	return MLXR_VDEC_OK;
}

// Free all CAPTURE buffers. Every buffer must be back from the app.
static int teardown_capture(mlxr_vdec *d)
{
	if (d->cap_streaming)
		stream_capture(d, false);
	free_capture_fds(d);
	struct v4l2_requestbuffers rb = { .count = 0, .type = CAP_TYPE, .memory = V4L2_MEMORY_MMAP };
	int r = d->n_cap ? XIOCTL(d->fd, VIDIOC_REQBUFS, &rb) : 0;
	d->n_cap = 0;
	return r < 0 ? MLXR_VDEC_E_IO : MLXR_VDEC_OK;
}

// Same size after a SOURCE_CHANGE: restart CAPTURE with the same buffers (the
// Vulkan imports stay valid). Buffers the app holds get queued on release.
static int restart_capture_same(mlxr_vdec *d)
{
	int r = stream_capture(d, false);
	if (r)
		return r;
	for (uint32_t i = 0; i < d->n_cap; i++)
		if (d->cap[i].state == CAP_DRIVER)
			d->cap[i].state = CAP_FREE;
	if ((r = stream_capture(d, true)))
		return r;
	for (uint32_t i = 0; i < d->n_cap; i++)
		if (d->cap[i].state == CAP_FREE && (r = queue_capture(d, i)))
			return r;
	return MLXR_VDEC_OK;
}

// Drain finished (LAST buffer or EPIPE) after a mid-stream SOURCE_CHANGE.
static int on_drained(mlxr_vdec *d)
{
	d->change_pending = false;
	struct v4l2_format f;
	uint32_t vw, vh;
	int r = read_capture_format(d, &f, &vw, &vh);
	if (r)
		return r;
	if (format_matches(d, &f, vw, vh)) {
		if (d->cfg.verbose)
			vlog("SOURCE_CHANGE with unchanged format %ux%u: restarting CAPTURE", vw, vh);
		return restart_capture_same(d);
	}
	if (d->cfg.verbose)
		vlog("SOURCE_CHANGE: %ux%u -> %ux%u (coded %ux%u), owner must apply", d->vis_w,
		     d->vis_h, vw, vh, f.fmt.pix_mp.width, f.fmt.pix_mp.height);
	d->change_ready = true;
	return MLXR_VDEC_FORMAT_CHANGE;
}

int mlxr_vdec_apply_format_change(mlxr_vdec *d)
{
	if (!d)
		return MLXR_VDEC_E_INVALID;
	pthread_mutex_lock(&d->cap_mtx);
	int r = MLXR_VDEC_OK;
	if (!d->change_ready) {
		r = MLXR_VDEC_E_INVALID;
		goto out;
	}
	for (uint32_t i = 0; i < d->n_cap; i++) {
		if (d->cap[i].state == CAP_APP) {
			vlog("apply_format_change: buffer %u is still held by the app", i);
			r = MLXR_VDEC_E_INVALID;
			goto out;
		}
	}
	if ((r = teardown_capture(d)))
		goto out;
	d->change_ready = false;
	r = setup_capture(d);
out:
	pthread_mutex_unlock(&d->cap_mtx);
	return r;
}

// Drain pending events. Returns <0 on error.
static int handle_events(mlxr_vdec *d)
{
	for (;;) {
		struct v4l2_event ev = { 0 };
		int r = XIOCTL_Q(d->fd, VIDIOC_DQEVENT, &ev, ENOENT);
		if (r == -ENOENT)
			return 0;
		if (r < 0)
			return MLXR_VDEC_E_IO;
		if (ev.type == V4L2_EVENT_SOURCE_CHANGE) {
			pthread_mutex_lock(&d->stats_mtx);
			d->st.source_changes++;
			pthread_mutex_unlock(&d->stats_mtx);
			if (d->cfg.verbose)
				vlog("event SOURCE_CHANGE changes 0x%x seq %u (capture %s)",
				     ev.u.src_change.changes, ev.sequence,
				     d->n_cap ? "allocated" : "not allocated");
			if (!d->n_cap) {
				int s = setup_capture(d);
				if (s)
					return s;
			} else {
				d->change_pending = true;
			}
		} else if (ev.type == V4L2_EVENT_EOS) {
			if (d->cfg.verbose)
				vlog("event EOS");
		}
	}
}

static void lookup_pts(mlxr_vdec *d, const struct timeval *tv, mlxr_vframe *f)
{
	uint64_t seq = (uint64_t)tv->tv_sec * 1000000ull + (uint64_t)tv->tv_usec;
	pthread_mutex_lock(&d->pts_mtx);
	pts_entry *e = &d->pts[seq % PTS_RING];
	if (e->seq == seq) {
		f->pts = e->pts;
		f->t_submit_ns = e->t_submit_ns;
	} else {
		f->pts = -1;
		f->t_submit_ns = 0;
	}
	pthread_mutex_unlock(&d->pts_mtx);
}

// Buffer layout part of a frame (no per-picture fields). cap_mtx held.
static void fill_layout(mlxr_vdec *d, uint32_t i, mlxr_vframe *f)
{
	memset(f, 0, sizeof(*f));
	cap_buf *cb = &d->cap[i];
	f->index = i;
	f->generation = d->generation;
	f->width = d->vis_w;
	f->height = d->vis_h;
	f->coded_width = d->coded_w;
	f->coded_height = d->coded_h;
	f->drm_format = DRM_FORMAT_NV12;
	f->drm_modifier = DRM_FORMAT_MOD_LINEAR;
	f->num_planes = 2;
	if (d->cap_planes == 1) {
		// NV12 in one buffer: CbCr follows the Y plane at bpl * coded height.
		f->fd[0] = f->fd[1] = cb->fd[0];
		f->inode[0] = f->inode[1] = cb->inode[0];
		f->offset[0] = 0;
		f->pitch[0] = d->bpl[0];
		f->offset[1] = d->bpl[0] * d->coded_h;
		f->pitch[1] = d->bpl[0];
		f->object_size = cb->length[0];
	} else {
		for (int p = 0; p < 2; p++) {
			f->fd[p] = cb->fd[p];
			f->inode[p] = cb->inode[p];
			f->offset[p] = 0;
			f->pitch[p] = d->bpl[p];
		}
		f->object_size = cb->length[0];
	}
	f->v4l2_colorspace = d->colorspace;
	f->v4l2_ycbcr_enc = d->ycbcr_enc;
	f->v4l2_quantization = d->quantization;
	f->pts = -1;
}

static void fill_frame(mlxr_vdec *d, uint32_t i, const struct v4l2_buffer *b, mlxr_vframe *f)
{
	fill_layout(d, i, f);
	f->sequence = b->sequence;
	f->corrupt = (b->flags & V4L2_BUF_FLAG_ERROR) != 0;
	lookup_pts(d, &b->timestamp, f);
	f->t_dequeue_ns = now_ns();
}

// Try to dequeue one CAPTURE buffer. Called with cap_mtx held.
// Returns OK with *f filled, AGAIN, FORMAT_CHANGE, or an error.
static int try_dequeue(mlxr_vdec *d, mlxr_vframe *f)
{
	if (!d->cap_streaming)
		return MLXR_VDEC_AGAIN;
	struct v4l2_plane pl[VIDEO_MAX_PLANES] = { 0 };
	struct v4l2_buffer b = { .type = CAP_TYPE, .memory = V4L2_MEMORY_MMAP, .m.planes = pl,
				 .length = VIDEO_MAX_PLANES };
	int r = XIOCTL_Q(d->fd, VIDIOC_DQBUF, &b, EAGAIN);
	if (r == -EAGAIN)
		return MLXR_VDEC_AGAIN;
	if (r == -EPIPE) { // the LAST buffer was already dequeued
		int e = handle_events(d);
		return e ? e : on_drained(d);
	}
	if (r < 0)
		return MLXR_VDEC_E_IO;
	if (b.index >= d->n_cap)
		return MLXR_VDEC_E_FATAL;
	d->cap[b.index].state = CAP_FREE;
	bool last = (b.flags & V4L2_BUF_FLAG_LAST) != 0;
	bool empty = pl[0].bytesused == 0;
	if (last) {
		// The SOURCE_CHANGE event may be queued just behind the buffer.
		int e = handle_events(d);
		if (e)
			return e;
	}

	if (empty) {
		if (last)
			return on_drained(d);
		// An empty non-final buffer (e.g. a header-only AU): give it back.
		int s = queue_capture(d, b.index);
		return s ? s : MLXR_VDEC_AGAIN;
	}

	pthread_mutex_lock(&d->stats_mtx);
	d->dequeued_aus++;
	pthread_mutex_unlock(&d->stats_mtx);
	if (b.flags & V4L2_BUF_FLAG_ERROR) {
		pthread_mutex_lock(&d->stats_mtx);
		d->st.frames_error++;
		pthread_mutex_unlock(&d->stats_mtx);
		vlog("CAPTURE buffer %u flagged V4L2_BUF_FLAG_ERROR (sequence %u)", b.index, b.sequence);
		if (last) {
			int s = on_drained(d);
			if (s < 0)
				return s;
		} else {
			int s = queue_capture(d, b.index);
			if (s)
				return s;
		}
		return MLXR_VDEC_E_DECODE;
	}
	fill_frame(d, b.index, &b, f);
	d->cap[b.index].state = CAP_APP;
	pthread_mutex_lock(&d->stats_mtx);
	d->st.frames_out++;
	pthread_mutex_unlock(&d->stats_mtx);
	if (last) {
		// Deliver this picture. on_drained() restarts CAPTURE if the format
		// did not change, else the next receive() reports FORMAT_CHANGE.
		int s = on_drained(d);
		if (s < 0)
			return s;
	}
	return MLXR_VDEC_OK;
}

int mlxr_vdec_receive(mlxr_vdec *d, mlxr_vframe *f, int timeout_ms)
{
	if (!d || !f)
		return MLXR_VDEC_E_INVALID;
	uint64_t deadline = now_ns() + (uint64_t)(timeout_ms > 0 ? timeout_ms : 0) * 1000000ull;
	for (;;) {
		pthread_mutex_lock(&d->cap_mtx);
		int r;
		if (d->change_ready) {
			r = MLXR_VDEC_FORMAT_CHANGE;
		} else {
			r = handle_events(d);
			if (r == 0)
				r = try_dequeue(d, f);
		}
		pthread_mutex_unlock(&d->cap_mtx);
		if (r != MLXR_VDEC_AGAIN)
			return r;

		uint64_t now = now_ns();
		if (now >= deadline)
			return MLXR_VDEC_AGAIN;
		int wait_ms = (int)((deadline - now + 999999) / 1000000);
		struct pollfd p = { .fd = d->fd, .events = POLLIN | POLLPRI };
		int pr = poll(&p, 1, wait_ms);
		if (pr < 0 && errno != EINTR) {
			vlog("poll failed: %s (errno %d)", strerror(errno), errno);
			return MLXR_VDEC_E_IO;
		}
		// vb2's m2m poll reports POLLERR while no CAPTURE is streaming and
		// no OUTPUT is queued; that is not an error here, just back off.
		if (pr > 0 && (p.revents & POLLERR) && !(p.revents & (POLLIN | POLLPRI))) {
			struct timespec nap = { 0, 500000 };
			nanosleep(&nap, NULL);
		}
	}
}

int mlxr_vdec_get_buffers(mlxr_vdec *d, mlxr_vframe *out, uint32_t max, uint32_t *count)
{
	if (!d || !out || !count)
		return MLXR_VDEC_E_INVALID;
	pthread_mutex_lock(&d->cap_mtx);
	uint32_t n = d->change_ready ? 0 : d->n_cap;
	if (n > max)
		n = max;
	for (uint32_t i = 0; i < n; i++)
		fill_layout(d, i, &out[i]);
	*count = n;
	pthread_mutex_unlock(&d->cap_mtx);
	return MLXR_VDEC_OK;
}

int mlxr_vdec_release(mlxr_vdec *d, const mlxr_vframe *f)
{
	if (!d || !f)
		return MLXR_VDEC_E_INVALID;
	pthread_mutex_lock(&d->cap_mtx);
	int r = MLXR_VDEC_OK;
	if (f->generation != d->generation || f->index >= d->n_cap ||
	    d->cap[f->index].state != CAP_APP) {
		vlog("release: buffer %u gen %u is not held by the app", f->index, f->generation);
		pthread_mutex_lock(&d->stats_mtx);
		d->st.requeue_errors++;
		pthread_mutex_unlock(&d->stats_mtx);
		r = MLXR_VDEC_E_INVALID;
		goto out;
	}
	d->cap[f->index].state = CAP_FREE;
	// While draining for a change or stopped, keep it; it is queued on restart.
	if (d->cap_streaming && !d->change_ready && !d->change_pending) {
		r = queue_capture(d, f->index);
		if (r) {
			pthread_mutex_lock(&d->stats_mtx);
			d->st.requeue_errors++;
			pthread_mutex_unlock(&d->stats_mtx);
		}
	}
out:
	pthread_mutex_unlock(&d->cap_mtx);
	return r;
}

void mlxr_vdec_get_stats(mlxr_vdec *d, mlxr_vdec_stats *s)
{
	pthread_mutex_lock(&d->stats_mtx);
	*s = d->st;
	pthread_mutex_unlock(&d->stats_mtx);
}

// ------------------------------------------------------------ open / close

int mlxr_vdec_open(const mlxr_vdec_config *cfg, mlxr_vdec **out)
{
	if (!cfg || !out)
		return MLXR_VDEC_E_INVALID;
	*out = NULL;
	mlxr_vdec *d = calloc(1, sizeof(*d));
	if (!d)
		return MLXR_VDEC_E_FATAL;
	d->cfg = *cfg;
	if (!d->cfg.device)
		d->cfg.device = "/dev/video-dec0";
	if (!d->cfg.capture_buffers)
		d->cfg.capture_buffers = 12;
	if (!d->cfg.output_buffers)
		d->cfg.output_buffers = 6;
	if (!d->cfg.width_hint || !d->cfg.height_hint) {
		d->cfg.width_hint = 1920;
		d->cfg.height_hint = 1080;
	}
	if (d->cfg.capture_buffers > MAX_CAP)
		d->cfg.capture_buffers = MAX_CAP;
	if (d->cfg.output_buffers > MAX_OUT)
		d->cfg.output_buffers = MAX_OUT;
	d->out_fourcc = cfg->codec == MLXR_CODEC_H264 ? V4L2_PIX_FMT_H264 : V4L2_PIX_FMT_HEVC;
	pthread_mutex_init(&d->out_mtx, NULL);
	pthread_mutex_init(&d->cap_mtx, NULL);
	pthread_mutex_init(&d->pts_mtx, NULL);
	pthread_mutex_init(&d->stats_mtx, NULL);
	for (int i = 0; i < PTS_RING; i++)
		d->pts[i].seq = UINT64_MAX;
	for (int i = 0; i < MAX_CAP; i++)
		d->cap[i].fd[0] = d->cap[i].fd[1] = -1;

	d->fd = open(d->cfg.device, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (d->fd < 0) {
		vlog("open %s failed: %s (errno %d)", d->cfg.device, strerror(errno), errno);
		free(d);
		return MLXR_VDEC_E_FATAL;
	}
	int r;
	struct v4l2_capability cap = { 0 };
	if (XIOCTL(d->fd, VIDIOC_QUERYCAP, &cap) < 0) {
		r = MLXR_VDEC_E_IO;
		goto fail;
	}
	uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
	if (!(caps & V4L2_CAP_VIDEO_M2M_MPLANE) || !(caps & V4L2_CAP_STREAMING)) {
		vlog("%s (%s) is not a multiplanar M2M device", d->cfg.device, cap.card);
		r = MLXR_VDEC_E_FATAL;
		goto fail;
	}
	if (d->cfg.verbose)
		vlog("%s: driver %s card %s", d->cfg.device, cap.driver, cap.card);

	if ((r = setup_output(d)))
		goto fail;
	// Subscribe before STREAMON so the first SOURCE_CHANGE cannot be missed.
	struct v4l2_event_subscription sub = { .type = V4L2_EVENT_SOURCE_CHANGE };
	if (XIOCTL(d->fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0) {
		r = MLXR_VDEC_E_IO;
		goto fail;
	}
	struct v4l2_event_subscription sub_eos = { .type = V4L2_EVENT_EOS };
	XIOCTL_Q(d->fd, VIDIOC_SUBSCRIBE_EVENT, &sub_eos, EINVAL); // optional
	int type = OUT_TYPE;
	if (XIOCTL(d->fd, VIDIOC_STREAMON, &type) < 0) {
		r = MLXR_VDEC_E_IO;
		goto fail;
	}
	*out = d;
	return MLXR_VDEC_OK;
fail:
	mlxr_vdec_close(d);
	return r;
}

void mlxr_vdec_close(mlxr_vdec *d)
{
	if (!d)
		return;
	if (d->fd >= 0) {
		int type = OUT_TYPE;
		XIOCTL_Q(d->fd, VIDIOC_STREAMOFF, &type, EINVAL);
		pthread_mutex_lock(&d->cap_mtx);
		teardown_capture(d);
		pthread_mutex_unlock(&d->cap_mtx);
		for (uint32_t i = 0; i < d->n_out; i++)
			if (d->out[i].map)
				munmap(d->out[i].map, d->out[i].length);
		struct v4l2_requestbuffers rb = { .count = 0, .type = OUT_TYPE, .memory = V4L2_MEMORY_MMAP };
		if (d->n_out)
			XIOCTL_Q(d->fd, VIDIOC_REQBUFS, &rb, EINVAL);
		close(d->fd);
	}
	pthread_mutex_destroy(&d->out_mtx);
	pthread_mutex_destroy(&d->cap_mtx);
	pthread_mutex_destroy(&d->pts_mtx);
	pthread_mutex_destroy(&d->stats_mtx);
	free(d);
}
