cmake_minimum_required(VERSION 3.17)


project(EnginePro)

add_executable(Engine WIN32)


target_sources(Engine PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/main.cpp
    ${CMAKE_SOURCE_DIR}/include/application.cpp
    ${CMAKE_SOURCE_DIR}/Core/auxi/PathUtils.cpp
    ${CMAKE_SOURCE_DIR}/Core/Auxi/Math.cpp
    ${CMAKE_SOURCE_DIR}/Core/Asset/ModelAsset.cpp
    ${CMAKE_SOURCE_DIR}/Core/Asset/ModelGpuResource.cpp
    ${CMAKE_SOURCE_DIR}/Core/Resource/LoadImage.cpp
    ${CMAKE_SOURCE_DIR}/importLib/stb/stb_image.cpp
    ${CMAKE_SOURCE_DIR}/tools/clipmap_port/GroundMesh.cpp
    ${CMAKE_SOURCE_DIR}/tools/clipmap_port/Heightmap.cpp
    ${CMAKE_SOURCE_DIR}/tools/clipmap_port/TerrainData.cpp
    ${CMAKE_SOURCE_DIR}/resource/shaders/appShader/appTerrian.hlsl
    ${CMAKE_SOURCE_DIR}/resource/shaders/appShader/appModel.hlsl)

set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resource/shaders/appShader/appTerrian.hlsl
    ${CMAKE_SOURCE_DIR}/resource/shaders/appShader/appModel.hlsl
    PROPERTIES
    HEADER_FILE_ONLY TRUE
)

target_compile_features(Engine PRIVATE cxx_std_23)
target_compile_definitions(Engine PUBLIC
    _ITERATOR_DEBUG_LEVEL=0
    CLIPMAP_LEVEL_NUM=10
    QUVIER_TREE_NUM_DEFAULT=3000000
    QUVIER_TREE_VISIBLE_DISTANCE=600.0f
)

set_property(TARGET Engine PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

target_compile_definitions(Engine PUBLIC
    _ITERATOR_DEBUG_LEVEL=0
)

target_compile_options(Engine PUBLIC /utf-8)

target_include_directories(Engine PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Core
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/importLib
    ${CMAKE_SOURCE_DIR}/importLib/imgui
    ${CMAKE_SOURCE_DIR}/importLib/open3d/3rdparty
    ${CMAKE_SOURCE_DIR}/importLib/directx
    ${CMAKE_SOURCE_DIR}/tools/clipmap_port
    )

target_link_libraries(Engine PUBLIC
    d3d12.lib
    dxgi.lib
    dxguid.lib
    d3dcompiler.lib
    user32.lib
    windowscodecs.lib
    ${CMAKE_SOURCE_DIR}/lib/shared/assimp-vc143-mt.lib
)

add_custom_command(TARGET Engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/lib/shared/assimp-vc143-mt.dll
        $<TARGET_FILE_DIR:Engine>
)
