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

#include "device.hpp"

#include <stdexcept>
#include <vector>

using namespace ruis::render::directx;

namespace {

D3D_FEATURE_LEVEL pick_feature_level(IDXGIFactory1* factory, IDXGIAdapter1* adapter)
{
	// D3D12CreateDevice() with pAdapter reports the highest supported feature
	// level through the output parameter
	D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_12_0;
	ID3D12Device* tmp = nullptr;
	const auto hr = D3D12CreateDevice(
		reinterpret_cast<IUnknown*>(adapter),
		D3D_FEATURE_LEVEL(12 * 10 + 1), // D3D_FEATURE_LEVEL_12_1
		IID_PPV_ARGS(&tmp)
	);
	if (SUCCEEDED(hr) && tmp != nullptr) {
		level = D3D_FEATURE_LEVEL_12_1;
		tmp->Release();
		return level;
	}
	{
		ID3D12Device* tmp0 = nullptr;
		const auto hr0 =
			D3D12CreateDevice(reinterpret_cast<IUnknown*>(adapter), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&tmp0));
		if (SUCCEEDED(hr0) && tmp0 != nullptr) {
			tmp0->Release();
			return D3D_FEATURE_LEVEL_12_0;
		}
	}
	(void)factory;
	return D3D_FEATURE_LEVEL_12_0; // not supported
}

} // namespace

device::device()
{
	IDXGIFactory1* factory = nullptr;
	check_hresult(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
	{
		std::vector<IDXGIAdapter1*> adapters;
		for (UINT i = 0;; ++i) {
			IDXGIAdapter1* adapter = nullptr;
			const auto hr = factory->EnumAdapters1(i, &adapter);
			if (hr == DXGI_ERROR_NOT_FOUND || adapter == nullptr) {
				break;
			}
			adapters.push_back(adapter);
		}

		ID3D12Device* dev = nullptr;
		D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_12_0;
		for (auto* adapter : adapters) {
			const auto adapter_level = pick_feature_level(factory, adapter);
			if (adapter_level >= D3D_FEATURE_LEVEL_12_0) {
				ID3D12Device* candidate = nullptr;
				const auto hr = D3D12CreateDevice(
					reinterpret_cast<IUnknown*>(adapter),
					adapter_level == D3D_FEATURE_LEVEL_12_1 ? D3D_FEATURE_LEVEL_12_1 : D3D_FEATURE_LEVEL_12_0,
					IID_PPV_ARGS(&candidate)
				);
				if (SUCCEEDED(hr) && candidate != nullptr) {
					dev = candidate;
					level = adapter_level;
					break;
				}
			}
		}
		for (auto* adapter : adapters) {
			adapter->Release();
		}
		if (dev == nullptr) {
			throw std::runtime_error("DirectX 12: no suitable adapter found");
		}
		this->dev = dev;
		(void)level;
	}
	factory->Release();

	// the shared command queue (all contexts and uploads submit to it)
	const auto desc = D3D12_COMMAND_QUEUE_DESC{
		.Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
		.Priority = 0,
		.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
		.NodeMask = 0
	};
	check_hresult(this->dev->CreateCommandQueue(&desc, IID_PPV_ARGS(&this->queue)), "CreateCommandQueue");
}

device::~device()
{
	if (this->queue != nullptr) {
		this->queue->Release();
	}
	// release the device last: it owns all the GPU objects
	if (this->dev != nullptr) {
		this->dev->Release();
	}
}
