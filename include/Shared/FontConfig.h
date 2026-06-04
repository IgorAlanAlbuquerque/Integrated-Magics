#pragma once
#include <string>

namespace IntegratedMagic {
    struct FontConfig {
        std::string path = "";
        float size = 28.f;
        bool rangePolish = false;
        bool rangeCyrillic = false;
        bool rangeJapanese = false;
        bool rangeChineseSimplified = false;
        bool rangeKorean = false;
        bool rangeGreek = false;
    };
}
