#include "global.h"


struct Imgae
{
    std::vector<unsigned char> data;
    int width = 0;
    int height = 0;
    int channels = 0;
    bool isHdr =false;
};

bool LoadTexture(std::string filePath,Imgae& buffer,bool flip = true);

//加载立方体贴图
bool LoadCubeTexture(std::string filePath,bool flip = true);

































// #include <vector>
// #include <wrl.h>
// #include <d3d12.h>
// #include <dxgi1_6.h>
// #include "d3dx12.h"
// #include "d3dx12_resource_helpers.h"
// #include <DirectXTex.h>

// using Microsoft::WRL::ComPtr;

// struct TextureUploadResult
// {
//     ComPtr<ID3D12Resource> texture;
//     ComPtr<ID3D12Resource> upload;
//     D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
// };

// bool LoadTextureWithMipmaps(
//     ID3D12Device* device,
//     ID3D12GraphicsCommandList* commandList,
//     const std::wstring& filePath,
//     TextureUploadResult& outResult)
// {
//     if (!device || !commandList)
//         return false;

//     // 1. 从文件读取图片到 CPU 内存
//     DirectX::ScratchImage srcImage;
//     DirectX::TexMetadata srcMetadata{};
//     HRESULT hr = DirectX::LoadFromWICFile(
//         filePath.c_str(),
//         DirectX::WIC_FLAGS_FORCE_RGBA32,
//         &srcMetadata,
//         srcImage);

//     if (FAILED(hr))
//         return false;

//     // 2. 在 CPU 侧生成完整 mip 链
//     DirectX::ScratchImage mipChain;
//     hr = DirectX::GenerateMipMaps(
//         srcImage.GetImages(),
//         srcImage.GetImageCount(),
//         srcImage.GetMetadata(),
//         DirectX::TEX_FILTER_DEFAULT,
//         0,
//         mipChain);

//     if (FAILED(hr))
//         return false;

//     const DirectX::TexMetadata& metadata = mipChain.GetMetadata();
//     const DirectX::Image* images = mipChain.GetImages();
//     const size_t imageCount = mipChain.GetImageCount();

//     if (!images || imageCount == 0)
//         return false;

//     // 3. 创建 GPU 纹理资源
//     D3D12_RESOURCE_DESC texDesc{};
//     texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
//     texDesc.Alignment = 0;
//     texDesc.Width = static_cast<UINT64>(metadata.width);
//     texDesc.Height = static_cast<UINT>(metadata.height);
//     texDesc.DepthOrArraySize = static_cast<UINT16>(metadata.arraySize);
//     texDesc.MipLevels = static_cast<UINT16>(metadata.mipLevels);
//     texDesc.Format = metadata.format;
//     texDesc.SampleDesc.Count = 1;
//     texDesc.SampleDesc.Quality = 0;
//     texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
//     texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

//     auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

//     hr = device->CreateCommittedResource(
//         &defaultHeap,
//         D3D12_HEAP_FLAG_NONE,
//         &texDesc,
//         D3D12_RESOURCE_STATE_COPY_DEST,
//         nullptr,
//         IID_PPV_ARGS(outResult.texture.GetAddressOf()));

//     if (FAILED(hr))
//         return false;

//     // 4. 组织每个 mip 的 subresource 数据
//     std::vector<D3D12_SUBRESOURCE_DATA> subresources(imageCount);
//     for (size_t i = 0; i < imageCount; ++i)
//     {
//         subresources[i].pData = images[i].pixels;
//         subresources[i].RowPitch = static_cast<LONG_PTR>(images[i].rowPitch);
//         subresources[i].SlicePitch = static_cast<LONG_PTR>(images[i].slicePitch);
//     }

//     // 5. 计算上传缓冲大小
//     const UINT64 uploadBufferSize = GetRequiredIntermediateSize(
//         outResult.texture.Get(),
//         0,
//         static_cast<UINT>(imageCount));

//     // 6. 创建上传堆
//     auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
//     auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

//     hr = device->CreateCommittedResource(
//         &uploadHeap,
//         D3D12_HEAP_FLAG_NONE,
//         &uploadDesc,
//         D3D12_RESOURCE_STATE_GENERIC_READ,
//         nullptr,
//         IID_PPV_ARGS(outResult.upload.GetAddressOf()));

//     if (FAILED(hr))
//         return false;

//     // 7. 一次性把所有 mip 拷到 GPU 纹理
//     UpdateSubresources(
//         commandList,
//         outResult.texture.Get(),
//         outResult.upload.Get(),
//         0,
//         0,
//         static_cast<UINT>(imageCount),
//         subresources.data());

//     // 8. 切换到可采样状态
//     auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
//         outResult.texture.Get(),
//         D3D12_RESOURCE_STATE_COPY_DEST,
//         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

//     commandList->ResourceBarrier(1, &barrier);

//     // 9. 准备 SRV 描述
//     outResult.srvDesc = {};
//     outResult.srvDesc.Format = metadata.format;
//     outResult.srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
//     outResult.srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
//     outResult.srvDesc.Texture2D.MostDetailedMip = 0;
//     outResult.srvDesc.Texture2D.MipLevels = static_cast<UINT>(metadata.mipLevels);
//     outResult.srvDesc.Texture2D.PlaneSlice = 0;
//     outResult.srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

//     return true;
// }
