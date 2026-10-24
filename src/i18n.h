// UI translations. Source strings are the Turkish texts used in the code; T() maps them to the active
// language and returns unknown strings (key names, numbers) unchanged.
#pragma once

#include <string>

enum class Lang { TR, EN, DE, ZH, JA, Count };

void SetLanguage(Lang lang);
Lang GetLanguage();
Lang DefaultLanguage();  // from the Windows UI language
const char* LangName(Lang lang);  // native name, e.g. "Deutsch"
const char* LangCode(Lang lang);  // "tr", "en", ...
bool LangFromCode(const std::string& code, Lang* out);

const char* T(const char* text);

// Every string of the active language plus the language names, for building font glyph ranges.
std::string I18nGlyphText();
// CJK font to merge into the UI font for the active language, or nullptr.
const char* I18nCjkFontPath();
