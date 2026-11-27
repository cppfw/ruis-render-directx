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

#include "directx.hpp"

#include <cstring>
#include <utility>

namespace ruis::render::directx {

std::vector<uint8_t> compile_hlsl(const char* source, const char* entry_point, const char* profile)
{
	ID3DBlob* blob = nullptr;
	ID3DBlob* err_blob = nullptr;
	const auto hr = D3DCompile(
		source,
		strlen(source),
		"shader.hlsl",
		nullptr,
		nullptr,
		entry_point,
		profile,
		0,
		0,
		&blob,
		&err_blob
	);
	if (FAILED(hr)) {
		const auto err = err_blob != nullptr ? reinterpret_cast<const char*>(err_blob->GetBufferPointer()) : "";
		const auto msg = std::string("HLSL compilation failed (") + entry_point + " " + profile + "): " + err;
		if (err_blob != nullptr) {
			err_blob->Release();
		}
		throw std::runtime_error(msg);
	}

	const auto size = blob->GetBufferSize();
	std::vector<uint8_t> bytecode(size);
	std::copy_n(static_cast<const uint8_t*>(blob->GetBufferPointer()), size, bytecode.begin());
	blob->Release();
	if (err_blob != nullptr) {
		err_blob->Release();
	}
	return bytecode;
}

} // namespace ruis::render::directx
