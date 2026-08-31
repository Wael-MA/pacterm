// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Wael (https://wael.work.gd)
// pacterm v1.4.0
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <format>
#include <cstdint>

namespace I18n {

enum class Language : uint8_t {
    En = 0,
    Ar = 1,
    Fr = 2,
    Es = 3,
    De = 4,
    It = 5,
    Ja = 6,
};

struct LanguageInfo {
    Language lang;
    std::string_view code;
    std::string_view name;
};

const std::vector<LanguageInfo>& getAvailableLanguages() noexcept;

Language getCurrentLanguage() noexcept;
std::string_view getCurrentLanguageCode() noexcept;
std::string_view getCurrentLanguageName() noexcept;

void setLanguage(Language lang) noexcept;
bool setLanguageByCode(std::string_view code) noexcept;
void cycleLanguage(int dir = 1) noexcept;

void initFromLocale() noexcept;

std::string_view t(std::string_view key) noexcept;

template <typename... Args>
std::string format(std::string_view key, Args&&... args) {
    std::string_view tmpl = t(key);
    try {
        return std::vformat(tmpl, std::make_format_args(args...));
    } catch (...) {
        return std::string(tmpl);
    }
}

} // namespace I18n
