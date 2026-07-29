cmake_minimum_required(VERSION 3.17)
project(SkyBox)

add_executable(SkyBox WIN32
    main.cpp
    SkyBoxDemo.cpp
    ../../Core/Render/SkyBox.cpp
    ../../Core/Resource/LoadImage.cpp
    ../../Core/Resource/Texture.cpp
    ../../importLib/stb/stb_image.cpp
)

target_compile_features(SkyBox PRIVATE cxx_std_23)
set_property(TARGET SkyBox PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

target_compile_definitions(SkyBox PRIVATE
    _ITERATOR_DEBUG_LEVEL=0
)

target_compile_options(SkyBox PRIVATE /utf-8)

target_include_directories(SkyBox PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core
    ${CMAKE_CURRENT_SOURCE_DIR}/../../importLib
    ${CMAKE_CURRENT_SOURCE_DIR}/../../importLib/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/../../importLib/open3d/3rdparty
    ${CMAKE_CURRENT_SOURCE_DIR}/../../importLib/directx
)

target_link_libraries(SkyBox PRIVATE
    d3d12.lib
    dxgi.lib
    dxguid.lib
    d3dcompiler.lib
    user32.lib
    windowscodecs.lib
    ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/shared/tbb12.lib
    ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/shared/Open3D.lib
)

add_custom_command(TARGET SkyBox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/shared/Open3D.dll
        $<TARGET_FILE_DIR:SkyBox>/Open3D.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/shared/tbb12.dll
        $<TARGET_FILE_DIR:SkyBox>/tbb12.dll
)
