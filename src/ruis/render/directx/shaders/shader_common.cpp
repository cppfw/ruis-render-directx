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

#include "shader_common.hpp"

#include <utility>

#include "../context.hpp"
#include "../texture_2d.hpp"

namespace ruis::render::directx {

void directx_shader_render(
	shader_kind kind,
	const char* vertex_hlsl,
	const char* fragment_hlsl,
	const texture_2d* texture, // nullptr for non-textured shaders
	const r4::matrix4<float>& m,
	const r4::vector4<float>& color,
	const ruis::render::vertex_array& va
)
{
	// use the currently active context (set by ruis::render::context::apply()),
	// not the context stored in the shader (which may be the shared resource-loader context)
	auto& cur = ruis::render::context::current();
	utki::assert(dynamic_cast<const context*>(&cur) != nullptr, SL);
	auto& ctx =
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "assert(dynamic_cast) done above")
		static_cast<context&>(cur);

	// make sure the frame is begun (the back buffer is acquired and the command
	// list is reset)
	ctx.ensure_frame_began();

	// skip drawing if the viewport is zero-sized (e.g. resource loader context)
	{
		const auto& vp = ctx.get_viewport();
		if (vp.d.x() == 0 || vp.d.y() == 0) {
			return;
		}
	}

	// the per-draw uniforms (root constants)
	draw_uniforms u{};
	write_draw_uniforms(&u, m, color);

	// get or create the pipeline state object for the current state
	const auto pso = ctx.get_or_create_pipeline(
		kind,
		shader_has_texture(kind),
		to_topology(va.rendering_mode),
		vertex_hlsl,
		fragment_hlsl
	);

	ctx.draw_indexed(pso, va, texture, u);
}

} // namespace ruis::render::directx
