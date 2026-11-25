#pragma once

#include "editor/gui/editor_spin_slider.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/inspector/editor_properties.h"

// "Stereo Depth" row for every CanvasItem in the inspector: edits the same
// value as Nintendo3DS.set_depth(), with undo and redo.
class EditorPropertyStereoDepth : public EditorProperty {
	GDCLASS(EditorPropertyStereoDepth, EditorProperty);

	EditorSpinSlider *slider = nullptr;
	ObjectID item_id;

	void _value_changed(double p_value);

public:
	void setup(CanvasItem *p_item);
	virtual void update_property() override;

	EditorPropertyStereoDepth();
};

class EditorInspectorPluginStereoDepth : public EditorInspectorPlugin {
	GDCLASS(EditorInspectorPluginStereoDepth, EditorInspectorPlugin);

public:
	virtual bool can_handle(Object *p_object) override;
	virtual void parse_begin(Object *p_object) override;
};
