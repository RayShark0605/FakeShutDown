#define UNICODE
#define _UNICODE

#include <windows.h>
#include <vector>

namespace
{
    struct OverlayContext
    {
        HINSTANCE instanceHandle = nullptr;
        const wchar_t* className = L"FakeShutdownBlackOverlayWindow";
        HBRUSH blackBrush = nullptr;
        std::vector<HWND> windows;
        HHOOK keyboardHook = nullptr;
    };

    OverlayContext gContext;

    void HideCursor()
    {
        while (ShowCursor(FALSE) >= 0)
        {
        }
    }

    void ShowCursorBack()
    {
        while (ShowCursor(TRUE) < 0)
        {
        }
    }

    LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
    {
        if (nCode == HC_ACTION)
        {
            const KBDLLHOOKSTRUCT* keyboardInfo = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
            const bool isKeyDown = (wParam == WM_KEYDOWN) || (wParam == WM_SYSKEYDOWN);
            if (isKeyDown && keyboardInfo && keyboardInfo->vkCode == VK_ESCAPE)
            {
                PostQuitMessage(0);
                return 1; // 吞掉 Esc，减少“嘀”声/传递
            }
        }

        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_SETCURSOR:
            SetCursor(nullptr); // 隐藏光标
            return TRUE;

        case WM_ERASEBKGND:
            return 1; // 避免闪烁，背景我们自己涂黑

        case WM_PAINT:
        {
            PAINTSTRUCT paintStruct;
            HDC deviceContext = BeginPaint(hwnd, &paintStruct);
            FillRect(deviceContext, &paintStruct.rcPaint, gContext.blackBrush);
            EndPaint(hwnd, &paintStruct);
            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)
            {
                PostQuitMessage(0);
                return 0;
            }
            break;

        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_CLOSE)
            {
                return 0;
            }
            break;

        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    BOOL CALLBACK MonitorEnumProc(HMONITOR monitorHandle, HDC, LPRECT, LPARAM lParam)
    {
        auto* context = reinterpret_cast<OverlayContext*>(lParam);

        MONITORINFO monitorInfo;
        monitorInfo.cbSize = sizeof(MONITORINFO);
        if (!GetMonitorInfoW(monitorHandle, &monitorInfo))
        {
            return TRUE;
        }

        const RECT monitorRect = monitorInfo.rcMonitor;
        const int width = monitorRect.right - monitorRect.left;
        const int height = monitorRect.bottom - monitorRect.top;

        const DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
        const DWORD style = WS_POPUP;

        HWND hwnd = CreateWindowExW(
            exStyle,
            context->className,
            L"",
            style,
            monitorRect.left,
            monitorRect.top,
            width,
            height,
            nullptr,
            nullptr,
            context->instanceHandle,
            nullptr);

        if (!hwnd)
        {
            return TRUE;
        }

        // 置顶显示
        SetWindowPos(hwnd, HWND_TOPMOST, monitorRect.left, monitorRect.top, width, height,
            SWP_SHOWWINDOW);

        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        context->windows.push_back(hwnd);
        return TRUE;
    }

    bool CreateOverlayWindows()
    {
        gContext.blackBrush = CreateSolidBrush(RGB(0, 0, 0));
        if (!gContext.blackBrush)
        {
            return false;
        }

        WNDCLASSEXW windowClass;
        ZeroMemory(&windowClass, sizeof(windowClass));
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = gContext.instanceHandle;
        windowClass.hCursor = nullptr;
        windowClass.hbrBackground = gContext.blackBrush;
        windowClass.lpszClassName = gContext.className;

        if (!RegisterClassExW(&windowClass))
        {
            return false;
        }

        // 为每个显示器创建一张全屏黑色覆盖窗
        if (!EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&gContext)))
        {
            return false;
        }

        if (!gContext.windows.empty())
        {
            SetForegroundWindow(gContext.windows[0]);
            SetFocus(gContext.windows[0]);
        }

        return !gContext.windows.empty();
    }

    void DestroyOverlayWindows()
    {
        for (size_t i = 0; i < gContext.windows.size(); i++)
        {
            if (gContext.windows[i])
            {
                DestroyWindow(gContext.windows[i]);
                gContext.windows[i] = nullptr;
            }
        }
        gContext.windows.clear();

        UnregisterClassW(gContext.className, gContext.instanceHandle);

        if (gContext.blackBrush)
        {
            DeleteObject(gContext.blackBrush);
            gContext.blackBrush = nullptr;
        }
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    gContext.instanceHandle = hInstance;

    HideCursor();

    if (!CreateOverlayWindows())
    {
        ShowCursorBack();
        DestroyOverlayWindows();
        return 1;
    }

    // 安装全局低级键盘钩子（需要消息循环）
    gContext.keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandleW(nullptr), 0);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (gContext.keyboardHook)
    {
        UnhookWindowsHookEx(gContext.keyboardHook);
        gContext.keyboardHook = nullptr;
    }

    DestroyOverlayWindows();
    ShowCursorBack();
    return 0;
}
