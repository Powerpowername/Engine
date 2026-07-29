#include "Window/WindowsSystem.hpp"
#include "tools/CascadeShadowMap/CascadeTestPro.h"

std::vector<std::function<void(MSG msg)>> DXEngine::WindowsSystem::processCallbacks;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    DXEngine::WindowsSystem window(hInstance, nCmdShow);
    HWND hwnd = FindWindow(TEXT("MyWindowClass"), TEXT("My Window"));
    if(!CascadeShadow::initialize(hwnd))
    {
        CascadeShadow::Shutdown();
        return 0;
    }

    window.ShowThisWindow([&]()
    {
        CascadeShadow::InputCallBack();
        CascadeShadow::RenderCallBack();
    });

    CascadeShadow::Shutdown();
    return 0;
}
