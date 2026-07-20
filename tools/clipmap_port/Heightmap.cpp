#include "Heightmap.h"

#include <algorithm>
#include <cmath>

namespace Engine {

bool Heightmap::init(uint32_t clipmap_level_size, uint32_t clipmap_level_count) {
    cleanup();

    if (clipmap_level_size == 0u || clipmap_level_count == 0u) {
        return false;
    }

    // CPU 侧只维护每层环形缓存的“当前源坐标”和本帧更新矩形。
    // 真正的高度值不存这里，而是由 compute shader 从 TerrainData 上传的高度图采样。
    level_size = clipmap_level_size;
    level_count = std::min(clipmap_level_count, MAX_UPDATE_COUNT / 8u);
    level_infos.assign(level_count, HeightmapLevelInfo{});
    update_infos.reserve(MAX_UPDATE_COUNT);
    source_dirty = true;
    return true;
}

void Heightmap::cleanup() {
    level_size = 0u;
    level_count = 0u;
    source_resolution = 0;
    source_dirty = true;
    terrain_size = MakeFloat2(1.0f, 1.0f);
    terrain_center_offset = MakeFloat2(0.0f, 0.0f);
    level_infos.clear();
    update_infos.clear();
}

int32_t Heightmap::idiv(int32_t value, int32_t divisor) {
    const int32_t quotient = value / divisor;
    const int32_t remainder = value % divisor;
    // clipmap 可以跨到负坐标。普通 C++ 除法对 -1 / 32 得到 0，
    // 但环形分块需要 floor 语义得到 -1，否则负半轴会错一个缓存块。
    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) {
        return quotient - 1;
    }
    return quotient;
}

void Heightmap::sync_source_metadata(const TerrainData& terrain_data) {
    const DirectX::XMFLOAT2 new_size = terrain_data.GetSize();
    const DirectX::XMFLOAT2 new_offset = terrain_data.GetCenterOffset();
    // 源高度图的分辨率、世界尺寸或中心偏移变了，旧 clipmap 缓存就不能继续复用，
    // 下一次 update_level 会把每层当成未初始化状态重新刷一遍。
    if (source_resolution != terrain_data.GetResolution() ||
        terrain_size.x != new_size.x ||
        terrain_size.y != new_size.y ||
        terrain_center_offset.x != new_offset.x ||
        terrain_center_offset.y != new_offset.y) {
        source_resolution = terrain_data.GetResolution();
        terrain_size = new_size;
        terrain_center_offset = new_offset;
        source_dirty = true;
    }
}

void Heightmap::register_update_region(
    int32_t tex_x,
    int32_t tex_y,
    int32_t size_x,
    int32_t size_y,
    int32_t start_x,
    int32_t start_y,
    uint32_t level) {
    if (size_x <= 0 || size_y <= 0 || update_infos.size() >= MAX_UPDATE_COUNT) {
        return;
    }

    // tex 表示写到环形纹理的哪个位置；start 表示从源地形的哪个逻辑 texel 开始读。
    // 二者通常不相等，因为环形缓存会用取模后的 tex 坐标复用同一张 Texture2DArray。
    HeightmapUpdateInfo info;
    info.tex = MakeInt2(tex_x, tex_y);
    info.size = MakeInt2(size_x, size_y);
    info.start = MakeInt2(start_x, start_y);
    info.level = level;
    update_infos.push_back(info);
}

void Heightmap::update_level(const DirectX::XMINT2& offset, uint32_t level) {
    HeightmapLevelInfo& info = level_infos[level];
    const int32_t current_level_size = static_cast<int32_t>(level_size);
    const int32_t level_step = static_cast<int32_t>(1u << level);

    // GroundMesh 的 offset 是 level 0 网格单位；高度缓存每一层按自己的 texel 单位记录，
    // 所以这里要除以 2^level，把全局坐标转换成该层的源 texel 起点。
    const int32_t start_x = idiv(offset.x, level_step);
    const int32_t start_y = idiv(offset.y, level_step);

    if (start_x == info.x && start_y == info.y && !info.cleared) {
        return;
    }

    const int32_t delta_x = start_x - info.x;
    const int32_t delta_y = start_y - info.y;

    const int32_t old_base_x = idiv(info.x, current_level_size) * current_level_size;
    const int32_t old_base_y = idiv(info.y, current_level_size) * current_level_size;
    const int32_t base_x = idiv(start_x, current_level_size) * current_level_size;
    const int32_t base_y = idiv(start_y, current_level_size) * current_level_size;

    if (std::abs(delta_x) >= current_level_size || std::abs(delta_y) >= current_level_size || info.cleared) {
        // 相机移动超过整张环形缓存，或者该层首次使用：旧数据完全不可信。
        // 由于 start 可能落在环形纹理中间，整层刷新会被拆成四个矩形象限。
        const int32_t wrapped_x = start_x - base_x;
        const int32_t wrapped_y = start_y - base_y;

        register_update_region(0, 0, wrapped_x, wrapped_y, base_x + current_level_size, base_y + current_level_size, level);
        register_update_region(
            wrapped_x,
            0,
            current_level_size - wrapped_x,
            wrapped_y,
            start_x,
            base_y + current_level_size,
            level);
        register_update_region(
            0,
            wrapped_y,
            wrapped_x,
            current_level_size - wrapped_y,
            base_x + current_level_size,
            start_y,
            level);
        register_update_region(
            wrapped_x,
            wrapped_y,
            current_level_size - wrapped_x,
            current_level_size - wrapped_y,
            start_x,
            start_y,
            level);

        info.cleared = false;
    } else {
        // 小范围移动时只刷新“新露出来”的条带。
        // old_wrapped_* 是旧起点在环形纹理内的位置，wrapped_* 是新起点在环形纹理内的位置。
        const int32_t old_wrapped_x = info.x - old_base_x;
        const int32_t old_wrapped_y = info.y - old_base_y;
        const int32_t wrapped_x = start_x - base_x;
        const int32_t wrapped_y = start_y - base_y;

        // wrap_delta 表示环形纹理写入起点移动了多少。它和 delta 的符号组合
        // 决定是否发生回卷；发生回卷时，新条带会被拆成左右或上下两段。
        const int32_t wrap_delta_x = wrapped_x - old_wrapped_x;
        const int32_t wrap_delta_y = wrapped_y - old_wrapped_y;

        if (wrap_delta_x >= 0 && delta_x >= 0) {
            // 向 +X 移动且没有跨过环形边界：只刷新右侧新露出的竖条。
            register_update_region(old_wrapped_x, 0, wrap_delta_x, old_wrapped_y, info.x + current_level_size, old_base_y + current_level_size, level);
            register_update_region(
                old_wrapped_x,
                old_wrapped_y,
                wrap_delta_x,
                current_level_size - old_wrapped_y,
                info.x + current_level_size,
                info.y,
                level);
        } else if (wrap_delta_x < 0 && delta_x < 0) {
            // 向 -X 移动且没有跨过环形边界：只刷新左侧新露出的竖条。
            register_update_region(wrapped_x, 0, -wrap_delta_x, old_wrapped_y, start_x, old_base_y + current_level_size, level);
            register_update_region(
                wrapped_x,
                old_wrapped_y,
                -wrap_delta_x,
                current_level_size - old_wrapped_y,
                start_x,
                info.y,
                level);
        } else if (wrap_delta_x < 0 && delta_x >= 0) {
            // 逻辑上向 +X 移动，但环形写入位置回卷到了左侧，需要拆成左右两段。
            register_update_region(0, 0, wrapped_x, old_wrapped_y, base_x + current_level_size, old_base_y + current_level_size, level);
            register_update_region(
                old_wrapped_x,
                0,
                current_level_size - old_wrapped_x,
                old_wrapped_y,
                base_x + old_wrapped_x,
                old_base_y + current_level_size,
                level);
            register_update_region(
                0,
                old_wrapped_y,
                wrapped_x,
                current_level_size - old_wrapped_y,
                base_x + current_level_size,
                info.y,
                level);
            register_update_region(
                old_wrapped_x,
                old_wrapped_y,
                current_level_size - old_wrapped_x,
                current_level_size - old_wrapped_y,
                base_x + old_wrapped_x,
                info.y,
                level);
        } else if (wrap_delta_x >= 0 && delta_x < 0) {
            // 逻辑上向 -X 移动，但环形写入位置从左侧回卷到右侧，也要拆成两段。
            register_update_region(0, 0, old_wrapped_x, old_wrapped_y, base_x + current_level_size, old_base_y + current_level_size, level);
            register_update_region(wrapped_x, 0, current_level_size - wrapped_x, old_wrapped_y, start_x, old_base_y + current_level_size, level);
            register_update_region(
                0,
                old_wrapped_y,
                old_wrapped_x,
                current_level_size - old_wrapped_y,
                base_x + current_level_size,
                info.y,
                level);
            register_update_region(
                wrapped_x,
                old_wrapped_y,
                current_level_size - wrapped_x,
                current_level_size - old_wrapped_y,
                start_x,
                info.y,
                level);
        }

        if (wrap_delta_y >= 0 && delta_y >= 0) {
            // 向 +Y 移动且没有跨过环形边界：刷新下方新露出的横条。
            register_update_region(0, old_wrapped_y, wrapped_x, wrap_delta_y, base_x + current_level_size, info.y + current_level_size, level);
            register_update_region(
                wrapped_x,
                old_wrapped_y,
                current_level_size - wrapped_x,
                wrap_delta_y,
                start_x,
                info.y + current_level_size,
                level);
        } else if (wrap_delta_y < 0 && delta_y < 0) {
            // 向 -Y 移动且没有跨过环形边界：刷新上方新露出的横条。
            register_update_region(0, wrapped_y, wrapped_x, -wrap_delta_y, base_x + current_level_size, start_y, level);
            register_update_region(wrapped_x, wrapped_y, current_level_size - wrapped_x, -wrap_delta_y, start_x, start_y, level);
        } else if (wrap_delta_y < 0 && delta_y >= 0) {
            // 向 +Y 移动并发生环形回卷：横条被拆到纹理上下两侧。
            register_update_region(0, 0, wrapped_x, wrapped_y, base_x + current_level_size, base_y + current_level_size, level);
            register_update_region(
                0,
                old_wrapped_y,
                wrapped_x,
                current_level_size - old_wrapped_y,
                base_x + current_level_size,
                base_y + old_wrapped_y,
                level);
            register_update_region(wrapped_x, 0, current_level_size - wrapped_x, wrapped_y, start_x, base_y + current_level_size, level);
            register_update_region(
                wrapped_x,
                old_wrapped_y,
                current_level_size - wrapped_x,
                current_level_size - old_wrapped_y,
                start_x,
                base_y + old_wrapped_y,
                level);
        } else if (wrap_delta_y >= 0 && delta_y < 0) {
            // 向 -Y 移动并发生环形回卷：同样需要拆分到上下两段。
            register_update_region(0, 0, wrapped_x, old_wrapped_y, base_x + current_level_size, base_y + current_level_size, level);
            register_update_region(0, wrapped_y, wrapped_x, current_level_size - wrapped_y, base_x + current_level_size, start_y, level);
            register_update_region(wrapped_x, 0, current_level_size - wrapped_x, old_wrapped_y, start_x, base_y + current_level_size, level);
            register_update_region(
                wrapped_x,
                wrapped_y,
                current_level_size - wrapped_x,
                current_level_size - wrapped_y,
                start_x,
                start_y,
                level);
        }
    }

    info.x = start_x;
    info.y = start_y;
}

const std::vector<HeightmapUpdateInfo>& Heightmap::update(
    const TerrainData& terrain_data,
    const std::vector<DirectX::XMINT2>& level_offsets,
    float /*clipmap_scale*/) {
    update_infos.clear();
    if (level_size == 0u || level_count == 0u || !terrain_data.IsValid()) {
        return update_infos;
    }

    sync_source_metadata(terrain_data);

    // level_offsets 由 GroundMesh 决定，保证网格实例和高度缓存采样对齐。
    // 这里只负责把每层偏移变化转换成矩形更新任务；clipmap_scale 会在 GPU 常量里参与世界坐标换算。
    const uint32_t update_level_count = std::min<uint32_t>(level_count, static_cast<uint32_t>(level_offsets.size()));
    for (uint32_t i = 0u; i < update_level_count; ++i) {
        update_level(level_offsets[i], i);
    }

    return update_infos;
}

} // namespace Engine
