// Translations of the headset UI (PLAN 6.1): every language answers every id (its own text or the
// English), its formats take the same values as the English ones, and the locale and code
// lookups. GPLv3.
#include <stdlib.h>
#include <string.h>

#include "check.h"
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

int main(void) {
    test_tables();
    test_lookup();
    return checksDone("ui_i18n");
}
