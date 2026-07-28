#include "Resource/LoadImage.hpp"
#include "stb/stb_image.h"
bool LoadTexture(std::string filePath,Imgae& buffer,bool flip)
{
    // 可以添加一个检测文件是否存在的判断

    //不反转图像
    stbi_set_flip_vertically_on_load(flip);
    if (stbi_is_hdr(filePath.c_str()))
    {
        int sourceChannels = 0;
        float *pixels = stbi_loadf(filePath.c_str(), &buffer.width, &buffer.height, &sourceChannels, 4);
        if (!pixels)
        {
            return false;
        }

        buffer.channels = 4;
        buffer.data.clear();
        size_t byte_count = static_cast<size_t>(buffer.width) * buffer.height * buffer.channels * sizeof(float);
        buffer.data.resize(byte_count);
        std::memcpy(buffer.data.data(),pixels,byte_count);
        buffer.isHdr = true;
        stbi_image_free(pixels);
    }
    else
    {
        int sourceChannels = 0;
        unsigned char *pixels = stbi_load(filePath.c_str(), &buffer.width, &buffer.height, &sourceChannels, 4);
        if (!pixels)
        {
            return false;
        }

        buffer.channels = 4;
        buffer.data.clear();
        size_t byte_count = static_cast<size_t>(buffer.width) * buffer.height * buffer.channels * sizeof(unsigned char);
        buffer.data.resize(byte_count);
        std::memcpy(buffer.data.data(),pixels,byte_count);
        buffer.isHdr = false;
        stbi_image_free(pixels);
    }

    return true;
}

