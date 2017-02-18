#include <stdio.h>
#include <stdlib.h>

#include <windows.h>
#include <d3d9.h>
#include <dinput.h>

#include "framework.h"

static config g_config = {
    "Direct3D 9 Demo", 100, 100, 800, 600
};

// error reporting

static void fatal(const char *file, int line, const char *message) {
    fprintf(stderr, "%s:%d %s\n", file, line, message);
    abort();
}

// directinput stuffs

static IDirectInput8 *g_input;

IDirectInputDevice8 *g_keyboard;
IDirectInputDevice8 *g_mouse;

#define OK_DI(command)                                  \
    do {                                                \
        HRESULT result = (command);                     \
        if (result != DI_OK) {                          \
            fatal(__FILE__, __LINE__, #command);        \
        }                                               \
    } while (false)

static void input_init(HWND window) {
    DWORD cooperative_level = DISCL_FOREGROUND | DISCL_NONEXCLUSIVE;

    OK_DI(DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION,
                             IID_IDirectInput8, (LPVOID *)&g_input, NULL));

    OK_DI(g_input->CreateDevice(GUID_SysKeyboard, &g_keyboard, NULL));
    OK_DI(g_keyboard->SetDataFormat(&c_dfDIKeyboard));
    OK_DI(g_keyboard->SetCooperativeLevel(window, cooperative_level));
    OK_DI(g_keyboard->Acquire());

    OK_DI(g_input->CreateDevice(GUID_SysMouse, &g_mouse, NULL));
    OK_DI(g_mouse->SetDataFormat(&c_dfDIMouse2));
    OK_DI(g_mouse->SetCooperativeLevel(window, cooperative_level));
    OK_DI(g_mouse->Acquire());
}

static void input_free() {
    g_keyboard->Unacquire();
    g_mouse->Unacquire();
    g_keyboard->Release();
    g_mouse->Release();
    g_input->Release();
}

#undef OK_DI

// direct3d stuffs

static IDirect3D9 *g_d3dobject;
static D3DPRESENT_PARAMETERS g_d3dpresent;

IDirect3DDevice9 *g_direct3d;

#define OK_3D(command)                                  \
    do {                                                \
        HRESULT result = (command);                     \
        if (result != D3D_OK) {                         \
            fatal(__FILE__, __LINE__, #command);        \
        }                                               \
    } while (false)

static void direct3d_init(HWND window) {
    D3DDEVTYPE type = D3DDEVTYPE_HAL;

    g_d3dobject = Direct3DCreate9(D3D_SDK_VERSION);
    if (g_d3dobject == NULL) {
        fatal(__FILE__, __LINE__, "Direct3DCreate9");
    }

    D3DDISPLAYMODE mode;
    OK_3D(g_d3dobject->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode));
    OK_3D(g_d3dobject->CheckDeviceType(D3DADAPTER_DEFAULT, type,
                                       mode.Format, mode.Format,
                                       true));
    OK_3D(g_d3dobject->CheckDeviceType(D3DADAPTER_DEFAULT, type,
                                       D3DFMT_X8R8G8B8, D3DFMT_X8R8G8B8,
                                       false));

    D3DCAPS9 caps;
    OK_3D(g_d3dobject->GetDeviceCaps(D3DADAPTER_DEFAULT, type, &caps));
    if ((caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) == 0) {
        fatal(__FILE__, __LINE__, "D3DDEVCAPS_HWTRANSFORMANDLIGHT");
    }

    g_d3dpresent.BackBufferWidth            = 0;
    g_d3dpresent.BackBufferHeight           = 0;
    g_d3dpresent.BackBufferFormat           = D3DFMT_UNKNOWN;
    g_d3dpresent.BackBufferCount            = 1;
    g_d3dpresent.MultiSampleType            = D3DMULTISAMPLE_NONE;
    g_d3dpresent.MultiSampleQuality         = 0;
    g_d3dpresent.SwapEffect                 = D3DSWAPEFFECT_DISCARD;
    g_d3dpresent.hDeviceWindow              = window;
    g_d3dpresent.Windowed                   = true;
    g_d3dpresent.EnableAutoDepthStencil     = true;
    g_d3dpresent.AutoDepthStencilFormat     = D3DFMT_D24S8;
    g_d3dpresent.Flags                      = 0;
    g_d3dpresent.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
    g_d3dpresent.PresentationInterval       = D3DPRESENT_INTERVAL_IMMEDIATE;
    OK_3D(g_d3dobject->CreateDevice(D3DADAPTER_DEFAULT, type, window,
                                    D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                    &g_d3dpresent, &g_direct3d));
}

static void direct3d_free() {
    g_direct3d->Release();
    g_d3dobject->Release();
}

static void direct3d_on_loss() {
    int width = g_d3dpresent.BackBufferWidth;
    int height = g_d3dpresent.BackBufferHeight;

    on_loss();
    OK_3D(g_direct3d->Reset(&g_d3dpresent));
    on_reset(width, height);
}

static bool direct3d_is_loss() {
    HRESULT result = g_direct3d->TestCooperativeLevel();

    if (result == D3DERR_DEVICELOST) {
        Sleep(20);
        return true;
    } else if (result == D3DERR_DRIVERINTERNALERROR) {
        fatal(__FILE__, __LINE__, "D3DERR_DRIVERINTERNALERROR");
        return true;
    } else if (result == D3DERR_DEVICENOTRESET) {
        direct3d_on_loss();
        return false;
    } else {
        return false;
    }
}

#undef OK_3D

// window stuffs

static bool g_ready = false;
static bool g_paused = false;

static void window_fullscreen(HWND window, bool enable) {
    if (enable) {
        if (!g_d3dpresent.Windowed) {
            return;
        }

        int width = GetSystemMetrics(SM_CXSCREEN);
        int height = GetSystemMetrics(SM_CYSCREEN);

        g_d3dpresent.BackBufferFormat = D3DFMT_X8R8G8B8;
        g_d3dpresent.BackBufferWidth  = width;
        g_d3dpresent.BackBufferHeight = height;
        g_d3dpresent.Windowed         = false;

        SetWindowLongPtr(window, GWL_STYLE, WS_POPUP);
        SetWindowPos(window, HWND_TOP, 0, 0, width, height,
                     SWP_NOZORDER | SWP_SHOWWINDOW);
    } else {
        if (g_d3dpresent.Windowed) {
            return;
        }

        RECT rect = {0, 0, g_config.width, g_config.height};
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, false);
        int width = rect.right;
        int height = rect.bottom;

        g_d3dpresent.BackBufferFormat = D3DFMT_UNKNOWN;
        g_d3dpresent.BackBufferWidth  = g_config.width;
        g_d3dpresent.BackBufferHeight = g_config.height;
        g_d3dpresent.Windowed         = true;

        SetWindowLongPtr(window, GWL_STYLE, WS_OVERLAPPEDWINDOW);
        SetWindowPos(window, HWND_TOP, g_config.x, g_config.y, width, height,
                     SWP_NOZORDER | SWP_SHOWWINDOW);
    }
    direct3d_on_loss();
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT uMsg,
                                    WPARAM wParam, LPARAM lParam) {
    if (g_ready == false) {
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    } else {
        static bool min_or_max = false;
        RECT client_rect;

        switch(uMsg) {
            case WM_ACTIVATE:
                g_paused = (LOWORD(wParam) == WA_INACTIVE) ? true : false;
                return 0;

            case WM_SIZE:
                g_d3dpresent.BackBufferWidth  = LOWORD(lParam);
                g_d3dpresent.BackBufferHeight = HIWORD(lParam);
                if (wParam == SIZE_MINIMIZED) {
                    g_paused = true;
                    min_or_max = true;
                } else if (wParam == SIZE_MAXIMIZED) {
                    g_paused = false;
                    min_or_max = true;
                    direct3d_on_loss();
                } else if (wParam == SIZE_RESTORED) {
                    g_paused = false;
                    if (g_d3dpresent.Windowed) {
                        if (min_or_max) {
                            direct3d_on_loss();
                        } else {
                            // resizing by dragging the window edges
                            // wait until a WM_EXITSIZEMOVE message comes
                        }
                    } else {
                        // restoring to full screen mode
                        // handled by window_fullscreen()
                    }
                    min_or_max = false;
                }
                return 0;

            case WM_EXITSIZEMOVE:
                GetClientRect(hwnd, &client_rect);
                g_d3dpresent.BackBufferWidth  = client_rect.right;
                g_d3dpresent.BackBufferHeight = client_rect.bottom;
                direct3d_on_loss();
                return 0;

            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;

            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;

            case WM_KEYDOWN:
                if (wParam == VK_F11) {
                    window_fullscreen(hwnd, true);
                } else if (wParam == VK_ESCAPE) {
                    window_fullscreen(hwnd, false);
                }
                return 0;
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

static void window_init(HWND *window) {
    const char *class_name = "Direct3DWindowClass";

    WNDCLASS wc;
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = window_proc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = class_name;
    if (RegisterClass(&wc) == 0) {
        fatal(__FILE__, __LINE__, "RegisterClass");
    }

    RECT rect = {0, 0, g_config.width, g_config.height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, 0);
    int width = rect.right;
    int height = rect.bottom;

    *window = CreateWindow(class_name, g_config.title, WS_OVERLAPPEDWINDOW,
                           g_config.x, g_config.y, width, height,
                           NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (*window == NULL) {
        fatal(__FILE__, __LINE__, "CreateWindow");
    }

    ShowWindow(*window, SW_SHOW);
    UpdateWindow(*window);
}

// eventloop function

static int eventloop() {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    float period = 1.0f / frequency.QuadPart;

    LARGE_INTEGER prev_time;
    QueryPerformanceCounter(&prev_time);

    MSG message;
    message.message = WM_NULL;
    while (message.message != WM_QUIT) {
        if (PeekMessage(&message, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessage(&message);
        } else {
            if (g_paused) {
                Sleep(20);
            } else if (direct3d_is_loss() == false) {
                LARGE_INTEGER curr_time;
                QueryPerformanceCounter(&curr_time);
                float dcount = curr_time.QuadPart - prev_time.QuadPart;
                float dtime = dcount * period;

                on_render(dtime);

                prev_time = curr_time;
            }
        }
    }
    return message.wParam;
}

// main function

int main() {
    HWND window;

    on_setup(&g_config);

    window_init(&window);
    direct3d_init(window);
    input_init(window);
    g_ready = true;

    on_ready();

    eventloop();

    direct3d_free();
    input_free();
    return 0;
}