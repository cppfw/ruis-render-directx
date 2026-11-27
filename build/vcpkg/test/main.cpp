// this file is to test basic usage of the library,
// mainly test that linking to the library works, so just reference any
// non-inline function of the library to test it.

#include <ruis/render/directx/context.hpp>
#include <ruis/render/directx/device.hpp>

#include <functional>
#include <iostream>

int main(int argc, const char** argv){
    // reference a non-inline function of the library to test that linking works.
    // do not call it (would require a real D3D12 device and a window).
    std::function<void()> f = [](){
        auto d = utki::make_shared<ruis::render::directx::device>();
        (void)d;
    };

    if(!f){
        return 1;
    }

    std::cout << "Hello ruis-render-directx!" << std::endl;
    return 0;
}
