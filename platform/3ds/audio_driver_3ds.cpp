/**************************************************************************/
/*  audio_driver_3ds.cpp                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "audio_driver_3ds.h"

#include "ctru.h"

#include "core/os/memory.h"
#include "core/string/print_string.h"

Error AudioDriver3DS::init() {
	Result res = ndspInit();
	if (R_FAILED(res)) {
		ERR_PRINT(vformat("3DS: ndspInit failed (0x%08X): no audio. On real hardware the DSP firmware must be dumped to sdmc:/3ds/dspfirm.cdc (e.g. with the DSP1 homebrew).", (uint32_t)res));
		return ERR_CANT_OPEN;
	}
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspChnReset(CHANNEL);
	ndspChnSetInterp(CHANNEL, NDSP_INTERP_LINEAR);
	ndspChnSetRate(CHANNEL, MIX_RATE);
	ndspChnSetFormat(CHANNEL, NDSP_FORMAT_STEREO_PCM16);
	float mix[12] = { 1.0f, 1.0f }; // Front left/right at full volume.
	ndspChnSetMix(CHANNEL, mix);

	samples = (int16_t *)linearAlloc(sizeof(int16_t) * 2 * BUFFER_FRAMES * BUFFER_COUNT);
	mix_buffer = (int32_t *)memalloc(sizeof(int32_t) * 2 * BUFFER_FRAMES);
	wave_bufs = memalloc(sizeof(ndspWaveBuf) * BUFFER_COUNT);
	if (!samples || !mix_buffer || !wave_bufs) {
		finish();
		return ERR_OUT_OF_MEMORY;
	}
	memset(samples, 0, sizeof(int16_t) * 2 * BUFFER_FRAMES * BUFFER_COUNT);
	memset(wave_bufs, 0, sizeof(ndspWaveBuf) * BUFFER_COUNT);
	ndspWaveBuf *wb = (ndspWaveBuf *)wave_bufs;
	for (int i = 0; i < BUFFER_COUNT; i++) {
		wb[i].data_vaddr = samples + i * BUFFER_FRAMES * 2;
		wb[i].nsamples = BUFFER_FRAMES;
		wb[i].status = NDSP_WBUF_FREE;
	}
	print_line(vformat("3DS audio: ndsp, %d Hz stereo, %.0f ms buffered.", MIX_RATE, get_latency() * 1000.0f));
	return OK;
}

void AudioDriver3DS::start() {
	if (!samples) {
		return;
	}
	active = true;
	update(); // Queue the first buffers right away.
}

void AudioDriver3DS::_fill(int p_index) {
	ndspWaveBuf *wb = &((ndspWaveBuf *)wave_bufs)[p_index];
	audio_server_process(BUFFER_FRAMES, mix_buffer);
	int16_t *out = samples + p_index * BUFFER_FRAMES * 2;
	int32_t peak = 0;
	for (int i = 0; i < BUFFER_FRAMES * 2; i++) {
		out[i] = (int16_t)(mix_buffer[i] >> 16);
		int32_t a = out[i] < 0 ? -(int32_t)out[i] : (int32_t)out[i];
		peak = MAX(peak, a);
	}
	last_peak = peak;
	DSP_FlushDataCache(out, sizeof(int16_t) * 2 * BUFFER_FRAMES);
	ndspChnWaveBufAdd(CHANNEL, wb);
}

void AudioDriver3DS::update() {
	if (!active) {
		return;
	}
	ndspWaveBuf *wb = (ndspWaveBuf *)wave_bufs;
	for (int i = 0; i < BUFFER_COUNT; i++) {
		if (wb[i].status == NDSP_WBUF_DONE) {
			buffers_played++;
			wb[i].status = NDSP_WBUF_FREE;
		}
		if (wb[i].status == NDSP_WBUF_FREE) {
			_fill(i);
		}
	}
}

void AudioDriver3DS::finish() {
	if (active || samples) {
		ndspChnWaveBufClear(CHANNEL);
		ndspExit();
	}
	active = false;
	if (samples) {
		linearFree(samples);
		samples = nullptr;
	}
	if (mix_buffer) {
		memfree(mix_buffer);
		mix_buffer = nullptr;
	}
	if (wave_bufs) {
		memfree(wave_bufs);
		wave_bufs = nullptr;
	}
}
