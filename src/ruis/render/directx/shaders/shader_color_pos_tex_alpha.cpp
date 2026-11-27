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

#include "shader_color_pos_tex_alpha.hpp"

#include "../texture_2d.hpp"

#include "shader_src.hpp"

using namespace ruis::render::directx;

shader_color_pos_tex_alpha::shader_color_pos_tex_alpha(utki::shared_ref<const ruis::render::factory> factory) :
	ruis::render::coloring_texturing_shader(std::move(factory))
{}

void shader_color_pos_tex_alpha::render(
	const r4::matrix4<float>& m,
	const ruis::render::vertex_array& va,
	const r4::vector4<float>& color,
	const ruis::render::texture_2d& tex
) const
{
	utki::assert(dynamic_cast<const texture_2d*>(&tex), SL);
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "assert(dynamic_cast) done")
	const auto& dx_tex = static_cast<const texture_2d&>(tex);

	directx_shader_render(
		shader_kind::color_pos_tex_alpha,
		hlsl_shader_color_pos_tex_alpha_vertex,
		hlsl_shader_color_pos_tex_alpha_fragment,
		&dx_tex, //
		m,
		color,
		va
	);
}
