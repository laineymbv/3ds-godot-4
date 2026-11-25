/**************************************************************************/
/*  mesh_storage_citro3d.h                                                */
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

#include "citro3d_common.h"

#include "servers/rendering/dummy/storage/mesh_storage.h"

namespace Citro3D {

// Interleaved vertex as the PICA200 attribute loader reads it.
struct Vertex {
	float position[3];
	float normal[3];
	float uv[2];
	uint8_t color[4];
};
static_assert(sizeof(Vertex) == 36);

struct Surface {
	RSE::PrimitiveType primitive = RSE::PRIMITIVE_TRIANGLES;
	RID material;
	AABB aabb;
	Vertex *vertices = nullptr; // Linear memory.
	uint16_t *indices = nullptr; // Linear memory, or null for arrays.
	int vertex_count = 0;
	int index_count = 0;
	bool has_color = false;
	RenderingServerTypes::SurfaceData data; // Kept for mesh_get_surface().

	// Skinning input (CPU skinning; the PICA200 lacks uniform space for bones).
	PackedInt32Array bones;
	PackedFloat32Array weights;
	int bones_per_vertex = 0;
	// Blend shapes, decoded on first use: absolute positions/normals per shape.
	bool blend_shapes_decoded = false;
	Vector<PackedVector3Array> shape_positions;
	Vector<PackedVector3Array> shape_normals;
};

struct Mesh {
	Vector<Surface> surfaces;
	AABB aabb;
	AABB custom_aabb;
	int blend_shape_count = 0;
	RSE::BlendShapeMode blend_shape_mode = RSE::BLEND_SHAPE_MODE_NORMALIZED;
	RID shadow_mesh;
	String path;
	Dependency dependency;
};

struct MultiMesh {
	RID mesh;
	int instances = 0;
	RSE::MultimeshTransformFormat xform_format = RSE::MULTIMESH_TRANSFORM_3D;
	bool uses_colors = false;
	bool uses_custom_data = false;
	int stride = 12;
	int visible_instances = -1;
	Vector<float> data;
	AABB custom_aabb;
	AABB aabb;
	bool aabb_dirty = true;
	Dependency dependency;

	int color_offset() const { return xform_format == RSE::MULTIMESH_TRANSFORM_2D ? 8 : 12; }
	int custom_offset() const { return color_offset() + (uses_colors ? 4 : 0); }
	int visible_count() const { return visible_instances < 0 ? instances : MIN(visible_instances, instances); }
	Transform3D get_transform(int p_index) const;
	Transform2D get_transform_2d(int p_index) const;
	Color get_color(int p_index) const;
};

struct Skeleton {
	int size = 0;
	bool use_2d = false;
	Vector<Transform3D> bones;
	Vector<Transform2D> bones_2d;
	Transform2D base_transform_2d;
	uint64_t version = 1;
	Dependency dependency;
};

struct MeshInstance {
	RID mesh;
	RID skeleton;
	Vector<float> blend_weights;
	Vector<Vertex *> vertices; // Per surface; null = draw the mesh's own.
	uint64_t skeleton_version = 0;
	bool dirty = true;
	bool in_dirty_list = false;
};

class MeshStorage : public RendererDummy::MeshStorage {
	static inline MeshStorage *singleton = nullptr;
	mutable RID_Owner<Mesh, true> mesh_owner;
	mutable RID_Owner<MultiMesh, true> multimesh_owner;
	mutable RID_Owner<Skeleton, true> skeleton_owner;
	mutable RID_Owner<MeshInstance> mesh_instance_owner;
	LocalVector<RID> dirty_mesh_instances;

	void _mesh_instance_release(MeshInstance *p_mi);
	void _mesh_instance_update(MeshInstance *p_mi);
	void _decode_blend_shapes(RID p_mesh, int p_surface, Surface &p_s);
	LocalVector<void *> dead_buffers; // Freed once the GPU is done with them.

	void _surface_release(Surface &p_surface);
	void _update_aabb(Mesh *p_mesh);

public:
	static MeshStorage *get_singleton() { return singleton; }

	void frame_started();

	Mesh *get_mesh(RID p_rid) const { return mesh_owner.get_or_null(p_rid); }
	bool owns_mesh(RID p_rid) const { return mesh_owner.owns(p_rid); }

	RID mesh_allocate() override;
	void mesh_initialize(RID p_rid) override;
	void mesh_free(RID p_rid) override;
	void mesh_set_blend_shape_count(RID p_mesh, int p_blend_shape_count) override;
	void mesh_add_surface(RID p_mesh, const RenderingServerTypes::SurfaceData &p_surface) override;
	int mesh_get_blend_shape_count(RID p_mesh) const override;
	void mesh_set_blend_shape_mode(RID p_mesh, RSE::BlendShapeMode p_mode) override;
	RSE::BlendShapeMode mesh_get_blend_shape_mode(RID p_mesh) const override;
	void mesh_surface_update_vertex_region(RID p_mesh, int p_surface, int p_offset, const Vector<uint8_t> &p_data) override;
	void mesh_surface_update_attribute_region(RID p_mesh, int p_surface, int p_offset, const Vector<uint8_t> &p_data) override;
	void mesh_surface_set_material(RID p_mesh, int p_surface, RID p_material) override;
	RID mesh_surface_get_material(RID p_mesh, int p_surface) const override;
	RenderingServerTypes::SurfaceData mesh_get_surface(RID p_mesh, int p_surface) const override;
	int mesh_get_surface_count(RID p_mesh) const override;
	void mesh_set_custom_aabb(RID p_mesh, const AABB &p_aabb) override;
	AABB mesh_get_custom_aabb(RID p_mesh) const override;
	AABB mesh_get_aabb(RID p_mesh, RID p_skeleton = RID()) override;
	void mesh_set_path(RID p_mesh, const String &p_path) override;
	String mesh_get_path(RID p_mesh) const override;
	void mesh_set_shadow_mesh(RID p_mesh, RID p_shadow_mesh) override;
	void mesh_surface_remove(RID p_mesh, int p_surface) override;
	void mesh_clear(RID p_mesh) override;
	bool mesh_needs_instance(RID p_mesh, bool p_has_skeleton) override;

	/* Mesh instances (skinned or blend-shaped meshes) */
	MeshInstance *get_mesh_instance(RID p_rid) const { return mesh_instance_owner.get_or_null(p_rid); }
	RID mesh_instance_create(RID p_base) override;
	void mesh_instance_free(RID p_rid) override;
	void mesh_instance_set_skeleton(RID p_mesh_instance, RID p_skeleton) override;
	void mesh_instance_set_blend_shape_weight(RID p_mesh_instance, int p_shape, float p_weight) override;
	void mesh_instance_check_for_update(RID p_mesh_instance) override;
	void mesh_instance_set_canvas_item_transform(RID p_mesh_instance, const Transform2D &p_transform) override {}
	void update_mesh_instances() override;

	/* MultiMesh */
	MultiMesh *get_multimesh(RID p_rid) const { return multimesh_owner.get_or_null(p_rid); }
	bool owns_multimesh(RID p_rid) const { return multimesh_owner.owns(p_rid); }
	RID _multimesh_allocate() override;
	void _multimesh_initialize(RID p_rid) override;
	void _multimesh_free(RID p_rid) override;
	void _multimesh_allocate_data(RID p_multimesh, int p_instances, RSE::MultimeshTransformFormat p_transform_format, bool p_use_colors = false, bool p_use_custom_data = false, bool p_use_indirect = false) override;
	int _multimesh_get_instance_count(RID p_multimesh) const override;
	void _multimesh_set_mesh(RID p_multimesh, RID p_mesh) override;
	void _multimesh_instance_set_transform(RID p_multimesh, int p_index, const Transform3D &p_transform) override;
	void _multimesh_instance_set_transform_2d(RID p_multimesh, int p_index, const Transform2D &p_transform) override;
	void _multimesh_instance_set_color(RID p_multimesh, int p_index, const Color &p_color) override;
	void _multimesh_instance_set_custom_data(RID p_multimesh, int p_index, const Color &p_color) override;
	void _multimesh_set_custom_aabb(RID p_multimesh, const AABB &p_aabb) override;
	AABB _multimesh_get_custom_aabb(RID p_multimesh) const override;
	RID _multimesh_get_mesh(RID p_multimesh) const override;
	AABB _multimesh_get_aabb(RID p_multimesh) override;
	Transform3D _multimesh_instance_get_transform(RID p_multimesh, int p_index) const override;
	Transform2D _multimesh_instance_get_transform_2d(RID p_multimesh, int p_index) const override;
	Color _multimesh_instance_get_color(RID p_multimesh, int p_index) const override;
	Color _multimesh_instance_get_custom_data(RID p_multimesh, int p_index) const override;
	void _multimesh_set_buffer(RID p_multimesh, const Vector<float> &p_buffer) override;
	Vector<float> _multimesh_get_buffer(RID p_multimesh) const override;
	void _multimesh_set_visible_instances(RID p_multimesh, int p_visible) override;
	int _multimesh_get_visible_instances(RID p_multimesh) const override;

	/* Skeletons */
	Skeleton *get_skeleton(RID p_rid) const { return skeleton_owner.get_or_null(p_rid); }
	bool owns_skeleton(RID p_rid) const { return skeleton_owner.owns(p_rid); }
	RID skeleton_allocate() override;
	void skeleton_initialize(RID p_rid) override;
	void skeleton_free(RID p_rid) override;
	void skeleton_allocate_data(RID p_skeleton, int p_bones, bool p_2d_skeleton = false) override;
	void skeleton_set_base_transform_2d(RID p_skeleton, const Transform2D &p_base_transform) override;
	int skeleton_get_bone_count(RID p_skeleton) const override;
	void skeleton_bone_set_transform(RID p_skeleton, int p_bone, const Transform3D &p_transform) override;
	Transform3D skeleton_bone_get_transform(RID p_skeleton, int p_bone) const override;
	void skeleton_bone_set_transform_2d(RID p_skeleton, int p_bone, const Transform2D &p_transform) override;
	Transform2D skeleton_bone_get_transform_2d(RID p_skeleton, int p_bone) const override;
	void skeleton_update_dependency(RID p_base, DependencyTracker *p_instance) override;

	MeshStorage();
	~MeshStorage();
};

} // namespace Citro3D
