#pragma once
#include "global.h"
using Microsoft::WRL::ComPtr;

namespace DXEngine
{
    struct KeyboardState
    {
        std::array<bool, 256> down = {};
        std::array<bool, 256> pressed = {};
        std::array<bool, 256> released = {};
        void BeginFrame()
        {
            pressed.fill(false);
            released.fill(false);
        }

        void Reset()
        {
            down.fill(false);
            pressed.fill(false);
            released.fill(false);
        }
    };

    struct MouseState
    {
        POINT position = {};
        LONG deltaX = 0;
        LONG deltaY = 0;
        float wheelDelta = 0.0f;

        bool leftDown = false;
        bool rightDown = false;
        bool middleDown = false;

        bool leftPressed = false;
        bool rightPressed = false;
        bool middlePressed = false;

        bool leftReleased = false;
        bool rightReleased = false;
        bool middleReleased = false;

        bool hasFocus = false;
        bool insideWindow = false;
        bool hasLastPosition = false;
        bool trackingLeave = false;

        void BeginFrame()
        {
            deltaX = 0;
            deltaY = 0;
            wheelDelta = 0.0f;

            leftPressed = false;
            rightPressed = false;
            middlePressed = false;

            leftReleased = false;
            rightReleased = false;
            middleReleased = false;
        }

        void Reset()
        {
            position = {};
            deltaX = 0;
            deltaY = 0;
            wheelDelta = 0.0f;

            leftDown = false;
            rightDown = false;
            middleDown = false;

            leftPressed = false;
            rightPressed = false;
            middlePressed = false;

            leftReleased = false;
            rightReleased = false;
            middleReleased = false;

            hasFocus = false;
            insideWindow = false;
            hasLastPosition = false;
            trackingLeave = false;
        }
    };
    class WindowsSystem
    {
        static constexpr LPCTSTR kWindowClassName = TEXT("MyWindowClass");
        inline static HWND hWnd = nullptr;
        inline static UINT msg = 0;
        inline static WPARAM wParam = 0;
        inline static LPARAM lParam = 0;
        inline static int nCmdShow = 0;
        inline static bool shouldQuit = false;
        inline static KeyboardState keyboardState = {};
        inline static MouseState mouseState = {};
        static std::vector<std::function<void(MSG msg)>> processCallbacks;
        /// @brief 消息处理函数
        /// @param hWnd 
        /// @param msg 
        /// @param wParam 
        /// @param lParam 
        /// @return 
        static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            switch (msg)
            {
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            }

            return DefWindowProc(hWnd, msg, wParam, lParam);

        }

        std::shared_ptr<WNDCLASS> cndClassInit(HINSTANCE hInstance, int cmdShow)
        {
            std::shared_ptr<WNDCLASS> wc = std::make_shared<WNDCLASS>();
            WindowsSystem::nCmdShow = cmdShow;
            wc->style = CS_HREDRAW | CS_VREDRAW;
            wc->lpfnWndProc = &WindowsSystem::WndProc;
            wc->hInstance = hInstance;
            wc->hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc->hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wc->lpszClassName = kWindowClassName;

            if (RegisterClass(wc.get()) == 0)
            {
                MessageBox(nullptr, TEXT("RegisterClass failed"), TEXT("WindowsSystem"), MB_OK | MB_ICONERROR);
                return wc;
            }

            hWnd = CreateWindowEx(
                0,
                kWindowClassName,
                TEXT("My Window"),
                WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT, CW_USEDEFAULT,
                800, 600,
                nullptr, nullptr, hInstance, nullptr);

            if (hWnd == nullptr)
            {
                MessageBox(nullptr, TEXT("CreateWindowEx failed"), TEXT("WindowsSystem"), MB_OK | MB_ICONERROR);
            }

            return wc;
        }

        static POINT PointFromLParam(LPARAM lParam)
        {
            POINT point = {};
            point.x = static_cast<LONG>(static_cast<short>(LOWORD(lParam)));
            point.y = static_cast<LONG>(static_cast<short>(HIWORD(lParam)));
            return point;
        }

        static void BeginInputFrame()
        {
            keyboardState.BeginFrame();
            mouseState.BeginFrame();
        }

        static void ResetInputState()
        {
            keyboardState.Reset();
            mouseState.Reset();
        }

        static void SetKeyState(WPARAM key, bool isDown)
        {
            if (key > 255)
            {
                return;
            }
            const size_t index = static_cast<size_t>(key);
            if (isDown)
            {
                if (!keyboardState.down[index])
                {
                    keyboardState.pressed[index] = true;
                }
                keyboardState.down[index] = true;
            }
            else
            {
                if (keyboardState.down[index])
                {
                    keyboardState.released[index] = true;
                }
                keyboardState.down[index] = false;
            }
        }
    
        static void TrackMouseLeave(HWND windowHandle)
        {
            if (mouseState.trackingLeave)
            {
                return;
            }

            TRACKMOUSEEVENT eventData = {};
            eventData.cbSize = sizeof(eventData);
            eventData.dwFlags = TME_LEAVE;
            eventData.hwndTrack = windowHandle;

            if (TrackMouseEvent(&eventData))
            {
                mouseState.trackingLeave = true;
            }
        }

        static void UpdateMousePosition(HWND windowHandle, LPARAM lParam)
        {
            POINT current = PointFromLParam(lParam);
            if (mouseState.hasLastPosition)
            {
                // deltaX/deltaY 是本帧内鼠标总偏移量，要累加整帧所有鼠标移动消息的差值，不能直接覆盖；
                // 而 position 是当前最新坐标，每次直接覆盖。
                mouseState.deltaX += current.x - mouseState.position.x;
                mouseState.deltaY += current.y - mouseState.position.y;
            }

            mouseState.position = current;
            mouseState.hasLastPosition = true;
            mouseState.insideWindow = true;

            TrackMouseLeave(windowHandle);            
        }

        static void SetLeftMouse(bool isDown)
        {
            if (isDown)
            {
                if (!mouseState.leftDown)
                {
                    mouseState.leftPressed = true;
                }
                mouseState.leftDown = true;
            }
            else
            {
                if (mouseState.leftDown)
                {
                    mouseState.leftReleased = true;
                }
                mouseState.leftDown = false;
            }
        }
        static void SetRightMouse(bool isDown)
        {
            if (isDown)
            {
                if (!mouseState.rightDown)
                {
                    mouseState.rightPressed = true;
                }
                mouseState.rightDown = true;
            }
            else
            {
                if (mouseState.rightDown)
                {
                    mouseState.rightReleased = true;
                }
                mouseState.rightDown = false;
            }
        }
        static void SetMiddleMouse(bool isDown)
        {
            if (isDown)
            {
                if (!mouseState.middleDown)
                {
                    mouseState.middlePressed = true;
                }
                mouseState.middleDown = true;
            }
            else
            {
                if (mouseState.middleDown)
                {
                    mouseState.middleReleased = true;
                }
                mouseState.middleDown = false;
            }
        }
    public:



        WindowsSystem(HINSTANCE hInstance, int nCmdShow)
        {
            // // WNDCLASS wc要做成单例
            shouldQuit = false;
            ResetInputState();
            static std::shared_ptr<WNDCLASS> wc = cndClassInit(hInstance, nCmdShow);
        }
        const KeyboardState& GetKeyboardState() const
        {
            return keyboardState;
        }

        const MouseState& GetMouseState() const
        {
            return mouseState;
        }

        bool IsKeyDown(unsigned char key) const
        {
            return keyboardState.down[key];
        }

        bool WasKeyPressed(unsigned char key) const
        {
            return keyboardState.pressed[key];
        }

        bool WasKeyReleased(unsigned char key) const
        {
            return keyboardState.released[key];
        }

        bool ProcessMessages()
        {
            BeginInputFrame();

            MSG msg = {};
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                {
                    shouldQuit = true;
                    return false;
                }
                for(auto func : processCallbacks)
                {
                    func(msg);
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            return !shouldQuit;
        }

        void ShowThisWindow(std::function<void()> callback)
        {
            if (hWnd == nullptr)
            {
                return;
            }

            ShowWindow(hWnd, nCmdShow);
            UpdateWindow(hWnd);
            MSG msg = {};
            while (ProcessMessages())
            {
                callback();
            }
        }

        void RegisterProcessCallback(std::function<void(MSG msg)> processCallback)
        {
            processCallbacks.push_back(processCallback);
        }

        static HWND& GetWindowHwnd(){return hWnd;}

    };
};
