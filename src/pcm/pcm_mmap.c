/*
 *  PCM Interface - mmap
 *  Copyright (c) 2000 by Abramo Bagnara <abramo@alsa-project.org>
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
  
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <sys/poll.h>
#include <sys/uio.h>
#include "pcm_local.h"

int snd_pcm_avail(snd_pcm_t *handle, ssize_t *frames)
{
        assert(handle);
	assert(handle->mmap_status && handle->mmap_control);
	if (handle->stream == SND_PCM_STREAM_PLAYBACK)
		*frames = snd_pcm_mmap_playback_avail(handle);
	else
		*frames = snd_pcm_mmap_capture_avail(handle);
	return 0;
}

static int snd_pcm_mmap_playback_ready(snd_pcm_t *handle)
{
	if (handle->mmap_status->state == SND_PCM_STATE_XRUN)
		return -EPIPE;
	return snd_pcm_mmap_playback_avail(handle) >= handle->setup.avail_min;
}

static int snd_pcm_mmap_capture_ready(snd_pcm_t *handle)
{
	int ret = 0;
	if (handle->mmap_status->state == SND_PCM_STATE_XRUN) {
		ret = -EPIPE;
		if (handle->setup.xrun_mode == SND_PCM_XRUN_DRAIN)
			return -EPIPE;
	}
	if (snd_pcm_mmap_capture_avail(handle) >= handle->setup.avail_min)
		return 1;
	return ret;
}

int snd_pcm_mmap_ready(snd_pcm_t *handle)
{
        assert(handle);
	assert(handle->mmap_status && handle->mmap_control);
	assert(handle->mmap_status->state >= SND_PCM_STATE_PREPARED);
	if (handle->stream == SND_PCM_STREAM_PLAYBACK) {
		return snd_pcm_mmap_playback_ready(handle);
	} else {
		return snd_pcm_mmap_capture_ready(handle);
	}
}

static size_t snd_pcm_mmap_playback_xfer(snd_pcm_t *handle, size_t frames)
{
	snd_pcm_mmap_control_t *control = handle->mmap_control;
	size_t cont;
	size_t avail = snd_pcm_mmap_playback_avail(handle);
	if (avail < frames)
		frames = avail;
	cont = handle->setup.buffer_size - control->appl_ptr % handle->setup.buffer_size;
	if (cont < frames)
		frames = cont;
	return frames;
}

static size_t snd_pcm_mmap_capture_xfer(snd_pcm_t *handle, size_t frames)
{
	snd_pcm_mmap_control_t *control = handle->mmap_control;
	size_t cont;
	size_t avail = snd_pcm_mmap_capture_avail(handle);
	if (avail < frames)
		frames = avail;
	cont = handle->setup.buffer_size - control->appl_ptr % handle->setup.buffer_size;
	if (cont < frames)
		frames = cont;
	return frames;
}

ssize_t snd_pcm_mmap_xfer(snd_pcm_t *handle, size_t frames)
{
        assert(handle);
	assert(handle->mmap_status && handle->mmap_control);
	if (handle->stream == SND_PCM_STREAM_PLAYBACK)
		return snd_pcm_mmap_playback_xfer(handle, frames);
	else
		return snd_pcm_mmap_capture_xfer(handle, frames);
}

ssize_t snd_pcm_mmap_offset(snd_pcm_t *handle)
{
        assert(handle);
	assert(handle->mmap_control);
	return handle->mmap_control->appl_ptr % handle->setup.buffer_size;
}

int snd_pcm_mmap_state(snd_pcm_t *handle)
{
	assert(handle);
	assert(handle->mmap_status);
	return handle->mmap_status->state;
}

ssize_t snd_pcm_mmap_hw_ptr(snd_pcm_t *handle)
{
	assert(handle);
	assert(handle->mmap_status);
	return handle->mmap_status->hw_ptr;
}

ssize_t snd_pcm_mmap_appl_ptr(snd_pcm_t *handle, off_t offset)
{
	ssize_t appl_ptr;
	assert(handle);
	assert(handle->mmap_status && handle->mmap_control);
	assert(offset == 0 || handle->type == SND_PCM_TYPE_HW);
	appl_ptr = handle->mmap_control->appl_ptr;
	if (offset == 0)
		return appl_ptr;
	switch (handle->mmap_status->state) {
	case SND_PCM_STATE_RUNNING:
		if (handle->setup.mode == SND_PCM_MODE_FRAME)
			snd_pcm_hw_ptr(handle, 1);
		break;
	case SND_PCM_STATE_READY:
	case SND_PCM_STATE_NOTREADY:
		return -EBADFD;
	}
	if (offset < 0) {
		if (offset < -(ssize_t)handle->setup.buffer_size)
			offset = -(ssize_t)handle->setup.buffer_size;
		else
			offset -= offset % handle->setup.align;
		appl_ptr += offset;
		if (appl_ptr < 0)
			appl_ptr += handle->setup.boundary;
	} else {
		size_t avail;
		if (handle->stream == SND_PCM_STREAM_PLAYBACK)
			avail = snd_pcm_mmap_playback_avail(handle);
		else
			avail = snd_pcm_mmap_capture_avail(handle);
		if ((size_t)offset > avail)
			offset = avail;
		offset -= offset % handle->setup.align;
		appl_ptr += offset;
		if ((size_t)appl_ptr >= handle->setup.boundary)
			appl_ptr -= handle->setup.boundary;
	}
	handle->mmap_control->appl_ptr = appl_ptr;
	return appl_ptr;
}

ssize_t snd_pcm_mmap_write_areas(snd_pcm_t *handle, snd_pcm_channel_area_t *channels, size_t frames)
{
	snd_pcm_mmap_status_t *status;
	size_t offset = 0;
	size_t result = 0;
	int err;

	assert(handle->mmap_data && handle->mmap_status && handle->mmap_control);
	status = handle->mmap_status;
	assert(status->state >= SND_PCM_STATE_PREPARED);
	if (handle->setup.mode == SND_PCM_MODE_FRAGMENT) {
		assert(frames % handle->setup.frag_size == 0);
	} else {
		if (status->state == SND_PCM_STATE_RUNNING &&
		    handle->mode & SND_PCM_NONBLOCK)
			snd_pcm_hw_ptr(handle, 1);
	}
	while (frames > 0) {
		ssize_t mmap_offset;
		size_t frames1;
		int ready = snd_pcm_mmap_playback_ready(handle);
		if (ready < 0)
			return ready;
		if (!ready) {
			struct pollfd pfd;
			if (status->state != SND_PCM_STATE_RUNNING)
				return result > 0 ? result : -EPIPE;
			if (handle->mode & SND_PCM_NONBLOCK)
				return result > 0 ? result : -EAGAIN;
			pfd.fd = snd_pcm_file_descriptor(handle);
			pfd.events = POLLOUT | POLLERR;
			ready = poll(&pfd, 1, 10000);
			if (ready < 0)
				return result > 0 ? result : ready;
			if (ready && pfd.revents & POLLERR)
				return result > 0 ? result : -EPIPE;
			assert(snd_pcm_mmap_playback_ready(handle));
		}
		frames1 = snd_pcm_mmap_playback_xfer(handle, frames);
		assert(frames1 > 0);
		mmap_offset = snd_pcm_mmap_offset(handle);
		snd_pcm_areas_copy(channels, offset, handle->channels, mmap_offset, handle->setup.format.channels, frames1, handle->setup.format.format);
		if (status->state == SND_PCM_STATE_XRUN)
			return result > 0 ? result : -EPIPE;
		snd_pcm_appl_ptr(handle, frames1);
		frames -= frames1;
		offset += frames1;
		result += frames1;
		if (status->state == SND_PCM_STATE_PREPARED &&
		    (handle->setup.start_mode == SND_PCM_START_DATA ||
		     (handle->setup.start_mode == SND_PCM_START_FULL &&
		      !snd_pcm_mmap_playback_ready(handle)))) {
			err = snd_pcm_go(handle);
			if (err < 0)
				return result > 0 ? result : err;
		}
	}
	return result;
}

ssize_t snd_pcm_mmap_write(snd_pcm_t *handle, const void *buffer, size_t frames)
{
	unsigned int nchannels;
	assert(handle);
	assert(handle->mmap_data && handle->mmap_status && handle->mmap_control);
	assert(frames == 0 || buffer);
	nchannels = handle->setup.format.channels;
	assert(handle->setup.format.interleave || nchannels == 1);
	{
		snd_pcm_channel_area_t channels[nchannels];
		unsigned int channel;
		for (channel = 0; channel < nchannels; ++channel) {
			channels[channel].addr = (char*)buffer;
			channels[channel].first = handle->bits_per_sample * channel;
			channels[channel].step = handle->bits_per_frame;
		}
		return snd_pcm_mmap_write_areas(handle, channels, frames);
	}
}

ssize_t snd_pcm_mmap_writev(snd_pcm_t *handle, const struct iovec *vector, unsigned long vcount)
{
	size_t result = 0;
	unsigned int nchannels;
	assert(handle);
	assert(handle->mmap_data && handle->mmap_status && handle->mmap_control);
	assert(vcount == 0 || vector);
	nchannels = handle->setup.format.channels;
	if (handle->setup.format.interleave) {
		unsigned int b;
		for (b = 0; b < vcount; b++) {
			ssize_t ret;
			size_t frames = vector[b].iov_len;
			ret = snd_pcm_mmap_write(handle, vector[b].iov_base, frames);
			if (ret < 0) {
				if (result <= 0)
					return ret;
				break;
			}
			result += ret;
		}
	} else {
		snd_pcm_channel_area_t channels[nchannels];
		unsigned long bcount;
		unsigned int b;
		assert(vcount % nchannels == 0);
		bcount = vcount / nchannels;
		for (b = 0; b < bcount; b++) {
			unsigned int v;
			ssize_t ret;
			size_t frames = vector[0].iov_len;
			for (v = 0; v < nchannels; ++v) {
				assert(vector[v].iov_len == frames);
				channels[v].addr = vector[v].iov_base;
				channels[v].first = 0;
				channels[v].step = handle->bits_per_sample;
			}
			ret = snd_pcm_mmap_write_areas(handle, channels, frames);
			if (ret < 0) {
				if (result <= 0)
					return ret;
				break;
			}
			result += ret;
			if ((size_t)ret != frames)
				break;
			vector += nchannels;
		}
	}
	return result;
}

ssize_t snd_pcm_mmap_read_areas(snd_pcm_t *handle, snd_pcm_channel_area_t *channels, size_t frames)
{
	snd_pcm_mmap_status_t *status;
	size_t offset = 0;
	size_t result = 0;
	int err;

	assert(handle->mmap_data && handle->mmap_status && handle->mmap_control);
	status = handle->mmap_status;
	assert(status->state >= SND_PCM_STATE_PREPARED);
	if (handle->setup.mode == SND_PCM_MODE_FRAGMENT) {
		assert(frames % handle->setup.frag_size == 0);
	} else {
		if (status->state == SND_PCM_STATE_RUNNING &&
		    handle->mode & SND_PCM_NONBLOCK)
			snd_pcm_hw_ptr(handle, 1);
	}
	if (status->state == SND_PCM_STATE_PREPARED &&
	    handle->setup.start_mode == SND_PCM_START_DATA) {
		err = snd_pcm_go(handle);
		if (err < 0)
			return err;
	}
	while (frames > 0) {
		ssize_t mmap_offset;
		size_t frames1;
		int ready = snd_pcm_mmap_capture_ready(handle);
		if (ready < 0)
			return ready;
		if (!ready) {
			struct pollfd pfd;
			if (status->state != SND_PCM_STATE_RUNNING)
				return result > 0 ? result : -EPIPE;
			if (handle->mode & SND_PCM_NONBLOCK)
				return result > 0 ? result : -EAGAIN;
			pfd.fd = snd_pcm_file_descriptor(handle);
			pfd.events = POLLIN | POLLERR;
			ready = poll(&pfd, 1, 10000);
			if (ready < 0)
				return result > 0 ? result : ready;
			if (ready && pfd.revents & POLLERR)
				return result > 0 ? result : -EPIPE;
			assert(snd_pcm_mmap_capture_ready(handle));
		}
		frames1 = snd_pcm_mmap_capture_xfer(handle, frames);
		assert(frames1 > 0);
		mmap_offset = snd_pcm_mmap_offset(handle);
		snd_pcm_areas_copy(handle->channels, mmap_offset, channels, offset, handle->setup.format.channels, frames1, handle->setup.format.format);
		if (status->state == SND_PCM_STATE_XRUN &&
		    handle->setup.xrun_mode == SND_PCM_XRUN_DRAIN)
			return result > 0 ? result : -EPIPE;
		snd_pcm_appl_ptr(handle, frames1);
		frames -= frames1;
		offset += frames1;
		result += frames1;
	}
	return result;
}

ssize_t snd_pcm_mmap_read(snd_pcm_t *handle, void *buffer, size_t frames)
{
	unsigned int nchannels;
	assert(handle);
	assert(handle->mmap_data && handle->mmap_status && handle->mmap_control);
	assert(frames == 0 || buffer);
	nchannels = handle->setup.format.channels;
	assert(handle->setup.format.interleave || nchannels == 1);
	{
		snd_pcm_channel_area_t channels[nchannels];
		unsigned int channel;
		for (channel = 0; channel < nchannels; ++channel) {
			channels[channel].addr = (char*)buffer;
			channels[channel].first = handle->bits_per_sample * channel;
			channels[channel].step = handle->bits_per_frame;
		}
		return snd_pcm_mmap_read_areas(handle, channels, frames);
	}
}

ssize_t snd_pcm_mmap_readv(snd_pcm_t *handle, const struct iovec *vector, unsigned long vcount)
{
	size_t result = 0;
	unsigned int nchannels;
	assert(handle);
	assert(handle->mmap_data && handle->mmap_status && handle->mmap_control);
	assert(vcount == 0 || vector);
	nchannels = handle->setup.format.channels;
	if (handle->setup.format.interleave) {
		unsigned int b;
		for (b = 0; b < vcount; b++) {
			ssize_t ret;
			size_t frames = vector[b].iov_len;
			ret = snd_pcm_mmap_read(handle, vector[b].iov_base, frames);
			if (ret < 0) {
				if (result <= 0)
					return ret;
				break;
			}
			result += ret;
		}
	} else {
		snd_pcm_channel_area_t channels[nchannels];
		unsigned long bcount;
		unsigned int b;
		assert(vcount % nchannels == 0);
		bcount = vcount / nchannels;
		for (b = 0; b < bcount; b++) {
			unsigned int v;
			ssize_t ret;
			size_t frames = vector[0].iov_len;
			for (v = 0; v < nchannels; ++v) {
				assert(vector[v].iov_len == frames);
				channels[v].addr = vector[v].iov_base;
				channels[v].first = 0;
				channels[v].step = handle->bits_per_sample;
			}
			ret = snd_pcm_mmap_read_areas(handle, channels, frames);
			if (ret < 0) {
				if (result <= 0)
					return ret;
				break;
			}
			result += ret;
			if ((size_t)ret != frames)
				break;
			vector += nchannels;
		}
	}
	return result;
}

int snd_pcm_mmap_status(snd_pcm_t *handle, snd_pcm_mmap_status_t **status)
{
	int err;
	assert(handle);
	assert(handle->valid_setup);
	if (handle->mmap_status) {
		if (status)
			*status = handle->mmap_status;
		return 0;
	}

	if ((err = handle->fast_ops->mmap_status(handle->fast_op_arg, &handle->mmap_status)) < 0)
		return err;
	if (status)
		*status = handle->mmap_status;
	return 0;
}

int snd_pcm_mmap_control(snd_pcm_t *handle, snd_pcm_mmap_control_t **control)
{
	int err;
	assert(handle);
	assert(handle->valid_setup);
	if (handle->mmap_control) {
		if (control)
			*control = handle->mmap_control;
		return 0;
	}

	if ((err = handle->fast_ops->mmap_control(handle->fast_op_arg, &handle->mmap_control)) < 0)
		return err;
	if (control)
		*control = handle->mmap_control;
	return 0;
}

int snd_pcm_mmap_get_areas(snd_pcm_t *handle, snd_pcm_channel_area_t *areas)
{
	snd_pcm_channel_setup_t s;
	snd_pcm_channel_area_t *a, *ap;
	unsigned int channel;
	int interleaved = 1, noninterleaved = 1;
	int err;
	assert(handle);
	assert(handle->mmap_data);
	a = calloc(handle->setup.format.channels, sizeof(*areas));
	for (channel = 0, ap = a; channel < handle->setup.format.channels; ++channel, ++ap) {
		s.channel = channel;
		err = snd_pcm_channel_setup(handle, &s);
		if (err < 0) {
			free(a);
			return err;
		}
		if (areas)
			areas[channel] = s.area;
		*ap = s.area;
		if (ap->step != handle->bits_per_sample || ap->first != 0)
			noninterleaved = 0;
		if (ap->addr != a[0].addr || 
		    ap->step != handle->bits_per_frame || 
		    ap->first != channel * handle->bits_per_sample)
			interleaved = 0;
	}
	if (noninterleaved)
		handle->mmap_type = _NONINTERLEAVED;
	else if (interleaved)
		handle->mmap_type = _INTERLEAVED;
	else
		handle->mmap_type = _COMPLEX;
	handle->channels = a;
	return 0;
}

int snd_pcm_mmap_data(snd_pcm_t *handle, void **data)
{
	int err;
	assert(handle);
	assert(handle->valid_setup);
	if (handle->mmap_data) {
		if (data)
			*data = handle->mmap_data;
		return 0;
	}

	if (handle->setup.mmap_bytes == 0)
		return -ENXIO;
	if ((err = handle->fast_ops->mmap_data(handle->fast_op_arg, (void**)&handle->mmap_data, handle->setup.mmap_bytes)) < 0)
		return err;
	if (data) 
		*data = handle->mmap_data;
	err = snd_pcm_mmap_get_areas(handle, NULL);
	if (err < 0)
		return err;
	return 0;
}

int snd_pcm_mmap(snd_pcm_t *handle, snd_pcm_mmap_status_t **status, snd_pcm_mmap_control_t **control, void **data)
{
	int err;
	err = snd_pcm_mmap_status(handle, status);
	if (err < 0)
		return err;
	err = snd_pcm_mmap_control(handle, control);
	if (err < 0) {
		snd_pcm_munmap_status(handle);
		return err;
	}
	err = snd_pcm_mmap_data(handle, data);
	if (err < 0) {
		snd_pcm_munmap_status(handle);
		snd_pcm_munmap_control(handle);
		return err;
	}
	return 0;
}

int snd_pcm_munmap_status(snd_pcm_t *handle)
{
	int err;
	assert(handle);
	assert(handle->mmap_status);
	if ((err = handle->fast_ops->munmap_status(handle->fast_op_arg, handle->mmap_status)) < 0)
		return err;
	handle->mmap_status = 0;
	return 0;
}

int snd_pcm_munmap_control(snd_pcm_t *handle)
{
	int err;
	assert(handle);
	assert(handle->mmap_control);
	if ((err = handle->fast_ops->munmap_control(handle->fast_op_arg, handle->mmap_control)) < 0)
		return err;
	handle->mmap_control = 0;
	return 0;
}

int snd_pcm_munmap_data(snd_pcm_t *handle)
{
	int err;
	assert(handle);
	assert(handle->mmap_data);
	if ((err = handle->fast_ops->munmap_data(handle->fast_op_arg, handle->mmap_data, handle->setup.mmap_bytes)) < 0)
		return err;
	free(handle->channels);
	handle->channels = 0;
	handle->mmap_data = 0;
	return 0;
}

int snd_pcm_munmap(snd_pcm_t *handle)
{
	int err;
	err = snd_pcm_munmap_status(handle);
	if (err < 0)
		return err;
	err = snd_pcm_munmap_control(handle);
	if (err < 0)
		return err;
	return snd_pcm_munmap_data(handle);
}

