// Moonlight XR Native, tests/m4 (lane wave6-i18n): the headset UI's language ([ui] language) is
// taken at the first xrApplySettings of the process, from the locale when it is auto, and kept
// for the run: a later apply with another language changes nothing (a restart applies it).
// GPLv3.
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "scene.h"

int main(void) {
    // auto: the locale, LC_ALL before LC_MESSAGES before LANG
    unsetenv("LC_ALL");
    setenv("LC_MESSAGES", "de_DE.UTF-8", 1);
    setenv("LANG", "fr_FR.UTF-8", 1);
    CHECK(ui_str_lang_current() == 0);
    Scene* s = scene_new();   // settings_defaults: language auto
    CHECK(s->settings.language == 0);
    CHECK(ui_str_lang_current() == ui_lang_find("de"));
    CHECK(strcmp(ui_str(UIS_ROW_DISTANCE), "Abstand") == 0);
    CHECK(strcmp(ui_str(UIS_NATIVE_PC_LIST_TITLE), "Computers") == 0);   // no translation: English
    // the panels draw it: the cog's Screen tab differs from the English one
    // a second apply with another language: the run keeps the first
    Settings other = s->settings;
    other.language = 1 + ui_lang_find("ru");
    xrApplySettings(s->ctx, &other);
    CHECK(ui_str_lang_current() == ui_lang_find("de"));
    // what the panel draws comes from ui_str: the German row label is wider than the English one
    UiFont* font = ui_font_default();
    CHECK(font != NULL);
    if (font) {
        CHECK(ui_text_width(font, UI_WEIGHT_REGULAR, 22, ui_str(UIS_ROW_AIM_SENSITIVITY), -1) !=
              ui_text_width(font, UI_WEIGHT_REGULAR, 22, ui_str_lang(0, UIS_ROW_AIM_SENSITIVITY), -1));
    }
    scene_free(s);
    // settings_ui_language for a chosen one, and English for auto without a locale
    Settings x;
    settings_defaults(&x);
    x.language = 1 + ui_lang_find("ja");
    CHECK(settings_ui_language(&x) == ui_lang_find("ja"));
    unsetenv("LC_MESSAGES");
    unsetenv("LANG");
    x.language = 0;
    CHECK(settings_ui_language(&x) == 0);
    ui_str_set_lang(0);
    return checksDone("m4 lang");
}
