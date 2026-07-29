#include "Window/WindowsSystem.hpp"
#include "tools/model/ModelDemo.h"
#include <iostream>

std::vector<std::function<void(MSG msg)>> DXEngine::WindowsSystem::processCallbacks;

int WINAPI WinMain(HINSTANCE hInstance,HINSTANCE,LPSTR,int nCmdShow)
{
    DXEngine::WindowsSystem window(hInstance,nCmdShow);
    HWND hwnd = FindWindow(TEXT("MyWindowClass"),TEXT("My Window"));
    if(!InitializeTreeModelDemo(hwnd))
    {
        ShutdownTreeModelDemo();
        return 0;
    }

    window.ShowThisWindow([&]()
    {
        InputTreeModelDemo();
        RenderTreeModelDemo();
    });

    ShutdownTreeModelDemo();
    return 0;
}
