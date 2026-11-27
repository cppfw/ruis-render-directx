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

// The Windows SDK headers (<d3d12.h>, <dxgi.h>) declare interface IIDs as
// `const GUID far <name>;`, relying on `far` and `near` being empty macros
// (defined by <minwindef.h>). <r4/matrix.hpp> undefines these macros (the
// r4::matrix class uses `far` and `near` as member names), which makes the
// SDK headers fail to parse if <r4/matrix.hpp> is included first. Re-define
// the macros for the duration of the SDK includes, and restore the previous
// state afterwards.
//
// The Windows SDK also defines the `max` and `min` function-like macros
// (<minwindef.h>), which break the C++ code using e.g. `std::numeric_limits<>::max()`.
// Undefine them after the SDK includes, following <utki/windows.hpp>.

#ifndef far
#	define far
#	define ruis_render_directx_far_redefined
#endif
#ifndef near
#	define near
#	define ruis_render_directx_near_redefined
#endif

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>

#ifdef ruis_render_directx_far_redefined
#	undef far
#	undef ruis_render_directx_far_redefined
#endif
#ifdef ruis_render_directx_near_redefined
#	undef near
#	undef ruis_render_directx_near_redefined
#endif

#ifdef max
#	undef max
#endif
#ifdef min
#	undef min
#endif
#ifdef DELETE
#	undef DELETE
#endif
