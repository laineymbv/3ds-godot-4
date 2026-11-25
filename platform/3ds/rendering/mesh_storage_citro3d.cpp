/**************************************************************************/
/*  mesh_storage_citro3d.cpp                                              */
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

#include "mesh_storage_citro3d.h"

#include "servers/rendering/rendering_server.h"

using namespace Citro3D;

MeshStorage::MeshStorage() {
	singleton = this;
}

MeshStorage::~MeshStorage() {
	singleton = nullptr;
}

RID MeshStorage::mesh_allocate() {
	return mesh_owner.allocate_rid();
}

void MeshStorage::mesh_initialize(RID p_rid) {
	mesh_owner.initialize_rid(p_rid, Mesh());
}

void MeshStorage::_surface_release(Surface &p_surface) {
	if (p_surface.vertices) {
		dead_buffers.push_back(p_surface.vertices);
		p_surface.vertices = nullptr;
	}
	if (p_surface.indices) {
		dead_buffers.push_back(p_surface.indices);
		p_surface.indices = nullptr;
	}
}

void MeshStorage::frame_started() {
	for (void *p : dead_buffers) {
		linearFree(p);
	}
	dead_buffers.clear();
}

void MeshStorage::mesh_free(RID p_rid) {
	Mesh *mesh = mesh_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(mesh);
	for (Surface &s : mesh->surfaces) {
		_surface_release(s);
	}
	mesh->dependency.deleted_notify(p_rid);
	mesh_owner.free(p_rid);
}

void MeshStorage::mesh_set_blend_shape_count(RID p_mesh, int p_blend_shape_count) {
	Mesh *m = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(m);
	m->blend_shape_count = p_blend_shape_count;
}

void MeshStorage::_update_aabb(Mesh *p_mesh) {
	AABB aabb;
	for (int i = 0; i < p_mesh->surfaces.size(); i++) {
		if (i == 0) {
			aabb = p_mesh->surfaces[i].aabb;
		} else {
			aabb.merge_with(p_mesh->surfaces[i].aabb);
		}
	}
	p_mesh->aabb = aabb;
}

void MeshStorage::mesh_add_surface(RID p_mesh, const RenderingServerTypes::SurfaceData &p_surface) {
	Mesh *m = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(m);

	Surface s;
	s.primitive = p_surface.primitive;
	s.material = p_surface.material;
	s.aabb = p_surface.aabb;
	s.data = p_surface;

	// Decode Godot's packed vertex streams with the server's own helper, then
	// interleave them the way the PICA200 attribute loader reads them.
	Array arrays = RenderingServer::get_singleton()->mesh_create_arrays_from_surface_data(p_surface);
	PackedVector3Array positions;
	if (arrays[RSE::ARRAY_VERTEX].get_type() == Variant::PACKED_VECTOR2_ARRAY) {
		// 2D meshes (MeshInstance2D, CPUParticles2D): z = 0.
		PackedVector2Array p2 = arrays[RSE::ARRAY_VERTEX];
		positions.resize(p2.size());
		for (int i = 0; i < p2.size(); i++) {
			positions.write[i] = Vector3(p2[i].x, p2[i].y, 0);
		}
	} else {
		positions = arrays[RSE::ARRAY_VERTEX];
	}
	PackedVector3Array normals = arrays[RSE::ARRAY_NORMAL];
	PackedVector2Array uvs = arrays[RSE::ARRAY_TEX_UV];
	PackedColorArray colors = arrays[RSE::ARRAY_COLOR];
	PackedInt32Array indices = arrays[RSE::ARRAY_INDEX];
	if (arrays[RSE::ARRAY_BONES].get_type() != Variant::NIL && arrays[RSE::ARRAY_WEIGHTS].get_type() != Variant::NIL) {
		s.bones = arrays[RSE::ARRAY_BONES];
		s.weights = arrays[RSE::ARRAY_WEIGHTS];
		if (positions.size() > 0 && s.bones.size() == s.weights.size() && s.bones.size() % positions.size() == 0) {
			s.bones_per_vertex = s.bones.size() / positions.size();
		}
	}

	int vc = positions.size();
	if (vc > 0) {
		s.vertices = (Vertex *)linearAlloc(sizeof(Vertex) * vc);
		ERR_FAIL_NULL_MSG(s.vertices, "3DS: out of linear memory for mesh vertices.");
		s.vertex_count = vc;
		s.has_color = colors.size() == vc;
		bool has_n = normals.size() == vc;
		bool has_uv = uvs.size() == vc;
		for (int i = 0; i < vc; i++) {
			Vertex &v = s.vertices[i];
			const Vector3 &p = positions[i];
			v.position[0] = p.x;
			v.position[1] = p.y;
			v.position[2] = p.z;
			Vector3 n = has_n ? normals[i] : Vector3(0, 1, 0);
			v.normal[0] = n.x;
			v.normal[1] = n.y;
			v.normal[2] = n.z;
			Vector2 uv = has_uv ? uvs[i] : Vector2();
			v.uv[0] = uv.x;
			v.uv[1] = uv.y;
			Color c = s.has_color ? colors[i] : Color(1, 1, 1, 1);
			v.color[0] = (uint8_t)CLAMP(c.r * 255.0f, 0.0f, 255.0f);
			v.color[1] = (uint8_t)CLAMP(c.g * 255.0f, 0.0f, 255.0f);
			v.color[2] = (uint8_t)CLAMP(c.b * 255.0f, 0.0f, 255.0f);
			v.color[3] = (uint8_t)CLAMP(c.a * 255.0f, 0.0f, 255.0f);
		}
		GSPGPU_FlushDataCache(s.vertices, sizeof(Vertex) * vc);
	}

	if (indices.size() > 0) {
		if (vc > 65536) {
			ERR_PRINT("3DS: meshes are limited to 65536 vertices per surface; surface skipped.");
			_surface_release(s);
			return;
		}
		s.index_count = indices.size();
		s.indices = (uint16_t *)linearAlloc(sizeof(uint16_t) * s.index_count);
		ERR_FAIL_NULL_MSG(s.indices, "3DS: out of linear memory for mesh indices.");
		for (int i = 0; i < s.index_count; i++) {
			s.indices[i] = (uint16_t)indices[i];
		}
		GSPGPU_FlushDataCache(s.indices, sizeof(uint16_t) * s.index_count);
	}

	m->surfaces.push_back(s);
	_update_aabb(m);
	m->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MESH);
}

int MeshStorage::mesh_get_blend_shape_count(RID p_mesh) const {
	Mesh *m = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL_V(m, 0);
	return m->blend_shape_count;
}

void MeshStorage::mesh_set_blend_shape_mode(RID p_mesh, RSE::BlendShapeMode p_mode) {
	Mesh *m = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(m);
	m->blend_shape_mode = p_mode;
}

RSE::BlendShapeMode MeshStorage::mesh_get_blend_shape_mode(RID p_mesh) const {
	Mesh *m = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL_V(m, RSE::BLEND_SHAPE_MODE_NORMALIZED);
	return m->blend_shape_mode;
}

void MeshStorage::mesh_surface_update_vertex_region(RID p_mesh, int p_surface, int p_offset, const Vector<uint8_t> &p_data) {
	// Re-decode the whole surface: simple, and updates are rare at runtime.
	Mesh *m = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(m);
	ERR_FAIL_INDEX(p_surface, m->surfaces.size());
	RenderingServerTypes::SurfaceData sd = m->surfaces[p_surface].data;
	ERR_FAIL_COND(p_offset + p_data.size() > sd.vertex_data.size());
	memcpy(sd.vertex_data.ptrw() + p_offset, p_data.ptr(), p_data.size());
	sd.material = m->surfaces[p_surface].material;
	_surface_release(m->surfaces.write[p_surface]);
	m->surfaces.remove_at(p_surface);
	mesh_add_surface(p_mesh, sd);
	// mesh_add_surface appends; move it back into place.
	Surface s = m->surfaces[m->surfaces.size() - 1];
	m->surfaces.remove_at(m->surfaces.size() - 1);
	m->surfaces.insert(p_surface, s);
}

void MeshStorage::mesh_surface_update_attribute_region(RID p_mesh, int p_surface, int p_offset, const Vector<uint8_t> &p_data) {
	Mesh *m = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(m);
	ERR_FAIL_INDEX(p_surface, m->surfaces.size());
	RenderingServerTypes::SurfaceData sd = m->surfaces[p_surface].data;
	ERR_FAIL_COND(p_offset + p_data.size() > sd.attribute_data.size());
	memcpy(sd.attribute_data.ptrw() + p_offset, p_data.ptr(), p_data.size());
	sd.material = m->surfaces[p_surface].material;
	_surface_release(m->surfaces.write[p_surface]);
	m->surfaces.remove_at(p_surface);
	mesh_add_surface(p_mesh, sd);
	Surface s = m->surfaces[m->surfaces.size() - 1];
	m->surfaces.remove_at(m->surfaces.size() - 1);
	m->surfaces.insert(p_surface, s);
}

void MeshStorage::mesh_surface_set_material(RID p_mesh, int p_surface, RID p_material) {
	Mesh *m = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(m);
	ERR_FAIL_INDEX(p_surface, m->surfaces.size());
	m->surfaces.write[p_surface].material = p_material;
	m->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MATERIAL);
}

RID MeshStorage::mesh_surface_get_material(RID p_mesh, int p_surface) const {
	Mesh *m = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL_V(m, RID());
	ERR_FAIL_INDEX_V(p_surface, m->surfaces.size(), RID());
	return m->surfaces[p_surface].material;
}

RenderingServerTypes::SurfaceData MeshStorage::mesh_get_surface(RID p_mesh, int p_surface) const {
	Mesh *m = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL_V(m, RenderingServerTypes::SurfaceData());
	ERR_FAIL_INDEX_V(p_surface, m->surfaces.size(), RenderingServerTypes::SurfaceData());
	RenderingServerTypes::SurfaceData sd = m->surfaces[p_surface].data;
	sd.material = m->surfaces[p_surface].material;
	return sd;
}

int MeshStorage::mesh_get_surface_count(RID p_mesh) const {
	Mesh *m = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL_V(m, 0);
	return m->surfaces.size();
}

void MeshStorage::mesh_set_custom_aabb(RID p_mesh, const AABB &p_aabb) {
	Mesh *m = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(m);
	m->custom_aabb = p_aabb;
	m->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_AABB);
}

AABB MeshStorage::mesh_get_custom_aabb(RID p_mesh) const {
	Mesh *m = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL_V(m, AABB());
	return m->custom_aabb;
}

AABB MeshStorage::mesh_get_aabb(RID p_mesh, RID p_skeleton) {
	Mesh *m = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL_V(m, AABB());
	return m->custom_aabb != AABB() ? m->custom_aabb : m->aabb;
}

void MeshStorage::mesh_set_path(RID p_mesh, const String &p_path) {
	Mesh *m = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(m);
	m->path = p_path;
}

String MeshStorage::mesh_get_path(RID p_mesh) const {
	Mesh *m = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL_V(m, String());
	return m->path;
}

void MeshStorage::mesh_set_shadow_mesh(RID p_mesh, RID p_shadow_mesh) {
	Mesh *m = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(m);
	m->shadow_mesh = p_shadow_mesh;
}

void MeshStorage::mesh_surface_remove(RID p_mesh, int p_surface) {
	Mesh *m = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(m);
	ERR_FAIL_INDEX(p_surface, m->surfaces.size());
	_surface_release(m->surfaces.write[p_surface]);
	m->surfaces.remove_at(p_surface);
	_update_aabb(m);
	m->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MESH);
}

void MeshStorage::mesh_clear(RID p_mesh) {
	Mesh *m = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(m);
	for (Surface &s : m->surfaces) {
		_surface_release(s);
	}
	m->surfaces.clear();
	m->aabb = AABB();
	m->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MESH);
}

/* Mesh instances: CPU skinning and blend shapes */

bool MeshStorage::mesh_needs_instance(RID p_mesh, bool p_has_skeleton) {
	Mesh *m = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL_V(m, false);
	return p_has_skeleton || m->blend_shape_count > 0;
}

RID MeshStorage::mesh_instance_create(RID p_base) {
	Mesh *m = mesh_owner.get_or_null(p_base);
	ERR_FAIL_NULL_V(m, RID());
	MeshInstance mi;
	mi.mesh = p_base;
	mi.blend_weights.resize(m->blend_shape_count);
	mi.blend_weights.fill(0.0f);
	return mesh_instance_owner.make_rid(mi);
}

void MeshStorage::_mesh_instance_release(MeshInstance *p_mi) {
	for (Vertex *v : p_mi->vertices) {
		if (v) {
			dead_buffers.push_back(v);
		}
	}
	p_mi->vertices.clear();
}

void MeshStorage::mesh_instance_free(RID p_rid) {
	MeshInstance *mi = mesh_instance_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(mi);
	_mesh_instance_release(mi);
	if (mi->in_dirty_list) {
		dirty_mesh_instances.erase(p_rid);
	}
	mesh_instance_owner.free(p_rid);
}

void MeshStorage::mesh_instance_set_skeleton(RID p_mesh_instance, RID p_skeleton) {
	MeshInstance *mi = mesh_instance_owner.get_or_null(p_mesh_instance);
	ERR_FAIL_NULL(mi);
	mi->skeleton = p_skeleton;
	mi->skeleton_version = 0;
	mi->dirty = true;
}

void MeshStorage::mesh_instance_set_blend_shape_weight(RID p_mesh_instance, int p_shape, float p_weight) {
	MeshInstance *mi = mesh_instance_owner.get_or_null(p_mesh_instance);
	ERR_FAIL_NULL(mi);
	ERR_FAIL_INDEX(p_shape, mi->blend_weights.size());
	if (mi->blend_weights[p_shape] != p_weight) {
		mi->blend_weights.write[p_shape] = p_weight;
		mi->dirty = true;
	}
}

void MeshStorage::mesh_instance_check_for_update(RID p_mesh_instance) {
	MeshInstance *mi = mesh_instance_owner.get_or_null(p_mesh_instance);
	if (!mi) {
		return;
	}
	Skeleton *sk = skeleton_owner.get_or_null(mi->skeleton);
	if (sk && sk->version != mi->skeleton_version) {
		mi->dirty = true;
	}
	if (mi->dirty && !mi->in_dirty_list) {
		mi->in_dirty_list = true;
		dirty_mesh_instances.push_back(p_mesh_instance);
	}
}

void MeshStorage::update_mesh_instances() {
	for (const RID &rid : dirty_mesh_instances) {
		MeshInstance *mi = mesh_instance_owner.get_or_null(rid);
		if (mi) {
			mi->in_dirty_list = false;
			_mesh_instance_update(mi);
		}
	}
	dirty_mesh_instances.clear();
}

void MeshStorage::_decode_blend_shapes(RID p_mesh, int p_surface, Surface &p_s) {
	p_s.blend_shapes_decoded = true;
	TypedArray<Array> shapes = RenderingServer::get_singleton()->mesh_surface_get_blend_shape_arrays(p_mesh, p_surface);
	for (int i = 0; i < shapes.size(); i++) {
		Array a = shapes[i];
		PackedVector3Array pos;
		PackedVector3Array nrm;
		if (a.size() > RSE::ARRAY_NORMAL) {
			if (a[RSE::ARRAY_VERTEX].get_type() == Variant::PACKED_VECTOR3_ARRAY) {
				pos = a[RSE::ARRAY_VERTEX];
			}
			if (a[RSE::ARRAY_NORMAL].get_type() == Variant::PACKED_VECTOR3_ARRAY) {
				nrm = a[RSE::ARRAY_NORMAL];
			}
		}
		p_s.shape_positions.push_back(pos);
		p_s.shape_normals.push_back(nrm);
	}
}

void MeshStorage::_mesh_instance_update(MeshInstance *p_mi) {
	p_mi->dirty = false;
	Mesh *m = mesh_owner.get_or_null(p_mi->mesh);
	if (!m) {
		return;
	}
	Skeleton *sk = skeleton_owner.get_or_null(p_mi->skeleton);
	if (sk) {
		p_mi->skeleton_version = sk->version;
	}
	if (p_mi->vertices.size() != m->surfaces.size()) {
		_mesh_instance_release(p_mi);
		p_mi->vertices.resize(m->surfaces.size());
		p_mi->vertices.fill(nullptr);
	}

	bool any_weight = false;
	for (float w : p_mi->blend_weights) {
		any_weight = any_weight || w != 0.0f;
	}
	bool normalized = m->blend_shape_mode == RSE::BLEND_SHAPE_MODE_NORMALIZED;

	for (int si = 0; si < m->surfaces.size(); si++) {
		Surface &s = m->surfaces.write[si];
		bool skin = sk && s.bones_per_vertex > 0 && sk->size > 0 && !sk->use_2d;
		bool shapes = any_weight && m->blend_shape_count > 0;
		if (!s.vertices || (!skin && !shapes)) {
			continue;
		}
		if (shapes && !s.blend_shapes_decoded) {
			_decode_blend_shapes(p_mi->mesh, si, s);
		}
		Vertex *&out = p_mi->vertices.write[si];
		if (!out) {
			out = (Vertex *)linearAlloc(sizeof(Vertex) * s.vertex_count);
			if (!out) {
				ERR_PRINT_ONCE("3DS: out of linear memory for a skinned mesh.");
				continue;
			}
		}
		const Transform3D *bones = sk ? sk->bones.ptr() : nullptr;
		for (int v = 0; v < s.vertex_count; v++) {
			const Vertex &src = s.vertices[v];
			Vector3 p(src.position[0], src.position[1], src.position[2]);
			Vector3 n(src.normal[0], src.normal[1], src.normal[2]);

			if (shapes) {
				Vector3 dp, dn;
				for (int b = 0; b < s.shape_positions.size() && b < p_mi->blend_weights.size(); b++) {
					float w = p_mi->blend_weights[b];
					if (w == 0.0f) {
						continue;
					}
					const PackedVector3Array &sp = s.shape_positions[b];
					const PackedVector3Array &sn = s.shape_normals[b];
					if (v < sp.size()) {
						dp += (normalized ? sp[v] - p : sp[v]) * w;
					}
					if (v < sn.size()) {
						dn += (normalized ? sn[v] - n : sn[v]) * w;
					}
				}
				p += dp;
				n += dn;
			}

			if (skin) {
				Vector3 sp, sn;
				float total = 0.0f;
				for (int k = 0; k < s.bones_per_vertex; k++) {
					int idx = v * s.bones_per_vertex + k;
					float w = s.weights[idx];
					int bone = s.bones[idx];
					if (w == 0.0f || bone < 0 || bone >= sk->size) {
						continue;
					}
					const Transform3D &bx = bones[bone];
					sp += bx.xform(p) * w;
					sn += bx.basis.xform(n) * w;
					total += w;
				}
				if (total > 0.0f) {
					p = sp / total;
					n = sn;
				}
			}

			Vertex &d = out[v];
			d = src;
			d.position[0] = p.x;
			d.position[1] = p.y;
			d.position[2] = p.z;
			n.normalize();
			d.normal[0] = n.x;
			d.normal[1] = n.y;
			d.normal[2] = n.z;
		}
		GSPGPU_FlushDataCache(out, sizeof(Vertex) * s.vertex_count);
	}
}

/* MultiMesh */

Transform3D MultiMesh::get_transform(int p_index) const {
	const float *d = data.ptr() + p_index * stride;
	if (xform_format == RSE::MULTIMESH_TRANSFORM_2D) {
		return Transform3D(Basis(d[0], d[1], 0, d[4], d[5], 0, 0, 0, 1), Vector3(d[3], d[7], 0));
	}
	return Transform3D(Basis(d[0], d[1], d[2], d[4], d[5], d[6], d[8], d[9], d[10]), Vector3(d[3], d[7], d[11]));
}

Transform2D MultiMesh::get_transform_2d(int p_index) const {
	const float *d = data.ptr() + p_index * stride;
	return Transform2D(d[0], d[4], d[1], d[5], d[3], d[7]);
}

Color MultiMesh::get_color(int p_index) const {
	if (!uses_colors) {
		return Color(1, 1, 1, 1);
	}
	const float *d = data.ptr() + p_index * stride + color_offset();
	return Color(d[0], d[1], d[2], d[3]);
}

RID MeshStorage::_multimesh_allocate() {
	return multimesh_owner.allocate_rid();
}

void MeshStorage::_multimesh_initialize(RID p_rid) {
	multimesh_owner.initialize_rid(p_rid, MultiMesh());
}

void MeshStorage::_multimesh_free(RID p_rid) {
	MultiMesh *mm = multimesh_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(mm);
	mm->dependency.deleted_notify(p_rid);
	multimesh_owner.free(p_rid);
}

void MeshStorage::_multimesh_allocate_data(RID p_multimesh, int p_instances, RSE::MultimeshTransformFormat p_transform_format, bool p_use_colors, bool p_use_custom_data, bool p_use_indirect) {
	MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL(mm);
	mm->instances = MAX(p_instances, 0);
	mm->xform_format = p_transform_format;
	mm->uses_colors = p_use_colors;
	mm->uses_custom_data = p_use_custom_data;
	mm->stride = (p_transform_format == RSE::MULTIMESH_TRANSFORM_2D ? 8 : 12) + (p_use_colors ? 4 : 0) + (p_use_custom_data ? 4 : 0);
	mm->data.resize(mm->instances * mm->stride);
	float *w = mm->data.ptrw();
	for (int i = 0; i < mm->instances; i++) {
		float *d = w + i * mm->stride;
		memset(d, 0, sizeof(float) * mm->stride);
		d[0] = 1; // Identity.
		d[5] = 1;
		if (p_transform_format == RSE::MULTIMESH_TRANSFORM_3D) {
			d[10] = 1;
		}
		if (p_use_colors) {
			float *c = d + mm->color_offset();
			c[0] = c[1] = c[2] = c[3] = 1;
		}
	}
	mm->aabb_dirty = true;
	mm->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MULTIMESH);
}

int MeshStorage::_multimesh_get_instance_count(RID p_multimesh) const {
	MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL_V(mm, 0);
	return mm->instances;
}

void MeshStorage::_multimesh_set_mesh(RID p_multimesh, RID p_mesh) {
	MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL(mm);
	mm->mesh = p_mesh;
	mm->aabb_dirty = true;
	mm->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MESH);
}

void MeshStorage::_multimesh_instance_set_transform(RID p_multimesh, int p_index, const Transform3D &p_transform) {
	MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL(mm);
	ERR_FAIL_INDEX(p_index, mm->instances);
	ERR_FAIL_COND(mm->xform_format != RSE::MULTIMESH_TRANSFORM_3D);
	float *d = mm->data.ptrw() + p_index * mm->stride;
	for (int r = 0; r < 3; r++) {
		d[r * 4 + 0] = p_transform.basis.rows[r][0];
		d[r * 4 + 1] = p_transform.basis.rows[r][1];
		d[r * 4 + 2] = p_transform.basis.rows[r][2];
		d[r * 4 + 3] = p_transform.origin[r];
	}
	mm->aabb_dirty = true;
	mm->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_AABB);
}

void MeshStorage::_multimesh_instance_set_transform_2d(RID p_multimesh, int p_index, const Transform2D &p_transform) {
	MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL(mm);
	ERR_FAIL_INDEX(p_index, mm->instances);
	ERR_FAIL_COND(mm->xform_format != RSE::MULTIMESH_TRANSFORM_2D);
	float *d = mm->data.ptrw() + p_index * mm->stride;
	d[0] = p_transform.columns[0][0];
	d[1] = p_transform.columns[1][0];
	d[2] = 0;
	d[3] = p_transform.columns[2][0];
	d[4] = p_transform.columns[0][1];
	d[5] = p_transform.columns[1][1];
	d[6] = 0;
	d[7] = p_transform.columns[2][1];
	mm->aabb_dirty = true;
}

void MeshStorage::_multimesh_instance_set_color(RID p_multimesh, int p_index, const Color &p_color) {
	MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL(mm);
	ERR_FAIL_INDEX(p_index, mm->instances);
	ERR_FAIL_COND(!mm->uses_colors);
	float *d = mm->data.ptrw() + p_index * mm->stride + mm->color_offset();
	d[0] = p_color.r;
	d[1] = p_color.g;
	d[2] = p_color.b;
	d[3] = p_color.a;
}

void MeshStorage::_multimesh_instance_set_custom_data(RID p_multimesh, int p_index, const Color &p_color) {
	MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL(mm);
	ERR_FAIL_INDEX(p_index, mm->instances);
	ERR_FAIL_COND(!mm->uses_custom_data);
	float *d = mm->data.ptrw() + p_index * mm->stride + mm->custom_offset();
	d[0] = p_color.r;
	d[1] = p_color.g;
	d[2] = p_color.b;
	d[3] = p_color.a;
}

void MeshStorage::_multimesh_set_custom_aabb(RID p_multimesh, const AABB &p_aabb) {
	MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL(mm);
	mm->custom_aabb = p_aabb;
	mm->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_AABB);
}

AABB MeshStorage::_multimesh_get_custom_aabb(RID p_multimesh) const {
	MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL_V(mm, AABB());
	return mm->custom_aabb;
}

RID MeshStorage::_multimesh_get_mesh(RID p_multimesh) const {
	MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL_V(mm, RID());
	return mm->mesh;
}

AABB MeshStorage::_multimesh_get_aabb(RID p_multimesh) {
	MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL_V(mm, AABB());
	if (mm->custom_aabb != AABB()) {
		return mm->custom_aabb;
	}
	if (mm->aabb_dirty) {
		mm->aabb_dirty = false;
		Mesh *m = mesh_owner.get_or_null(mm->mesh);
		AABB mesh_aabb = m ? (m->custom_aabb != AABB() ? m->custom_aabb : m->aabb) : AABB();
		AABB aabb;
		int n = mm->visible_count();
		for (int i = 0; i < n; i++) {
			AABB a = mm->get_transform(i).xform(mesh_aabb);
			if (i == 0) {
				aabb = a;
			} else {
				aabb.merge_with(a);
			}
		}
		mm->aabb = aabb;
	}
	return mm->aabb;
}

Transform3D MeshStorage::_multimesh_instance_get_transform(RID p_multimesh, int p_index) const {
	MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL_V(mm, Transform3D());
	ERR_FAIL_INDEX_V(p_index, mm->instances, Transform3D());
	return mm->get_transform(p_index);
}

Transform2D MeshStorage::_multimesh_instance_get_transform_2d(RID p_multimesh, int p_index) const {
	MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL_V(mm, Transform2D());
	ERR_FAIL_INDEX_V(p_index, mm->instances, Transform2D());
	return mm->get_transform_2d(p_index);
}

Color MeshStorage::_multimesh_instance_get_color(RID p_multimesh, int p_index) const {
	MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL_V(mm, Color());
	ERR_FAIL_INDEX_V(p_index, mm->instances, Color());
	return mm->get_color(p_index);
}

Color MeshStorage::_multimesh_instance_get_custom_data(RID p_multimesh, int p_index) const {
	MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL_V(mm, Color());
	ERR_FAIL_INDEX_V(p_index, mm->instances, Color());
	if (!mm->uses_custom_data) {
		return Color();
	}
	const float *d = mm->data.ptr() + p_index * mm->stride + mm->custom_offset();
	return Color(d[0], d[1], d[2], d[3]);
}

void MeshStorage::_multimesh_set_buffer(RID p_multimesh, const Vector<float> &p_buffer) {
	MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL(mm);
	ERR_FAIL_COND(p_buffer.size() != mm->instances * mm->stride);
	mm->data = p_buffer;
	mm->aabb_dirty = true;
	mm->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_AABB);
}

Vector<float> MeshStorage::_multimesh_get_buffer(RID p_multimesh) const {
	MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL_V(mm, Vector<float>());
	return mm->data;
}

void MeshStorage::_multimesh_set_visible_instances(RID p_multimesh, int p_visible) {
	MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL(mm);
	mm->visible_instances = p_visible;
	mm->aabb_dirty = true;
	mm->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MULTIMESH_VISIBLE_INSTANCES);
}

int MeshStorage::_multimesh_get_visible_instances(RID p_multimesh) const {
	MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL_V(mm, 0);
	return mm->visible_instances;
}

/* Skeletons */

RID MeshStorage::skeleton_allocate() {
	return skeleton_owner.allocate_rid();
}

void MeshStorage::skeleton_initialize(RID p_rid) {
	skeleton_owner.initialize_rid(p_rid, Skeleton());
}

void MeshStorage::skeleton_free(RID p_rid) {
	Skeleton *sk = skeleton_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(sk);
	sk->dependency.deleted_notify(p_rid);
	skeleton_owner.free(p_rid);
}

void MeshStorage::skeleton_allocate_data(RID p_skeleton, int p_bones, bool p_2d_skeleton) {
	Skeleton *sk = skeleton_owner.get_or_null(p_skeleton);
	ERR_FAIL_NULL(sk);
	sk->size = MAX(p_bones, 0);
	sk->use_2d = p_2d_skeleton;
	if (p_2d_skeleton) {
		sk->bones_2d.resize(sk->size);
		sk->bones_2d.fill(Transform2D());
	} else {
		sk->bones.resize(sk->size);
		sk->bones.fill(Transform3D());
	}
	sk->version++;
	sk->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_SKELETON_DATA);
}

void MeshStorage::skeleton_set_base_transform_2d(RID p_skeleton, const Transform2D &p_base_transform) {
	Skeleton *sk = skeleton_owner.get_or_null(p_skeleton);
	ERR_FAIL_NULL(sk);
	sk->base_transform_2d = p_base_transform;
}

int MeshStorage::skeleton_get_bone_count(RID p_skeleton) const {
	Skeleton *sk = skeleton_owner.get_or_null(p_skeleton);
	ERR_FAIL_NULL_V(sk, 0);
	return sk->size;
}

void MeshStorage::skeleton_bone_set_transform(RID p_skeleton, int p_bone, const Transform3D &p_transform) {
	Skeleton *sk = skeleton_owner.get_or_null(p_skeleton);
	ERR_FAIL_NULL(sk);
	ERR_FAIL_INDEX(p_bone, sk->bones.size());
	sk->bones.write[p_bone] = p_transform;
	sk->version++;
}

Transform3D MeshStorage::skeleton_bone_get_transform(RID p_skeleton, int p_bone) const {
	Skeleton *sk = skeleton_owner.get_or_null(p_skeleton);
	ERR_FAIL_NULL_V(sk, Transform3D());
	ERR_FAIL_INDEX_V(p_bone, sk->bones.size(), Transform3D());
	return sk->bones[p_bone];
}

void MeshStorage::skeleton_bone_set_transform_2d(RID p_skeleton, int p_bone, const Transform2D &p_transform) {
	Skeleton *sk = skeleton_owner.get_or_null(p_skeleton);
	ERR_FAIL_NULL(sk);
	ERR_FAIL_INDEX(p_bone, sk->bones_2d.size());
	sk->bones_2d.write[p_bone] = p_transform;
	sk->version++;
}

Transform2D MeshStorage::skeleton_bone_get_transform_2d(RID p_skeleton, int p_bone) const {
	Skeleton *sk = skeleton_owner.get_or_null(p_skeleton);
	ERR_FAIL_NULL_V(sk, Transform2D());
	ERR_FAIL_INDEX_V(p_bone, sk->bones_2d.size(), Transform2D());
	return sk->bones_2d[p_bone];
}

void MeshStorage::skeleton_update_dependency(RID p_base, DependencyTracker *p_instance) {
	Skeleton *sk = skeleton_owner.get_or_null(p_base);
	if (sk) {
		p_instance->update_dependency(&sk->dependency);
	}
}
