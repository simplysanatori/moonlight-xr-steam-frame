/* Native (lane wave4-apollo): what the Host sheet may offer on a host: Apollo's server commands and
 * its clipboard, with the permission gating Apollo applies. The commands themselves are listed in
 * serverinfo (computer.c) and run over the control stream by index (stream/), so there is no HTTP
 * call here; this is the decision and the words for it. GPLv3. */
#include "internal.h"

#include <strings.h>

mlxr_hostfeat mlxr_server_cmd_gate(const mlxr_computer *c, int index)
{
    if (!c) return MLXR_HOSTFEAT_NOT_APOLLO;
    /* Apollo before 0.2.0 sends commands but no <Permission>; any command is proof enough */
    if (!mlxr_computer_is_apollo(c) && c->n_server_cmds == 0) return MLXR_HOSTFEAT_NOT_APOLLO;
    if (c->permission >= 0 && !(c->permission & MLXR_PERM_SERVER_CMD)) return MLXR_HOSTFEAT_NO_PERMISSION;
    if (c->n_server_cmds <= 0) return MLXR_HOSTFEAT_NONE_CONFIGURED;
    if (index >= c->n_server_cmds || index < -1) return MLXR_HOSTFEAT_BAD_INDEX;
    return MLXR_HOSTFEAT_OK;
}

int mlxr_server_cmd_find(const mlxr_computer *c, const char *name)
{
    if (!c || !name) return -1;
    for (int i = 0; i < c->n_server_cmds && i < 8; i++)
        if (!strcasecmp(c->server_cmds[i], name)) return i;
    return -1;
}

mlxr_hostfeat mlxr_server_cmd_resolve(const mlxr_computer *c, const char *name, int *index)
{
    mlxr_hostfeat f = mlxr_server_cmd_gate(c, -1);
    if (f != MLXR_HOSTFEAT_OK) return f;
    int i = mlxr_server_cmd_find(c, name);
    if (i < 0) return MLXR_HOSTFEAT_BAD_INDEX;
    if (index) *index = i;
    return MLXR_HOSTFEAT_OK;
}

mlxr_hostfeat mlxr_clipboard_gate(const mlxr_computer *c, bool write)
{
    if (!c || !mlxr_computer_is_apollo(c)) return MLXR_HOSTFEAT_NOT_APOLLO;
    int bit = write ? MLXR_PERM_CLIPBOARD_SET : MLXR_PERM_CLIPBOARD_READ;
    if (c->permission >= 0 && !(c->permission & bit)) return MLXR_HOSTFEAT_NO_PERMISSION;
    return MLXR_HOSTFEAT_OK;
}

const char *mlxr_hostfeat_text(mlxr_hostfeat f)
{
    switch (f) {
    case MLXR_HOSTFEAT_OK: return "";
    case MLXR_HOSTFEAT_NOT_APOLLO: return "Not supported by this host";
    case MLXR_HOSTFEAT_NO_PERMISSION: return "This host has not allowed it for the Frame";
    case MLXR_HOSTFEAT_NONE_CONFIGURED: return "This host has no commands set up";
    case MLXR_HOSTFEAT_BAD_INDEX: return "The host has no such command";
    }
    return "";
}

const char *mlxr_hostfeat_http_text(int status)
{
    switch (status) {
    case 401: return "This host has not allowed it for the Frame";
    case 403: return "The host sees no stream from this client";
    case 404: return "Not supported by this host";
    default: return NULL;
    }
}
