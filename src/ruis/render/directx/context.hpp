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

#include <memory>
#include <unordered_map>
#include <vector>

#include <rasterimage/image_variant.hpp>
#include <ruis/render/context.hpp>
#include <ruis/render/frame_buffer.hpp>
#include <utki/shared.hpp>

#include "shaders/shader_common.hpp"

#include "directx.hpp"
#include "factory.hpp"
#include "sdk_prelude.hpp"
#include "surface.hpp"

namespace ruis::render::directx {

class texture_2d;
class texture_depth;
class index_buffer;

// a key identifying a unique ID3D12PipelineState (PSO). The pipeline is
// independent of the particular vertex array, since the vertex input layout
// depends only on the shader (determining the attributes), and the actual
// buffer GPU virtual addresses are bound at draw time.
struct pipeline_key {
	uint32_t shader_kind = 0;
	uint32_t blend_key = 0;
	uint32_t depth_enabled = 0;
	uint32_t textured = 0;
	uint32_t topology = 0; // D3D12_PRIMITIVE_TOPOLOGY

	bool operator==(const pipeline_key& o) const
	{
		return this->shader_kind == o.shader_kind && this->blend_key == o.blend_key &&
			this->depth_enabled == o.depth_enabled && this->textured == o.textured && this->topology == o.topology;
	}
};

// boost-style hash combine
inline void hash_combine(size_t& h, size_t v)
{
	// NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers, "boost hash_combine constants")
	h ^= v + 0x9e3779b97f4a7c15 + (h << 6) + (h >> 2);
}

struct pipeline_key_hash {
	size_t operator()(const pipeline_key& k) const noexcept
	{
		size_t h = 0;
		hash_combine(h, std::hash<uint32_t>()(k.shader_kind));
		hash_combine(h, std::hash<uint32_t>()(k.blend_key));
		hash_combine(h, std::hash<uint32_t>()(k.depth_enabled));
		hash_combine(h, std::hash<uint32_t>()(k.textured));
		hash_combine(h, std::hash<uint32_t>()(k.topology));
		return h;
	}
};

class context : public ruis::render::context
{
public:
	context(
		utki::shared_ref<ruis::render::native_window> native_window, //
		utki::shared_ref<const surface> surface, //
		const utki::shared_ref<const ruis::render::directx::factory>& factory
	);
	~context() override;

	// noncopyable: owns the GPU resources of a single window
	context(const context&) = delete;
	context& operator=(const context&) = delete;
	context(context&&) = delete;
	context& operator=(context&&) = delete;

	// ===============================
	// ====== factory functions ======
	// NOTE: the shareable render objects (textures, buffers, vertex arrays, shaders)
	// are created by the injected render factory (see factory()). The only object a
	// context creates is the (per-window) frame buffer.

	utki::shared_ref<ruis::render::frame_buffer> make_framebuffer( //
		std::shared_ptr<ruis::render::texture_2d> color,
		std::shared_ptr<ruis::render::texture_depth> depth,
		std::shared_ptr<ruis::render::texture_stencil> stencil
	) override;

	// =====================================
	// ====== state control functions ======

	void set_framebuffer_internal(ruis::render::frame_buffer* fb) override;

	void clear_framebuffer_color() override;
	void clear_framebuffer_depth() override;
	void clear_framebuffer_stencil() override;

	r4::vector2<uint32_t> to_window_coords(const ruis::vec2& point) const override;

	bool is_scissor_enabled() const noexcept override;
	void enable_scissor(bool enable) override;
	r4::rectangle<uint32_t> get_scissor() const override;
	void set_scissor(const r4::rectangle<uint32_t>& r) override;

	r4::rectangle<uint32_t> get_viewport() const override;
	void set_viewport(const r4::rectangle<uint32_t>& r) override;

	void enable_blend(bool enable) override;
	void set_blend_func(
		blend_factor src_color,
		blend_factor dst_color,
		blend_factor src_alpha,
		blend_factor dst_alpha
	) override;

	bool is_depth_enabled() const noexcept override;
	void enable_depth(bool enable) override;

	void swap_frame_buffers() override;

	// ===============================
	// = directx-specific interface =

	// the packed key of the current blend state (for pipeline selection)
	uint32_t get_blend_key() const;

	// get or create a pipeline state object for the given shader + current state
	ID3D12PipelineState* get_or_create_pipeline(
		shader_kind kind, //
		bool textured,
		D3D12_PRIMITIVE_TOPOLOGY topology,
		const char* vertex_hlsl,
		const char* fragment_hlsl
	);

	// begin a frame (acquire the back buffer, reset the command list). Idempotent
	// within a frame.
	void ensure_frame_began();

	// bind the pipeline, the vertex/index buffers and issue an indexed draw.
	// 'uniforms' is the per-draw uniform data (matrix + color).
	// 'tex' is the texture to bind (nullptr for non-textured shaders).
	void draw_indexed(
		ID3D12PipelineState* pso, //
		const ruis::render::vertex_array& va,
		const texture_2d* tex,
		const draw_uniforms& uniforms
	);

	// read back the color attachment of the given frame buffer to CPU RAM (screenshot)
	rasterimage::image_variant readback_framebuffer_color(const ruis::render::frame_buffer& fb);

public:
	// used by frame_buffer to read back its color pixels
	friend class frame_buffer;

	// the render factory (shared across all windows): provides the device, the
	// one-shot upload path, and the shared root signature
	const utki::shared_ref<const ruis::render::directx::factory> factory;

	// ---- directx core (used internally by this per-window context) ----
	ID3D12Device* device = nullptr;
	ID3D12CommandQueue* queue = nullptr;
	ID3D12RootSignature* root_signature = nullptr;

private:
	// keep the surface provider alive for the lifetime of the context
	// (the surface must outlive the swapchain)
	utki::shared_ref<const surface> surface_ref;

	// ---- swapchain ----
	IDXGISwapChain3* swapchain = nullptr;
	std::vector<ID3D12Resource*> back_buffers;
	ID3D12DescriptorHeap* rtv_heap = nullptr; // one RTV per back buffer
	UINT back_buffer_count = 0;
	UINT frame_index = 0; // the back buffer index of the current frame
	r4::vector2<uint32_t> window_dims{0, 0};
	bool swapchain_vsync = false; // vsync state used when swapchain was created

	// depth texture used for screen rendering (the swapchain has no depth)
	ID3D12Resource* screen_depth = nullptr;
	ID3D12DescriptorHeap* screen_dsv_heap = nullptr;

	// ---- command submission ----
	ID3D12CommandAllocator* command_allocator = nullptr;
	ID3D12GraphicsCommandList* command_list = nullptr;
	ID3D12Fence* frame_fence = nullptr;
	HANDLE frame_fence_events[3] = {nullptr, nullptr, nullptr};
	bool frame_pending[3] = {false, false, false}; // a frame fence is pending for the back buffer

	// ---- cached pipeline state objects ----
	std::unordered_map<pipeline_key, ID3D12PipelineState*, pipeline_key_hash> pipelines;
	// the shader blobs compiled for the pipelines created this frame
	// (they must stay alive while their PSOs are alive)
	std::vector<std::unique_ptr<ID3DBlob>> shader_blobs;

	// ---- current frame state ----
	bool frame_begun = false;

	// ---- renderer state ----
	ruis::render::frame_buffer* current_fb = nullptr; // the current framebuffer (nullptr = screen)
	r4::rectangle<uint32_t> viewport{0, 0, 0, 0};
	r4::rectangle<uint32_t> scissor{0, 0, 0, 0};
	bool scissor_enabled = false;
	bool blend_enabled = false;
	ruis::render::context::blend_factor blend_src_color = ruis::render::context::blend_factor::one;
	ruis::render::context::blend_factor blend_dst_color = ruis::render::context::blend_factor::zero;
	ruis::render::context::blend_factor blend_src_alpha = ruis::render::context::blend_factor::one;
	ruis::render::context::blend_factor blend_dst_alpha = ruis::render::context::blend_factor::zero;
	bool depth_enabled = false;

	// ---- helpers ----
	void create_swapchain();
	void destroy_swapchain();

	// recreate the swapchain + depth if the window was resized
	void ensure_swapchain_valid();
	void present_frame();
	// the RTV handle of the current render target (the frame buffer color or the back buffer)
	D3D12_CPU_DESCRIPTOR_HANDLE get_color_rtv() const;
	// bind the current render target(s) to the output merger (OMSetRenderTargets),
	// including the depth texture (the frame buffer depth or the screen depth)
	void bind_render_target();
	void create_screen_depth();
	void destroy_screen_depth();
	void wait_for_gpu();
	// compile an HLSL source to DXIL; the returned blob is kept in shader_blobs
	ID3DBlob* compile_shader(const char* hlsl, const char* entry_point, const char* profile);
	ID3D12PipelineState* build_pipeline(
		const pipeline_key& key, //
		bool has_a1,
		const char* vertex_hlsl,
		const char* fragment_hlsl
	);
};

} // namespace ruis::render::directx
