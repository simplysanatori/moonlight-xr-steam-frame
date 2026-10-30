// Native (lane artemis-settings): prints what the VR client reads from a config.ini, one
// "section.key = value" per setting, so tests/launcher can check the flat launcher's edits with the
// client's own parser.   mlxr-settings-dump <config.ini>   GPLv3.
// (lane launcher-bundle) mlxr-settings-dump --table: one line per setting, "section.key kind default min max
// names" (kind b, i, e or s; names joined by |, "-" for none), so the launcher's own table of the keys it shows
// (MlxrPrefs::vrKeyTable) can be checked against the client's.
#include <stdio.h>
#include <string.h>

#include "settings.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <config.ini> | --table\n", argv[0]);
        return 2;
    }
    Settings s;
    settings_defaults(&s);
    if (strcmp(argv[1], "--table") == 0) {
        int n = 0;
        const SettingDesc* t = settings_table(&n);
        for (int i = 0; i < n; i++) {
            const SettingDesc* d = &t[i];
            int v = d->type == SET_STR ? 0 : settings_get(&s, d);
            char kind = d->type == SET_BOOL ? 'b' : d->type == SET_INT ? 'i' : d->type == SET_ENUM ? 'e' : 's';
            printf("%s.%s %c ", d->section, d->key, kind);
            if (d->type == SET_STR) printf("%s", settings_get_str(&s, d)[0] ? settings_get_str(&s, d) : "-");
            else if (d->type == SET_BOOL) printf("%s", v ? "true" : "false");
            else if (d->type == SET_ENUM) printf("%s", d->names[v]);
            else printf("%d", v);
            if (d->type == SET_INT) printf(" %d %d ", d->min, d->max);
            else printf(" - - ");
            if (d->type == SET_ENUM) {
                for (int k = 0; k < d->nnames; k++) printf("%s%s", k ? "|" : "", d->names[k]);
            } else {
                printf("-");
            }
            printf("\n");
        }
        return 0;
    }
    SettingsLoadInfo info;
    if (settings_load(&s, argv[1], &info) != 0) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    int n = 0;
    const SettingDesc* t = settings_table(&n);
    for (int i = 0; i < n; i++) {
        const SettingDesc* d = &t[i];
        int v = d->type == SET_STR ? 0 : settings_get(&s, d);
        if (d->type == SET_STR) printf("%s.%s = %s\n", d->section, d->key, settings_get_str(&s, d));
        else if (d->type == SET_BOOL) printf("%s.%s = %s\n", d->section, d->key, v ? "true" : "false");
        else if (d->type == SET_ENUM) printf("%s.%s = %s\n", d->section, d->key, d->names[v]);
        else printf("%s.%s = %d\n", d->section, d->key, v);
    }
    printf("unknown %d, unreadable %d\n", info.unknown, info.bad);
    return 0;
}
