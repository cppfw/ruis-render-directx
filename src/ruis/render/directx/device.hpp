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

#include "directx.hpp"
#include "sdk_prelude.hpp"

namespace ruis::render::directx {

/**
 * @brief Shared DirectX 12 device (ID3D12Device + the shared command queue).
 *
 * Created once per process and shared by all render factories and contexts,
 * so that GPU objects created for one window can be used by any other window.
 */
class device
{
public:
	~device();

	device(const device&) = delete;
	device& operator=(const device&) = delete;
	device(device&&) = delete;
	device& operator=(device&&) = delete;

	// create the device and the shared command queue
	device();

	ID3D12Device* get_device() const
	{
		return this->dev;
	}

	ID3D12CommandQueue* get_queue() const
	{
		return this->queue;
	}

private:
	ID3D12Device* dev = nullptr;
	ID3D12CommandQueue* queue = nullptr;
};

} // namespace ruis::render::directx
