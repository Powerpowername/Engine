#include "Window/WindowsSystem.hpp"
#include "tools/SkyBox/SkyBoxDemo.h"

std::vector<std::function<void(MSG msg)>> DXEngine::WindowsSystem::processCallbacks;

int WINAPI WinMain(HINSTANCE hInstance,HINSTANCE,LPSTR,int nCmdShow)
{
    DXEngine::WindowsSystem window(hInstance,nCmdShow);
    HWND hwnd = FindWindow(TEXT("MyWindowClass"),TEXT("My Window"));
    if(!InitializeSkyBoxDemo(hwnd))
    {
        ShutdownSkyBoxDemo();
        return 0;
    }

    window.ShowThisWindow([&]()
    {
        InputSkyBoxDemo();
        RenderSkyBoxDemo();
    });

    ShutdownSkyBoxDemo();
    return 0;
}
