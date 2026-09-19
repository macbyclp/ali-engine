#pragma once

namespace eng::i18n {

enum class Lang { En, Tr };

// Picks the language: ALI_LANG=tr|en wins, otherwise LC_ALL / LC_MESSAGES / LANG ("tr_TR.UTF-8" -> Turkish).
void init();
Lang language();
void set_language(Lang lang);

// Translated text for an English source string; the string itself when there is no translation.
// Format specifiers (%d, %s ...) must survive translation unchanged.
const char* T(const char* en);

// T(en) + "###" + en: a label whose ImGui ID stays the English text, so windows, docking
// and widget state survive a language switch. "##..." and empty strings pass through.
const char* L(const char* en);

} // namespace eng::i18n
