/**************************************************************************/
/*  nintendo_3ds.h                                                        */
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

#include "core/object/object.h"
#include "core/variant/dictionary.h"

class CanvasItem;
class Material;

// Script access to 3DS-specific features. Exists in every build so scripts
// type-check in the editor; off-device it returns neutral values.
class Nintendo3DS : public Object {
	GDCLASS(Nintendo3DS, Object);

	static inline Nintendo3DS *singleton = nullptr;

protected:
	static void _bind_methods();

public:
	static Nintendo3DS *get_singleton() { return singleton; }

	float get_3d_slider() const;
	bool is_new_3ds() const;
	int get_battery_level() const;
	bool is_charging() const;

	// Per-scene override of rendering/3ds/stereo/*; negative restores them.
	void set_stereo_separation(float p_max_separation);
	float get_stereo_separation() const;
	void set_stereo_convergence(float p_convergence);
	float get_stereo_convergence() const;

	// Manual stereo depth for 2D: pixels of disparity at full slider (+ pops
	// out of the screen, - sinks behind it; 0 = on the screen plane). Gives the
	// item a material with a "stereo_depth" parameter; with p_recursive the
	// descendants that have no material of their own use it too. Keep depths
	// small (about -6..+6): large shifts are hard to look at.
	void set_depth(CanvasItem *p_item, float p_depth, bool p_recursive = true);
	float get_depth(CanvasItem *p_item) const;
	// True when p_material is the material set_depth() creates (safe to change).
	static bool is_depth_material(const Ref<Material> &p_material);

	// Writes top_left.png, top_right.png and bottom.png (what the screens
	// show) into p_dir after the current frame.
	void save_screens(const String &p_dir);

	// { active, buffers_played, last_peak } of the DSP audio driver.
	Dictionary get_audio_stats() const;

	Nintendo3DS();
	~Nintendo3DS();
};
