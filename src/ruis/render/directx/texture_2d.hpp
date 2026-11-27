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

#include <vector>

#include <rasterimage/image_variant.hpp>
#include <ruis/render/texture_2d.hpp>

#include "directx.hpp"

namespace ruis::render::directx {

class factory;

class texture_2d : public ruis::render::texture_2d
{
public:
	ID3D12Resource* resource = nullptr;
	ID3D12DescriptorHeap* srv_heap = nullptr; // 1 SRV descriptor (the sampled texture)
	ID3D12DescriptorHeap* rtv_heap = nullptr; // 1 RTV descriptor (for rendering into the texture)
	rasterimage::format ri_format = rasterimage::format::rgba;

	texture_2d(
		utki::shared_ref<const ruis::render::factory> factory, //
		rasterimage::format type,
		rasterimage::dimensioned::dimensions_type dims,
		const uint8_t* data,
		size_t data_size,
		ruis::render::factory::texture_2d_parameters params
	);

	texture_2d(const texture_2d&) = delete;
	texture_2d& operator=(const texture_2d&) = delete;
	texture_2d(texture_2d&&) = delete;
	texture_2d& operator=(texture_2d&&) = delete;

	~texture_2d() override;

	// deferred upload (public for the factory's upload_texture())
	bool uploaded = false;
	rasterimage::format deferred_format = rasterimage::format::rgba;
	rasterimage::dimensioned::dimensions_type deferred_dims{};
	std::vector<uint8_t> deferred_data;

	// record the upload commands on the given command list
	// (called from factory::upload_texture() inside a one-shot submission)
	void record_upload_commands(ID3D12GraphicsCommandList* cmd, ID3D12Resource* upload_buffer);

	// the row pitch (bytes) of the staging buffer (256-byte aligned, as required
	// by the D3D12 texture copy API)
	UINT64 staging_row_pitch() const;

	// the number of bytes of the uploaded (RGBA8) texture data
	UINT64 staging_size() const;

private:
	void create_descriptors(ruis::render::factory::texture_2d_parameters params);
};

} // namespace ruis::render::directx
