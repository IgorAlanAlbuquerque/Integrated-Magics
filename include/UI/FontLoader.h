#pragma once
#include <imgui.h>

namespace FontLoader {

    inline const ImWchar* GetGlyphRangesDefault() { return ImGui::GetIO().Fonts->GetGlyphRangesDefault(); }

    inline const ImWchar* GetGlyphRangesJapanese() { return ImGui::GetIO().Fonts->GetGlyphRangesJapanese(); }

    inline const ImWchar* GetGlyphRangesChineseSimplified() {
        return ImGui::GetIO().Fonts->GetGlyphRangesChineseSimplifiedCommon();
    }

    inline const ImWchar* GetGlyphRangesKorean() { return ImGui::GetIO().Fonts->GetGlyphRangesKorean(); }

    inline const ImWchar* GetGlyphRangesPolish() {
        static ImVector<ImWchar> ranges;
        if (ranges.empty()) {
            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(GetGlyphRangesDefault());
            const ImWchar polish[] = {0x0104, 0x0105, 0x0106, 0x0107, 0x0118, 0x0119, 0x0141, 0x0142, 0x0143, 0x0144,
                                      0x00D3, 0x00F3, 0x015A, 0x015B, 0x0179, 0x017A, 0x017B, 0x017C, 0};
            for (int i = 0; polish[i]; i++) builder.AddChar(polish[i]);
            builder.BuildRanges(&ranges);
        }
        return ranges.Data;
    }

    inline const ImWchar* GetGlyphRangesCyrillic() {
        static ImVector<ImWchar> ranges;
        if (ranges.empty()) {
            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(GetGlyphRangesDefault());
            const ImWchar cyrillic[] = {0x0400, 0x04FF, 0};
            builder.AddRanges(cyrillic);
            builder.BuildRanges(&ranges);
        }
        return ranges.Data;
    }

    inline const ImWchar* GetGlyphRangesGreek() {
        static ImVector<ImWchar> ranges;
        if (ranges.empty()) {
            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(GetGlyphRangesDefault());
            const ImWchar greek[] = {0x0370, 0x03FF, 0};
            builder.AddRanges(greek);
            builder.BuildRanges(&ranges);
        }
        return ranges.Data;
    }

    inline const ImWchar* GetGlyphRangesAll() {
        static ImVector<ImWchar> ranges;
        if (ranges.empty()) {
            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(GetGlyphRangesDefault());
            builder.AddRanges(GetGlyphRangesPolish());
            builder.AddRanges(GetGlyphRangesCyrillic());
            builder.AddRanges(GetGlyphRangesGreek());
            builder.AddRanges(GetGlyphRangesJapanese());
            builder.AddRanges(GetGlyphRangesChineseSimplified());
            builder.AddRanges(GetGlyphRangesKorean());
            builder.BuildRanges(&ranges);
        }
        return ranges.Data;
    }
}