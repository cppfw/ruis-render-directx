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

#include "shader_color.hpp"

#include "shader_src.hpp"

using namespace ruis::render::directx;

shader_color::shader_color(utki::shared_ref<const ruis::render::factory> factory) :
	ruis::render::coloring_shader(std::move(factory))
{}

void shader_color::render(
	const r4::matrix4<float>& m,
	const ruis::render::vertex_array& va,
	const r4::vector4<float>& color
) const
{
	directx_shader_render(
		shader_kind::color,
		hlsl_shader_color_vertex,
		hlsl_shader_color_fragment,
		nullptr, // no texture
		m,
		color,
		va
	);
}
