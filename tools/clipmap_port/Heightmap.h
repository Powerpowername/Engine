#pragma once

#include "ClipmapMath.h"
#include "TerrainData.h"

#include <cstdint>
#include <vector>

namespace Engine {

// 记录某一层 clipmap 高度缓存当前对应的源地形网格起点。
// x/y 用“当前层自己的 texel 单位”表示，cleared 表示这一层还没完整初始化。
struct HeightmapLevelInfo {
    int32_t x = 0;
    int32_t y = 0;
    bool cleared = true;
};

// 一次 GPU compute 更新任务。CPU 只计算哪些区域需要刷新，
// 具体采样源高度图、写入 Texture2DArray 的工作由 HLSL compute shader 完成。
struct HeightmapUpdateInfo {
    // 写入 clipmap 环形纹理的 texel 起点。
    DirectX::XMINT2 tex = MakeInt2(0, 0);
    // 本次更新区域的宽高。
    DirectX::XMINT2 size = MakeInt2(0, 0);
    // 从源地形/世界网格读取的起点，仍然是当前 level 的 texel 坐标。
    DirectX::XMINT2 start = MakeInt2(0, 0);
    // 要更新 Texture2DArray 的哪一层。
    uint32_t level = 0;
    uint32_t padding0 = 0.0f;
};

static_assert(sizeof(HeightmapUpdateInfo) == 32u, "HeightmapUpdateInfo must match HLSL layout.");

class Heightmap {
public:
    // HLSL 常量缓冲里为更新任务预留了 80 个槽位。
    // 每层最坏情况会拆成最多 8 个矩形，所以可安全支持 10 层 clipmap。
    // 是更新矩形
    static constexpr uint32_t MAX_UPDATE_COUNT = 80u;

    // 初始化 CPU 侧的环形缓存状态。这里不创建 GPU 资源，只记录尺寸和层数。
    bool init(uint32_t clipmap_level_size, uint32_t clipmap_level_count);
    void cleanup();

    // 根据 GroundMesh 计算出的 level_offsets，生成本帧需要刷新的矩形列表。
    const std::vector<HeightmapUpdateInfo>& update(
        const TerrainData& terrain_data,
        const std::vector<DirectX::XMINT2>& level_offsets,
        float clipmap_scale);

    // 源高度图上传到 GPU 后清除 dirty 标记；如果 TerrainData 尺寸/分辨率变了会重新置位。
    void acknowledge_source_upload() { source_dirty = false; }
    bool is_source_dirty() const { return source_dirty; }

    uint32_t get_level_size() const { return level_size; }
    uint32_t get_level_count() const { return level_count; }
    int get_source_resolution() const { return source_resolution; }
    const DirectX::XMFLOAT2& get_terrain_size() const { return terrain_size; }
    const DirectX::XMFLOAT2& get_terrain_center_offset() const { return terrain_center_offset; }
    const std::vector<HeightmapUpdateInfo>& get_update_infos() const { return update_infos; }

private:
    // 带向下取整语义的整数除法。普通 C++ 负数除法向 0 截断，
    // clipmap 偏移允许为负，必须用 floor 才能保证环形分块正确。
    static int32_t idiv(int32_t value, int32_t divisor);

    // 同步源地形的分辨率、世界尺寸和中心偏移，变化时强制全量刷新。
    void sync_source_metadata(const TerrainData& terrain_data);
    // 把一个待刷新的矩形区域加入 update_infos，空区域和超过上限的任务会被忽略。
    void register_update_region(
        int32_t tex_x,
        int32_t tex_y,
        int32_t size_x,
        int32_t size_y,
        int32_t start_x,
        int32_t start_y,
        uint32_t level);

    // 更新单个 level 的环形缓存状态，并把移动后露出的新区域拆成若干矩形。
    void update_level(const DirectX::XMINT2& offset, uint32_t level);

    // 每层环形 Texture2D 的边长，对应 GroundMesh::level_size。
    uint32_t level_size = 0u;
    uint32_t level_count = 0u;
    // 源 TerrainData 高度图分辨率。
    int source_resolution = 0;
    // true 表示源高度图数据需要重新上传或所有层需要重新采样。
    bool source_dirty = true;
    // 源地形的世界尺寸和中心偏移，用于 compute shader 做 world -> uv 换算。
    DirectX::XMFLOAT2 terrain_size = MakeFloat2(1.0f, 1.0f);
    DirectX::XMFLOAT2 terrain_center_offset = MakeFloat2(0.0f, 0.0f);
    // 每层上一次对齐到的源 texel 起点。
    std::vector<HeightmapLevelInfo> level_infos;
    // 本帧提交给 GPU compute shader 的更新矩形列表。
    std::vector<HeightmapUpdateInfo> update_infos;
};

} // namespace Engine
