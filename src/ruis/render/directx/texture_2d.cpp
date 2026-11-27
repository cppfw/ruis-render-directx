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

#include "texture_2d.hpp"

#include <algorithm>
#include <vector>

#include "factory.hpp"

using namespace ruis::render::directx;

namespace {
// fully opaque alpha value
constexpr uint8_t opaque_alpha = 255;
} // namespace

texture_2d::texture_2d(
	utki::shared_ref<const ruis::render::factory> factory, //
	rasterimage::format type,
	rasterimage::dimensioned::dimensions_type dims,
	const uint8_t* data,
	size_t data_size,
	ruis::render::factory::texture_2d_parameters params
) :
	ruis::render::texture_2d(std::move(factory), dims),
	ri_format(type)
{
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "the factory is owned by the renderer; the base-class reference comes from the ruis API")
	auto& dx_f = static_cast<const ruis::render::directx::factory&>(this->factory.get());
	auto* device = dx_f.device;

	// ---- the texture resource ----
	{
		auto desc = make_tex2d_desc(
			to_dxgi_format_8bit(type),
			UINT(std::max<UINT64>(1, dims.x())),
			UINT(std::max<UINT64>(1, dims.y()))
		);
		desc.MipLevels = 1;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		// created in the COPY_DEST state: the data is uploaded right after
		// (or the texture is cleared in that state and then transitioned)
		check_hresult(
			device->CreateCommittedResource(
				&default_heap_properties(),
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(&this->resource)
			),
			"CreateCommittedResource (texture)"
		);
	}

	// ---- descriptor heaps: one SRV and one RTV ----
	{
		const auto srv_desc = D3D12_DESCRIPTOR_HEAP_DESC{
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			.NumDescriptors = 1,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
			.NodeMask = 0
		};
		check_hresult(
			device->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&this->srv_heap)),
			"CreateDescriptorHeap (srv)"
		);

		const auto rtv_desc = D3D12_DESCRIPTOR_HEAP_DESC{
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
			.NumDescriptors = 1,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
			.NodeMask = 0
		};
		check_hresult(
			device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&this->rtv_heap)),
			"CreateDescriptorHeap (rtv)"
		);
	}

	this->create_descriptors(params);

	if (data != nullptr && data_size != 0) {
		// convert the data to RGBA (4 bytes per pixel) for the GPU
		const auto w = dims.x();
		const auto h = dims.y();
		const auto pixel_count = size_t(w) * size_t(h);
		this->deferred_data.reserve(pixel_count * 4);
		const auto data_span = utki::span<const uint8_t>(data, data_size);
		switch (type) {
			case rasterimage::format::rgba:
				this->deferred_data.assign(data_span.begin(), data_span.end());
				break;
			case rasterimage::format::rgb:
				for (size_t i = 0; i < pixel_count; ++i) {
					this->deferred_data.push_back(data_span[i * 3 + 0]);
					this->deferred_data.push_back(data_span[i * 3 + 1]);
					this->deferred_data.push_back(data_span[i * 3 + 2]);
					this->deferred_data.push_back(opaque_alpha);
				}
				break;
			case rasterimage::format::greya:
				for (size_t i = 0; i < pixel_count; ++i) {
					auto v = data_span[i * 2 + 0];
					auto a = data_span[i * 2 + 1];
					this->deferred_data.push_back(v);
					this->deferred_data.push_back(v);
					this->deferred_data.push_back(v);
					this->deferred_data.push_back(a);
				}
				break;
			case rasterimage::format::grey:
				for (size_t i = 0; i < pixel_count; ++i) {
					this->deferred_data.push_back(data_span[i]);
					this->deferred_data.push_back(data_span[i]);
					this->deferred_data.push_back(data_span[i]);
					this->deferred_data.push_back(opaque_alpha);
				}
				break;
			default:
				this->deferred_data.assign(data_span.begin(), data_span.end());
				break;
		}
		// upload the texture to the GPU synchronously
		dx_f.upload_texture(this);
	} else {
		// no data: clear the texture to transparent so that undefined GPU memory
		// does not produce garbage pixels
		dx_f.one_shot([this](ID3D12GraphicsCommandList* cmd) {
			const auto barrier1 =
				transition_barrier(this->resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
			cmd->ResourceBarrier(1, &barrier1);
			const FLOAT clear_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
			cmd->ClearRenderTargetView(this->rtv_heap->GetCPUDescriptorHandleForHeapStart(), clear_color, 0, nullptr);
			const auto barrier2 = transition_barrier(
				this->resource,
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			);
			cmd->ResourceBarrier(1, &barrier2);
		});
		dx_f.one_shot_finish();
		this->uploaded = true;
	}
}

void texture_2d::create_descriptors(ruis::render::factory::texture_2d_parameters params)
{
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "the factory is owned by the renderer; the base-class reference comes from the ruis API")
	auto& dx_f = static_cast<const ruis::render::directx::factory&>(this->factory.get());
	auto* device = dx_f.device;

	// the SRV (the sampling state comes from the root signature's static
	// sampler, so the view has no filter of its own)
	{
		const auto srv_desc = D3D12_SHADER_RESOURCE_VIEW_DESC{
			.Format = to_dxgi_format_8bit(this->ri_format),
			.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
			.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
			.Texture2D =
				D3D12_TEX2D_SRV{.MostDetailedMip = 0, .MipLevels = 1, .PlaneSlice = 0, .ResourceMinLODClamp = 0.0f}
		};
		(void)params;
		device
			->CreateShaderResourceView(this->resource, &srv_desc, this->srv_heap->GetCPUDescriptorHandleForHeapStart());
	}

	// the RTV (for rendering into the texture)
	device->CreateRenderTargetView(this->resource, nullptr, this->rtv_heap->GetCPUDescriptorHandleForHeapStart());
}

void texture_2d::record_upload_commands(ID3D12GraphicsCommandList* cmd, ID3D12Resource* upload_buffer)
{
	// the resource is in the COPY_DEST state (see the constructor), so no barrier
	// is needed before the copy; the upload buffer needs to transition to
	// COPY_SOURCE
	const auto barrier1 =
		transition_barrier(upload_buffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
	cmd->ResourceBarrier(1, &barrier1);

	// the new SDK has no CopyBufferToTexture(); use CopyTextureRegion with a
	// placed-footprint buffer source (the buffer rows use the 256-byte-aligned
	// staging row pitch)
	{
		const auto w = UINT(this->dims().x());
		const auto h = UINT(this->dims().y());
		const auto dst = D3D12_TEXTURE_COPY_LOCATION{
			.pResource = this->resource,
			.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
			.SubresourceIndex = 0
		};
		const auto src = D3D12_TEXTURE_COPY_LOCATION{
			.pResource = upload_buffer,
			.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
			.PlacedFootprint = {
								.Offset = 0,
								.Footprint = {
					.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
					.Width = w,
					.Height = h,
					.Depth = 1,
					.RowPitch = UINT(this->staging_row_pitch())
				}
			}
		};
		cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
	}

	// the texture is used both as a render target (frame buffers) and as a
	// sampled texture, so transition to both states at once
	const auto barrier2 = transition_barrier(
		this->resource,
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
	);
	cmd->ResourceBarrier(1, &barrier2);
}

UINT64 texture_2d::staging_row_pitch() const
{
	// the D3D12 texture row pitch must be 256-byte aligned
	return (UINT64(this->dims().x()) * 4 + 255) & ~(UINT64)255;
}

UINT64 texture_2d::staging_size() const
{
	return this->staging_row_pitch() * UINT64(this->dims().y());
}

texture_2d::~texture_2d()
{
	if (this->rtv_heap != nullptr) {
		this->rtv_heap->Release();
	}
	if (this->srv_heap != nullptr) {
		this->srv_heap->Release();
	}
	if (this->resource != nullptr) {
		this->resource->Release();
	}
}
