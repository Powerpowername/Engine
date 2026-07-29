#include "application.hpp"

std::vector<std::function<void(MSG msg)>> DXEngine::WindowsSystem::processCallbacks;

int WINAPI WinMain(HINSTANCE hInstance,HINSTANCE,LPSTR,int nCmdShow)
{
    windowsSystem = std::make_shared<DXEngine::WindowsSystem>(hInstance,nCmdShow);
    HWND hwnd = DXEngine::WindowsSystem::GetWindowHwnd();
    if(hwnd == nullptr)
    {
        return 0;
    }

    appInitial(hwnd);

    windowsSystem->ShowThisWindow([]()
    {
        appInputCallBack();
        appUpdate();
        appRenderCallBack();
    });

    return 0;
}
