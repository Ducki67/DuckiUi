// ============================================================================
//  DuckUI example - DX11 Present-hook overlay skeleton (for injected DLLs).
//
//  This shows how DuckUI fits a typical hook setup. It deliberately does NOT
//  ship a hooking library - drop in MinHook/kiero and point the detour at
//  hkPresent below. DuckUI gives you the one-call init/frame helpers.
//
//  Compile as a DLL (DuckUI.h + libs beside it):
//      cl /nologo /std:c++17 /EHsc /MD /LD /I. example_dll_hook.cpp /Fe:Overlay.dll
// ============================================================================
#include "DuckUI.h"

// --- swap these for your real hook library (MinHook, kiero, ...) ------------
using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
static PresentFn oPresent = nullptr;        // original Present (trampoline)
static WNDPROC   oWndProc = nullptr;        // original window proc
static bool      g_init   = false;
static HWND      g_hwnd   = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;

static LRESULT CALLBACK hkWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    DuckUI::WndProc(hWnd, msg, wParam, lParam);     // feed input to ImGui
    return CallWindowProcA(oWndProc, hWnd, msg, wParam, lParam);
}

// Detour target. Register this with your hook lib as the Present replacement.
HRESULT __stdcall hkPresent(IDXGISwapChain* sc, UINT sync, UINT flags)
{
    if (!g_init)
    {
        if (DuckUI::QuickInit(sc))      // pulls device/ctx/hwnd from swapchain
        {
            DXGI_SWAP_CHAIN_DESC d = {};
            sc->GetDesc(&d);
            g_hwnd   = d.OutputWindow;
            oWndProc = (WNDPROC)SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);

            ID3D11Device* dev = nullptr;
            sc->GetDevice(__uuidof(ID3D11Device), (void**)&dev);
            ID3D11Texture2D* back = nullptr;
            if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back)) && back)
            {
                dev->CreateRenderTargetView(back, nullptr, &g_rtv);
                back->Release();
            }
            if (dev) dev->Release();
            g_init = true;
        }
        if (!g_init)
            return oPresent(sc, sync, flags);
    }

    DuckUI::BeginFrame();
    ImGui::Begin("DuckUI Overlay");
    ImGui::Text("Injected. Press INSERT to toggle (wire this up yourself).");
    ImGui::End();

    ID3D11Device* dev = nullptr;
    sc->GetDevice(__uuidof(ID3D11Device), (void**)&dev);
    if (dev)
    {
        ID3D11DeviceContext* ctx = nullptr;
        dev->GetImmediateContext(&ctx);
        DuckUI::EndFrame(ctx, g_rtv);   // binds our RTV then renders
        ctx->Release();
        dev->Release();
    }
    return oPresent(sc, sync, flags);
}

static void Cleanup()
{
    if (g_init)
    {
        DuckUI::QuickShutdown();
        if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
        if (g_hwnd && oWndProc)
            SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
        g_init = false;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        // TODO: install your Present hook here (MinHook/kiero) -> hkPresent.
        break;
    case DLL_PROCESS_DETACH:
        Cleanup();
        break;
    }
    return TRUE;
}
