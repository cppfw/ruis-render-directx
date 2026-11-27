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

#include <r4/matrix.hpp>
#include <r4/vector.hpp>
#include <ruis/render/vertex_array.hpp>

#include "../sdk_prelude.hpp"

namespace ruis::render::directx {

// identifies the shader (determines the number of attributes and the uniforms used)
enum class shader_kind : uint32_t {
	color = 0, // a0 = position, uniform color
	pos_clr = 1, // a0 = position, a1 = color
	pos_tex = 2, // a0 = position, a1 = texcoord, texture
	color_pos_tex = 3, // a0 = position, a1 = texcoord, uniform color, texture
	color_pos_tex_alpha = 4, // a0 = position, a1 = texcoord, uniform color, texture alpha
	color_pos_lum = 5 // a0 = position, a1 = luminance, uniform color
};

// does the shader use the a1 attribute?
constexpr bool shader_has_a1(shader_kind k)
{
	return k != shader_kind::color;
}

// does the shader use a texture?
constexpr bool shader_has_texture(shader_kind k)
{
	return k == shader_kind::pos_tex || k == shader_kind::color_pos_tex || k == shader_kind::color_pos_tex_alpha;
}

// the format of the a1 attribute (a0 is always a float4 position)
constexpr DXGI_FORMAT a1_format(shader_kind k)
{
	switch (k) {
		case shader_kind::pos_clr:
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case shader_kind::color_pos_lum:
			return DXGI_FORMAT_R32_FLOAT;
		default:
			// the textured shaders: a1 is a float2 texture coordinate
			return DXGI_FORMAT_R32G32_FLOAT;
	}
}

// map the VAO's rendering mode to a DirectX primitive topology
inline D3D12_PRIMITIVE_TOPOLOGY to_topology(ruis::render::vertex_array::mode m)
{
	switch (m) {
		case ruis::render::vertex_array::mode::triangles:
			return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		case ruis::render::vertex_array::mode::triangle_fan:
			return D3D_PRIMITIVE_TOPOLOGY_TRIANGLEFAN;
		case ruis::render::vertex_array::mode::line_loop:
			return D3D_PRIMITIVE_TOPOLOGY_LINELIST; // D3D12 has no line loop; use a line list
		case ruis::render::vertex_array::mode::triangle_strip:
			return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
		default:
			return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	}
}

// NOLINTNEXTLINE(bugprone-forward-declaration-namespace, "this is the directx-specific context, not ruis::render::context")
class context;
class texture_2d;

/**
 * @brief Common render implementation shared by all DirectX 12 shaders.
 *
 * Downcasts the rendering context to the DirectX context, ensures the frame is
 * begun, obtains/creates the pipeline state object, updates the per-draw
 * uniforms and issues the draw.
 *
 * The texture (if any) is passed as a pointer to the DirectX texture_2d so the
 * texture's SRV descriptor can be bound at draw time.
 */
void directx_shader_render(
	shader_kind kind,
	const char* vertex_hlsl,
	const char* fragment_hlsl,
	const texture_2d* texture, // nullptr for non-textured shaders
	const r4::matrix4<float>& m,
	const r4::vector4<float>& color,
	const ruis::render::vertex_array& va
);

} // namespace ruis::render::directx
