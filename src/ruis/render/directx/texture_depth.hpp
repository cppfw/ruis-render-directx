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

#include <rasterimage/dimensioned.hpp>
#include <ruis/render/texture_depth.hpp>

#include "directx.hpp"

namespace ruis::render::directx {

class texture_depth : public ruis::render::texture_depth
{
public:
	ID3D12Resource* resource = nullptr;
	ID3D12DescriptorHeap* dsv_heap = nullptr; // 1 DSV descriptor

	texture_depth(
		utki::shared_ref<const ruis::render::factory> factory, //
		rasterimage::dimensioned::dimensions_type dims
	);

	texture_depth(const texture_depth&) = delete;
	texture_depth& operator=(const texture_depth&) = delete;
	texture_depth(texture_depth&&) = delete;
	texture_depth& operator=(texture_depth&&) = delete;

	~texture_depth() override;
};

} // namespace ruis::render::directx
