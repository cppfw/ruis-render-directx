/*
ruis-render-directx - DirectX 12 renderer

Copyright (C) 2012-2026  Ivan Gagis <igagis@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

/* ================ LICENSE END ================ */

#pragma once

#include <functional>
#include <vector>

#include <rasterimage/image_variant.hpp>
#include <ruis/render/factory.hpp>

#include "directx.hpp"
#include "sdk_prelude.hpp"
#include "surface.hpp"

namespace ruis::render::directx {

// the directx derived classes intentionally share names with the ruis::render base classes
// NOLINTNEXTLINE(bugprone-forward-declaration-namespace, "derived class shadows the ruis::render base class by design")
class texture_2d;
// NOLINTNEXTLINE(bugprone-forward-declaration-namespace, "derived class shadows the ruis::render base class by design")
class texture_depth;
// NOLINTNEXTLINE(bugprone-forward-declaration-namespace, "derived class shadows the ruis::render base class by design")
class texture_cube;
// NOLINTNEXTLINE(bugprone-forward-declaration-namespace, "derived class shadows the ruis::render base class by design")
class vertex_array;
// NOLINTNEXTLINE(bugprone-forward-declaration-namespace, "derived class shadows the ruis::render base class by design")
class vertex_buffer;
// NOLINTNEXTLINE(bugprone-forward-declaration-namespace, "derived class shadows the ruis::render base class by design")
class index_buffer;

/**
 * @brief DirectX 12 render factory.
 *
 * Owns the shared DirectX 12 resources (the ID3D12Device (via the surface),
 * the shared root signature, and the one-shot command allocator used for
 * uploads) and is the only place that creates the shareable rendering
 * objects (textures, vertex/index buffers, vertex arrays, shaders).
 *
 * A single factory is shared across all windows of an application: it wraps
 * the shared @ref device so that GPU objects created for one window (e.g. the
 * common white texture and quad vertex buffers) can be used by any other
 * window.
 *
 * Root signature layout (shared by all pipelines):
 *   - root parameter 0: constant buffer view (the per-draw uniforms, bound as
 *     a raw GPU virtual address), visible to vertex and pixel shaders;
 *   - root parameter 1: a single SRV (descriptor slot in the texture's
 *     CBV_SRV_UAV heap), visible to the pixel shader.
 */
class factory : public ruis::render::factory
{
public:
	// ---- shared DirectX handles (public: used by derived GPU resource classes) ----
	ID3D12Device* device = nullptr;
	ID3D12CommandQueue* queue = nullptr;

	// the shared root signature (see the class documentation for the layout)
	ID3D12RootSignature* root_signature = nullptr;

	factory(const utki::shared_ref<const surface>& surface);
	~factory() override;

	// noncopyable: owns the shared GPU resources
	factory(const factory&) = delete;
	factory& operator=(const factory&) = delete;
	factory(factory&&) = delete;
	factory& operator=(factory&&) = delete;

	// ===============================
	// ====== factory functions ======

	utki::shared_ref<shaders> make_shaders() const override;

	utki::shared_ref<ruis::render::texture_2d> make_texture_2d(
		rasterimage::format format,
		rasterimage::dimensioned::dimensions_type dims,
		texture_2d_parameters params
	) const override;

	utki::shared_ref<ruis::render::texture_2d> make_texture_2d(
		const rasterimage::image_variant& imvar,
		texture_2d_parameters params
	) const override;

	utki::shared_ref<ruis::render::texture_2d> make_texture_2d(
		rasterimage::image_variant&& imvar,
		texture_2d_parameters params
	) const override;

	utki::shared_ref<ruis::render::texture_depth> make_texture_depth( //
		rasterimage::dimensioned::dimensions_type dims
	) const override;

	utki::shared_ref<ruis::render::texture_cube> make_texture_cube(
		rasterimage::image_variant&& positive_x,
		rasterimage::image_variant&& negative_x,
		rasterimage::image_variant&& positive_y,
		rasterimage::image_variant&& negative_y,
		rasterimage::image_variant&& positive_z,
		rasterimage::image_variant&& negative_z
	) const override;

	utki::shared_ref<ruis::render::vertex_buffer> make_vertex_buffer( //
		utki::span<const r4::vector4<float>> vertices
	) const override;
	utki::shared_ref<ruis::render::vertex_buffer> make_vertex_buffer( //
		utki::span<const r4::vector3<float>> vertices
	) const override;
	utki::shared_ref<ruis::render::vertex_buffer> make_vertex_buffer( //
		utki::span<const r4::vector2<float>> vertices
	) const override;
	utki::shared_ref<ruis::render::vertex_buffer> make_vertex_buffer( //
		utki::span<const float> vertices
	) const override;

	utki::shared_ref<ruis::render::index_buffer> make_index_buffer( //
		utki::span<const uint16_t> indices
	) const override;
	utki::shared_ref<ruis::render::index_buffer> make_index_buffer( //
		utki::span<const uint32_t> indices
	) const override;

	utki::shared_ref<ruis::render::vertex_array> make_vertex_array(
		std::vector<utki::shared_ref<const ruis::render::vertex_buffer>> buffers, //
		utki::shared_ref<const ruis::render::index_buffer> indices,
		ruis::render::vertex_array::mode rendering_mode
	) const override;

	// ===============================
	// = directx-specific interface =

	// run a one-shot command list (used for texture and buffer uploads); blocks until complete.
	// const: it only submits GPU work, it does not mutate the factory's C++ state.
	void one_shot(std::function<void(ID3D12GraphicsCommandList*)> cmd) const;

	// upload a single texture to the GPU synchronously (blocks until complete).
	// called from the texture_2d constructor after the data is converted to RGBA.
	void upload_texture(texture_2d* t) const;

	// wait for the one-shot command list to complete (blocks until complete)
	void one_shot_finish() const;

private:
	// ---- one-shot command submission (texture and buffer uploads) ----
	ID3D12CommandAllocator* upload_allocator = nullptr;
	ID3D12GraphicsCommandList* upload_command_list = nullptr;
	ID3D12Fence* upload_fence = nullptr;
	HANDLE upload_fence_event = nullptr;
	// mutable: one_shot()/one_shot_finish() are const (they only submit GPU
	// work), but they advance this internal submission state
	mutable uint64_t upload_fence_value = 0;
	mutable bool upload_submitted = false;
};

} // namespace ruis::render::directx
