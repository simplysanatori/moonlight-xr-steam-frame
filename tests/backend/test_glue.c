/* The moonlight-common-c glue (only built when that target exists). GPLv3. */
#include "backend_limelight.h"
#include "check.h"

int main(void)
{
    /* our audio macros must agree with Limelight.h */
    CHECK_EQ_INT(MLXR_AUDIO_STEREO, AUDIO_CONFIGURATION_STEREO);
    CHECK_EQ_INT(MLXR_AUDIO_51, AUDIO_CONFIGURATION_51_SURROUND);
    CHECK_EQ_INT(MLXR_AUDIO_71, AUDIO_CONFIGURATION_71_SURROUND);

    mlxr_launch_params p;
    CHECK_OK(mlxr_launch_params_init_ll(&p));
    CHECK(p.extra_query && strstr(p.extra_query, "corever=") != NULL);
    p.width = 3200; p.height = 1800; p.fps = 90; p.audio_configuration = MLXR_AUDIO_51;

    STREAM_CONFIGURATION cfg;
    mlxr_fill_stream_config(&cfg, &p);
    CHECK_EQ_INT(cfg.width, 3200);
    CHECK_EQ_INT(cfg.height, 1800);
    CHECK_EQ_INT(cfg.fps, 90);
    CHECK_EQ_INT(cfg.clientRefreshRateX100, 9000);
    CHECK_EQ_INT(cfg.audioConfiguration, AUDIO_CONFIGURATION_51_SURROUND);
    CHECK(memcmp(cfg.remoteInputAesKey, p.ri_key, 16) == 0);
    CHECK(memcmp(cfg.remoteInputAesIv, p.ri_iv, 16) == 0);
    /* rikeyid on the wire == big-endian first 4 bytes of the IV that common-c uses */
    int id;
    memcpy(&id, cfg.remoteInputAesIv, 4);
    id = (int)__builtin_bswap32((uint32_t)id);
    CHECK_EQ_INT(id, mlxr_launch_rikeyid(&p));

    /* a higher request leaves the glue as 90 (or the display's lower rate): fps and clientRefreshRateX100 */
    p.fps = 360;
    mlxr_fill_stream_config(&cfg, &p);
    CHECK_EQ_INT(cfg.fps, 90);
    CHECK_EQ_INT(cfg.clientRefreshRateX100, 9000);
    p.fps = 144; p.max_fps = mlxr_fps_cap_for_display(72);
    mlxr_fill_stream_config(&cfg, &p);
    CHECK_EQ_INT(cfg.fps, 72);
    CHECK_EQ_INT(cfg.clientRefreshRateX100, 7200);
    CHECK_EQ_INT(cfg.width, 3200);                       /* the rest of the mode stays */
    CHECK_EQ_INT(cfg.height, 1800);

    mlxr_computer c;
    memset(&c, 0, sizeof c);
    strcpy(c.app_version, "7.1.431.-1");
    c.server_codec_mode_support = SCM_H264 | SCM_HEVC | SCM_AV1_MAIN8;
    SERVER_INFORMATION si;
    mlxr_fill_server_info(&si, "192.168.1.10", &c, "rtspenc://192.168.1.10:48010");
    CHECK_EQ_STR(si.address, "192.168.1.10");
    CHECK_EQ_STR(si.serverInfoAppVersion, "7.1.431.-1");
    CHECK(si.serverInfoGfeVersion == NULL);             /* empty GfeVersion -> NULL */
    CHECK_EQ_STR(si.rtspSessionUrl, "rtspenc://192.168.1.10:48010");
    CHECK_EQ_INT(si.serverCodecModeSupport, c.server_codec_mode_support);
    strcpy(c.gfe_version, "3.23.0.74");
    mlxr_fill_server_info(&si, "h", &c, NULL);
    CHECK_EQ_STR(si.serverInfoGfeVersion, "3.23.0.74");
    CHECK(si.rtspSessionUrl == NULL);
    TEST_DONE();
}
