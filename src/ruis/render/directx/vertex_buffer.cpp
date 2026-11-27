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

#include "vertex_buffer.hpp"

#include <cstring>

#include "factory.hpp"

namespace ruis::render::directx {

vertex_buffer::vertex_buffer(
	utki::shared_ref<const ruis::render::factory> factory, //
	std::vector<float>&& padded_data
) :
	ruis::render::vertex_buffer(
		std::move(factory),
		padded_data.size() // the number of float components
	),
	byte_size(padded_data.size() * sizeof(float))
{
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "the factory is owned by the renderer; the base-class reference comes from the ruis API")
	auto& dx_f = static_cast<const ruis::render::directx::factory&>(this->factory.get());
	auto* device = dx_f.device;

	// ---- the vertex buffer (default heap) ----
	{
		const auto desc = make_buffer_desc(UINT64(this->byte_size), D3D12_RESOURCE_FLAG_NONE);
		check_hresult(
			device->CreateCommittedResource(
				&default_heap_properties(),
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(&this->resource)
			),
			"CreateCommittedResource (vertex buffer)"
		);
		this->gpu_va = this->resource->GetGPUVirtualAddress();
	}

	// ---- upload the data ----
	{
		ID3D12Resource* upload = nullptr;
		{
			const auto desc = make_buffer_desc(UINT64(this->byte_size), D3D12_RESOURCE_FLAG_NONE);
			check_hresult(
				device->CreateCommittedResource(
					&upload_heap_properties(),
					D3D12_HEAP_FLAG_NONE,
					&desc,
					D3D12_RESOURCE_STATE_COMMON,
					nullptr,
					IID_PPV_ARGS(&upload)
				),
				"CreateCommittedResource (vertex buffer upload)"
			);
		}

		void* mapped = nullptr;
		check_hresult(upload->Map(0, nullptr, &mapped), "Map (vertex buffer upload)");
		std::memcpy(mapped, padded_data.data(), this->byte_size);
		upload->Unmap(0, nullptr);

		dx_f.one_shot([&](ID3D12GraphicsCommandList* cmd) {
			const auto barrier1 =
				transition_barrier(upload, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
			cmd->ResourceBarrier(1, &barrier1);
			cmd->CopyBufferRegion(this->resource, 0, upload, 0, UINT64(this->byte_size));
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

vertex_buffer::~vertex_buffer()
{
	if (this->resource != nullptr) {
		this->resource->Release();
	}
}

} // namespace ruis::render::directx
