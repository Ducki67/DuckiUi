# DuckiUi
My personal ImGui fast library for linker and in general for easy ImGui set up

**Dear ImGui, packaged into one header and one lib.** Win32 + DirectX 11, x64.

> `#include "DuckUI.h"` and you're done — ImGui, the Win32/DX11 backends, and
> *all* the linking (`DuckUI.lib`, `d3d11.lib`, `d3dcompiler.lib`) are wired up
> automatically. No vendoring ImGui sources into your project, no fiddling with
> linker inputs, no backend boilerplate.

**You still write real ImGui.** Your UI is plain `ImGui::Begin` / `ImGui::Button`
/ `ImGui::Text` — DuckUI never wraps the widgets, so what you learn *is* ImGui.
The only things DuckUI takes off your plate are the two that get in the way:
**linking** (1 lib, auto-linked) and **render/window setup** (no manual
`ImGui_ImplWin32_Init` / `ImGui_ImplDX11_*` to get a window + the demo on screen).

By **Ducki67** · bundles [Dear ImGui](https://github.com/ocornut/imgui) (MIT).

---

## What you get (in `dist/`)

| File | What it is |
|------|------------|
| `DuckUI.h` | One monolithic header: `imgui.h` + `imgui_internal.h` + `imgui_impl_win32.h` + `imgui_impl_dx11.h` + `imconfig.h`, all inlined, plus the `DuckUI::` helper API. |
| `DuckUI_x64_Release.lib` | Static lib, `/MD` `/O2`. |
| `DuckUI_x64_Debug.lib` | Static lib, `/MDd` `/Od` `/Zi`. |
| `LICENSE-imgui.txt` | Dear ImGui's license (bundled for compliance). |

`DuckUI.h` picks the right `.lib` automatically from `_DEBUG`, and also links
`d3d11.lib` + `d3dcompiler.lib` for you via `#pragma comment(lib, ...)`.

## Quick start

Copy the four files from `dist/` somewhere your project can `#include` them
(e.g. next to your `.cpp`, or a folder on your include path). Then:

```cpp
#include "DuckUI.h"

int main()
{
    return DuckUI::RunWindow("My Tool", 1280, 720, []
    {
        ImGui::Begin("Hello");
        ImGui::Text("One header. One lib. Zero linker config.");
        ImGui::End();
        ImGui::ShowDemoWindow();
    });
}
```

Compile — **note there are no `/link` arguments and no extra `.lib` inputs:**

```bat
cl /nologo /std:c++17 /EHsc /MD /I. app.cpp /Fe:app.exe
```

That's the whole build. The `#pragma comment(lib, ...)` lines inside `DuckUI.h`
pull in `DuckUI_x64_Release.lib`, `d3d11.lib` and `d3dcompiler.lib` during link.

> **Match the CRT.** Build your app with `/MD` (Release) or `/MDd` (Debug) — the
> same Multithreaded-DLL runtime the libs use. `DuckUI.h` selects the matching
> lib from `_DEBUG` (which `/MDd` defines), so this just works if you stick to
> `/MD` ↔ Release and `/MDd` ↔ Debug.

## The `DuckUI::` API

`DuckUI.h` is still 100% Dear ImGui — every `ImGui::` call works as usual. The
`DuckUI::` namespace just removes the backend boilerplate:

```cpp
// --- if you own the device/window already (apps, tools) ---
bool    DuckUI::QuickInit(HWND hwnd, ID3D11Device* dev, ID3D11DeviceContext* ctx);
bool    DuckUI::QuickInit(IDXGISwapChain* swapchain);   // derives dev/ctx/hwnd
void    DuckUI::QuickShutdown();

void    DuckUI::BeginFrame();                            // NewFrame x3
void    DuckUI::EndFrame();                              // Render onto current RTV
void    DuckUI::EndFrame(ctx, rtv);                      // bind rtv, then render
LRESULT DuckUI::WndProc(hwnd, msg, wParam, lParam);      // feed input to ImGui

// --- or let DuckUI own everything (standalone windows) ---
int     DuckUI::RunWindow(title, w, h, frameCallback [, clearColor]);
void    DuckUI::Close();                                 // exit RunWindow's loop
```

### Inside an injected DLL (DX11 `Present` hook)

`QuickInit(swapchain)` + `EndFrame(ctx, rtv)` are built for exactly this. See
[`examples/example_dll_hook.cpp`](examples/example_dll_hook.cpp) for a full
Present-hook skeleton (bring your own MinHook/kiero — DuckUI stays hook-agnostic).

## Building the libraries yourself

You only need this if you want a different ImGui version or to rebuild from
source. Requires **Visual Studio 2022 or 2026** (Build Tools is enough) + git.

```powershell
.\build.ps1                  # clone ImGui if missing, build BOTH configs + header
.\build.ps1 -Config Release  # one config
.\build.ps1 -ImGuiTag v1.91.5
.\build.ps1 -Clean           # wipe build/ and dist/
```

What it does:

1. Clones Dear ImGui into `.\imgui` (skips if already there — or drop the sources
   in yourself).
2. Loads the x64 MSVC environment automatically (via `vswhere`).
3. Compiles the 7 translation units with `/MD`(`/MDd`) into
   `dist\DuckUI_x64_{Release,Debug}.lib`.
4. Inlines the vendor headers (stripping their `#pragma once` and sibling
   `#include`s) into `dist\DuckUI.h`, baking absolute paths to both libs into the
   auto-link `#pragma`s.

### Compiled sources

The libs contain these translation units:

```
imgui.cpp  imgui_draw.cpp  imgui_tables.cpp  imgui_widgets.cpp
imgui_demo.cpp                       <- so ImGui::ShowDemoWindow() links
backends/imgui_impl_win32.cpp        backends/imgui_impl_dx11.cpp
```

> `imgui_demo.cpp` is bundled beyond the strict "core 6" so the canonical
> `ShowDemoWindow()` smoke-test links out of the box. Omit it from `build.ps1`'s
> `$sources` if you want a couple hundred KB back.

## Notes & knobs

- **x64 only.** The header `#error`s on 32-bit; rebuild the libs for x86 if needed.
- **MSVC / clang-cl only** — auto-linking relies on `#pragma comment(lib)`.
- **Relocating the libs?** The header bakes in absolute lib paths so it works
  with no `LIBPATH`. If you move the `.lib`s, either keep them on your linker's
  library path and `#define DUCKUI_LIB_RELEASE "DuckUI_x64_Release.lib"` (and the
  Debug one) before `#include "DuckUI.h"`, or `#define DUCKUI_NO_AUTOLINK` and
  link them yourself.
- **`IMGUI_DEFINE_MATH_OPERATORS`** is forced on (required by `imgui_internal.h`).

## Layout

```
DuckiUi/
├─ build.ps1                 # the build system
├─ src/
│  ├─ duckui_prefix.h        # top of the generated header (guards + auto-link)
│  └─ duckui_wrapper.h       # the DuckUI:: API (appended after the ImGui headers)
├─ examples/
│  ├─ example_window.cpp     # standalone window in ~15 lines
│  └─ example_dll_hook.cpp   # DX11 Present-hook overlay skeleton
└─ dist/                     # build output — ship these
   ├─ DuckUI.h
   ├─ DuckUI_x64_Release.lib
   ├─ DuckUI_x64_Debug.lib
   └─ LICENSE-imgui.txt
```

`src/duckui_prefix.h` and `src/duckui_wrapper.h` are the only hand-written parts
of `DuckUI.h`; `build.ps1` sandwiches the inlined Dear ImGui headers between them.

## License

DuckUI's own glue code: do whatever you want. Bundled Dear ImGui is MIT — see
`dist/LICENSE-imgui.txt`.
