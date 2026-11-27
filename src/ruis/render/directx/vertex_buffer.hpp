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

#include <r4/vector.hpp>
#include <ruis/render/vertex_buffer.hpp>
#include <utki/span.hpp>

#include "directx.hpp"

namespace ruis::render::directx {

class factory;

class vertex_buffer : public ruis::render::vertex_buffer
{
public:
	ID3D12Resource* resource = nullptr;
	UINT64 gpu_va = 0; // the GPU virtual address of the buffer (for the root CBV)
	size_t byte_size = 0;

	vertex_buffer(
		utki::shared_ref<const ruis::render::factory> factory, //
		std::vector<float>&& padded_data
	);

	vertex_buffer(const vertex_buffer&) = delete;
	vertex_buffer& operator=(const vertex_buffer&) = delete;
	vertex_buffer(vertex_buffer&&) = delete;
	vertex_buffer& operator=(vertex_buffer&&) = delete;

	~vertex_buffer() override;
};

} // namespace ruis::render::directx
