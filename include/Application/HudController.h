#pragma once

namespace Application {

    class HudController {
    public:
        static HudController& Get();

        void OnFrame();

    private:
        HudController() = default;
    };

}