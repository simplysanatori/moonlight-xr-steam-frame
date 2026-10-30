// Translations of the headset UI (PLAN 6.1): every language answers every id (its own text or the
// English), its formats take the same values as the English ones, and the locale and code
// lookups. GPLv3.
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"
#include "settings.h"
#include "text.h"
#include "ui_strings.h"

// The printf conversions of a string, %% left out, as a string of their letters ("sd")
static void conv(const char* s, char* out, size_t cap) {
    size_t n = 0;
    for (const char* p = s; *p; p++) {
        if (*p != '%') continue;
        p++;
        if (*p == '%') continue;
        while (*p && strchr("-#+ 0123456789.$lhz", *p)) p++;
        if (!*p) break;
        if (n + 1 < cap) out[n++] = *p;
    }
    out[n] = 0;
}

static void test_tables(void) {
    CHECK(ui_lang_count() == 20);   // English and upstream's 19
    CHECK(strcmp(ui_lang_code(0), "en") == 0);
    CHECK(strcmp(ui_lang_code(ui_lang_count()), "") == 0);
    int translated_somewhere[UIS_COUNT] = { 0 };
    for (int l = 0; l < ui_lang_count(); l++) {
        CHECK(ui_lang_code(l)[0] != 0 && ui_lang_name(l)[0] != 0);
        CHECK(ui_lang_find(ui_lang_code(l)) == l);
        int own = 0;
        for (int id = 0; id < UIS_COUNT; id++) {
            const char* en = ui_str_lang(0, (UiStr)id);
            const char* t = ui_str_lang(l, (UiStr)id);
            // a text or the English fallback, never NULL or empty where the English is not
            CHECK(t != NULL);
            if (!t) continue;
            CHECK(en[0] == 0 || t[0] != 0);
            char a[16], b[16];
            conv(en, a, sizeof a);
            conv(t, b, sizeof b);
            if (strcmp(a, b) != 0) {
                fprintf(stderr, "%s %s: \"%s\" takes %s, the English %s\n", ui_lang_code(l), ui_str_key((UiStr)id), t, b, a);
            }
            CHECK(strcmp(a, b) == 0);
            if (ui_str_translated(l, (UiStr)id)) {
                own++;
                translated_somewhere[id] = 1;
                CHECK(l > 0);
            } else {
                CHECK(t == en);
            }
        }
        // every language upstream completed has the 111 keys it shares with this port
        if (l > 0) CHECK(own == 111);
    }
    // the strings written for this port are English everywhere (docs/dev/i18n.md lists them)
    CHECK(!translated_somewhere[UIS_NATIVE_PC_LIST_TITLE]);
    CHECK(!translated_somewhere[UIS_OK]);
    CHECK(!translated_somewhere[UIS_REPORT_SEND]);   // "Save" here, "Send" upstream
    CHECK(translated_somewhere[UIS_CANCEL]);         // upstream's applist_menu_cancel
    CHECK(translated_somewhere[UIS_ROW_DISTANCE]);
}

static void test_lookup(void) {
    int de = ui_lang_find("de");
    CHECK(de > 0);
    CHECK(ui_lang_find("DE") == de);
    CHECK(ui_lang_find("pt_br") == ui_lang_find("pt-BR") && ui_lang_find("pt-BR") > 0);
    CHECK(ui_lang_find("xx") == -1 && ui_lang_find("") == -1 && ui_lang_find(NULL) == -1);
    CHECK(ui_lang_from_locale("de_DE.UTF-8") == de);
    CHECK(ui_lang_from_locale("de_AT@euro") == de);
    CHECK(ui_lang_from_locale("de") == de);
    CHECK(ui_lang_from_locale("en_US.UTF-8") == 0);
    CHECK(ui_lang_from_locale("C") == 0 && ui_lang_from_locale("POSIX") == 0 && ui_lang_from_locale("") == 0);
    CHECK(ui_lang_from_locale(NULL) == 0);
    CHECK(ui_lang_from_locale("xx_YY") == 0);
    CHECK(ui_lang_from_locale("pt_BR.UTF-8") == ui_lang_find("pt-BR"));
    CHECK(ui_lang_from_locale("pt_PT") == ui_lang_find("pt-BR"));
    CHECK(ui_lang_from_locale("zh_CN.UTF-8") == ui_lang_find("zh-CN"));
    CHECK(ui_lang_from_locale("zh_SG") == ui_lang_find("zh-CN"));
    CHECK(ui_lang_from_locale("zh_TW.UTF-8") == ui_lang_find("zh-TW"));
    CHECK(ui_lang_from_locale("zh_HK") == ui_lang_find("zh-TW"));
    CHECK(ui_lang_from_locale("nn_NO") == ui_lang_find("nb") && ui_lang_from_locale("no") == ui_lang_find("nb"));
    CHECK(ui_lang_from_locale("ja_JP.UTF-8") == ui_lang_find("ja"));
    CHECK(ui_lang_script(ui_lang_find("ja")) == UI_SCRIPT_CJK_JA);
    CHECK(ui_lang_script(ui_lang_find("ru")) == UI_SCRIPT_CYRILLIC);
    CHECK(ui_lang_script(de) == UI_SCRIPT_LATIN);
    // the environment: LC_ALL, then LC_MESSAGES, then LANG
    setenv("LANG", "fr_FR.UTF-8", 1);
    unsetenv("LC_ALL");
    unsetenv("LC_MESSAGES");
    CHECK(ui_lang_from_env() == ui_lang_find("fr"));
    setenv("LC_MESSAGES", "de_DE.UTF-8", 1);
    CHECK(ui_lang_from_env() == de);
    setenv("LC_ALL", "C", 1);
    CHECK(ui_lang_from_env() == 0);
    unsetenv("LC_ALL");
    unsetenv("LC_MESSAGES");
    unsetenv("LANG");
    CHECK(ui_lang_from_env() == 0);
    // what ui_str answers
    CHECK(ui_str_lang_current() == 0);
    CHECK(strcmp(ui_str(UIS_ROW_DISTANCE), "Distance") == 0);
    ui_str_set_lang(de);
    CHECK(ui_str_lang_current() == de);
    CHECK(strcmp(ui_str(UIS_ROW_DISTANCE), "Abstand") == 0);
    CHECK(strcmp(ui_str(UIS_OK), "OK") == 0);   // English fallback
    ui_str_set_lang(999);
    CHECK(ui_str_lang_current() == 0 && strcmp(ui_str(UIS_ROW_DISTANCE), "Distance") == 0);
    ui_str_set_lang(-1);
    CHECK(ui_str_lang_current() == 0);
}

// [ui] language in config.ini: auto, en, then the languages in ui_strings.h's order
static void test_setting(void) {
    const SettingDesc* d = settings_find("ui.language");
    CHECK(d != NULL && d->type == SET_ENUM);
    if (!d) return;
    CHECK(d->nnames == 1 + ui_lang_count());
    CHECK(strcmp(d->names[0], "auto") == 0);
    for (int l = 0; l < ui_lang_count() && l + 1 < d->nnames; l++) CHECK(strcmp(d->names[l + 1], ui_lang_code(l)) == 0);
    Settings s;
    settings_defaults(&s);
    CHECK(s.language == 0 && d->def == 0);
    // written commented out at its default, in its own [ui] section
    char* text = settings_to_text(&s);
    CHECK(text && strstr(text, "\n[ui]\n") && strstr(text, "\n#language = auto\n"));
    free(text);
    SettingsLoadInfo info;
    settings_from_text(&s, "[ui]\nlanguage = de\n", &info);
    CHECK(info.applied == 1 && settings_ui_language(&s) == ui_lang_find("de"));
    text = settings_to_text(&s);
    CHECK(text && strstr(text, "\nlanguage = de\n"));
    Settings back;
    settings_defaults(&back);
    settings_from_text(&back, text, NULL);
    CHECK(settings_equal(&s, &back));
    free(text);
    // the locale's spelling and any case
    settings_from_text(&s, "[ui]\nlanguage = pt_BR\n", &info);
    CHECK(info.bad == 0 && settings_ui_language(&s) == ui_lang_find("pt-BR"));
    settings_from_text(&s, "[ui]\nlanguage = ZH-tw\n", &info);
    CHECK(info.bad == 0 && settings_ui_language(&s) == ui_lang_find("zh-TW"));
    settings_from_text(&s, "[ui]\nlanguage = en\n", &info);
    CHECK(s.language == 1 && settings_ui_language(&s) == 0);
    // an unknown one is not read: the value stays
    settings_from_text(&s, "[ui]\nlanguage = klingon\n", &info);
    CHECK(info.bad == 1 && s.language == 1);
    // auto follows the locale
    settings_from_text(&s, "[ui]\nlanguage = auto\n", &info);
    setenv("LANG", "sv_SE.UTF-8", 1);
    unsetenv("LC_ALL");
    unsetenv("LC_MESSAGES");
    CHECK(settings_ui_language(&s) == ui_lang_find("sv"));
    unsetenv("LANG");
    CHECK(settings_ui_language(&s) == 0);
}

// Chinese, Japanese and Korean: the system font search, the fallback face, and English when the
// font is missing or lacks a glyph. A stand-in font named like a CJK one (the bundled Noto Sans)
// is found but has no kana, so Japanese falls back to English with the reason.
static void test_cjk_fallback(void) {
    char dir[] = "/tmp/mlxr-i18n-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char sub[256], link[300], path[512], med[512];
    snprintf(sub, sizeof sub, "%s/opentype/noto", dir);
    char mid[200];
    snprintf(mid, sizeof mid, "%s/opentype", dir);
    mkdir(mid, 0755);
    mkdir(sub, 0755);
    int ri = -1, mi = -1;
    // nothing there
    CHECK(ui_font_find_cjk(dir, "JP", path, sizeof path, &ri, med, sizeof med, &mi) == 0 && path[0] == 0);
    setenv("MLXR_SYSTEM_FONT_DIR", dir, 1);
    char msg[768];
    int ja = ui_lang_find("ja");
    CHECK(ui_lang_start(ja, msg, sizeof msg) == 0 && ui_str_lang_current() == 0);
    CHECK(strstr(msg, "needs Noto Sans CJK") != NULL);
    // a NotoSansJP-Regular.otf, two levels down: found for JP, not for KR
    snprintf(link, sizeof link, "%s/NotoSansJP-Regular.otf", sub);
    CHECK(symlink(MLXR_ASSETS_DIR "/fonts/NotoSans-Regular.ttf", link) == 0);
    CHECK(ui_font_find_cjk(dir, "JP", path, sizeof path, &ri, med, sizeof med, &mi) == 1);
    CHECK(strcmp(path, link) == 0 && ri == 0 && med[0] == 0);
    CHECK(ui_font_find_cjk(dir, "KR", path, sizeof path, &ri, med, sizeof med, &mi) == 0);
    // its Medium beside it
    snprintf(link, sizeof link, "%s/NotoSansJP-Medium.otf", sub);
    CHECK(symlink(MLXR_ASSETS_DIR "/fonts/NotoSans-Medium.ttf", link) == 0);
    CHECK(ui_font_find_cjk(dir, "JP", path, sizeof path, &ri, med, sizeof med, &mi) == 1 && strcmp(med, link) == 0);
    // a NotoSansCJK file of another region does for any region, after the region's own
    snprintf(link, sizeof link, "%s/NotoSansCJKsc-Regular.otf", sub);
    CHECK(symlink(MLXR_ASSETS_DIR "/fonts/NotoSans-Regular.ttf", link) == 0);
    CHECK(ui_font_find_cjk(dir, "KR", path, sizeof path, &ri, med, sizeof med, &mi) == 1 && strcmp(path, link) == 0);
    CHECK(ui_font_find_cjk(dir, "JP", path, sizeof path, &ri, med, sizeof med, &mi) == 1 && strstr(path, "NotoSansJP-Regular"));
    // found and loaded, but no kana in it: English, and the fallback is let go
    CHECK(ui_lang_start(ja, msg, sizeof msg) == 0 && ui_str_lang_current() == 0);
    CHECK(strstr(msg, "no glyph for U+") != NULL);
    UiFont* font = ui_font_default();
    CHECK(font != NULL && !ui_font_has_fallback(font));
    // a language the bundled font draws needs nothing
    CHECK(ui_lang_start(ui_lang_find("ru"), msg, sizeof msg) == ui_lang_find("ru") && strstr(msg, "Русский"));
    CHECK(ui_lang_start(0, msg, sizeof msg) == 0 && ui_str_lang_current() == 0);
    unsetenv("MLXR_SYSTEM_FONT_DIR");
    snprintf(link, sizeof link, "%s/NotoSansJP-Regular.otf", sub);
    unlink(link);
    snprintf(link, sizeof link, "%s/NotoSansJP-Medium.otf", sub);
    unlink(link);
    snprintf(link, sizeof link, "%s/NotoSansCJKsc-Regular.otf", sub);
    unlink(link);
    rmdir(sub);
    rmdir(mid);
    rmdir(dir);
}

// The fallback face draws what the family lacks: the arrows Noto Sans has not, from DejaVu Sans
// when the system has it (a stand-in for a CJK font; skipped without it)
static void test_fallback_face(void) {
    static const char* const DEJAVU[] = { "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                          "/usr/share/fonts/TTF/DejaVuSans.ttf" };
    const char* path = NULL;
    for (size_t i = 0; i < 2; i++) {
        if (access(DEJAVU[i], R_OK) == 0) path = DEJAVU[i];
    }
    if (!path) {
        printf("ui_i18n: no DejaVu Sans here, fallback face drawing not checked\n");
        return;
    }
    UiFont* font = ui_font_open(MLXR_ASSETS_DIR "/fonts/NotoSans-Regular.ttf", MLXR_ASSETS_DIR "/fonts/NotoSans-Medium.ttf");
    CHECK(font != NULL);
    if (!font) return;
    static const char ARROW[] = "A\xE2\x86\x91";   // A and U+2191
    uint32_t miss = 0;
    CHECK(!ui_font_covers(font, UI_WEIGHT_REGULAR, ARROW, &miss) && miss == 0x2191);
    float before = ui_text_width(font, UI_WEIGHT_REGULAR, 40, ARROW, -1);
    CHECK(ui_font_set_fallback(font, path, 0, NULL, 0) == 1 && ui_font_has_fallback(font));
    CHECK(ui_font_covers(font, UI_WEIGHT_REGULAR, ARROW, NULL) && ui_font_covers(font, UI_WEIGHT_MEDIUM, ARROW, NULL));
    CHECK(!ui_font_has(font, UI_WEIGHT_REGULAR, 0x2191));   // the family's own answer is unchanged
    float after = ui_text_width(font, UI_WEIGHT_REGULAR, 40, ARROW, -1);
    CHECK(after != before && after > ui_text_width(font, UI_WEIGHT_REGULAR, 40, "A", -1));
    // the Latin part is the family's, to the pixel
    CHECK_NEAR(ui_text_width(font, UI_WEIGHT_REGULAR, 22, "Distance", -1), 89.122, 0.01);
    // it draws ink where the arrow is, and nothing is missing
    uint8_t px[120 * 60 * 4];
    memset(px, 0, sizeof px);
    UiCanvas cv;
    ui_cv_init(&cv, px, 120, 60);
    ui_text_draw(&cv, font, UI_WEIGHT_REGULAR, 40, 4, 44, UI_ALIGN_LEFT, 0xFFFFFFFFu, ARROW, -1);
    int ink = 0;
    for (int y = 0; y < 60; y++)
        for (int x = 34; x < 120; x++) ink += px[(y * 120 + x) * 4 + 3] > 0;
    CHECK(ink > 20);
    float above = 0, below = 0;
    ui_text_ink_extent(font, UI_WEIGHT_REGULAR, 40, "gj", &above, &below);
    CHECK(below > 5.0f && above > 15.0f);
    ui_text_ink_extent(font, UI_WEIGHT_REGULAR, 40, "Mode", NULL, &below);
    CHECK(below < 1.0f);
    ui_font_clear_fallback(font);
    CHECK(!ui_font_has_fallback(font) && !ui_font_covers(font, UI_WEIGHT_REGULAR, ARROW, NULL));
    CHECK_NEAR(ui_text_width(font, UI_WEIGHT_REGULAR, 40, ARROW, -1), before, 0.001);
    CHECK(ui_font_set_fallback(font, "/nonexistent.ttc", 0, NULL, 0) == 0 && !ui_font_has_fallback(font));
    ui_font_close(font);
}

int main(void) {
    test_tables();
    test_lookup();
    test_setting();
    test_cjk_fallback();
    test_fallback_face();
    return checksDone("ui_i18n");
}
