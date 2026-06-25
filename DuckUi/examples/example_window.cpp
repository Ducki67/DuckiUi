// ============================================================================
//  DuckUI example - standalone window in ~15 lines.
//
//  Compile (from the dist folder, with DuckUI.h + the .libs beside it):
//      cl /nologo /std:c++17 /EHsc /MD /I. example_window.cpp /Fe:demo.exe
//
//  No /link, no ImGui sources, no d3d11.lib - DuckUI.h handles all of it.
// ============================================================================
#include "DuckUI.h"

int main()
{
    bool show_demo = true;
    float color[3] = { 0.20f, 0.55f, 0.90f };

    return DuckUI::RunWindow("DuckUI Demo", 1280, 720, [&]
    {
        ImGui::Begin("Hello from DuckUI");
        ImGui::Text("One header. One lib. Zero linker config.");
        ImGui::Separator();
        ImGui::Checkbox("Show ImGui demo window", &show_demo);
        ImGui::ColorEdit3("Accent", color);
        if (ImGui::Button("Quit"))
            DuckUI::Close();
        ImGui::End();

        if (show_demo)
            ImGui::ShowDemoWindow(&show_demo);
    });
}
