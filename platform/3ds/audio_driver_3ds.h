/**************************************************************************/
/*  audio_driver_3ds.h                                                    */
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

#pragma once

#include "servers/audio/audio_server.h"

#include <cstdint>

// Audio through the 3DS DSP (libctru ndsp). The build has no threads, so the
// main loop calls update() every frame to mix into whichever wave buffers the
// DSP has finished playing.
class AudioDriver3DS : public AudioDriver {
	static constexpr int MIX_RATE = 32000;
	static constexpr int BUFFER_FRAMES = 512; // 16 ms at 32 kHz.
	static constexpr int BUFFER_COUNT = 6; // ~96 ms of queued audio.
	static constexpr int CHANNEL = 0;

	bool active = false;
	int16_t *samples = nullptr; // Linear memory: BUFFER_COUNT x BUFFER_FRAMES x 2.
	int32_t *mix_buffer = nullptr;
	void *wave_bufs = nullptr; // ndspWaveBuf[BUFFER_COUNT]
	uint64_t buffers_played = 0;
	int32_t last_peak = 0;

	void _fill(int p_index);

public:
	const char *get_name() const override { return "3DS"; }
	Error init() override;
	void start() override;
	int get_mix_rate() const override { return MIX_RATE; }
	SpeakerMode get_speaker_mode() const override { return SPEAKER_MODE_STEREO; }
	float get_latency() override { return (float)(BUFFER_FRAMES * BUFFER_COUNT) / MIX_RATE; }
	void lock() override {}
	void unlock() override {}
	void finish() override;

	void update();
	bool is_active() const { return active; }
	uint64_t get_buffers_played() const { return buffers_played; }
	int32_t get_last_peak() const { return last_peak; }
};
