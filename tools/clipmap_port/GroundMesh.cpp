#include "GroundMesh.h"

#include <algorithm>
#include <cmath>

namespace Engine {

namespace {

// 平面方程 ax + by + cz + d 的有符号距离。
// 这里约定 > 0 表示点在视锥外侧，后面的 AABB 测试会按这个约定剔除。
float DotPlane(const DirectX::XMFLOAT4& plane, const DirectX::XMFLOAT3& point) {
    return plane.x * point.x + plane.y * point.y + plane.z * point.z + plane.w;
}

} // namespace

unsigned int GroundMesh::clamp_mesh_size(unsigned int mesh_size) {
    return std::clamp(mesh_size, 2u, 64u);
}

GroundMesh::GroundMesh(unsigned int mesh_size, unsigned int level_count, float clip_scale)
    : size(clamp_mesh_size(mesh_size)),
      // 一整层 clipmap 环形缓存的边长。terrain3 原实现用 4*size-1，
      // 这样中心 3x3 小块、四周 4x4 普通块和补缝块能刚好拼成闭合环。
      level_size(4u * clamp_mesh_size(mesh_size) - 1u),
      levels(std::max(1u, level_count)),
      clipmap_scale(std::max(0.0001f, clip_scale)) {
    // 静态网格模板只依赖 size，和相机位置、层级数量无关，构造时生成一次即可。
    setup_mesh();
    level_offsets.assign(levels, MakeInt2(0, 0));
    update_level_offsets(MakeFloat2(0.0f, 0.0f));
}

uint32_t GroundMesh::calculate_vertex_count(const Rect& rect) {
    return rect.size_x * rect.size_z;
}

uint32_t GroundMesh::calculate_index_count(const Rect& rect) {
    if (rect.size_x < 2u || rect.size_z < 2u) {
        return 0u;
    }

    const uint32_t strip_length = rect.size_x;
    const uint32_t strip_count = rect.size_z - 1u;
    return strip_count * (2u * strip_length + 1u);
}

DirectX::XMUINT2 GroundMesh::calculate_range(const Rect& rect) {
    return MakeUInt2(std::max(1u, rect.size_x) - 1u, std::max(1u, rect.size_z) - 1u);
}

uint32_t GroundMesh::append_vertices(const Rect& rect) {
    const uint32_t vertex_buffer_offset = static_cast<uint32_t>(static_mesh.vertices.size());
    const uint32_t end_x = rect.origin_x + rect.size_x;
    const uint32_t end_z = rect.origin_z + rect.size_z;

    for (uint32_t z = rect.origin_z; z < end_z; ++z) {
        for (uint32_t x = rect.origin_x; x < end_x; ++x) {
            static_mesh.vertices.push_back(MakeUInt2(x, z));
        }
    }

    return vertex_buffer_offset;
}

void GroundMesh::append_indices(const Rect& rect, uint32_t vertex_buffer_offset) {
    if (rect.size_x < 2u || rect.size_z < 2u) {
        return;
    }

    const uint32_t strip_length = rect.size_x;
    const uint32_t strip_count = rect.size_z - 1u;
    int pos = static_cast<int>(vertex_buffer_offset);

    // 每一行用 triangle strip 画两个三角形带，行尾插入 UINT16_MAX 作为 strip restart。
    // 这样所有规则网格块可以放进同一个 index buffer，渲染时只切换 offset/count。
    for (uint32_t strip = 0u; strip < strip_count; ++strip) {
        for (uint32_t i = 0u; i < 2u * strip_length; ++i) {
            static_mesh.indices.push_back(static_cast<uint16_t>(pos));
            pos += (i & 1u) != 0u ? 1 - static_cast<int>(strip_length) : static_cast<int>(strip_length);
        }
        static_mesh.indices.push_back(UINT16_MAX);
    }
}

void GroundMesh::setup_mesh() {
    static_mesh = {};

    // 下面这些 Rect 是“模板坐标”，不是世界坐标：
    // quadlet 负责最中心高精度 3x3，quad 是普通网格块，vertical/horizontal 是中心十字补缝，
    // trim_* 和 degenerate_* 用来连接相邻 LOD 层，避免不同采样间距之间出现裂缝。
    const Rect quadlet{0u, 0u, 3u, 3u};
    const Rect quad{0u, 0u, size, size};
    const Rect vertical{0u, 0u, 3u, size};
    const Rect horizontal{0u, 0u, size, 3u};
    const Rect trim_top{0u, 0u, 2u * size + 1u, 2u};
    const Rect trim_right{2u * size - 1u, 0u, 2u, 2u * size + 1u};
    const Rect trim_bottom{0u, 2u * size - 1u, 2u * size + 1u, 2u};
    const Rect trim_left{0u, 0u, 2u, 2u * size + 1u};

    const uint32_t trim_vertices = calculate_vertex_count(trim_top);
    const uint32_t degenerate_vertices = ((size - 1u) * 2u + 1u) * 5u;
    static_mesh.vertices.reserve(
        calculate_vertex_count(quadlet) + calculate_vertex_count(quad) +
        calculate_vertex_count(vertical) + calculate_vertex_count(horizontal) +
        trim_vertices * 4u + degenerate_vertices * 2u);

    const uint32_t quadlet_vertices_offset = append_vertices(quadlet);
    const uint32_t quad_vertices_offset = append_vertices(quad);
    const uint32_t vertical_vertices_offset = append_vertices(vertical);
    const uint32_t horizontal_vertices_offset = append_vertices(horizontal);
    const uint32_t trim_top_vertices_offset = append_vertices(trim_top);
    const uint32_t trim_right_vertices_offset = append_vertices(trim_right);
    const uint32_t trim_bottom_vertices_offset = append_vertices(trim_bottom);
    const uint32_t trim_left_vertices_offset = append_vertices(trim_left);

    const uint32_t degenerate_x_vertices_offset = static_cast<uint32_t>(static_mesh.vertices.size());
    for (uint32_t z = 0u; z < (size - 1u) * 2u + 1u; ++z) {
        // 退化边通过重复端点制造零面积三角形，让高精度边界能顺滑过渡到低精度边界。
        static_mesh.vertices.push_back(MakeUInt2(0u, z * 2u));
        static_mesh.vertices.push_back(MakeUInt2(0u, z * 2u));
        static_mesh.vertices.push_back(MakeUInt2(0u, z * 2u + 1u));
        static_mesh.vertices.push_back(MakeUInt2(0u, z * 2u + 2u));
        static_mesh.vertices.push_back(MakeUInt2(0u, z * 2u + 2u));
    }

    const uint32_t degenerate_z_vertices_offset = static_cast<uint32_t>(static_mesh.vertices.size());
    for (uint32_t x = 0u; x < (size - 1u) * 2u + 1u; ++x) {
        static_mesh.vertices.push_back(MakeUInt2(x * 2u, 0u));
        static_mesh.vertices.push_back(MakeUInt2(x * 2u, 0u));
        static_mesh.vertices.push_back(MakeUInt2(x * 2u + 1u, 0u));
        static_mesh.vertices.push_back(MakeUInt2(x * 2u + 2u, 0u));
        static_mesh.vertices.push_back(MakeUInt2(x * 2u + 2u, 0u));
    }

    static_mesh.quadlet.count = calculate_index_count(quadlet);
    static_mesh.quadlet.range = calculate_range(quadlet);
    static_mesh.quad.count = calculate_index_count(quad);
    static_mesh.quad.range = calculate_range(quad);
    static_mesh.vertical.count = calculate_index_count(vertical);
    static_mesh.vertical.range = calculate_range(vertical);
    static_mesh.horizontal.count = calculate_index_count(horizontal);
    static_mesh.horizontal.range = calculate_range(horizontal);

    const uint32_t top_count = calculate_index_count(trim_top);
    const DirectX::XMUINT2 top_range = calculate_range(trim_top);
    const uint32_t right_count = calculate_index_count(trim_right);
    const DirectX::XMUINT2 right_range = calculate_range(trim_right);
    const uint32_t bottom_count = calculate_index_count(trim_bottom);
    const DirectX::XMUINT2 bottom_range = calculate_range(trim_bottom);
    const uint32_t left_count = calculate_index_count(trim_left);
    const DirectX::XMUINT2 left_range = calculate_range(trim_left);

    static_mesh.trim_top_right.count = top_count + right_count;
    static_mesh.trim_top_right.range = Max(top_range, right_range);
    static_mesh.trim_bottom_right.count = bottom_count + right_count;
    static_mesh.trim_bottom_right.range = Max(bottom_range, right_range);
    static_mesh.trim_bottom_left.count = bottom_count + left_count;
    static_mesh.trim_bottom_left.range = Max(bottom_range, left_range);
    static_mesh.trim_top_left.count = top_count + left_count;
    static_mesh.trim_top_left.range = Max(top_range, left_range);

    const uint32_t degenerate_count = ((size - 1u) * 2u + 1u) * 6u;
    static_mesh.degenerate_left.count = degenerate_count;
    static_mesh.degenerate_left.range = MakeUInt2(0u, level_size - 1u);
    static_mesh.degenerate_right.count = degenerate_count;
    static_mesh.degenerate_right.range = MakeUInt2(0u, level_size - 1u);
    static_mesh.degenerate_top.count = degenerate_count;
    static_mesh.degenerate_top.range = MakeUInt2(level_size - 1u, 0u);
    static_mesh.degenerate_bottom.count = degenerate_count;
    static_mesh.degenerate_bottom.range = MakeUInt2(level_size - 1u, 0u);

    static_mesh.quadlet.offset = static_mesh.indices.size();
    append_indices(quadlet, quadlet_vertices_offset);
    static_mesh.quad.offset = static_mesh.indices.size();
    append_indices(quad, quad_vertices_offset);
    static_mesh.vertical.offset = static_mesh.indices.size();
    append_indices(vertical, vertical_vertices_offset);
    static_mesh.horizontal.offset = static_mesh.indices.size();
    append_indices(horizontal, horizontal_vertices_offset);

    static_mesh.trim_top_right.offset = static_mesh.indices.size();
    append_indices(trim_top, trim_top_vertices_offset);
    append_indices(trim_right, trim_right_vertices_offset);
    // 四个 trim 块都是由两条窄边组合成 L 形，只是边的组合方向不同。
    static_mesh.trim_bottom_right.offset = static_mesh.indices.size();
    append_indices(trim_right, trim_right_vertices_offset);
    append_indices(trim_bottom, trim_bottom_vertices_offset);
    static_mesh.trim_bottom_left.offset = static_mesh.indices.size();
    append_indices(trim_bottom, trim_bottom_vertices_offset);
    append_indices(trim_left, trim_left_vertices_offset);
    static_mesh.trim_top_left.offset = static_mesh.indices.size();
    append_indices(trim_left, trim_left_vertices_offset);
    append_indices(trim_top, trim_top_vertices_offset);

    static_mesh.degenerate_left.offset = static_mesh.indices.size();
    for (uint32_t z = 0u; z < (size - 1u) * 2u + 1u; ++z) {
        const uint16_t base = static_cast<uint16_t>(degenerate_x_vertices_offset + 5u * z);
        static_mesh.indices.insert(static_mesh.indices.end(), {
            base, static_cast<uint16_t>(base + 1u), static_cast<uint16_t>(base + 2u),
            static_cast<uint16_t>(base + 3u), static_cast<uint16_t>(base + 4u),
            static_cast<uint16_t>(base + 4u)});
    }

    static_mesh.degenerate_right.offset = static_mesh.indices.size();
    const uint32_t start_z = (size - 1u) * 2u;
    for (uint32_t z = 0u; z < (size - 1u) * 2u + 1u; ++z) {
        const uint16_t base = static_cast<uint16_t>(degenerate_x_vertices_offset + 5u * (start_z - z));
        static_mesh.indices.insert(static_mesh.indices.end(), {
            static_cast<uint16_t>(base + 4u), static_cast<uint16_t>(base + 3u),
            static_cast<uint16_t>(base + 2u), static_cast<uint16_t>(base + 1u), base, base});
    }

    static_mesh.degenerate_top.offset = static_mesh.indices.size();
    const uint32_t start_x = (size - 1u) * 2u;
    for (uint32_t x = 0u; x < (size - 1u) * 2u + 1u; ++x) {
        const uint16_t base = static_cast<uint16_t>(degenerate_z_vertices_offset + 5u * (start_x - x));
        static_mesh.indices.insert(static_mesh.indices.end(), {
            static_cast<uint16_t>(base + 4u), static_cast<uint16_t>(base + 3u),
            static_cast<uint16_t>(base + 2u), static_cast<uint16_t>(base + 1u), base, base});
    }

    static_mesh.degenerate_bottom.offset = static_mesh.indices.size();
    for (uint32_t x = 0u; x < (size - 1u) * 2u + 1u; ++x) {
        const uint16_t base = static_cast<uint16_t>(degenerate_z_vertices_offset + 5u * x);
        static_mesh.indices.insert(static_mesh.indices.end(), {
            base, static_cast<uint16_t>(base + 1u), static_cast<uint16_t>(base + 2u),
            static_cast<uint16_t>(base + 3u), static_cast<uint16_t>(base + 4u),
            static_cast<uint16_t>(base + 4u)});
    }

    static_mesh.index_count = static_cast<uint32_t>(static_mesh.indices.size());
}

int GroundMesh::floor_div(int value, int divisor) {
    const int quotient = value / divisor;
    const int remainder = value % divisor;
    // C++ 的整数除法对负数会向 0 截断。clipmap 可以移动到负世界坐标，
    // 如果这里不用 floor 语义，跨过 0 时环形缓存会错一格。
    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) {
        return quotient - 1;
    }
    return quotient;
}

DirectX::XMINT2 GroundMesh::idiv2(const DirectX::XMINT2& value, const DirectX::XMINT2& divisor) {
    return MakeInt2(floor_div(value.x, divisor.x), floor_div(value.y, divisor.y));
}

DirectX::XMINT2 GroundMesh::get_offset_level(const DirectX::XMFLOAT2& camera_pos, unsigned int level) const {
    // 先把相机世界坐标转换为 level 0 网格坐标，再按当前层需要的步长吸附。
    const DirectX::XMFLOAT2 scaled_pos_f = Div(camera_pos, clipmap_scale);
    const DirectX::XMINT2 scaled_pos = MakeInt2(
        static_cast<int>(std::floor(scaled_pos_f.x)),
        static_cast<int>(std::floor(scaled_pos_f.y)));

    // 关键点：第 level 层要吸附到下一层的网格间距 2^(level+1)。
    // 这样相邻两层的采样点始终落在同一套粗网格上，trim/degenerate 才能无缝对齐。
    const int next_level_res = static_cast<int>(1u << (level + 1u));
    const DirectX::XMINT2 snapped_pos = Mul(idiv2(scaled_pos, MakeInt2(next_level_res, next_level_res)), next_level_res);
    const int level_half_size = static_cast<int>((size - 1u) * (1u << (level + 1u)));
    // offset 表示该层左上角在全局网格中的起点，减去半边长后让相机落在环中心附近。
    return Sub(snapped_pos, MakeInt2(level_half_size, level_half_size));
}

void GroundMesh::update_level_offsets(const DirectX::XMFLOAT2& camera_pos) {
    level_offsets.resize(levels);
    for (unsigned int i = 0u; i < levels; ++i) {
        level_offsets[i] = get_offset_level(camera_pos, i);
    }
}

void GroundMesh::set_frustum_height_range(float min_height, float max_height) {
    frustum_min_height = std::min(min_height, max_height);
    frustum_max_height = std::max(min_height, max_height);
    if (std::abs(frustum_max_height - frustum_min_height) < 0.001f) {
        frustum_min_height -= 0.5f;
        frustum_max_height += 0.5f;
    }
}

void GroundMesh::construct_frustum(DirectX::FXMMATRIX view_projection) {
    DirectX::XMFLOAT4X4 matrix = {};
    DirectX::XMStoreFloat4x4(&matrix, view_projection);

    const DirectX::XMFLOAT4 m0 = MakeFloat4(matrix._11, matrix._12, matrix._13, matrix._14);
    const DirectX::XMFLOAT4 m1 = MakeFloat4(matrix._21, matrix._22, matrix._23, matrix._24);
    const DirectX::XMFLOAT4 m2 = MakeFloat4(matrix._31, matrix._32, matrix._33, matrix._34);
    const DirectX::XMFLOAT4 m3 = MakeFloat4(matrix._41, matrix._42, matrix._43, matrix._44);

    // 从 VP 矩阵提取左右、上下、近远 6 个平面。这里取负号是为了配合 DotPlane：
    // AABB 八个点全部 > 0 时代表这个盒子完全在该平面外侧。
    frustum.planes[0] = MakeFloat4(-(m3.x + m0.x), -(m3.y + m0.y), -(m3.z + m0.z), -(m3.w + m0.w));
    frustum.planes[1] = MakeFloat4(-(m3.x - m0.x), -(m3.y - m0.y), -(m3.z - m0.z), -(m3.w - m0.w));
    frustum.planes[2] = MakeFloat4(-(m3.x + m1.x), -(m3.y + m1.y), -(m3.z + m1.z), -(m3.w + m1.w));
    frustum.planes[3] = MakeFloat4(-(m3.x - m1.x), -(m3.y - m1.y), -(m3.z - m1.z), -(m3.w - m1.w));
    frustum.planes[4] = MakeFloat4(-(m3.x + m2.x), -(m3.y + m2.y), -(m3.z + m2.z), -(m3.w + m2.w));
    frustum.planes[5] = MakeFloat4(-(m3.x - m2.x), -(m3.y - m2.y), -(m3.z - m2.z), -(m3.w - m2.w));
}

bool GroundMesh::intersect(const Frustum& current_frustum, const Aabb& box) {
    for (const DirectX::XMFLOAT4& plane : current_frustum.planes) {
        uint32_t out = 0u;
        out += DotPlane(plane, MakeFloat3(box.min.x, box.min.y, box.min.z)) > 0.0f ? 1u : 0u;
        out += DotPlane(plane, MakeFloat3(box.max.x, box.min.y, box.min.z)) > 0.0f ? 1u : 0u;
        out += DotPlane(plane, MakeFloat3(box.min.x, box.max.y, box.min.z)) > 0.0f ? 1u : 0u;
        out += DotPlane(plane, MakeFloat3(box.max.x, box.max.y, box.min.z)) > 0.0f ? 1u : 0u;
        out += DotPlane(plane, MakeFloat3(box.min.x, box.min.y, box.max.z)) > 0.0f ? 1u : 0u;
        out += DotPlane(plane, MakeFloat3(box.max.x, box.min.y, box.max.z)) > 0.0f ? 1u : 0u;
        out += DotPlane(plane, MakeFloat3(box.min.x, box.max.y, box.max.z)) > 0.0f ? 1u : 0u;
        out += DotPlane(plane, MakeFloat3(box.max.x, box.max.y, box.max.z)) > 0.0f ? 1u : 0u;

        if (out == 8u) {
            return false;
        }
    }

    return true;
}

bool GroundMesh::intersects_frustum(const DirectX::XMINT2& offset, const DirectX::XMUINT2& range, uint32_t level) const {
    if (level >= level_offsets.size()) {
        return false;
    }

    const DirectX::XMINT2 level_offset_i = level_offsets[level];
    // instance.offset 是局部网格偏移，level_offsets[level] 是该层全局起点；
    // 两者相加后再乘 clipmap_scale，才得到实际世界坐标。
    const DirectX::XMFLOAT3 position = Mul(
        MakeFloat3(
            static_cast<float>(level_offset_i.x + offset.x),
            0.0f,
            static_cast<float>(level_offset_i.y + offset.y)),
        clipmap_scale);
    const DirectX::XMFLOAT3 extent = Mul(
        MakeFloat3(static_cast<float>(range.x), 0.0f, static_cast<float>(range.y)),
        static_cast<float>(1u << level) * clipmap_scale);

    // 地形高度有起伏，包围盒 Y 方向必须使用整张地形的高度范围，
    // 否则只按 y=0 剔除会把高山或低谷所在的块误判为不可见。
    Aabb box;
    box.min = Min(
        MakeFloat3(position.x, frustum_min_height, position.z),
        MakeFloat3(position.x + extent.x, frustum_max_height, position.z + extent.z));
    box.max = Max(
        MakeFloat3(position.x, frustum_min_height, position.z),
        MakeFloat3(position.x + extent.x, frustum_max_height, position.z + extent.z));
    box.min = Sub(box.min, MakeFloat3(0.02f, 0.02f, 0.02f));
    box.max = Add(box.max, MakeFloat3(0.02f, 0.02f, 0.02f));

    return intersect(frustum, box);
}

void GroundMesh::append_instance(DrawInfo& info, const InstanceData& instance) {
    if (info.instance_count == 0u) {
        info.first_instance = instances.size();
    }
    instances.push_back(instance);
    ++info.instance_count;
}

DrawInfo GroundMesh::get_draw_info_quadlet() {
    DrawInfo info;
    info.index_buffer_offset = static_mesh.quadlet.offset;
    info.index_count = static_cast<uint32_t>(static_mesh.quadlet.count);

    InstanceData instance;
    instance.level = 0u;
    // 中心 quadlet 固定在最内层中心，只存在于 level 0。
    instance.offset = Mul(MakeInt2(2, 2), static_cast<int>(size - 1u));
    instance.id = 0u;

    if (intersects_frustum(instance.offset, static_mesh.quadlet.range, instance.level)) {
        append_instance(info, instance);
    }

    return info;
}

DrawInfo GroundMesh::get_draw_info_quads() {
    DrawInfo info;
    info.index_buffer_offset = static_mesh.quad.offset;
    info.index_count = static_cast<uint32_t>(static_mesh.quad.count);

    InstanceData instance;
    instance.id = 1u;

    for (uint32_t i = 0u; i < levels; ++i) {
        const int level_step = static_cast<int>(1u << i);
        const int block_stride = static_cast<int>(size - 1u) * level_step;
        for (uint32_t z = 0u; z < 4u; ++z) {
            for (uint32_t x = 0u; x < 4u; ++x) {
                // level 0 需要完整 4x4 普通块；更粗的层中心区域会被更细层覆盖，
                // 所以只保留外圈块，形成一圈一圈向外扩张的 clipmap 环。
                if (i > 0u && z != 0u && z != 3u && x != 0u && x != 3u) {
                    continue;
                }

                instance.level = i;
                instance.offset = Mul(MakeInt2(static_cast<int>(x), static_cast<int>(z)), block_stride);
                // 中心位置给 quadlet 和 fixup 条留出 2 个 level_step 的空隙。
                if (x >= 2u) {
                    instance.offset.x += 2 * level_step;
                }
                if (z >= 2u) {
                    instance.offset.y += 2 * level_step;
                }

                if (intersects_frustum(instance.offset, static_mesh.quad.range, i)) {
                    append_instance(info, instance);
                }
            }
        }
    }

    return info;
}

DrawInfo GroundMesh::get_draw_info_vertical_fixup() {
    DrawInfo info;
    info.index_buffer_offset = static_mesh.vertical.offset;
    info.index_count = static_cast<uint32_t>(static_mesh.vertical.count);

    InstanceData instance;
    instance.level = 0u;
    instance.id = 2u;
    // level 0 的竖向 fixup 填中心 quadlet 上下两侧的窄缝。
    instance.offset = MakeInt2(2 * static_cast<int>(size - 1u), static_cast<int>(size - 1u));
    if (intersects_frustum(instance.offset, static_mesh.vertical.range, 0u)) {
        append_instance(info, instance);
    }

    instance.offset = MakeInt2(2 * static_cast<int>(size - 1u), 2 * static_cast<int>(size - 1u) + 2);
    if (intersects_frustum(instance.offset, static_mesh.vertical.range, 0u)) {
        append_instance(info, instance);
    }

    instance.id = 3u;
    for (uint32_t i = 0u; i < levels; ++i) {
        const int level_step = static_cast<int>(1u << i);
        instance.level = i;

        // 每层外圈左右两条竖向补缝，跟普通块的间距一起按 2^level 放大。
        instance.offset = Mul(MakeInt2(2 * static_cast<int>(size - 1u), 0), level_step);
        if (intersects_frustum(instance.offset, static_mesh.vertical.range, i)) {
            append_instance(info, instance);
        }

        instance.offset = Mul(MakeInt2(2 * static_cast<int>(size - 1u), 3 * static_cast<int>(size - 1u) + 2), level_step);
        if (intersects_frustum(instance.offset, static_mesh.vertical.range, i)) {
            append_instance(info, instance);
        }
    }

    return info;
}

DrawInfo GroundMesh::get_draw_info_horizontal_fixup() {
    DrawInfo info;
    info.index_buffer_offset = static_mesh.horizontal.offset;
    info.index_count = static_cast<uint32_t>(static_mesh.horizontal.count);

    InstanceData instance;
    instance.level = 0u;
    instance.id = 2u;
    // level 0 的横向 fixup 填中心 quadlet 左右两侧的窄缝。
    instance.offset = MakeInt2(static_cast<int>(size - 1u), 2 * static_cast<int>(size - 1u));
    if (intersects_frustum(instance.offset, static_mesh.horizontal.range, 0u)) {
        append_instance(info, instance);
    }

    instance.offset = MakeInt2(2 * static_cast<int>(size - 1u) + 2, 2 * static_cast<int>(size - 1u));
    if (intersects_frustum(instance.offset, static_mesh.horizontal.range, 0u)) {
        append_instance(info, instance);
    }

    instance.id = 3u;
    for (uint32_t i = 0u; i < levels; ++i) {
        const int level_step = static_cast<int>(1u << i);
        instance.level = i;

        // 每层外圈上下两条横向补缝。
        instance.offset = Mul(MakeInt2(0, 2 * static_cast<int>(size - 1u)), level_step);
        if (intersects_frustum(instance.offset, static_mesh.horizontal.range, i)) {
            append_instance(info, instance);
        }

        instance.offset = Mul(MakeInt2(3 * static_cast<int>(size - 1u) + 2, 2 * static_cast<int>(size - 1u)), level_step);
        if (intersects_frustum(instance.offset, static_mesh.horizontal.range, i)) {
            append_instance(info, instance);
        }
    }

    return info;
}

DrawInfo GroundMesh::get_draw_info_degenerate(
    const Block& block,
    const DirectX::XMINT2& offset,
    const DirectX::XMINT2& ring_offset,
    uint32_t id) {
    DrawInfo info;
    info.index_buffer_offset = block.offset;
    info.index_count = static_cast<uint32_t>(block.count);

    InstanceData instance;
    instance.id = id;
    for (uint32_t i = 0u; i + 1u < levels; ++i) {
        const int level_step = static_cast<int>(1u << i);
        instance.level = i;
        // 退化边只需要画到倒数第二层，因为最后一层外侧没有更粗一层要衔接。
        instance.offset = Add(Mul(offset, level_step), Mul(ring_offset, level_step));
        if (intersects_frustum(instance.offset, block.range, i)) {
            append_instance(info, instance);
        }
    }

    return info;
}

DrawInfo GroundMesh::get_draw_info_degenerate_left() {
    return get_draw_info_degenerate(static_mesh.degenerate_left, MakeInt2(0, 0), MakeInt2(0, 0), 4u);
}

DrawInfo GroundMesh::get_draw_info_degenerate_right() {
    return get_draw_info_degenerate(
        static_mesh.degenerate_right,
        MakeInt2(4 * static_cast<int>(size - 1u), 0),
        MakeInt2(2, 0),
        5u);
}

DrawInfo GroundMesh::get_draw_info_degenerate_top() {
    return get_draw_info_degenerate(static_mesh.degenerate_top, MakeInt2(0, 0), MakeInt2(0, 0), 6u);
}

DrawInfo GroundMesh::get_draw_info_degenerate_bottom() {
    return get_draw_info_degenerate(
        static_mesh.degenerate_bottom,
        MakeInt2(0, 4 * static_cast<int>(size - 1u)),
        MakeInt2(0, 2),
        7u);
}

DrawInfo GroundMesh::get_draw_info_trim(const Block& block, TrimConditional cond) {
    DrawInfo info;
    info.index_buffer_offset = block.offset;
    info.index_count = static_cast<uint32_t>(block.count);

    InstanceData instance;
    instance.id = 7u;
    for (uint32_t i = 1u; i < levels; ++i) {
        // trim 的方向由“上一层起点相对当前层中心”的偏移决定。
        // 当前层因为吸附到更粗网格，中心可能落在上一层的四个象限之一。
        const DirectX::XMINT2 offset_prev_level = level_offsets[i - 1u];
        const DirectX::XMINT2 offset_current_level =
            Add(level_offsets[i], MakeInt2(static_cast<int>((size - 1u) * (1u << i)), static_cast<int>((size - 1u) * (1u << i))));
        const DirectX::XMINT2 trim_offset = Div(Sub(offset_prev_level, offset_current_level), static_cast<int>(1u << i));
        if (!(this->*cond)(trim_offset)) {
            continue;
        }

        instance.level = i;
        instance.offset = MakeInt2(static_cast<int>((size - 1u) * (1u << i)), static_cast<int>((size - 1u) * (1u << i)));
        if (intersects_frustum(instance.offset, block.range, i)) {
            append_instance(info, instance);
        }
    }

    return info;
}

bool GroundMesh::trim_top_right_cond(const DirectX::XMINT2& offset) const {
    return offset.x == 0 && offset.y == 1;
}

bool GroundMesh::trim_top_left_cond(const DirectX::XMINT2& offset) const {
    return offset.x == 1 && offset.y == 1;
}

bool GroundMesh::trim_bottom_right_cond(const DirectX::XMINT2& offset) const {
    return offset.x == 0 && offset.y == 0;
}

bool GroundMesh::trim_bottom_left_cond(const DirectX::XMINT2& offset) const {
    return offset.x == 1 && offset.y == 0;
}

DrawInfo GroundMesh::get_draw_info_trim_top_right() {
    return get_draw_info_trim(static_mesh.trim_top_right, &GroundMesh::trim_top_right_cond);
}

DrawInfo GroundMesh::get_draw_info_trim_top_left() {
    return get_draw_info_trim(static_mesh.trim_top_left, &GroundMesh::trim_top_left_cond);
}

DrawInfo GroundMesh::get_draw_info_trim_bottom_right() {
    return get_draw_info_trim(static_mesh.trim_bottom_right, &GroundMesh::trim_bottom_right_cond);
}

DrawInfo GroundMesh::get_draw_info_trim_bottom_left() {
    return get_draw_info_trim(static_mesh.trim_bottom_left, &GroundMesh::trim_bottom_left_cond);
}

void GroundMesh::update_draw_list() {
    instances.clear();
    draw_infos = {};

    // draw_infos 的顺序必须和 BLOCK_COUNT / 渲染端遍历顺序一致。
    // 每个函数会做视锥剔除，只把可见实例压入 instances。
    draw_infos[0] = get_draw_info_quadlet();
    draw_infos[1] = get_draw_info_quads();
    draw_infos[2] = get_draw_info_vertical_fixup();
    draw_infos[3] = get_draw_info_horizontal_fixup();
    draw_infos[4] = get_draw_info_degenerate_left();
    draw_infos[5] = get_draw_info_degenerate_right();
    draw_infos[6] = get_draw_info_degenerate_top();
    draw_infos[7] = get_draw_info_degenerate_bottom();
    draw_infos[8] = get_draw_info_trim_top_right();
    draw_infos[9] = get_draw_info_trim_top_left();
    draw_infos[10] = get_draw_info_trim_bottom_right();
    draw_infos[11] = get_draw_info_trim_bottom_left();
}

} // namespace Engine
