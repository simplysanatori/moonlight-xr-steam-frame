/* Private definition of mlxr_server. GPLv3. */
#ifndef MLXR_SERVER_H
#define MLXR_SERVER_H
#include "internal.h"

struct mlxr_server {
    char address[128];
    uint16_t http_port;
    uint16_t https_port;
    bool is_gfe;
    char unique_id[17];
    char *client_cert;    /* PEM */
    char *client_key;     /* PEM */
    char *pinned_pem;     /* host cert, NULL until paired */
    char *pinned_pin;     /* "sha256//..." of its SPKI */
};
#endif
