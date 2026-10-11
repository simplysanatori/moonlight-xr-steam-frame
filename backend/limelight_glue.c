/* GPLv3 */
#include "backend_limelight.h"
#include <string.h>

mlxr_status mlxr_launch_params_init_ll(mlxr_launch_params *p)
{
    mlxr_status st = mlxr_launch_params_init(p);
    p->extra_query = LiGetLaunchUrlQueryParameters();
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
