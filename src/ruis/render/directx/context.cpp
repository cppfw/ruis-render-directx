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

#include "context.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

#include <utki/windows.hpp>

#include "frame_buffer.hpp"
#include "index_buffer.hpp"
#include "texture_2d.hpp"
#include "texture_depth.hpp"
#include "vertex_array.hpp"
#include "vertex_buffer.hpp"

using namespace ruis::render::directx;

namespace {

constexpr ruis::real ndc_to_unit_scale = ruis::real(0.5);

// the size of a vertex in bytes: all vertex attributes are stored as float4 (4 floats)
constexpr UINT64 vertex_stride_bytes = 4 * sizeof(float);

} // namespace

context::context(
	utki::shared_ref<ruis::render::native_window> native_window, //
	utki::shared_ref<const surface> surface, //
	const utki::shared_ref<const ruis::render::directx::factory>& factory
) :
	ruis::render::context(
		std::move(native_window),
		factory,
		[] {
			r4::matrix4<float> m{};
			// maps the framework's normalized viewport coordinates (x right, y down,
			// zero at the top-left, right/bottom edges at (1, 1)) to the DirectX NDC.
			//
			// In the DirectX NDC -1 -1 corresponds to the top-left corner, 1 1 to the
			// bottom-right corner, and z points away from the viewer (same orientation
			// as the framework, so no axis flip is needed).
			m.set_identity().translate(-1, -1).scale(2, 2);
			return parameters{m};
		}()
	),
	factory(factory),
	device(factory.get().device),
	queue(factory.get().queue),
	root_signature(factory.get().root_signature),
	surface_ref(surface)
{
	// ---- command submission resources ----
	{
		check_hresult(
			this->device
				->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&this->command_allocator)),
			"CreateCommandAllocator"
		);
		check_hresult(
			this->device->CreateCommandList(
				0,
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				this->command_allocator,
				nullptr,
				IID_PPV_ARGS(&this->command_list)
			),
			"CreateCommandList"
		);
		check_hresult(
			this->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&this->frame_fence)),
			"CreateFence"
		);
		for (int i = 0; i < 3; ++i) {
			this->frame_fence_events[i] = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			utki::assert(this->frame_fence_events[i] != nullptr, SL);
		}
	}

	// ---- swapchain (only if we have a surface; headless contexts skip this) ----
	if (this->surface_ref.get().has_surface()) {
		this->create_swapchain();
	}
}

context::~context()
{
	this->wait_for_gpu();

	// release the cached pipeline state objects (they reference the root signature
	// and the shader blobs)
	for (auto& [key, pso] : this->pipelines) {
		(void)key;
		if (pso != nullptr) {
			pso->Release();
		}
	}
	this->pipelines.clear();

	// release the compiled shader blobs
	for (auto& blob : this->shader_blobs) {
		if (blob != nullptr) {
			blob->Release();
		}
	}
	this->shader_blobs.clear();

	// the screen depth
	this->destroy_screen_depth();

	// the swapchain
	this->destroy_swapchain();

	// command submission resources
	for (int i = 0; i < 3; ++i) {
		if (this->frame_fence_events[i] != nullptr) {
			CloseHandle(this->frame_fence_events[i]);
		}
	}
	if (this->frame_fence != nullptr) {
		this->frame_fence->Release();
	}
	if (this->command_list != nullptr) {
		this->command_list->Release();
	}
	if (this->command_allocator != nullptr) {
		this->command_allocator->Release();
	}
	// do NOT release the device, the queue or the root signature:
	// they are owned by the shared device / the factory
}

// ===============================
// =========== swapchain =========

void context::create_swapchain()
{
	utki::assert(this->surface_ref.get().has_surface(), SL);
	const auto dims = this->native_window.get().get_dims();
	this->window_dims = r4::vector2<uint32_t>(std::max<uint32_t>(1, dims.x()), std::max<uint32_t>(1, dims.y()));

	IDXGIFactory2* dxgi_factory = nullptr;
	check_hresult(CreateDXGIFactory(IID_PPV_ARGS(&dxgi_factory)), "CreateDXGIFactory");

	this->destroy_swapchain();

	{
		const auto desc = DXGI_SWAP_CHAIN_DESC1{
			.Width = this->window_dims.x(),
			.Height = this->window_dims.y(),
			.Format = DXGI_FORMAT_B8G8R8A8_UNORM,
			.SampleDesc = {.Count = 1, .Quality = 0},
			.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
			.BufferCount = 3,
			.Scaling = DXGI_SCALING_STRETCH,
			.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
			.AlphaMode = DXGI_ALPHA_MODE_IGNORE,
			.Flags = 0
		};
		IDXGISwapChain1* sc = nullptr;
		check_hresult(
			dxgi_factory->CreateSwapChainForHwnd(
				reinterpret_cast<IUnknown*>(this->device),
				this->surface_ref.get().get_hwnd(),
				&desc,
				nullptr,
				nullptr,
				&sc
			),
			"CreateSwapChainForHwnd"
		);
		check_hresult(sc->QueryInterface(IID_PPV_ARGS(&this->swapchain)), "IDXGISwapChain3 QueryInterface");
		sc->Release();
		dxgi_factory->MakeWindowAssociation(this->surface_ref.get().get_hwnd(), DXGI_MWA_NO_ALT_ENTER);
	}
	dxgi_factory->Release();

	// the back buffers
	this->back_buffers.clear();
	for (UINT i = 0;; ++i) {
		ID3D12Resource* buf = nullptr;
		const auto hr = this->swapchain->GetBuffer(i, IID_PPV_ARGS(&buf));
		if (FAILED(hr) || buf == nullptr) {
			break;
		}
		this->back_buffers.push_back(buf);
	}
	utki::assert(this->back_buffer_count == 0 || this->back_buffers.size() == this->back_buffer_count, SL);
	this->back_buffer_count = UINT(this->back_buffers.size());
	utki::assert(this->back_buffer_count >= 2, SL);

	// one RTV descriptor per back buffer
	{
		const auto heap_desc = D3D12_DESCRIPTOR_HEAP_DESC{
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
			.NumDescriptors = this->back_buffer_count,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
			.NodeMask = 0
		};
		check_hresult(
			this->device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&this->rtv_heap)),
			"CreateDescriptorHeap (rtv)"
		);
		const auto inc = this->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		for (UINT i = 0; i < this->back_buffer_count; ++i) {
			const auto handle =
				D3D12_CPU_DESCRIPTOR_HANDLE{.ptr = this->rtv_heap->GetCPUDescriptorHandleForHeapStart().ptr + i * inc};
			this->device->CreateRenderTargetView(this->back_buffers[i], nullptr, handle);
		}
	}

	this->frame_index = 0;
	this->swapchain_vsync = this->is_vsync_enabled();

	// the screen depth (recreated on resize)
	this->create_screen_depth();
}

void context::destroy_swapchain()
{
	if (this->rtv_heap != nullptr) {
		this->rtv_heap->Release();
		this->rtv_heap = nullptr;
	}
	for (auto* buf : this->back_buffers) {
		if (buf != nullptr) {
			buf->Release();
		}
	}
	this->back_buffers.clear();
	this->back_buffer_count = 0;
	if (this->swapchain != nullptr) {
		this->swapchain->Release();
		this->swapchain = nullptr;
	}
	// reset the frame fence pending flags (the fence values become stale)
	for (int i = 0; i < 3; ++i) {
		this->frame_pending[i] = false;
	}
}

void context::create_screen_depth()
{
	this->destroy_screen_depth();
	if (!this->surface_ref.get().has_surface()) {
		return;
	}

	auto desc = make_tex2d_desc(DXGI_FORMAT_D32_FLOAT, this->window_dims.x(), this->window_dims.y());
	{
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		// created directly in the DEPTH_STENCIL state (it is only used as a DSV)
		check_hresult(
			this->device->CreateCommittedResource(
				&default_heap_properties(),
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_DEPTH_STENCIL,
				nullptr,
				IID_PPV_ARGS(&this->screen_depth)
			),
			"CreateCommittedResource (screen depth)"
		);
	}

	{
		const auto heap_desc = D3D12_DESCRIPTOR_HEAP_DESC{
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
			.NumDescriptors = 1,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
			.NodeMask = 0
		};
		check_hresult(
			this->device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&this->screen_dsv_heap)),
			"CreateDescriptorHeap (screen dsv)"
		);
		this->device->CreateDepthStencilView(
			this->screen_depth,
			nullptr,
			this->screen_dsv_heap->GetCPUDescriptorHandleForHeapStart()
		);
	}
}

void context::destroy_screen_depth()
{
	if (this->screen_dsv_heap != nullptr) {
		this->screen_dsv_heap->Release();
		this->screen_dsv_heap = nullptr;
	}
	if (this->screen_depth != nullptr) {
		this->screen_depth->Release();
		this->screen_depth = nullptr;
	}
}

void context::ensure_swapchain_valid()
{
	// headless contexts (e.g. resource loading) have no surface; nothing to do
	if (!this->surface_ref.get().has_surface()) {
		return;
	}
	const auto native_dims = this->native_window.get().get_dims();
	const auto w = std::max<uint32_t>(1, native_dims.x());
	const auto h = std::max<uint32_t>(1, native_dims.y());
	// also check if vsync changed (requires swapchain recreation)
	const auto vsync_now = this->is_vsync_enabled();
	if (w == this->window_dims.x() && h == this->window_dims.y() && vsync_now == this->swapchain_vsync) {
		return;
	}
	// the window was resized (or the vsync changed); wait for the GPU to finish
	// before recreating the swapchain
	this->wait_for_gpu();
	// reset the frame state (the in-flight frame is discarded)
	this->frame_begun = false;
	this->create_swapchain();
}

// ===============================
// =========== frame begin =======

void context::wait_for_gpu()
{
	if (this->swapchain == nullptr) {
		return; // headless context
	}
	// wait for the frame that rendered to the back buffer we are about to reuse
	const auto i = int(this->frame_index);
	if (this->frame_pending[i]) {
		const auto res = WaitForSingleObject(this->frame_fence_events[i], 10000); // 10s timeout
		if (res != WAIT_OBJECT_0) {
			throw std::runtime_error("directx context wait_for_gpu: the frame fence did not complete in time");
		}
		this->frame_pending[i] = false;
	}
}

void context::ensure_frame_began()
{
	if (this->frame_begun) {
		return;
	}

	// recreate the swapchain if the window was resized (must happen before rendering)
	this->ensure_swapchain_valid();

	if (this->swapchain != nullptr) {
		// wait for the GPU to finish with the back buffer we are about to overwrite,
		// then advance to the next back buffer
		this->wait_for_gpu();
		this->frame_index = (this->frame_index + 1) % this->back_buffer_count;
	}

	// reset the command allocator and the command list (the GPU has finished with
	// the work recorded in them); a freshly created pair is already in the reset
	// state and some runtimes reject Reset() on it until the first work has
	// completed
	if (!this->frame_first_begin) {
		check_hresult(this->command_allocator->Reset(), "command allocator Reset");
		check_hresult(this->command_list->Reset(this->command_allocator, nullptr), "command list Reset");
	}
	this->frame_first_begin = false;

	this->frame_begun = true;
}

// ===============================
// =========== present ==========

void context::present_frame()
{
	check_hresult(this->command_list->Close(), "command list Close");

	const auto lists = std::array<ID3D12CommandList*, 1>{{this->command_list}};
	this->queue->ExecuteCommandLists(1, lists.data());

	// signal the fence for this back buffer (waited on before it is reused)
	const auto value = this->frame_fence->GetCompletedValue() + 1;
	this->frame_fence->Signal(value);
	const auto i = int(this->frame_index);
	check_hresult(
		this->frame_fence->SetEventOnCompletion(value, this->frame_fence_events[i]),
		"frame fence SetEventOnCompletion"
	);
	this->frame_pending[i] = true;

	// present the back buffer to the window
	if (this->swapchain != nullptr) {
		const auto hr = this->swapchain->Present(this->swapchain_vsync ? 1 : 0, 0);
		(void)hr; // a failed present (e.g. window minimized) is not fatal
	}
}

void context::swap_frame_buffers()
{
	// make sure the frame is begun (the command list is valid) and the GPU work is
	// recorded, then submit and present
	this->ensure_frame_began();
	this->present_frame();
	this->frame_begun = false;
}

// ===============================
// =========== render targets ====

D3D12_CPU_DESCRIPTOR_HANDLE context::get_color_rtv() const
{
	if (this->current_fb != nullptr) {
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "the renderer owns the object; the base-class reference comes from the ruis API")
		const auto& color = static_cast<const texture_2d&>(*this->current_fb->color);
		return color.rtv_heap->GetCPUDescriptorHandleForHeapStart();
	}
	// the current back buffer
	utki::assert(this->rtv_heap != nullptr, SL);
	const auto inc = this->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	return D3D12_CPU_DESCRIPTOR_HANDLE{
		.ptr = this->rtv_heap->GetCPUDescriptorHandleForHeapStart().ptr + UINT64(this->frame_index) * inc
	};
}

void context::bind_render_target()
{
	const auto rtv = this->get_color_rtv();

	D3D12_CPU_DESCRIPTOR_HANDLE* dsv = nullptr;
	if (this->depth_enabled) {
		if (this->current_fb != nullptr && this->current_fb->depth != nullptr) {
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "the renderer owns the object; the base-class reference comes from the ruis API")
			const auto& depth = static_cast<const texture_depth&>(*this->current_fb->depth);
			auto dsv_handle = depth.dsv_heap->GetCPUDescriptorHandleForHeapStart();
			dsv = &dsv_handle;
		} else if (this->current_fb == nullptr && this->screen_dsv_heap != nullptr) {
			auto dsv_handle2 = this->screen_dsv_heap->GetCPUDescriptorHandleForHeapStart();
			dsv = &dsv_handle2;
		}
		// if there is no depth texture available (e.g. a frame buffer without depth),
		// no depth is bound
	}
	this->command_list->OMSetRenderTargets(1, &rtv, TRUE, dsv);
}

// ===============================
// =========== state control =====

utki::shared_ref<ruis::render::frame_buffer> context::make_framebuffer( //
	std::shared_ptr<ruis::render::texture_2d> color, //
	std::shared_ptr<ruis::render::texture_depth> depth, //
	std::shared_ptr<ruis::render::texture_stencil> stencil
)
{
	return utki::make_shared<frame_buffer>(
		this->get_shared_ref(),
		std::move(color),
		std::move(depth),
		std::move(stencil)
	);
}

void context::set_framebuffer_internal(ruis::render::frame_buffer* fb)
{
	this->current_fb = fb;
}

void context::clear_framebuffer_color()
{
	this->ensure_frame_began();
	this->bind_render_target();
	const FLOAT color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	this->command_list->ClearRenderTargetView(this->get_color_rtv(), color, 0, nullptr);
}

void context::clear_framebuffer_depth()
{
	this->ensure_frame_began();
	if (!this->depth_enabled) {
		return;
	}
	D3D12_CPU_DESCRIPTOR_HANDLE* dsv = nullptr;
	if (this->current_fb != nullptr && this->current_fb->depth != nullptr) {
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "the renderer owns the object; the base-class reference comes from the ruis API")
		const auto& depth = static_cast<const texture_depth&>(*this->current_fb->depth);
		auto dsv_handle = depth.dsv_heap->GetCPUDescriptorHandleForHeapStart();
		dsv = &dsv_handle;
	} else if (this->current_fb == nullptr && this->screen_dsv_heap != nullptr) {
		auto dsv_handle2 = this->screen_dsv_heap->GetCPUDescriptorHandleForHeapStart();
		dsv = &dsv_handle2;
	}
	if (dsv == nullptr) {
		return; // no depth texture to clear
	}
	this->command_list->ClearDepthStencilView(*dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
}

void context::clear_framebuffer_stencil()
{
	// no stencil support in the DirectX 12 renderer: do nothing
}

r4::vector2<uint32_t> context::to_window_coords(const ruis::vec2& point) const
{
	// NDC -> window space using the swapchain (physical) dimensions
	const auto dims = this->window_dims.to<ruis::real>();
	// clamp the result to the window bounds (same as the GL variant): without the
	// clamp a point outside the window would produce a negative value which, when
	// cast to uint32_t, wraps around to a huge number and corrupts the scissor
	// rectangle (the widget's contents would disappear instead of being clipped)
	auto res = (point + ruis::vec2(1, 1)) * ndc_to_unit_scale;
	res.comp_multiply(dims);
	res = min(dims, max(res, ruis::vec2(0, 0)));
	return round(res).to<uint32_t>();
}

bool context::is_scissor_enabled() const noexcept
{
	return this->scissor_enabled;
}

void context::enable_scissor(bool enable)
{
	this->scissor_enabled = enable;
}

r4::rectangle<uint32_t> context::get_scissor() const
{
	return this->scissor;
}

void context::set_scissor(const r4::rectangle<uint32_t>& r)
{
	this->scissor = r;
}

r4::rectangle<uint32_t> context::get_viewport() const
{
	return this->viewport;
}

void context::set_viewport(const r4::rectangle<uint32_t>& r)
{
	this->viewport = r;
}

void context::enable_blend(bool enable)
{
	this->blend_enabled = enable;
}

void context::set_blend_func(
	blend_factor src_color,
	blend_factor dst_color,
	blend_factor src_alpha,
	blend_factor dst_alpha
)
{
	this->blend_src_color = src_color;
	this->blend_dst_color = dst_color;
	this->blend_src_alpha = src_alpha;
	this->blend_dst_alpha = dst_alpha;
}

bool context::is_depth_enabled() const noexcept
{
	return this->depth_enabled;
}

void context::enable_depth(bool enable)
{
	this->depth_enabled = enable;
}

uint32_t context::get_blend_key() const
{
	// pack the blend state into a key for the pipeline cache:
	// the enabled flag + the four factors (4 bits each)
	uint32_t key = uint32_t(this->blend_enabled ? 1 : 0);
	key = (key << 4) | uint32_t(this->blend_src_color);
	key = (key << 4) | uint32_t(this->blend_dst_color);
	key = (key << 4) | uint32_t(this->blend_src_alpha);
	key = (key << 4) | uint32_t(this->blend_dst_alpha);
	return key;
}

// ===============================
// =========== pipelines ==========

ID3DBlob* context::compile_shader(const char* hlsl, const char* entry_point, const char* profile)
{
	ID3DBlob* blob = nullptr;
	ID3DBlob* err_blob = nullptr;
	check_hresult(
		D3DCompile(
			hlsl,
			strlen(hlsl),
			"ruis-render-directx.hlsl",
			nullptr,
			nullptr,
			entry_point,
			profile,
			0,
			0,
			&blob,
			&err_blob
		),
		"D3DCompile"
	);
	if (err_blob != nullptr) {
		err_blob->Release();
	}
	this->shader_blobs.emplace_back(blob);
	return blob;
}

ID3D12PipelineState* context::build_pipeline(
	const pipeline_key& key, //
	bool has_a1,
	const char* vertex_hlsl,
	const char* fragment_hlsl
)
{
	// compile the shaders to DXIL (the blobs are kept alive in shader_blobs)
	auto* vs_blob = this->compile_shader(vertex_hlsl, "main", "vs_6_0");
	auto* fs_blob = this->compile_shader(fragment_hlsl, "main", "ps_6_0");

	// ---- the vertex input layout ----
	// all vertices are stored as float4 (4 floats) in the vertex buffer; the a1
	// attribute (if present) is the tail of the float4 (its format is shader-specific)
	const auto a1 = shader_kind(key.shader_kind);
	auto input_layout_elements = std::vector<D3D12_INPUT_ELEMENT_DESC>{
		{.SemanticName = "POSITION",
		 .SemanticIndex = 0,
		 .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
		 .InputSlot = 0,
		 .AlignedByteOffset = 0,
		 .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA}
	};
	if (has_a1) {
		input_layout_elements.push_back(
			D3D12_INPUT_ELEMENT_DESC{
				.SemanticName = "TEXCOORD",
				.SemanticIndex = 0,
				.Format = a1_format(a1),
				.InputSlot = 1,
				.AlignedByteOffset = UINT(vertex_stride_bytes),
				.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA
			}
		);
	}
	const auto input_layout = D3D12_INPUT_LAYOUT_DESC{
		.pInputElementDescs = input_layout_elements.data(),
		.NumElements = UINT(input_layout_elements.size())
	};

	// ---- the blend state ----
	auto blend = D3D12_BLEND_DESC{.AlphaToCoverageEnable = FALSE, .IndependentBlendEnable = FALSE};
	blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	if (this->blend_enabled) {
		blend.RenderTarget[0].BlendEnable = TRUE;
		blend.RenderTarget[0].SrcBlend = to_dxgi_blend_factor(this->blend_src_color);
		blend.RenderTarget[0].DestBlend = to_dxgi_blend_factor(this->blend_dst_color);
		blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blend.RenderTarget[0].SrcBlendAlpha = to_dxgi_blend_factor(this->blend_src_alpha);
		blend.RenderTarget[0].DestBlendAlpha = to_dxgi_blend_factor(this->blend_dst_alpha);
		blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	} else {
		blend.RenderTarget[0].BlendEnable = FALSE;
	}

	// ---- the depth-stencil state ----
	const auto use_depth = (key.depth_enabled != 0);
	auto depth_stencil = D3D12_DEPTH_STENCIL_DESC{
		.DepthEnable = use_depth ? TRUE : FALSE,
		.DepthWriteMask = use_depth ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO,
		.DepthFunc = D3D12_COMPARISON_FUNC_LESS,
		.StencilEnable = FALSE,
		.StencilReadMask = 0,
		.StencilWriteMask = 0
	};
	// the front and back faces use the same (unused) stencil values
	depth_stencil.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	depth_stencil.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;

	const auto pipeline_desc = D3D12_GRAPHICS_PIPELINE_STATE_DESC{
		.pRootSignature = this->root_signature,
		.VS = D3D12_SHADER_BYTECODE{vs_blob->GetBufferPointer(), vs_blob->GetBufferSize()},
		.PS = D3D12_SHADER_BYTECODE{fs_blob->GetBufferPointer(), fs_blob->GetBufferSize()},
		.BlendState = blend,
		.SampleMask = 0xFFFFFFFF,
		.RasterizerState =
			D3D12_RASTERIZER_DESC{
									.FillMode = D3D12_FILL_MODE_SOLID,
									.CullMode = D3D12_CULL_MODE_NONE,
									.FrontCounterClockwise = FALSE,
									.DepthBias = 0,
									.DepthBiasClamp = 0.0f,
									.SlopeScaledDepthBias = 0.0f,
									.DepthClipEnable = TRUE,
									.MultisampleEnable = FALSE,
									.AntialiasedLineEnable = FALSE,
									.ForcedSampleCount = 0,
									.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF
			},
		.DepthStencilState = depth_stencil,
		.InputLayout = input_layout,
		.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
		.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
		.NumRenderTargets = 1,
		.RTVFormats = {DXGI_FORMAT_UNKNOWN}, // the actual format depends on the render target
		.DSVFormat = DXGI_FORMAT_UNKNOWN, // the actual format depends on the depth texture
		.SampleDesc = {.Count = 1, .Quality = 0},
		.NodeMask = 0
	};

	ID3D12PipelineState* pso = nullptr;
	check_hresult(
		this->device->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&pso)),
		"CreateGraphicsPipelineState"
	);
	return pso;
}

ID3D12PipelineState* context::get_or_create_pipeline(
	shader_kind kind, //
	bool textured,
	D3D12_PRIMITIVE_TOPOLOGY topology,
	const char* vertex_hlsl,
	const char* fragment_hlsl
)
{
	const auto key = pipeline_key{
		.shader_kind = uint32_t(kind),
		.blend_key = this->get_blend_key(),
		.depth_enabled = uint32_t(this->depth_enabled ? 1 : 0),
		.textured = uint32_t(textured ? 1 : 0),
		.topology = uint32_t(topology)
	};

	const auto it = this->pipelines.find(key);
	if (it != this->pipelines.end()) {
		return it->second;
	}

	const auto pso = this->build_pipeline(key, shader_has_a1(kind), vertex_hlsl, fragment_hlsl);
	this->pipelines.emplace(key, pso);
	return pso;
}

// ===============================
// =========== draw ==============

void context::draw_indexed(
	ID3D12PipelineState* pso, //
	const ruis::render::vertex_array& va,
	const texture_2d* tex,
	const draw_uniforms& uniforms
)
{
	// the pipeline
	this->command_list->SetGraphicsRootSignature(this->root_signature);
	this->command_list->SetPipelineState(pso);
	this->command_list->IASetPrimitiveTopology(to_topology(va.rendering_mode));

	// the vertex buffer (the first buffer of the vertex array; the vertex data of
	// all buffers is expected to have the same float4 layout)
	{
		utki::assert(!va.buffers.empty(), SL);
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "the renderer owns the object; the base-class reference comes from the ruis API")
		const auto& vb = static_cast<const vertex_buffer&>(va.buffers.front().get());
		const auto vbv = D3D12_VERTEX_BUFFER_VIEW{
			.BufferLocation = vb.gpu_va,
			.SizeInBytes = UINT(vb.byte_size),
			.StrideInBytes = UINT(vertex_stride_bytes)
		};
		this->command_list->IASetVertexBuffers(0, 1, &vbv);
	}

	// the index buffer (an empty index buffer has no GPU resource)
	{
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "the renderer owns the object; the base-class reference comes from the ruis API")
		const auto& ib = static_cast<const index_buffer&>(va.indices.get());
		if (ib.index_count > 0) {
			const auto ibv = D3D12_INDEX_BUFFER_VIEW{
				.BufferLocation = ib.gpu_va,
				.SizeInBytes = UINT(UINT64(ib.index_count) * (ib.index_format == DXGI_FORMAT_R16_UINT ? 2 : 4)),
				.Format = ib.index_format
			};
			this->command_list->IASetIndexBuffer(&ibv);
		}
	}

	// the per-draw uniforms (root 32-bit constants)
	this->command_list->SetGraphicsRoot32BitConstants(0, 20, &uniforms, 0);

	// the texture (if any)
	if (tex != nullptr) {
		this->command_list->SetGraphicsRootDescriptorTable(1, tex->srv_heap->GetGPUDescriptorHandleForHeapStart());
	}

	// the viewport and the scissor (window coordinates: y goes down, like in the
	// framework, so no conversion is needed)
	{
		const auto& vp = this->viewport;
		const auto viewport_desc = D3D12_VIEWPORT{
			.TopLeftX = float(vp.p.x()),
			.TopLeftY = float(vp.p.y()),
			.Width = float(vp.d.x()),
			.Height = float(vp.d.y()),
			.MinDepth = 0.0f,
			.MaxDepth = 1.0f
		};
		this->command_list->RSSetViewports(1, &viewport_desc);
	}
	if (this->scissor_enabled) {
		const auto& sc = this->scissor;
		const auto scissor_desc = D3D12_RECT{
			.left = LONG(sc.p.x()),
			.top = LONG(sc.p.y()),
			.right = LONG(sc.p.x() + sc.d.x()),
			.bottom = LONG(sc.p.y() + sc.d.y())
		};
		this->command_list->RSSetScissorRects(1, &scissor_desc);
	} else {
		const auto scissor_desc = D3D12_RECT{
			.left = 0,
			.top = 0,
			.right = LONG(this->window_dims.x()),
			.bottom = LONG(this->window_dims.y())
		};
		this->command_list->RSSetScissorRects(1, &scissor_desc);
	}

	// the draw call (skipped for an empty index buffer)
	{
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "the renderer owns the object; the base-class reference comes from the ruis API")
		const auto& ib = static_cast<const index_buffer&>(va.indices.get());
		if (ib.index_count > 0) {
			this->command_list->DrawIndexedInstanced(ib.index_count, 1, 0, 0, 0);
		}
	}
}

// ===============================
// =========== readback =========

rasterimage::image_variant context::readback_framebuffer_color(const ruis::render::frame_buffer& fb)
{
	// read the color attachment back to CPU RAM. The frame was recorded in the
	// command list (deferred), so the readback is appended to it, then the frame
	// is submitted and waited on.
	this->ensure_frame_began();

	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "the renderer owns the object; the base-class reference comes from the ruis API")
	const auto& color = static_cast<const texture_2d&>(*fb.color);

	const auto w = uint32_t(fb.dims().x());
	const auto h = uint32_t(fb.dims().y());
	// the D3D12 texture row pitch must be 256-byte aligned
	const auto row_pitch = (UINT64(w) * 4 + 255) & ~(UINT64)255;

	// the readback buffer (GPU-visible heap)
	ID3D12Resource* gpu_readback = nullptr;
	{
		const auto desc = make_buffer_desc(row_pitch * h, D3D12_RESOURCE_FLAG_NONE);
		check_hresult(
			this->device->CreateCommittedResource(
				&default_heap_properties(),
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(&gpu_readback)
			),
			"CreateCommittedResource (readback)"
		);
	}

	// the CPU-visible readback buffer
	ID3D12Resource* cpu_readback = nullptr;
	{
		const auto desc = make_buffer_desc(row_pitch * h, D3D12_RESOURCE_FLAG_NONE);
		check_hresult(
			this->device->CreateCommittedResource(
				&upload_heap_properties(),
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(&cpu_readback)
			),
			"CreateCommittedResource (cpu readback)"
		);
	}

	// record the readback commands
	{
		{
			// the new SDK has no CopyTextureToBuffer(); use CopyTextureRegion with a
			// buffer destination (row pitch and size come from the allocation above)
			// the destination is a buffer, described as a placed footprint with the
			// pitch the buffer was allocated with
			const auto dst = D3D12_TEXTURE_COPY_LOCATION{
				.pResource = gpu_readback,
				.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
				.PlacedFootprint = {
									.Offset = 0,
									.Footprint = {
						.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
						.Width = UINT(w),
						.Height = UINT(h),
						.Depth = 1,
						.RowPitch = UINT(row_pitch)
					}
				}
			};
			const auto src = D3D12_TEXTURE_COPY_LOCATION{
				.pResource = color.resource,
				.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
				.SubresourceIndex = 0
			};
			const auto box = D3D12_BOX{0, 0, 0, UINT(w), UINT(h), 1};
			this->command_list->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
		}
		this->command_list->CopyBufferRegion(cpu_readback, 0, gpu_readback, 0, row_pitch * h);
	}

	// submit the frame and wait for the GPU to finish
	this->present_frame();
	this->frame_begun = false;
	this->wait_for_gpu();

	// read the data back (the row pitch is the one the buffer was allocated with)
	std::vector<uint8_t> pixels(row_pitch * h);
	{
		const auto pitch = row_pitch;
		D3D12_RANGE read_range{0, SIZE_T(pitch * h)};
		void* mapped = nullptr;
		check_hresult(cpu_readback->Map(0, &read_range, &mapped), "Map (cpu readback)");
		auto* dst = pixels.data();
		auto* src = static_cast<const uint8_t*>(mapped);
		for (UINT y = 0; y < h; ++y) {
			std::memcpy(dst, src, row_pitch);
			dst += row_pitch;
			src += pitch;
		}
		cpu_readback->Unmap(0, nullptr);
	}

	gpu_readback->Release();
	cpu_readback->Release();

	// the pixels are in RGBA8 format, stored top-to-bottom (row 0 = top), which is
	// already the conventional top-down order, so no flip is needed
	auto im = rasterimage::image<uint8_t, 4>::make(r4::vector2<uint32_t>(w, h), pixels.data());
	return {std::move(im)};
}
