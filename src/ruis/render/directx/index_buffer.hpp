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

#include <rasterimage/dimensioned.hpp>
#include <ruis/render/index_buffer.hpp>
#include <utki/span.hpp>

#include "directx.hpp"

namespace ruis::render::directx {

class factory;

class index_buffer : public ruis::render::index_buffer
{
public:
	ID3D12Resource* resource = nullptr;
	UINT64 gpu_va = 0; // the GPU virtual address of the buffer (for DrawIndexed)
	DXGI_FORMAT index_format = DXGI_FORMAT_R32_UINT;
	UINT index_count = 0;

	explicit index_buffer(
		utki::shared_ref<const ruis::render::factory> factory, //
		utki::span<const uint16_t> indices
	);

	explicit index_buffer(
		utki::shared_ref<const ruis::render::factory> factory, //
		utki::span<const uint32_t> indices
	);

	index_buffer(const index_buffer&) = delete;
	index_buffer& operator=(const index_buffer&) = delete;
	index_buffer(index_buffer&&) = delete;
	index_buffer& operator=(index_buffer&&) = delete;

	~index_buffer() override;

private:
	void upload(utki::span<const uint8_t> data, DXGI_FORMAT fmt, UINT count);
};

} // namespace ruis::render::directx
