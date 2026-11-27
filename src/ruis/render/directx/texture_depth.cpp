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

#include "texture_depth.hpp"

#include <algorithm>

#include "factory.hpp"

namespace ruis::render::directx {

texture_depth::texture_depth(
	utki::shared_ref<const ruis::render::factory> factory, //
	rasterimage::dimensioned::dimensions_type dims
) :
	ruis::render::texture_depth(std::move(factory), dims)
{
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "the factory is owned by the renderer; the base-class reference comes from the ruis API")
	auto& dx_f = static_cast<const ruis::render::directx::factory&>(this->factory.get());
	auto* device = dx_f.device;

	// ---- the depth texture resource (already in the DEPTH_STENCIL state) ----
	{
		auto desc = make_tex2d_desc(
			DXGI_FORMAT_D32_FLOAT,
			UINT(std::max<UINT64>(1, dims.x())),
			UINT(std::max<UINT64>(1, dims.y()))
		);
		desc.MipLevels = 1;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		check_hresult(
			device->CreateCommittedResource(
				&default_heap_properties(),
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_DEPTH_STENCIL,
				nullptr,
				IID_PPV_ARGS(&this->resource)
			),
			"CreateCommittedResource (depth texture)"
		);
	}

	// ---- the depth stencil view ----
	{
		const auto dsv_desc = D3D12_DESCRIPTOR_HEAP_DESC{
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
			.NumDescriptors = 1,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
			.NodeMask = 0
		};
		check_hresult(
			device->CreateDescriptorHeap(&dsv_desc, IID_PPV_ARGS(&this->dsv_heap)),
			"CreateDescriptorHeap (dsv)"
		);
		device->CreateDepthStencilView(this->resource, nullptr, this->dsv_heap->GetCPUDescriptorHandleForHeapStart());
	}
}

texture_depth::~texture_depth()
{
	if (this->dsv_heap != nullptr) {
		this->dsv_heap->Release();
	}
	if (this->resource != nullptr) {
		this->resource->Release();
	}
}

} // namespace ruis::render::directx
