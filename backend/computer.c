/* serverinfo -> mlxr_computer (nvcomputer.cpp NvComputer(NvHTTP&, serverInfo)). GPLv3. */
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_tag(const mlxr_xml *x, const char *tag, char *dst, size_t cap)
{
    char *v = mlxr_xml_get(x, tag);
    mlxr_strlcpy(dst, v ? v : "", cap);
    free(v);
    mlxr_trim(dst);
}

static unsigned parse_port(const char *s)
{
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    return (end != s && v > 0 && v <= 65535) ? (unsigned)v : 0;
}

static bool parse_mac(const char *s, uint8_t mac[6])
{
    unsigned b[6];
    char tail;
    if (sscanf(s, "%x:%x:%x:%x:%x:%x%c", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &tail) != 6) return false;
    int nz = 0;
    for (int i = 0; i < 6; i++) {
        if (b[i] > 255) return false;
        mac[i] = (uint8_t)b[i];
        nz |= b[i];
    }
    return nz != 0;   /* "00:00:00:00:00:00" means unknown */
}

mlxr_status mlxr_computer_parse(const char *xml, size_t len, uint16_t http_port, mlxr_computer *out, mlxr_error *err)
{
    memset(out, 0, sizeof *out);
    mlxr_xml *x = mlxr_xml_parse(xml, len);
    if (!x) return mlxr_fail(err, MLXR_E_XML, -1, "Malformed XML (missing root element)");
    int code = 0;
    char msg[160] = "";
    if (!mlxr_xml_status(x, &code, msg, sizeof msg)) {
        mlxr_xml_free(x);
        return mlxr_fail(err, MLXR_E_XML, -1, "Malformed XML (missing root element)");
    }
    if (code != 200) {
        mlxr_xml_free(x);
        return mlxr_fail(err, MLXR_E_HTTP, code, "host returned status %d: %s", code, msg);
    }

    char tmp[64];
    copy_tag(x, "hostname", out->name, sizeof out->name);
    if (!out->name[0]) strcpy(out->name, "UNKNOWN");
    copy_tag(x, "uniqueid", out->uuid, sizeof out->uuid);
    copy_tag(x, "mac", tmp, sizeof tmp);
    out->has_mac = parse_mac(tmp, out->mac);

    copy_tag(x, "ServerCodecModeSupport", tmp, sizeof tmp);
    out->server_codec_mode_support = tmp[0] ? (int)strtol(tmp, NULL, 10) : 0x1 /* SCM_H264 */;
    copy_tag(x, "MaxLumaPixelsHEVC", tmp, sizeof tmp);
    out->max_luma_pixels_hevc = tmp[0] ? (int)strtol(tmp, NULL, 10) : 0;

    /* modes, sorted ascending by w*h*refresh like moonlight-qt */
    size_t nm = mlxr_xml_count(x, "DisplayMode");
    for (size_t i = 0; i < nm && out->n_modes < MLXR_MAX_DISPLAY_MODES; i++) {
        char *w = mlxr_xml_get_nth(x, "DisplayMode", i, "Width");
        char *h = mlxr_xml_get_nth(x, "DisplayMode", i, "Height");
        char *r = mlxr_xml_get_nth(x, "DisplayMode", i, "RefreshRate");
        mlxr_display_mode *m = &out->modes[out->n_modes++];
        m->width = w ? atoi(w) : 0;
        m->height = h ? atoi(h) : 0;
        m->refresh_rate = r ? atoi(r) : 0;
        free(w); free(h); free(r);
    }
    for (int i = 1; i < out->n_modes; i++) {   /* stable insertion sort */
        mlxr_display_mode m = out->modes[i];
        int64_t k = (int64_t)m.width * m.height * m.refresh_rate;
        int j = i - 1;
        while (j >= 0 && (int64_t)out->modes[j].width * out->modes[j].height * out->modes[j].refresh_rate > k) {
            out->modes[j + 1] = out->modes[j];
            j--;
        }
        out->modes[j + 1] = m;
    }

    /* a loopback LocalIP shows up behind the GS IPv6 forwarder */
    copy_tag(x, "LocalIP", out->local_ip, sizeof out->local_ip);
    if (strncmp(out->local_ip, "127.", 4) == 0) out->local_ip[0] = 0;
    out->http_port = http_port ? http_port : MLXR_DEFAULT_HTTP_PORT;

    copy_tag(x, "HttpsPort", tmp, sizeof tmp);
    unsigned hp = parse_port(tmp);
    out->https_port = hp ? (uint16_t)hp : MLXR_DEFAULT_HTTPS_PORT;

    /* Sunshine extension: dynamic WAN HTTP port */
    copy_tag(x, "ExternalPort", tmp, sizeof tmp);
    unsigned ep = parse_port(tmp);
    out->external_port = ep ? (uint16_t)ep : out->http_port;
    copy_tag(x, "ExternalIP", out->external_ip, sizeof out->external_ip);

    /* Only real GFE/RTX Experience says MJOLNIR in <state> */
    copy_tag(x, "state", out->state, sizeof out->state);
    out->is_nvidia = strstr(out->state, "MJOLNIR") != NULL;

    copy_tag(x, "PairStatus", tmp, sizeof tmp);
    out->paired = strcmp(tmp, "1") == 0;

    /* GFE 2.8+ keeps currentgame at the last game: only trust it while *_SERVER_BUSY */
    size_t sl = strlen(out->state);
    if (sl >= 12 && strcmp(out->state + sl - 12, "_SERVER_BUSY") == 0) {
        copy_tag(x, "currentgame", tmp, sizeof tmp);
        out->current_game = (int)strtoll(tmp, NULL, 10);
    }

    copy_tag(x, "appversion", out->app_version, sizeof out->app_version);
    copy_tag(x, "GfeVersion", out->gfe_version, sizeof out->gfe_version);
    copy_tag(x, "gputype", out->gpu, sizeof out->gpu);
    mlxr_xml_free(x);
    return MLXR_OK;
}
