cmake_minimum_required(VERSION 3.17)
project(Model)

add_executable(Model WIN32
    main.cpp
    ModelDemo.cpp
    ../../Core/Asset/ModelAsset.cpp
    ../../Core/Asset/ModelGpuResource.cpp
    ../../Core/Resource/LoadImage.cpp
    ../../Core/Resource/Texture.cpp
    ../../importLib/stb/stb_image.cpp
)

target_compile_features(Model PRIVATE cxx_std_23)
set_property(TARGET Model PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

target_compile_definitions(Model PRIVATE
    _ITERATOR_DEBUG_LEVEL=0
)

target_compile_options(Model PRIVATE /utf-8)

target_include_directories(Model PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core
    ${CMAKE_CURRENT_SOURCE_DIR}/../../importLib
    ${CMAKE_CURRENT_SOURCE_DIR}/../../importLib/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/../../importLib/open3d/3rdparty
    ${CMAKE_CURRENT_SOURCE_DIR}/../../importLib/directx
)

target_link_libraries(Model PRIVATE
    d3d12.lib
    dxgi.lib
    dxguid.lib
    d3dcompiler.lib
    user32.lib
    windowscodecs.lib
    ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/shared/assimp-vc143-mt.lib
    ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/shared/tbb12.lib
    ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/shared/Open3D.lib
)

add_custom_command(TARGET Model POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/shared/Open3D.dll
        $<TARGET_FILE_DIR:Model>/Open3D.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/shared/tbb12.dll
        $<TARGET_FILE_DIR:Model>/tbb12.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/shared/assimp-vc143-mt.dll
        $<TARGET_FILE_DIR:Model>/assimp-vc143-mt.dll
)
