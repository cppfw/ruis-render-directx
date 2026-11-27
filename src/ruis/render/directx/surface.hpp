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

#include <memory>

#include <utki/windows.hpp>

#include "device.hpp"
#include "directx.hpp"
#include "sdk_prelude.hpp"

namespace ruis::render::directx {

/**
 * @brief A rendering surface: the (optional) window handle the context
 * presents to.
 *
 * Unlike Vulkan (where the surface is a platform-specific object created from
 * the window handle), DirectX 12 presents through a DXGI swapchain which the
 * render context creates from the window handle. This class just holds the
 * window handle (or none, for headless contexts) and a reference to the shared
 * device.
 */
class surface
{
public:
	surface(const device& dev, HWND hwnd);
	~surface();

	surface(const surface&) = delete;
	surface& operator=(const surface&) = delete;
	surface(surface&&) = delete;
	surface& operator=(surface&&) = delete;

	const device& get_device() const
	{
		return *this->dev;
	}

	bool has_surface() const
	{
		return this->hwnd != nullptr;
	}

	HWND get_hwnd() const
	{
		return this->hwnd;
	}

private:
	const device* dev = nullptr;
	HWND hwnd = nullptr;
};

} // namespace ruis::render::directx
