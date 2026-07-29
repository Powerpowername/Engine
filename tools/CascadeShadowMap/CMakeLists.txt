cmake_minimum_required(VERSION 3.17)
project(CascadeShadowMap)

add_executable(CascadeShadowMap WIN32
    cascadeMain.cpp
    CascadeTestPro.cpp
)

target_compile_features(CascadeShadowMap PRIVATE cxx_std_23)

target_include_directories(CascadeShadowMap PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core
    ${CMAKE_CURRENT_SOURCE_DIR}/../../importLib
    ${CMAKE_CURRENT_SOURCE_DIR}/../../importLib/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/../../importLib/open3d/3rdparty
    ${CMAKE_CURRENT_SOURCE_DIR}/../../importLib/directx)

target_link_libraries(CascadeShadowMap PRIVATE
    d3d12.lib
    dxgi.lib
    dxguid.lib
    d3dcompiler.lib
    user32.lib)

