// Native (lane artemis-settings): prints what the VR client reads from a config.ini, one
// "section.key = value" per setting, so tests/launcher can check the flat launcher's edits with the
// client's own parser.   mlxr-settings-dump <config.ini>   GPLv3.
#include <stdio.h>

#include "settings.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <config.ini>\n", argv[0]);
        return 2;
    }
    Settings s;
    settings_defaults(&s);
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
