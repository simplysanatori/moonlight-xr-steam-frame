// Moonlight XR Native (lane wave4-apollo): what the fork adds to the vendored moonlight-common-c. GPLv3.
//
// The submodule is upstream's, which has no way to run one of Apollo's server commands: that is a
// packet of its own on the control stream, which only ControlStream.c can send. stream/CMakeLists.txt
// builds ControlStream.c with commonc_servercmd.inc appended (the submodule itself is never changed),
// which defines the function below in the library.
#pragma once
#include <stdint.h>

// Apollo's "execute server command" (ClassicOldSong's LiSendExecServerCmd): a reliable 4-byte packet
// {cmdId, 0, 0, 0} of type 0x3000 on the encrypted control stream. cmdId is the command's place in the
// <ServerCommand> list serverinfo gave. Returns 1 if the packet was handed to the connection, 0 if not
// (no control connection up, or not a Sunshine-family host with the encrypted control stream). There
// is no answer: the host runs the command, or has no such command or no permission, and says nothing.
int mlxrSendExecServerCmd(uint8_t cmdId);
