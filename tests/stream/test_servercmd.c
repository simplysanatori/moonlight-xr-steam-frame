// Moonlight XR Native (lane wave4-apollo): Apollo's server command on the control stream.
//
// There is no Apollo to talk to here, so this holds the two things that can be checked: that what is
// built into the vendored common-c (stream/commonc_servercmd.inc, appended to ControlStream.c) sends
// nothing without a control connection and says so, and that the file has the packet Apollo's own
// LiSendExecServerCmd sends (type 0x3000, the 4 bytes {cmdId, 0, 0, 0}, reliable, on the encrypted
// control stream). A real command running is for the owner's Apollo PC. GPLv3.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../stream/commonc_ext.h"
#include "../../stream/session.h"
#include "../xrcore/check.h"

#ifndef SERVERCMD_INC
#error "SERVERCMD_INC must point at stream/commonc_servercmd.inc"
#endif

static char* slurp(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    char* buf = malloc(1 << 16);
    size_t n = buf ? fread(buf, 1, (1 << 16) - 1, f) : 0;
    if (buf) buf[n] = '\0';
    fclose(f);
    return buf;
}

int main(void) {
    // no stream, no control connection: nothing is sent, for any number
    CHECK(mlxrSendExecServerCmd(0) == 0);
    CHECK(mlxrSendExecServerCmd(7) == 0);
    CHECK(stream_exec_server_cmd(0) == 0);
    CHECK(stream_exec_server_cmd(3) == 0);
    CHECK(stream_exec_server_cmd(-1) == 0);
    CHECK(stream_exec_server_cmd(256) == 0);

    // the packet, as Apollo's client builds it
    char* inc = slurp(SERVERCMD_INC);
    CHECK(inc != NULL);
    if (inc != NULL) {
        CHECK(strstr(inc, "uint8_t payload[4] = { cmdId, 0, 0, 0 };") != NULL);
        CHECK(strstr(inc, "sendMessageAndForget(0x3000,") != NULL);
        CHECK(strstr(inc, "ENET_PACKET_FLAG_RELIABLE") != NULL);
        CHECK(strstr(inc, "encryptedControlStream") != NULL);
        free(inc);
    }
    return checksDone("servercmd");
}
