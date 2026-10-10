/* Glue between the backend and moonlight-common-c (Limelight.h). GPLv3.
 * Only built when the moonlight-common-c target exists (see backend/CMakeLists.txt). */
#ifndef MLXR_BACKEND_LIMELIGHT_H
#define MLXR_BACKEND_LIMELIGHT_H

#include "backend.h"
#include <Limelight.h>

#ifdef __cplusplus
extern "C" {
#endif

/* mlxr_launch_params_init + extra_query = LiGetLaunchUrlQueryParameters() ("&corever=N"). */
mlxr_status mlxr_launch_params_init_ll(mlxr_launch_params *p);

/* Copy the launch-time choices into STREAM_CONFIGURATION: width/height/fps,
 * audioConfiguration and remoteInputAesKey/Iv (identical to the rikey/rikeyid
 * the host was told). The caller still sets bitrate, packetSize,
 * supportedVideoFormats, colorSpace/Range, encryptionFlags, clientRefreshRateX100.
 * Calls LiInitializeStreamConfiguration first. */
void mlxr_fill_stream_config(PSTREAM_CONFIGURATION cfg, const mlxr_launch_params *p);

/* Fill SERVER_INFORMATION. All strings are borrowed: `address`, `c` and
 * `rtsp_url` must stay alive until LiStartConnection() returns. */
void mlxr_fill_server_info(PSERVER_INFORMATION si, const char *address, const mlxr_computer *c, const char *rtsp_url);

#ifdef __cplusplus
}
#endif
#endif
