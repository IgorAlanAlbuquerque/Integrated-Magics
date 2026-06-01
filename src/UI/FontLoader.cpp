#include "UI/FontLoader.h"

#include <imgui.h>

#include <filesystem>

#include "Config/StyleConfig.h"
#include "PCH.h"

namespace FontLoader {

    void LoadFontsFromConfig() {
        auto& io = ImGui::GetIO();
        const auto& fc = IntegratedMagic::StyleConfig::Get().font;

        const char* fontPath = fc.path.empty() ? nullptr : fc.path.c_str();
        if (!fontPath || !std::filesystem::exists(fontPath)) {
            io.Fonts->AddFontDefault();
            MAGIC_DEBUG_LOG("[HUD] FontLoader::LoadFontsFromConfig: font not found, using default");
            return;
        }

        io.Fonts->AddFontFromFileTTF(fontPath, fc.size, nullptr, GetGlyphRangesDefault());

        ImFontConfig mergeCfg;
        mergeCfg.MergeMode = true;

        if (fc.rangePolish) io.Fonts->AddFontFromFileTTF(fontPath, fc.size, &mergeCfg, GetGlyphRangesPolish());
        if (fc.rangeCyrillic) io.Fonts->AddFontFromFileTTF(fontPath, fc.size, &mergeCfg, GetGlyphRangesCyrillic());
        if (fc.rangeJapanese) io.Fonts->AddFontFromFileTTF(fontPath, fc.size, &mergeCfg, GetGlyphRangesJapanese());
        if (fc.rangeChineseSimplified)
            io.Fonts->AddFontFromFileTTF(fontPath, fc.size, &mergeCfg, GetGlyphRangesChineseSimplified());
        if (fc.rangeKorean) io.Fonts->AddFontFromFileTTF(fontPath, fc.size, &mergeCfg, GetGlyphRangesKorean());
        if (fc.rangeGreek) io.Fonts->AddFontFromFileTTF(fontPath, fc.size, &mergeCfg, GetGlyphRangesGreek());

        MAGIC_DEBUG_LOG("[HUD] FontLoader::LoadFontsFromConfig: loaded font '{}' size {}", fontPath, fc.size);
    }

}
