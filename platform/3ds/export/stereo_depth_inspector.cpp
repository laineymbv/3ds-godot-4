#include "stereo_depth_inspector.h"

#include "core/object/callable_mp.h"
#include "core/object/undo_redo.h"
#include "scene/scene_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "platform/3ds/api/nintendo_3ds.h"

static Nintendo3DS *_n3ds() {
	return Nintendo3DS::get_singleton();
}

EditorPropertyStereoDepth::EditorPropertyStereoDepth() {
	set_label(TTR("Stereo Depth"));
	set_tooltip_text(TTR("How far the item sits in front of (+) or behind (-) the screen, in pixels of disparity at full 3D slider. Applies on the 3DS only."));

	slider = memnew(EditorSpinSlider);
	slider->set_min(-20.0);
	slider->set_max(20.0);
	slider->set_step(0.1);
	slider->set_suffix("px");
	slider->connect(SceneStringName(value_changed), callable_mp(this, &EditorPropertyStereoDepth::_value_changed));
	add_child(slider);
	add_focusable(slider);
}

void EditorPropertyStereoDepth::setup(CanvasItem *p_item) {
	item_id = p_item->get_instance_id();
	update_property();
}

void EditorPropertyStereoDepth::update_property() {
	CanvasItem *item = Object::cast_to<CanvasItem>(ObjectDB::get_instance(item_id));
	Nintendo3DS *n3ds = _n3ds();
	if (!item || !n3ds) {
		return;
	}
	const bool inherits = item->get_material().is_null() && item->get_use_parent_material();
	const bool own_material = item->get_material().is_valid() && !Nintendo3DS::is_depth_material(item->get_material());
	slider->set_read_only(inherits || own_material);
	if (inherits) {
		set_tooltip_text(TTR("This item uses its parent's material, so its depth comes from the parent."));
	} else if (own_material) {
		set_tooltip_text(TTR("This item has its own material. Use a stereo_depth parameter on that material instead."));
	}
	slider->set_value_no_signal(n3ds->get_depth(item));
}

void EditorPropertyStereoDepth::_value_changed(double p_value) {
	CanvasItem *item = Object::cast_to<CanvasItem>(ObjectDB::get_instance(item_id));
	Nintendo3DS *n3ds = _n3ds();
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	if (!item || !n3ds || !ur) {
		return;
	}
	const float old_depth = n3ds->get_depth(item);
	const Ref<Material> old_material = item->get_material();
	// Dragging the slider merges into one undo step.
	ur->create_action(TTR("Set Stereo Depth"), UndoRedo::MERGE_ENDS);
	ur->add_do_method(n3ds, "set_depth", item, (float)p_value, false);
	ur->add_undo_property(item, "material", old_material);
	if (Nintendo3DS::is_depth_material(old_material)) {
		ur->add_undo_method(n3ds, "set_depth", item, old_depth, false);
	}
	ur->commit_action();
}

bool EditorInspectorPluginStereoDepth::can_handle(Object *p_object) {
	return Object::cast_to<CanvasItem>(p_object) != nullptr;
}

void EditorInspectorPluginStereoDepth::parse_begin(Object *p_object) {
	CanvasItem *item = Object::cast_to<CanvasItem>(p_object);
	if (!item) {
		return;
	}
	EditorPropertyStereoDepth *prop = memnew(EditorPropertyStereoDepth);
	prop->setup(item);
	add_custom_control(prop);
}
