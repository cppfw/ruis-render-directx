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

#include <cstdio>
#include <stdexcept>
#include <string>

#include <r4/matrix.hpp>
#include <r4/vector.hpp>
#include <rasterimage/image_variant.hpp>
#include <ruis/render/context.hpp>
#include <ruis/render/texture_2d.hpp>
#include <utki/debug.hpp>

#include "sdk_prelude.hpp"

namespace ruis::render::directx {

/**
 * @brief Throw an exception if a DirectX 12 call returned a failure HRESULT.
 */
inline void check_hresult(HRESULT hr, const char* what)
{
	if (FAILED(hr)) {
		char hr_buf[16] = {0};
		std::snprintf(hr_buf, sizeof(hr_buf), "0x%08x", static_cast<unsigned int>(hr));
		throw std::runtime_error(std::string("DirectX 12 call failed: ") + what + " (hr = " + hr_buf + ")");
	}
}

/**
 * @brief The shared draw uniforms, bound as a root constant buffer view.
 *
 * The matrix is stored in row-major order, which is the same order that
 * r4::matrix4 uses and the default packing order of a row-major float4x4 in
 * HLSL, so the matrix is copied directly from r4 without any transposition.
 */
struct draw_uniforms {
	float matrix[4][4];
	float color[4];
};

static_assert(sizeof(draw_uniforms) == 16 * sizeof(float) + 4 * sizeof(float));

inline void write_draw_uniforms(draw_uniforms* u, const r4::matrix4<float>& m, const r4::vector4<float>& color)
{
	for (uint32_t r = 0; r < 4; ++r) {
		for (uint32_t c = 0; c < 4; ++c) {
			u->matrix[r][c] = m[r][c];
		}
	}
	u->color[0] = color.x();
	u->color[1] = color.y();
	u->color[2] = color.z();
	u->color[3] = color.w();
}

/**
 * @brief Map the ruis blending factor to a DXGI blending factor.
 *
 * D3D12 has no factor separate for the alpha component
 * (D3D11_BLEND_CONSTANT_ALPHA), so constant_alpha is mapped to
 * D3D12_BLEND_BLEND_FACTOR (using the alpha component of the blend factor).
 */
inline D3D12_BLEND to_dxgi_blend_factor(ruis::render::context::blend_factor f)
{
	switch (f) {
		case ruis::render::context::blend_factor::zero:
			return D3D12_BLEND_ZERO;
		case ruis::render::context::blend_factor::one:
			return D3D12_BLEND_ONE;
		case ruis::render::context::blend_factor::src_color:
			return D3D12_BLEND_SRC_COLOR;
		case ruis::render::context::blend_factor::one_minus_src_color:
			return D3D12_BLEND_INV_SRC_COLOR;
		case ruis::render::context::blend_factor::dst_color:
			return D3D12_BLEND_DEST_COLOR;
		case ruis::render::context::blend_factor::one_minus_dst_color:
			return D3D12_BLEND_INV_DEST_COLOR;
		case ruis::render::context::blend_factor::src_alpha:
			return D3D12_BLEND_SRC_ALPHA;
		case ruis::render::context::blend_factor::one_minus_src_alpha:
			return D3D12_BLEND_INV_SRC_ALPHA;
		case ruis::render::context::blend_factor::dst_alpha:
			return D3D12_BLEND_DEST_ALPHA;
		case ruis::render::context::blend_factor::one_minus_dst_alpha:
			return D3D12_BLEND_INV_DEST_ALPHA;
		case ruis::render::context::blend_factor::constant_color:
			return D3D12_BLEND_BLEND_FACTOR;
		case ruis::render::context::blend_factor::one_minus_constant_color:
			return D3D12_BLEND_INV_BLEND_FACTOR;
		case ruis::render::context::blend_factor::constant_alpha:
			// no separate alpha factor in D3D12; use the blend factor
			return D3D12_BLEND_BLEND_FACTOR;
		case ruis::render::context::blend_factor::one_minus_constant_alpha:
			// no separate alpha factor in D3D12; use the blend factor
			return D3D12_BLEND_INV_BLEND_FACTOR;
		case ruis::render::context::blend_factor::src_alpha_saturate:
			return D3D12_BLEND_SRC_ALPHA_SAT;
		default:
			throw std::logic_error("to_dxgi_blend_factor: unknown blending factor");
	}
}

/**
 * @brief Map the ruis texture filter to a DXGI texture filter.
 */
inline D3D12_FILTER to_dxgi_filter(ruis::render::texture_2d::filter f)
{
	switch (f) {
		case ruis::render::texture_2d::filter::linear:
			return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		case ruis::render::texture_2d::filter::nearest:
			return D3D12_FILTER_MIN_MAG_MIP_POINT;
		default:
			throw std::logic_error("to_dxgi_filter: unknown filter");
	}
}

/**
 * @brief Map the rasterimage 8-bit format to the DXGI texture format used
 * for all ruis textures (all textures are stored as 8-bit RGBA on the GPU).
 */
inline DXGI_FORMAT to_dxgi_format_8bit(rasterimage::format f)
{
	(void)f;
	return DXGI_FORMAT_R8G8B8A8_UNORM;
}

// D3D12_RESOURCE_STATE_DEPTH_STENCIL was removed from the new SDK;
// it is the same combination of states as before
inline constexpr auto D3D12_RESOURCE_STATE_DEPTH_STENCIL = //
	D3D12_RESOURCE_STATE_DEPTH_WRITE | //
	D3D12_RESOURCE_STATE_DEPTH_READ;

// D3D12_MAX_LOD was removed from the new SDK;
// it is the maximum mip level (2048) that indicates "all mip levels"
inline constexpr float D3D12_MAX_LOD = 2048.0f;

inline const D3D12_HEAP_PROPERTIES& default_heap_properties()
{
	// a process-wide singleton (the properties are immutable)
	static const auto props = D3D12_HEAP_PROPERTIES{
		.Type = D3D12_HEAP_TYPE_DEFAULT,
		.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
		.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
		.CreationNodeMask = 0,
		.VisibleNodeMask = 0
	};
	return props;
}

/**
 * @brief Make a D3D12_RESOURCE_DESC for a buffer.
 * (the D3D12_RESOURCE_DESC::Buffer() helper is not available in the new SDK)
 */
inline D3D12_RESOURCE_DESC make_buffer_desc(UINT64 size, D3D12_RESOURCE_FLAGS flags)
{
	auto desc = D3D12_RESOURCE_DESC{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = size;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = flags;
	return desc;
}

/**
 * @brief Make a D3D12_RESOURCE_DESC for a 2D texture.
 * (the D3D12_RESOURCE_DESC::Tex2D() helper is not available in the new SDK)
 */
inline D3D12_RESOURCE_DESC make_tex2d_desc(DXGI_FORMAT format, UINT w, UINT h)
{
	auto desc = D3D12_RESOURCE_DESC{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = w;
	desc.Height = h;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	return desc;
}

inline const D3D12_HEAP_PROPERTIES& upload_heap_properties()
{
	// a process-wide singleton (the properties are immutable)
	static const auto props = D3D12_HEAP_PROPERTIES{
		.Type = D3D12_HEAP_TYPE_UPLOAD,
		.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
		.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
		.CreationNodeMask = 0,
		.VisibleNodeMask = 0
	};
	return props;
}

// a transition resource barrier (the new SDK has no D3D12_RESOURCE_BARRIER::
// Transition() static helper)
inline D3D12_RESOURCE_BARRIER transition_barrier(
	ID3D12Resource* resource,
	D3D12_RESOURCE_STATES before,
	D3D12_RESOURCE_STATES after
)
{
	auto b = D3D12_RESOURCE_BARRIER{};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	b.Transition.pResource = resource;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	b.Transition.StateBefore = before;
	b.Transition.StateAfter = after;
	return b;
}

// compile an HLSL shader source at runtime (via the D3D compiler);
// the returned blob contains the DXIL bytecode
std::vector<uint8_t> compile_hlsl(const char* source, const char* entry_point, const char* profile);

} // namespace ruis::render::directx
