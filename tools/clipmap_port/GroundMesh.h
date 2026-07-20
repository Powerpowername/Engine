#pragma once

#include "ClipmapMath.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Engine {

// DrawInfo 数组固定按 12 类 clipmap 拼块输出：
// 中心小块、普通网格块、横/竖补缝块、四条退化边、四个 L 形 trim 块。
inline constexpr uint32_t BLOCK_COUNT = 12u;

// 每个实例对应一次 DrawIndexedInstanced 中的一个 clipmap 拼块，就是trim，fixup这种
// 顶点缓冲只保存局部网格坐标，真正的世界位置由 shader 根据 offset/level 计算。
struct InstanceData {
    // 当前拼块在该层 clipmap 网格坐标里的起点偏移，不是世界坐标。
    DirectX::XMINT2 offset = MakeInt2(0, 0);
    // 第几层 clipmap：0 是最高精度近处层，层数越大采样间距越粗。
    uint32_t level = 0u;
    // 调试/可视化用的拼块类型编号，shader 目前只透传。
    uint32_t id = 0u;
};

static_assert(sizeof(InstanceData) == 16u, "InstanceData must match the shader input layout.");

struct DrawInfo {
    // 本类拼块使用的索引数量。
    uint32_t index_count = 0u;
    // 本类拼块在统一 index buffer 中的起始索引偏移。
    size_t index_buffer_offset = 0u;
    // 本帧真正可见、需要绘制的实例数量。
    uint32_t instance_count = 0u;
    // 本类拼块的实例数据在 instances 数组中的起始下标。
    size_t first_instance = 0u;
};

// 记录一种静态网格模板在共享顶点/索引缓冲中的位置。
struct Block {
    // 三角形索引起点。
    size_t offset = 0u;
    // 三角形索引数量。
    size_t count = 0u;
    // 该模板覆盖的局部网格范围，用来生成包围盒做视锥剔除。
    DirectX::XMUINT2 range = MakeUInt2(0u, 0u);
};

// 所有 clipmap 层共用同一套静态网格模板，运行时只改变实例 offset 和 level。
struct GroundMeshStaticMesh {
    // 中心 3x3 高精度小块。
    Block quadlet;
    // 环形区域里的普通 size x size 网格块。
    Block quad;
    // 竖向 3 格宽补缝条，填中心区域和环之间的缝。
    Block vertical;
    // 横向 3 格宽补缝条，填中心区域和环之间的缝。
    Block horizontal;
    // 四个 L 形 trim 块，用来根据相邻层偏移方向补齐环形缺口。
    Block trim_top_right;
    Block trim_bottom_right;
    Block trim_bottom_left;
    Block trim_top_left;
    // 四条退化边，把当前层边界和下一层粗网格衔接起来，避免裂缝。
    Block degenerate_left;
    Block degenerate_top;
    Block degenerate_right;
    Block degenerate_bottom;
    uint32_t index_count = 0u;
    std::vector<DirectX::XMUINT2> vertices;
    std::vector<uint16_t> indices;
};

class GroundMesh {
public:
    // size 控制单个普通块的分辨率，levels 控制 clipmap 层数，
    // clip_scale 是 level 0 的一个网格单位对应多少世界单位。
    GroundMesh(unsigned int size, unsigned int levels, float clip_scale);

    unsigned int get_size() const { return size; }
    unsigned int get_level_size() const { return level_size; }
    unsigned int get_levels() const { return levels; }
    float get_clipmap_scale() const { return clipmap_scale; }

    // 根据相机 XZ 位置重新计算每层环形网格的对齐偏移。
    void update_level_offsets(const DirectX::XMFLOAT2& camera_pos);
    const std::vector<DirectX::XMINT2>& get_level_offsets() const { return level_offsets; }
    // 从 view-projection 矩阵提取 6 个视锥平面，用于 CPU 侧剔除实例。
    void construct_frustum(DirectX::FXMMATRIX view_projection);
    // 地形高度范围参与包围盒计算，避免只按 y=0 平面剔除导致误删山体。
    void set_frustum_height_range(float min_height, float max_height);

    // 生成本帧可见实例列表和每类模板的 DrawInfo。
    void update_draw_list();
    const std::array<DrawInfo, BLOCK_COUNT>& get_draw_infos() const { return draw_infos; }
    const std::vector<InstanceData>& get_instance_data() const { return instances; }
    const GroundMeshStaticMesh& get_mesh() const { return static_mesh; }

private:
    // 描述一块局部规则网格，origin 是模板内起点，size 是顶点数量。
    struct Rect {
        uint32_t origin_x = 0u;
        uint32_t origin_z = 0u;
        uint32_t size_x = 0u;
        uint32_t size_z = 0u;
    };

    struct Aabb {
        DirectX::XMFLOAT3 min = MakeFloat3(0.0f, 0.0f, 0.0f);
        DirectX::XMFLOAT3 max = MakeFloat3(0.0f, 0.0f, 0.0f);
    };

    struct Frustum {
        std::array<DirectX::XMFLOAT4, 6> planes{};
    };

    using TrimConditional = bool (GroundMesh::*)(const DirectX::XMINT2& offset) const;

    static unsigned int clamp_mesh_size(unsigned int mesh_size);
    void setup_mesh();
    static uint32_t calculate_vertex_count(const Rect& rect);
    static uint32_t calculate_index_count(const Rect& rect);
    static DirectX::XMUINT2 calculate_range(const Rect& rect);
    uint32_t append_vertices(const Rect& rect);
    void append_indices(const Rect& rect, uint32_t vertex_buffer_offset);
    static int floor_div(int value, int divisor);
    static DirectX::XMINT2 idiv2(const DirectX::XMINT2& value, const DirectX::XMINT2& divisor);
    static bool intersect(const Frustum& current_frustum, const Aabb& box);

    DirectX::XMINT2 get_offset_level(const DirectX::XMFLOAT2& camera_pos, unsigned int level) const;
    bool intersects_frustum(const DirectX::XMINT2& offset, const DirectX::XMUINT2& range, uint32_t level) const;

    // 如果某个拼块通过视锥测试，就把它追加到本帧实例缓冲。
    void append_instance(DrawInfo& info, const InstanceData& instance);

    DrawInfo get_draw_info_quadlet();
    DrawInfo get_draw_info_quads();
    DrawInfo get_draw_info_vertical_fixup();
    DrawInfo get_draw_info_horizontal_fixup();
    DrawInfo get_draw_info_degenerate(
        const Block& block,
        const DirectX::XMINT2& offset,
        const DirectX::XMINT2& ring_offset,
        uint32_t id);
    DrawInfo get_draw_info_degenerate_left();
    DrawInfo get_draw_info_degenerate_right();
    DrawInfo get_draw_info_degenerate_top();
    DrawInfo get_draw_info_degenerate_bottom();
    DrawInfo get_draw_info_trim(const Block& block, TrimConditional cond);
    DrawInfo get_draw_info_trim_top_right();
    DrawInfo get_draw_info_trim_top_left();
    DrawInfo get_draw_info_trim_bottom_right();
    DrawInfo get_draw_info_trim_bottom_left();

    bool trim_top_right_cond(const DirectX::XMINT2& offset) const;
    bool trim_top_left_cond(const DirectX::XMINT2& offset) const;
    bool trim_bottom_right_cond(const DirectX::XMINT2& offset) const;
    bool trim_bottom_left_cond(const DirectX::XMINT2& offset) const;

    // 构造参数：size 是普通块边长，level_size 是整层环形缓存边长。
    unsigned int size = 0u;
    unsigned int level_size = 0u;
    unsigned int levels = 0u;
    // level 0 的网格间距，level n 的实际间距为 clipmap_scale * 2^n。
    float clipmap_scale = 1.0f;
    Frustum frustum;
    float frustum_min_height = -256.0f;
    float frustum_max_height = 256.0f;
    // 静态模板数据只创建一次，后续所有层通过实例化复用。
    GroundMeshStaticMesh static_mesh;
    // draw_infos 和 instances 是每帧更新的数据，供渲染端上传到 GPU。
    std::array<DrawInfo, BLOCK_COUNT> draw_infos{};
    std::vector<InstanceData> instances;
    // 每层 clipmap 的全局网格起点，shader 和 Heightmap 都依赖这个偏移对齐。
    std::vector<DirectX::XMINT2> level_offsets;
};

} // namespace Engine
