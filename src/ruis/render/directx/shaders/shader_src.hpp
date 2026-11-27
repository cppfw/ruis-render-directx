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

/*
 * The HLSL sources of the renderer's shaders.
 * They are compiled to DXIL at runtime with the D3D compiler (see
 * directx::compile_hlsl()).
 *
 * The per-draw uniforms (the transformation matrix and the color) are passed
 * as root 32-bit constants in register b0: first a float4x4 matrix (16 floats),
 * then a float4 color (4 floats), 20 floats (80 bytes) in total. Every shader
 * that uses the color has to declare the matrix variable first (even if it is
 * unused), because HLSL places consecutive root constant variables at register
 * b0 at sequential float offsets.
 *
 * The texture (for the textured shaders) is bound to the SRV root parameter
 * (t0) and is sampled with the static sampler at s0 (defined in the root
 * signature), so the shaders declare the SamplerState without any runtime
 * binding.
 */

#pragma once

namespace ruis::render::directx {

// ==============================
// ============== color =========

static const char* hlsl_shader_color_vertex = R"HLSL(
RootConstants<FLOAT4X4>[1] draw_matrix : register(b0);
RootConstants<FLOAT4>[1] draw_color : register(b16);

float4 main(float4 pos : POSITION) : SV_POSITION
{
	return draw_matrix * pos;
}
)HLSL";

static const char* hlsl_shader_color_fragment = R"HLSL(
RootConstants<FLOAT4X4>[1] draw_matrix : register(b0);
RootConstants<FLOAT4>[1] draw_color : register(b16);

float4 main() : SV_Target
{
	return draw_color;
}
)HLSL";

// ==============================
// ============== pos_clr =======

static const char* hlsl_shader_pos_clr_vertex = R"HLSL(
RootConstants<FLOAT4X4>[1] draw_matrix : register(b0);
RootConstants<FLOAT4>[1] draw_color : register(b16);

struct VS_OUT
{
	float4 pos : SV_POSITION;
	float4 col : COLOR;
};

VS_OUT main(float4 pos : POSITION, float4 col : COLOR)
{
	VS_OUT o;
	o.pos = draw_matrix * pos;
	o.col = col;
	return o;
}
)HLSL";

static const char* hlsl_shader_pos_clr_fragment = R"HLSL(
float4 main(float4 col : COLOR) : SV_Target
{
	return col;
}
)HLSL";

// ==============================
// ============== pos_tex =======

static const char* hlsl_shader_pos_tex_vertex = R"HLSL(
RootConstants<FLOAT4X4>[1] draw_matrix : register(b0);
RootConstants<FLOAT4>[1] draw_color : register(b16);

struct VS_OUT
{
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD0;
};

VS_OUT main(float4 pos : POSITION, float2 uv : TEXCOORD)
{
	VS_OUT o;
	o.pos = draw_matrix * pos;
	o.uv = uv;
	return o;
}
)HLSL";

static const char* hlsl_shader_pos_tex_fragment = R"HLSL(
RootConstants<FLOAT4X4>[1] draw_matrix : register(b0);
RootConstants<FLOAT4>[1] draw_color : register(b16);

Texture2D<float4> tex0 : register(t0);
SamplerState samp0 : register(s0);

float4 main(float2 uv : TEXCOORD) : SV_Target
{
	return tex0.Sample(samp0, uv) * draw_color;
}
)HLSL";

// ==============================
// ============== color_pos_tex =

static const char* hlsl_shader_color_pos_tex_vertex = hlsl_shader_pos_tex_vertex;
static const char* hlsl_shader_color_pos_tex_fragment = hlsl_shader_pos_tex_fragment;

// ==============================
// ============== color_pos_tex_alpha =

static const char* hlsl_shader_color_pos_tex_alpha_vertex = hlsl_shader_pos_tex_vertex;

static const char* hlsl_shader_color_pos_tex_alpha_fragment = R"HLSL(
RootConstants<FLOAT4X4>[1] draw_matrix : register(b0);
RootConstants<FLOAT4>[1] draw_color : register(b16);

Texture2D<float4> tex0 : register(t0);
SamplerState samp0 : register(s0);

float4 main(float2 uv : TEXCOORD) : SV_Target
{
	return float4(draw_color.xyz, draw_color.w * tex0.Sample(samp0, uv).r);
}
)HLSL";

// ==============================
// ============== color_pos_lum =

static const char* hlsl_shader_color_pos_lum_vertex = R"HLSL(
RootConstants<FLOAT4X4>[1] draw_matrix : register(b0);
RootConstants<FLOAT4>[1] draw_color : register(b16);

struct VS_OUT
{
	float4 pos : SV_POSITION;
	float lum : TEXCOORD0;
};

VS_OUT main(float4 pos : POSITION, float lum : TEXCOORD)
{
	VS_OUT o;
	o.pos = draw_matrix * pos;
	o.lum = lum;
	return o;
}
)HLSL";

static const char* hlsl_shader_color_pos_lum_fragment = R"HLSL(
RootConstants<FLOAT4X4>[1] draw_matrix : register(b0);
RootConstants<FLOAT4>[1] draw_color : register(b16);

float4 main(float lum : TEXCOORD) : SV_Target
{
	return float4(draw_color.xyz, draw_color.w * lum);
}
)HLSL";

} // namespace ruis::render::directx
