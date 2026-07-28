#include "global.h"
using Microsoft::WRL::ComPtr;
class Device
{
private:
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device> device;
public:
    bool Initialize()
    {
        UINT factoryFlags = 0;
        if(FAILED(CreateDXGIFactory2(factoryFlags,IID_PPV_ARGS(factory.GetAddressOf()))))
        {
            std::print("CreateDXGIFactory2 failed\n");
            return false;            
        }

        for(UINT i = 0;factory->EnumAdapters1(i,adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND;i++)
        {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            if(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                continue;
            }
            if(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,_uuidof(ID3D12Device),nullptr)))
            {
                break;
            }
        }

        if(!adapter)
        {
            std::print("No suitable adapter found\n");
            return false;
        }
        if (FAILED(D3D12CreateDevice(
            adapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(device.GetAddressOf())
        )))
        {
            std::print("D3D12CreateDevice failed\n");
            return false;
        }
        return true;
    }

    ComPtr<ID3D12Device> GetDeviceComPtr() const
    {
        return device;
    }

    ComPtr<IDXGIFactory4> GetFactoryComPtr() const
    {
        return factory;
    }

};
