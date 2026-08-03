# Clipmap 更新和采样简洁版

这份只讲最核心流程：clipmap 高度数据怎么更新，渲染时怎么采样。对应项目里的主要文件：

- `tools/clipmap_port/GroundMesh.cpp`
- `tools/clipmap_port/Heightmap.cpp`
- `include/application.cpp`
- `resource/shaders/appShader/appTerrian.hlsl`

## 1. 先记住几个核心量

### `level`

第几层 clipmap。

```text
level 0 最近、最细
level 越大越远、越粗
```

### `levelStep`

当前层相对 level 0 放大了多少。

```text
levelStep = 2^level
```

例如：

```text
level 0: levelStep = 1
level 1: levelStep = 2
level 2: levelStep = 4
```

### `GroundMesh::level_offsets[level]`

某一层 clipmap 的起点。

```text
level_offsets[level].x/y
= 第 level 层 clipmap 从全局 level 0 地形格子的哪里开始
```

它的单位是 level 0 grid，不是世界坐标，也不是纹理坐标。

### `HeightmapLevelInfo.x/y`

某一层 clipmap 高度数据上一帧的起点。

```text
HeightmapLevelInfo.x/y
= 第 level 层高度数据上一帧从当前层哪个高度格子开始
```

它的单位是当前 level 自己的 texel。

二者关系：

```text
newStart = floor(level_offsets[level] / 2^level)

HeightmapLevelInfo.x/y = oldStart
```

`newStart` 是本帧新算出来的高度数据起点，`oldStart` 是上一帧保存的高度数据起点。

## 2. 每帧 CPU 先算 clipmap 起点

对应函数：

```cpp
GroundMesh::update_level_offsets()
GroundMesh::get_offset_level()
```

核心公式：

```text
cameraGrid = floor(cameraXZ / clipmapScale)
snapStep = 2^(level + 1)
snappedCamera = floor(cameraGrid / snapStep) * snapStep

level_offsets[level] =
    snappedCamera - (size - 1) * 2^(level + 1)
```

意思：

```text
根据相机位置，算出每一层 clipmap 从哪里开始。
```

为什么要 `snapStep = 2^(level + 1)`？

```text
为了让当前层和下一层粗网格对齐，减少 LOD 接缝。
```

## 3. 更新 clipmap 高度数据

对应函数：

```cpp
Heightmap::update()
Heightmap::update_level()
```

CPU 会把 `level_offsets[level]` 换成当前 level 的高度数据起点：

```text
newStart = floor(level_offsets[level] / 2^level)
```

然后和上一帧保存的起点比较：

```text
oldStart = HeightmapLevelInfo.x/y
delta = newStart - oldStart
```

判断逻辑：

```text
如果 newStart == oldStart 并且 cleared == false
  不更新

如果移动太大，或者 cleared == true
  整层刷新

否则
  只刷新新露出来的边缘条带
```

生成的更新任务结构体是：

```cpp
struct HeightmapUpdateInfo {
    XMINT2 tex;
    XMINT2 size;
    XMINT2 start;
    uint32_t level;
};
```

字段含义：

```text
tex   = 写到环形高度纹理的哪里
size  = 写多大一块
start = 从当前 level 的哪个高度格子开始读取源地形
level = 写入 Texture2DArray 的哪一层
```

一句话：

```text
HeightmapUpdateInfo 告诉 compute shader：
这一帧，第几层 clipmap 的哪块高度数据需要重新写。
```

## 4. CPU 把更新任务交给 compute shader

对应位置：

```cpp
include/application.cpp::appUpdate()
```

代码里把 `HeightmapUpdateInfo` 拆到 compute 常量缓冲：

```cpp
cc.gUpdateTexSize[i] =
    XMINT4(update.tex.x, update.tex.y, update.size.x, update.size.y);

cc.gUpdateStartLevel[i] =
    XMINT4(update.start.x, update.start.y, update.level, 0);
```

对应 shader：

```hlsl
int4 texSize = gUpdateTexSize[updateIndex];
int4 startLevel = gUpdateStartLevel[updateIndex];
```

所以：

```text
HeightmapUpdateInfo 是 CPU 结构体。
compute shader 实际读的是 gUpdateTexSize 和 gUpdateStartLevel。
```

## 5. compute shader 怎么写高度缓存

对应函数：

```hlsl
CSMain()
```

每个线程负责更新一个 texel。

先算这个线程对应当前 level 的高度格子：

```text
level = startLevel.z
levelStep = 2^level

levelTexel =
    startLevel.xy + dispatchThreadId.xy
```

再换成世界坐标：

```text
worldXZ =
    levelTexel * levelStep * clipmapScale
```

然后从源高度图采样：

```text
sourceUV =
    (worldXZ - terrainCenter) / terrainSize + 0.5
```

最后写入 clipmap 高度缓存：

```text
writeTexel =
    texSize.xy + dispatchThreadId.xy

gOutputHeight[writeTexel.x, writeTexel.y, level] =
    height 和 gradient
```

这里的 `gOutputHeight` 是一个 `Texture2DArray`：

```text
slice 0 = level 0 高度缓存
slice 1 = level 1 高度缓存
slice 2 = level 2 高度缓存
...
```

## 6. 渲染时怎么采样 clipmap

对应函数：

```hlsl
VSMain()
```

渲染时输入有两部分：

```text
input.grid
  静态网格模板里的顶点坐标

input.instanceOffset / input.instanceLevel
  CPU 生成的 InstanceData
```

先算顶点世界坐标的 XZ：

```text
level = input.instanceLevel
levelStep = 2^level

worldXZ =
    (level_offsets[level] + instanceOffset) * clipmapScale
    + grid * levelStep * clipmapScale
```

再算这个顶点要采样当前 level 高度缓存的哪个 texel：

```text
currentLevelTexel =
    (level_offsets[level] + instanceOffset) / levelStep
    + grid
```

变成 UV：

```text
uv =
    frac((currentLevelTexel + 0.5) / clipmapResolution)
```

然后采样：

```hlsl
gClipmapHeight.SampleLevel(
    gWrapSampler,
    float3(uv, level),
    0)
```

这里：

```text
uv    = 当前层高度缓存里的位置
level = Texture2DArray 的第几层
```

`frac` 是因为高度缓存是环形缓存，坐标超过一圈就回到开头。

## 7. 一句话串起来

更新 clipmap：

```text
level_offsets[level]
  -> 除以 2^level 得到 newStart
  -> 和 HeightmapLevelInfo.x/y 比较
  -> 生成 HeightmapUpdateInfo
  -> compute shader 写 gOutputHeight
```

采样 clipmap：

```text
level_offsets[level] + InstanceData::offset + grid * 2^level
  -> 得到世界位置
  -> 换成当前 level 的 texel/UV
  -> vertex shader 采样 gClipmapHeight
```

最重要的关系：

```text
更新用 level_offsets 算高度数据该写哪里。
采样也用 level_offsets 算顶点该读哪里。

所以更新和采样能对上。
```
