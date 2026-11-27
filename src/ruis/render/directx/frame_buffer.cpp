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

#include "frame_buffer.hpp"

#include "context.hpp"

using namespace ruis::render::directx;

frame_buffer::frame_buffer(
	const utki::shared_ref<ruis::render::context>& rendering_context, //
	std::shared_ptr<ruis::render::texture_2d> color,
	std::shared_ptr<ruis::render::texture_depth> depth,
	std::shared_ptr<ruis::render::texture_stencil> stencil
) :
	ruis::render::frame_buffer(rendering_context, std::move(color), std::move(depth), std::move(stencil))
{
	// nothing to create: DirectX 12 binds the render targets directly
}

rasterimage::image_variant frame_buffer::get_color_pixels()
{
	// read the color attachment back to CPU RAM via the context
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "the renderer owns the object; the base-class reference comes from the ruis API")
	auto& ctx = static_cast<context&>(this->rendering_context.get());
	return ctx.readback_framebuffer_color(*this);
}
