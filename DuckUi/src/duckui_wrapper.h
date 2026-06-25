// ============================================================================
//  DuckUI wrapper API
// ============================================================================

// Dear ImGui ships this declaration inside an '#if 0' block (to avoid pulling
// windows.h into its header). DuckUI.h has windows.h, so declare it for real:
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace DuckUI
{
    // ------------------------------------------------------------------ core

    // Initialize ImGui + Win32 + DX11 backends on an existing device/window.
    // Use this from apps or DLLs that already own a device.
    inline bool QuickInit(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* ctx)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        if (!ImGui_ImplWin32_Init(hwnd))
        {
            ImGui::DestroyContext();
            return false;
        }
        if (!ImGui_ImplDX11_Init(device, ctx))
        {
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            return false;
        }
        return true;
    }

    // Same, but derives device/context/window from a swapchain.
    // Perfect for IDXGISwapChain::Present hooks.
    inline bool QuickInit(IDXGISwapChain* swapchain)
    {
        ID3D11Device* device = nullptr;
        if (FAILED(swapchain->GetDevice(__uuidof(ID3D11Device), (void**)&device)) || !device)
            return false;
        ID3D11DeviceContext* ctx = nullptr;
        device->GetImmediateContext(&ctx);
        DXGI_SWAP_CHAIN_DESC desc = {};
        swapchain->GetDesc(&desc);
        const bool ok = QuickInit(desc.OutputWindow, device, ctx);
        ctx->Release();     // the backends AddRef what they keep
        device->Release();
        return ok;
    }

    inline void QuickShutdown()
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    // ----------------------------------------------------------------- frame

    inline void BeginFrame()
    {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
    }

    // Render onto whatever render target is currently bound.
    inline void EndFrame()
    {
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    // Render after binding the given RTV. Games often unbind the backbuffer,
    // so use this overload inside Present hooks.
    inline void EndFrame(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv)
    {
        ImGui::Render();
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    // Forward window messages to ImGui. Call this first in your WndProc; a
    // nonzero return means ImGui consumed the message.
    inline LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
    }

    // ------------------------------------------------- standalone window app

    namespace detail
    {
        struct WindowState
        {
            HWND                    hwnd      = nullptr;
            ID3D11Device*           device    = nullptr;
            ID3D11DeviceContext*    context   = nullptr;
            IDXGISwapChain*         swapchain = nullptr;
            ID3D11RenderTargetView* rtv       = nullptr;
            UINT                    resizeW   = 0;
            UINT                    resizeH   = 0;
        };

        inline WindowState& State() { static WindowState s; return s; }

        inline void CreateRTV()
        {
            ID3D11Texture2D* back = nullptr;
            if (SUCCEEDED(State().swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back)) && back)
            {
                State().device->CreateRenderTargetView(back, nullptr, &State().rtv);
                back->Release();
            }
        }

        inline void DestroyRTV()
        {
            if (State().rtv) { State().rtv->Release(); State().rtv = nullptr; }
        }

        inline LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            if (DuckUI::WndProc(hwnd, msg, wParam, lParam))
                return 1;
            switch (msg)
            {
            case WM_SIZE:
                if (wParam != SIZE_MINIMIZED)
                {
                    State().resizeW = (UINT)LOWORD(lParam);
                    State().resizeH = (UINT)HIWORD(lParam);
                }
                return 0;
            case WM_SYSCOMMAND:
                if ((wParam & 0xFFF0) == SC_KEYMENU)    // no beep on Alt
                    return 0;
                break;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            }
            return DefWindowProcA(hwnd, msg, wParam, lParam);
        }
    }

    // Ask a running RunWindow() loop to exit.
    inline void Close() { PostQuitMessage(0); }

    // Create a window + DX11 device/swapchain and call 'frame' every frame
    // until the window is closed. Resize, vsync and shutdown are handled.
    //
    //     DuckUI::RunWindow("My Tool", 1280, 720, []{ ImGui::Text("hi"); });
    //
    // Returns 0 on clean exit, negative on init failure.
    template <typename FrameFn>
    int RunWindow(const char* title, int width, int height, FrameFn frame,
                  ImVec4 clear = ImVec4(0.08f, 0.08f, 0.10f, 1.00f))
    {
        ImGui_ImplWin32_EnableDpiAwareness();

        detail::WindowState& s = detail::State();
        s = detail::WindowState();

        WNDCLASSEXA wc = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_CLASSDC;
        wc.lpfnWndProc   = detail::WndProcThunk;
        wc.hInstance     = GetModuleHandleA(nullptr);
        wc.hCursor       = LoadCursorA(nullptr, MAKEINTRESOURCEA(32512)); // IDC_ARROW
        wc.lpszClassName = "DuckUIWindowClass";
        if (!RegisterClassExA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return -1;

        s.hwnd = CreateWindowExA(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW,
                                 CW_USEDEFAULT, CW_USEDEFAULT, width, height,
                                 nullptr, nullptr, wc.hInstance, nullptr);
        if (!s.hwnd)
        {
            UnregisterClassA(wc.lpszClassName, wc.hInstance);
            return -1;
        }

        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount                        = 2;
        sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator   = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow                       = s.hwnd;
        sd.SampleDesc.Count                   = 1;
        sd.Windowed                           = TRUE;
        sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

        const D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
        D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_11_0;
        HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                                   want, 2, D3D11_SDK_VERSION, &sd,
                                                   &s.swapchain, &s.device, &got, &s.context);
        if (hr == DXGI_ERROR_UNSUPPORTED)   // software fallback (VMs etc.)
            hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                                               want, 2, D3D11_SDK_VERSION, &sd,
                                               &s.swapchain, &s.device, &got, &s.context);
        if (FAILED(hr))
        {
            DestroyWindow(s.hwnd);
            UnregisterClassA(wc.lpszClassName, wc.hInstance);
            return -2;
        }

        detail::CreateRTV();
        ShowWindow(s.hwnd, SW_SHOWDEFAULT);
        UpdateWindow(s.hwnd);

        if (!QuickInit(s.hwnd, s.device, s.context))
        {
            detail::DestroyRTV();
            s.swapchain->Release();
            s.context->Release();
            s.device->Release();
            DestroyWindow(s.hwnd);
            UnregisterClassA(wc.lpszClassName, wc.hInstance);
            return -3;
        }
        s.resizeW = s.resizeH = 0;

        bool done = false;
        while (!done)
        {
            MSG msg;
            while (PeekMessageA(&msg, nullptr, 0U, 0U, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
                if (msg.message == WM_QUIT)
                    done = true;
            }
            if (done)
                break;

            if (s.resizeW != 0 && s.resizeH != 0)
            {
                detail::DestroyRTV();
                s.swapchain->ResizeBuffers(0, s.resizeW, s.resizeH, DXGI_FORMAT_UNKNOWN, 0);
                s.resizeW = s.resizeH = 0;
                detail::CreateRTV();
            }

            BeginFrame();
            frame();
            ImGui::Render();
            const float cc[4] = { clear.x * clear.w, clear.y * clear.w, clear.z * clear.w, clear.w };
            s.context->OMSetRenderTargets(1, &s.rtv, nullptr);
            s.context->ClearRenderTargetView(s.rtv, cc);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            s.swapchain->Present(1, 0);     // vsync on
        }

        QuickShutdown();
        detail::DestroyRTV();
        if (s.swapchain) { s.swapchain->Release(); s.swapchain = nullptr; }
        if (s.context)   { s.context->Release();   s.context   = nullptr; }
        if (s.device)    { s.device->Release();    s.device    = nullptr; }
        DestroyWindow(s.hwnd);
        s.hwnd = nullptr;
        UnregisterClassA(wc.lpszClassName, wc.hInstance);
        return 0;
    }
}
