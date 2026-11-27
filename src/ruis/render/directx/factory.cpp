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

#include "factory.hpp"

#include <cstring>
#include <stdexcept>

#include <utki/windows.hpp>

#include "shaders/shader_color.hpp"
#include "shaders/shader_color_pos_lum.hpp"
#include "shaders/shader_color_pos_tex.hpp"
#include "shaders/shader_color_pos_tex_alpha.hpp"
#include "shaders/shader_pos_clr.hpp"
#include "shaders/shader_pos_tex.hpp"

#include "index_buffer.hpp"
#include "texture_2d.hpp"
#include "texture_depth.hpp"
#include "vertex_array.hpp"
#include "vertex_buffer.hpp"

using namespace ruis::render::directx;

factory::factory(const utki::shared_ref<const surface>& surface) : //
	device(surface.get().get_device().get_device()), //
	queue(surface.get().get_device().get_queue())
{
	// ---- the shared root signature ----
	// Root parameter 0: 32-bit constants (the per-draw uniforms: a float4x4 matrix
	// followed by a float4 color, 20 x 32-bit values in total).
	// Root parameter 1: the texture (a single SRV descriptor slot in a CBV_SRV_UAV heap).
	// Static sampler at register s0: the (linear) sampling state used by all
	// textured shaders (no runtime binding needed).
	{
		// the SRV descriptor range (root parameter 1): one SRV at register t0 in
		// the space 0 (bound with SetGraphicsRootDescriptorTable)
		const auto srv_range = D3D12_DESCRIPTOR_RANGE{
			.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
			.NumDescriptors = 1,
			.BaseShaderRegister = 0,
			.RegisterSpace = 0,
			.OffsetInDescriptorsFromTableStart = 0 // a static (GPU-bound) offset
		};
		const auto params = std::array<D3D12_ROOT_PARAMETER, 2>{
			{{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
			  .Constants =
				  D3D12_ROOT_CONSTANTS{
					  .ShaderRegister = 0,
					  .RegisterSpace = 0,
					  .Num32BitValues = 20 // sizeof(draw_uniforms) / sizeof(float)
				  },
			  .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL},
			 {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
			  .DescriptorTable = D3D12_ROOT_DESCRIPTOR_TABLE{.NumDescriptorRanges = 1, .pDescriptorRanges = &srv_range},
			  .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL}}
		};

		const auto static_sampler = D3D12_STATIC_SAMPLER_DESC{
			.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			.MipLODBias = 0.0f,
			.MaxAnisotropy = 1,
			.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER,
			.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
			.MinLOD = 0.0f,
			.MaxLOD = D3D12_MAX_LOD,
			.ShaderRegister = 0,
			.RegisterSpace = 0,
			.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL
		};

		const auto desc = D3D12_ROOT_SIGNATURE_DESC{
			.NumParameters = uint32_t(params.size()),
			.pParameters = params.data(),
			.NumStaticSamplers = 1,
			.pStaticSamplers = &static_sampler,
			.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		};

		ID3DBlob* blob = nullptr;
		ID3DBlob* error_blob = nullptr;
		const auto hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, &error_blob);
		std::string err_text;
		if (error_blob != nullptr) {
			const auto ptr = reinterpret_cast<const char*>(error_blob->GetBufferPointer());
			err_text = (ptr != nullptr) ? std::string(ptr) : std::string();
			error_blob->Release();
		}
		check_hresult(
			FAILED(hr) ? hr : S_OK,
			FAILED(hr) && !err_text.empty() ? err_text.c_str() : "D3D12SerializeRootSignature"
		);
		check_hresult(
			this->device->CreateRootSignature(
				0,
				blob->GetBufferPointer(),
				blob->GetBufferSize(),
				IID_PPV_ARGS(&this->root_signature)
			),
			"CreateRootSignature"
		);
		blob->Release();
	}

	// ---- one-shot command submission resources (shared: used for uploads) ----
	{
		check_hresult(
			this->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&this->upload_allocator)),
			"CreateCommandAllocator"
		);
		check_hresult(
			this->device->CreateCommandList(
				0,
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				this->upload_allocator,
				nullptr,
				IID_PPV_ARGS(&this->upload_command_list)
			),
			"CreateCommandList"
		);
		check_hresult(
			this->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&this->upload_fence)),
			"CreateFence"
		);
		this->upload_fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		utki::assert(this->upload_fence_event != nullptr, SL);
	}
}

factory::~factory()
{
	this->one_shot_finish();

	if (this->upload_fence_event != nullptr) {
		CloseHandle(this->upload_fence_event);
	}
	if (this->upload_fence != nullptr) {
		this->upload_fence->Release();
	}
	if (this->upload_command_list != nullptr) {
		this->upload_command_list->Release();
	}
	if (this->upload_allocator != nullptr) {
		this->upload_allocator->Release();
	}
	if (this->root_signature != nullptr) {
		this->root_signature->Release();
	}
	// do NOT release the device or the queue: they are owned by the shared device
}

void factory::one_shot(std::function<void(ID3D12GraphicsCommandList*)> cmd) const
{
	// wait for the previously submitted one-shot work to complete
	// (the command allocator must be idle before it can be reset)
	if (this->upload_submitted) {
		const auto res = WaitForSingleObject(this->upload_fence_event, 10000); // 10s timeout
		if (res != WAIT_OBJECT_0) {
			throw std::runtime_error("factory one_shot: the one-shot upload did not complete in time");
		}
	}

	// a freshly created allocator and command list are already in the reset state;
	// some runtimes reject Reset() on them until the first work has completed
	if (!this->upload_first_shot) {
		check_hresult(this->upload_allocator->Reset(), "upload allocator Reset");
		check_hresult(this->upload_command_list->Reset(this->upload_allocator, nullptr), "upload command list Reset");
	}
	this->upload_first_shot = false;

	cmd(this->upload_command_list);

	check_hresult(this->upload_command_list->Close(), "upload command list Close");

	const auto lists = std::array<ID3D12CommandList*, 1>{{this->upload_command_list}};
	this->queue->ExecuteCommandLists(1, lists.data());

	this->upload_fence->Signal(++this->upload_fence_value);
	check_hresult(
		this->upload_fence->SetEventOnCompletion(this->upload_fence_value, this->upload_fence_event),
		"fence SetEvent"
	);
	this->upload_submitted = true;
}

void factory::one_shot_finish() const
{
	if (!this->upload_submitted) {
		return;
	}
	const auto res = WaitForSingleObject(this->upload_fence_event, 10000); // 10s timeout
	if (res != WAIT_OBJECT_0) {
		throw std::runtime_error("factory one_shot_finish: the one-shot upload did not complete in time");
	}
	this->upload_submitted = false;
}

utki::shared_ref<ruis::render::factory::shaders> factory::make_shaders() const
{
	auto ret = utki::make_shared<ruis::render::factory::shaders>();
	// NOLINTNEXTLINE(bugprone-unused-return-value, "false positive")
	ret.get().pos_tex = std::make_unique<shader_pos_tex>(this->get_shared_ref());
	// NOLINTNEXTLINE(bugprone-unused-return-value, "false positive")
	ret.get().color_pos = std::make_unique<shader_color>(this->get_shared_ref());
	// NOLINTNEXTLINE(bugprone-unused-return-value, "false positive")
	ret.get().pos_clr = std::make_unique<shader_pos_clr>(this->get_shared_ref());
	// NOLINTNEXTLINE(bugprone-unused-return-value, "false positive")
	ret.get().color_pos_tex = std::make_unique<shader_color_pos_tex>(this->get_shared_ref());
	// NOLINTNEXTLINE(bugprone-unused-return-value, "false positive")
	ret.get().color_pos_tex_alpha = std::make_unique<shader_color_pos_tex_alpha>(this->get_shared_ref());
	// NOLINTNEXTLINE(bugprone-unused-return-value, "false positive")
	ret.get().color_pos_lum = std::make_unique<shader_color_pos_lum>(this->get_shared_ref());
	return ret;
}

utki::shared_ref<ruis::render::texture_2d> factory::make_texture_2d(
	rasterimage::format format,
	rasterimage::dimensioned::dimensions_type dims,
	texture_2d_parameters params
) const
{
	return utki::make_shared<texture_2d>(this->get_shared_ref(), format, dims, nullptr, 0, params);
}

utki::shared_ref<ruis::render::texture_2d> factory::make_texture_2d(
	const rasterimage::image_variant& imvar,
	texture_2d_parameters params
) const
{
	(void)imvar;
	(void)params;
	// Not supported (need rvalue reference to move the data)
	throw std::logic_error("directx make_texture_2d: const image_variant not supported");
}

utki::shared_ref<ruis::render::texture_2d> factory::make_texture_2d(
	rasterimage::image_variant&& imvar, //
	texture_2d_parameters params
) const
{
	auto iv = std::move(imvar);
	return std::visit(
		[this, &imvar = iv, &params](auto&& im) -> utki::shared_ref<ruis::render::texture_2d> {
			if constexpr (sizeof(im.pixels().front().front()) != 1) {
				throw std::logic_error("directx make_texture_2d: non-8bit images are not supported");
			} else {
				// NOTE: do NOT flip_vertical() here. Like Vulkan, DirectX 12's image
				// row 0 is the top of the texture, so the top-to-bottom image data
				// must be uploaded as-is.
				auto data = im.pixels();
				return utki::make_shared<texture_2d>(
					this->get_shared_ref(), //
					imvar.get_format(),
					im.dims(),
					data.front().data(),
					data.size_bytes(),
					std::move(params)
				);
			}
		},
		iv.variant
	);
}

utki::shared_ref<ruis::render::texture_depth> factory::make_texture_depth( //
	rasterimage::dimensioned::dimensions_type dims
) const
{
	return utki::make_shared<texture_depth>(this->get_shared_ref(), dims);
}

utki::shared_ref<ruis::render::texture_cube> factory::make_texture_cube(
	// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved, "unimplemented stub: signature must match the base class")
	rasterimage::image_variant&& positive_x, //
	// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved, "unimplemented stub: signature must match the base class")
	rasterimage::image_variant&& negative_x, //
	// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved, "unimplemented stub: signature must match the base class")
	rasterimage::image_variant&& positive_y, //
	// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved, "unimplemented stub: signature must match the base class")
	rasterimage::image_variant&& negative_y, //
	// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved, "unimplemented stub: signature must match the base class")
	rasterimage::image_variant&& positive_z, //
	// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved, "unimplemented stub: signature must match the base class")
	rasterimage::image_variant&& negative_z //
) const
{
	(void)positive_x;
	(void)negative_x;
	(void)positive_y;
	(void)negative_y;
	(void)positive_z;
	(void)negative_z;
	throw std::runtime_error("directx make_texture_cube: not implemented");
}

utki::shared_ref<ruis::render::vertex_buffer> factory::make_vertex_buffer( //
	utki::span<const r4::vector4<float>> vertices
) const
{
	std::vector<float> padded;
	padded.reserve(vertices.size() * 4);
	for (const auto& v : vertices) {
		padded.push_back(v.x());
		padded.push_back(v.y());
		padded.push_back(v.z());
		padded.push_back(v.w());
	}
	return utki::make_shared<vertex_buffer>(this->get_shared_ref(), std::move(padded));
}

utki::shared_ref<ruis::render::vertex_buffer> factory::make_vertex_buffer( //
	utki::span<const r4::vector3<float>> vertices
) const
{
	std::vector<float> padded;
	padded.reserve(vertices.size() * 4);
	for (const auto& v : vertices) {
		padded.push_back(v.x());
		padded.push_back(v.y());
		padded.push_back(v.z());
		padded.push_back(1.0f);
	}
	return utki::make_shared<vertex_buffer>(this->get_shared_ref(), std::move(padded));
}

utki::shared_ref<ruis::render::vertex_buffer> factory::make_vertex_buffer( //
	utki::span<const r4::vector2<float>> vertices
) const
{
	std::vector<float> padded;
	padded.reserve(vertices.size() * 4);
	for (const auto& v : vertices) {
		padded.push_back(v.x());
		padded.push_back(v.y());
		padded.push_back(0.0f);
		padded.push_back(1.0f);
	}
	return utki::make_shared<vertex_buffer>(this->get_shared_ref(), std::move(padded));
}

utki::shared_ref<ruis::render::vertex_buffer> factory::make_vertex_buffer( //
	utki::span<const float> vertices
) const
{
	// a flat list of floats: interpreted as 4-component vertices
	std::vector<float> padded(vertices.begin(), vertices.end());
	return utki::make_shared<vertex_buffer>(this->get_shared_ref(), std::move(padded));
}

utki::shared_ref<ruis::render::index_buffer> factory::make_index_buffer( //
	utki::span<const uint16_t> indices
) const
{
	return utki::make_shared<index_buffer>(this->get_shared_ref(), indices);
}

utki::shared_ref<ruis::render::index_buffer> factory::make_index_buffer( //
	utki::span<const uint32_t> indices
) const
{
	return utki::make_shared<index_buffer>(this->get_shared_ref(), indices);
}

utki::shared_ref<ruis::render::vertex_array> factory::make_vertex_array(
	std::vector<utki::shared_ref<const ruis::render::vertex_buffer>> buffers, //
	utki::shared_ref<const ruis::render::index_buffer> indices,
	ruis::render::vertex_array::mode rendering_mode
) const
{
	return utki::make_shared<vertex_array>(
		this->get_shared_ref(),
		std::move(buffers),
		std::move(indices),
		rendering_mode
	);
}

void factory::upload_texture(texture_2d* t) const
{
	// the cast produces a size_t (not UINT64): used both for the DirectX API
	// (which widens it) and for host-side copies, avoiding narrowing conversions
	const auto total_size = size_t(t->staging_size());
	if (total_size == 0) {
		return;
	}

	ID3D12Resource* upload = nullptr;
	{
		const auto desc = make_buffer_desc(UINT64(total_size), D3D12_RESOURCE_FLAG_NONE);
		check_hresult(
			this->device->CreateCommittedResource(
				&upload_heap_properties(),
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COMMON,
				nullptr,
				IID_PPV_ARGS(&upload)
			),
			"CreateCommittedResource (upload buffer)"
		);
	}

	// the staging rows are 256-byte aligned (the D3D12 texture copy API
	// requirement), so the packed RGBA data is copied row by row
	{
		const auto w = UINT64(t->dims().x());
		const auto h = UINT64(t->dims().y());
		const auto row_bytes = w * 4;
		const auto pitch = t->staging_row_pitch();
		void* mapped = nullptr;
		check_hresult(upload->Map(0, nullptr, &mapped), "Map (upload buffer)");
		auto* dst = static_cast<uint8_t*>(mapped);
		const auto* src = t->deferred_data.data();
		for (size_t y = 0; y < SIZE_T(h); ++y) {
			std::memcpy(dst + y * SIZE_T(pitch), src + y * SIZE_T(row_bytes), SIZE_T(row_bytes));
		}
		upload->Unmap(0, nullptr);
	}

	// record the upload commands and submit
	this->one_shot([&](ID3D12GraphicsCommandList* cmd) {
		t->record_upload_commands(cmd, upload);
	});
	this->one_shot_finish();

	upload->Release();

	t->deferred_data.clear();
	t->uploaded = true;
}
