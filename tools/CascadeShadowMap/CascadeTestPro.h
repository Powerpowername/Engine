#pragma once
#include "global.h"

namespace CascadeShadow
{
    bool initialize(HWND hwnd);
    void InputCallBack();
    void RenderCallBack();
    void Shutdown();
}
