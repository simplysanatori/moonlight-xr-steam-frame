/* Glue between the backend and moonlight-common-c (Limelight.h). GPLv3.
 * Only built when the moonlight-common-c target exists (see backend/CMakeLists.txt). */
#ifndef MLXR_BACKEND_LIMELIGHT_H
#define MLXR_BACKEND_LIMELIGHT_H

#include "backend.h"
#include <Limelight.h>

#ifdef __cplusplus
extern "C" {
#endif

/* mlxr_launch_params_init + extra_query = LiGetLaunchUrlQueryParameters() ("&corever=N").
 * Native (lane wave4-launcher): plus "&appuuid=<uuid>" when MLXR_APP_UUID holds a UUID (below). */
mlxr_status mlxr_launch_params_init_ll(mlxr_launch_params *p);

/* Launch by app UUID (Apollo): `base` followed by "&appuuid=<uuid>" in `out` when `uuid` is a UUID
 * (8-4-4-4-12 hex digits), else `base` itself. The flat launcher reads the app's UUID from an Apollo
 * host's app list just before the stream and passes it in MLXR_APP_UUID; Apollo matches the app by
 * appid or appuuid and tells the running app by its UUID, other hosts ignore the field. */
const char *mlxr_appuuid_query(const char *base, const char *uuid, char *out, size_t cap);

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
