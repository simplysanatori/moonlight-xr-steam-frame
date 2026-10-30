/* GPLv3 */
#include "backend_limelight.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *mlxr_appuuid_query(const char *base, const char *uuid, char *out, size_t cap)
{
    if (!base) base = "";
    if (!uuid || strlen(uuid) != 36) return base;
    for (int i = 0; i < 36; i++) {
        bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? uuid[i] != '-' : !isxdigit((unsigned char)uuid[i])) return base;
    }
    int n = snprintf(out, cap, "%s&appuuid=%s", base, uuid);
    return (n > 0 && (size_t)n < cap) ? out : base;
}

mlxr_status mlxr_launch_params_init_ll(mlxr_launch_params *p)
{
    static char query[128];
    mlxr_status st = mlxr_launch_params_init(p);
    p->extra_query = mlxr_appuuid_query(LiGetLaunchUrlQueryParameters(), getenv("MLXR_APP_UUID"), query, sizeof query);
    return st;
}

void mlxr_fill_stream_config(PSTREAM_CONFIGURATION cfg, const mlxr_launch_params *p)
{
    LiInitializeStreamConfiguration(cfg);
    cfg->width = p->width;
    cfg->height = p->height;
    cfg->fps = mlxr_launch_fps(p);
    cfg->clientRefreshRateX100 = cfg->fps * 100;   /* what the host builds its display from, same cap */
    cfg->audioConfiguration = p->audio_configuration ? p->audio_configuration : AUDIO_CONFIGURATION_STEREO;
    memcpy(cfg->remoteInputAesKey, p->ri_key, sizeof cfg->remoteInputAesKey);
    memcpy(cfg->remoteInputAesIv, p->ri_iv, sizeof cfg->remoteInputAesIv);
}

void mlxr_fill_server_info(PSERVER_INFORMATION si, const char *address, const mlxr_computer *c, const char *rtsp_url)
{
    LiInitializeServerInformation(si);
    si->address = address;
    si->serverInfoAppVersion = c->app_version;
    si->serverCodecModeSupport = c->server_codec_mode_support;
    if (c->gfe_version[0]) si->serverInfoGfeVersion = c->gfe_version;   /* older GFE lacked it */
    if (rtsp_url && *rtsp_url) si->rtspSessionUrl = rtsp_url;           /* older GFE/Sunshine lacked it */
}
