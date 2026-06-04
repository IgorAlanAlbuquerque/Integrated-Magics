#pragma once

namespace Application {

    class HudController {
    public:
        static HudController& Get();

        void OnFrame();
        void InitializeGraphics();
        void RenderFrame(float backbufferW, float backbufferH);
        void OnWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    private:
        HudController() = default;
    };

}