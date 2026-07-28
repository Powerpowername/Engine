#pragma once
#include "global.h"
using Microsoft::WRL::ComPtr;

class Resoure
{
protected:
    ComPtr<ID3D12Resource1> resource = nullptr;

public:
    const ComPtr<ID3D12Resource1> GetResourceComPtr() const
    {
        return resource;
    }
};

