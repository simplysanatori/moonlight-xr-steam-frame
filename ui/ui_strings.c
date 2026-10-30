// See strings.h. GPLv3.
#include "ui_strings.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "text.h"

static const char* const TEXT[UIS_COUNT] = {
#define X(id, key, en) en,
    UI_STRINGS(X)
#undef X
};
static const char* const KEYS[UIS_COUNT] = {
#define X(id, key, en) key,
    UI_STRINGS(X)
#undef X
};

const char* ui_str(UiStr id) {
    return ui_str_lang(ui_str_lang_current(), id);
}

const char* ui_str_key(UiStr id) {
    return (unsigned)id < (unsigned)UIS_COUNT ? KEYS[id] : "";
}

// ---- languages ------------------------------------------------------------------------------------

#define UI_I18N_TABLES 1
#include "ui_strings_i18n.inc"

typedef struct {
    const char* code;
    const char* name;
    int script;
    const char* const* text;   // NULL for English
} Lang;

static const Lang LANGS[] = {
    { "en", "English", UI_SCRIPT_LATIN, NULL },
#define X(ident, code, name, script) { code, name, script, I18N_##ident },
    UI_I18N_LANGS(X)
#undef X
};
#define LANG_COUNT ((int)(sizeof(LANGS) / sizeof(LANGS[0])))

static int g_lang;

int ui_lang_count(void) { return LANG_COUNT; }

const char* ui_lang_code(int lang) { return lang >= 0 && lang < LANG_COUNT ? LANGS[lang].code : ""; }

const char* ui_lang_name(int lang) { return lang >= 0 && lang < LANG_COUNT ? LANGS[lang].name : ""; }

int ui_lang_script(int lang) { return lang >= 0 && lang < LANG_COUNT ? LANGS[lang].script : UI_SCRIPT_LATIN; }

// "pt_br" and "PT-BR" are the same code
static int code_eq(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char x = a[i] == '_' ? '-' : (char)tolower((unsigned char)a[i]);
        char y = b[i] == '_' ? '-' : (char)tolower((unsigned char)b[i]);
        if (x != y) return 0;
        if (!x) return 1;
    }
    return 1;
}

int ui_lang_find(const char* code) {
    if (!code) return -1;
    size_t n = strlen(code);
    for (int i = 0; i < LANG_COUNT; i++) {
        if (strlen(LANGS[i].code) == n && code_eq(code, LANGS[i].code, n)) return i;
    }
    return -1;
}

int ui_lang_from_locale(const char* locale) {
    if (!locale || !*locale) return 0;
    // language[_territory][.codeset][@modifier]
    char lang[8] = { 0 }, terr[8] = { 0 };
    size_t i = 0, j = 0;
    while (locale[i] && isalpha((unsigned char)locale[i]) && i < sizeof lang - 1) {
        lang[i] = (char)tolower((unsigned char)locale[i]);
        i++;
    }
    if (locale[i] == '_' || locale[i] == '-') {
        i++;
        while (locale[i] && isalnum((unsigned char)locale[i]) && j < sizeof terr - 1) {
            terr[j++] = (char)toupper((unsigned char)locale[i]);
            i++;
        }
    }
    if (!lang[0]) return 0;
    int found = -1;
    if (strcmp(lang, "pt") == 0) {
        // Brazilian Portuguese is the only Portuguese the table has
        found = ui_lang_find("pt-BR");
    } else if (strcmp(lang, "zh") == 0) {
        // Traditional characters in Taiwan, Hong Kong and Macau; Simplified elsewhere ("Hant"
        // as a script code in a territory's place counts too)
        int trad = strcmp(terr, "TW") == 0 || strcmp(terr, "HK") == 0 || strcmp(terr, "MO") == 0 ||
                   strcmp(terr, "HANT") == 0;
        found = ui_lang_find(trad ? "zh-TW" : "zh-CN");
    } else if (strcmp(lang, "no") == 0 || strcmp(lang, "nn") == 0 || strcmp(lang, "nb") == 0) {
        found = ui_lang_find("nb");
    } else {
        found = ui_lang_find(lang);
    }
    return found > 0 ? found : 0;
}

int ui_lang_from_env(void) {
    static const char* const VARS[] = { "LC_ALL", "LC_MESSAGES", "LANG" };
    for (size_t i = 0; i < sizeof VARS / sizeof VARS[0]; i++) {
        const char* v = getenv(VARS[i]);
        if (v && *v) return ui_lang_from_locale(v);
    }
    return 0;
}

const char* ui_str_lang(int lang, UiStr id) {
    if ((unsigned)id >= (unsigned)UIS_COUNT) return "";
    if (lang > 0 && lang < LANG_COUNT && LANGS[lang].text[id]) return LANGS[lang].text[id];
    return TEXT[id];
}

int ui_str_translated(int lang, UiStr id) {
    return (unsigned)id < (unsigned)UIS_COUNT && lang > 0 && lang < LANG_COUNT && LANGS[lang].text[id] != NULL;
}

void ui_str_set_lang(int lang) { g_lang = lang > 0 && lang < LANG_COUNT ? lang : 0; }

int ui_str_lang_current(void) { return g_lang; }

int ui_lang_start(int lang, char* msg, size_t cap) {
    char dummy[8];
    if (!msg || !cap) { msg = dummy; cap = sizeof dummy; }
    if (lang <= 0 || lang >= LANG_COUNT) {
        ui_str_set_lang(0);
        snprintf(msg, cap, "UI language en (English)");
        return 0;
    }
    UiFont* font = ui_font_default();
    int script = LANGS[lang].script;
    const char* region = script == UI_SCRIPT_CJK_JA ? "JP" : script == UI_SCRIPT_CJK_KO ? "KR"
                       : script == UI_SCRIPT_CJK_SC ? "SC" : script == UI_SCRIPT_CJK_TC ? "TC" : NULL;
    char reg[512] = "", med[512] = "";
    int ri = 0, mi = 0;
    if (region) {
        const char* root = getenv("MLXR_SYSTEM_FONT_DIR");
        if (!root || !*root) root = "/usr/share/fonts";
        if (!font || !ui_font_find_cjk(root, region, reg, sizeof reg, &ri, med, sizeof med, &mi)) {
            ui_str_set_lang(0);
            snprintf(msg, cap, "UI language %s (%s) needs Noto Sans CJK, which is not under %s: English instead",
                     LANGS[lang].code, LANGS[lang].name, root);
            return 0;
        }
        if (!ui_font_set_fallback(font, reg, ri, med[0] ? med : NULL, mi)) {
            ui_str_set_lang(0);
            snprintf(msg, cap, "UI language %s (%s): %s did not load: English instead", LANGS[lang].code,
                     LANGS[lang].name, reg);
            return 0;
        }
    }
    // every string the language has must draw: no .notdef boxes
    for (int id = 0; font && id < UIS_COUNT; id++) {
        const char* t = LANGS[lang].text[id];
        uint32_t cp = 0;
        if (t && (!ui_font_covers(font, UI_WEIGHT_REGULAR, t, &cp) || !ui_font_covers(font, UI_WEIGHT_MEDIUM, t, &cp))) {
            if (region) ui_font_clear_fallback(font);
            ui_str_set_lang(0);
            snprintf(msg, cap, "UI language %s (%s): no glyph for U+%04X (%s): English instead", LANGS[lang].code,
                     LANGS[lang].name, (unsigned)cp, KEYS[id]);
            return 0;
        }
    }
    ui_str_set_lang(lang);
    if (region) {
        snprintf(msg, cap, "UI language %s (%s), its characters from %s (face %d)%s%s", LANGS[lang].code,
                 LANGS[lang].name, reg, ri, med[0] ? " and " : "", med);
    } else {
        snprintf(msg, cap, "UI language %s (%s)", LANGS[lang].code, LANGS[lang].name);
    }
    return lang;
}
