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

#include "index_buffer.hpp"

#include <cstring>

#include "factory.hpp"

namespace ruis::render::directx {

index_buffer::index_buffer(
	utki::shared_ref<const ruis::render::factory> factory, //
	utki::span<const uint16_t> indices
) :
	ruis::render::index_buffer(std::move(factory))
{
	this->upload(
		utki::span<const uint8_t>(reinterpret_cast<const uint8_t*>(indices.data()), indices.size() * sizeof(uint16_t)),
		DXGI_FORMAT_R16_UINT,
		UINT(indices.size())
	);
}

index_buffer::index_buffer(
	utki::shared_ref<const ruis::render::factory> factory, //
	utki::span<const uint32_t> indices
) :
	ruis::render::index_buffer(std::move(factory))
{
	this->upload(
		utki::span<const uint8_t>(reinterpret_cast<const uint8_t*>(indices.data()), indices.size() * sizeof(uint32_t)),
		DXGI_FORMAT_R32_UINT,
		UINT(indices.size())
	);
}

void index_buffer::upload(utki::span<const uint8_t> data, DXGI_FORMAT fmt, UINT count)
{
	const auto byte_size = data.size();

	this->index_format = fmt;
	this->index_count = count;

	// an empty index buffer does not need a GPU resource
	if (byte_size == 0) {
		return;
	}

	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "the factory is owned by the renderer; the base-class reference comes from the ruis API")
	auto& dx_f = static_cast<const ruis::render::directx::factory&>(this->factory.get());
	auto* device = dx_f.device;

	// ---- the index buffer (default heap) ----
	{
		const auto desc = make_buffer_desc(UINT64(byte_size), D3D12_RESOURCE_FLAG_NONE);
		check_hresult(
			device->CreateCommittedResource(
				&default_heap_properties(),
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(&this->resource)
			),
			"CreateCommittedResource (index buffer)"
		);
		this->gpu_va = this->resource->GetGPUVirtualAddress();
	}

	// ---- upload the data ----
	{
		ID3D12Resource* upload = nullptr;
		{
			const auto desc = make_buffer_desc(UINT64(byte_size), D3D12_RESOURCE_FLAG_NONE);
			check_hresult(
				device->CreateCommittedResource(
					&upload_heap_properties(),
					D3D12_HEAP_FLAG_NONE,
					&desc,
					D3D12_RESOURCE_STATE_COMMON,
					nullptr,
					IID_PPV_ARGS(&upload)
				),
				"CreateCommittedResource (index buffer upload)"
			);
		}

		void* mapped = nullptr;
		check_hresult(upload->Map(0, nullptr, &mapped), "Map (index buffer upload)");
		std::memcpy(mapped, data.data(), byte_size);
		upload->Unmap(0, nullptr);

		dx_f.one_shot([&](ID3D12GraphicsCommandList* cmd) {
			const auto barrier1 =
				transition_barrier(upload, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
			cmd->ResourceBarrier(1, &barrier1);
			cmd->CopyBufferRegion(this->resource, 0, upload, 0, UINT64(byte_size));
			const auto barrier2 = transition_barrier(
				this->resource,
				D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
			);
			cmd->ResourceBarrier(1, &barrier2);
		});
		dx_f.one_shot_finish();

		upload->Release();
	}
}

index_buffer::~index_buffer()
{
	if (this->resource != nullptr) {
		this->resource->Release();
	}
}

} // namespace ruis::render::directx
