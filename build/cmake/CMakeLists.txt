cmake_minimum_required(VERSION 3.30)

project(ruis-render-directx)

set(dir_name ruis)

# !!! find_package must go after project() declaration !!!
# Otherwise VCPKG does not set the CMAKE_PREFIX_PATH to find packages.
find_package(myci CONFIG REQUIRED)

set(srcs)
myci_add_source_files(srcs
    DIRECTORY
        ../../src/${dir_name}
    RECURSIVE
)

if(NOT WIN32)
    # the DirectX 12 renderer is Windows-only
    message(STATUS "ruis-render-directx: skipping, it is Windows-only")
    return()
endif()

myci_declare_library(${PROJECT_NAME}
    SOURCES
        ${srcs}
    PUBLIC_INCLUDE_DIRECTORIES
        ../../src
    INSTALL_INCLUDE_DIRECTORIES
        ../../src/${dir_name}
    DEPENDENCIES
        rasterimage
        r4
        utki
        ruis
)

# link the Windows SDK DirectX 12 libraries (not available via vcpkg)
target_link_libraries(${PROJECT_NAME} PRIVATE d3d12 dxgi d3dcompiler)
