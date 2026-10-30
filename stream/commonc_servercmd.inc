/* Moonlight XR Native (lane wave4-apollo): appended to moonlight-common-c's ControlStream.c at build
 * time (stream/CMakeLists.txt), so it sees the file's statics. GPLv3.
 *
 * Apollo's "execute server command" is ClassicOldSong's LiSendExecServerCmd, which his fork sends as
 * packetTypes[IDX_EXEC_SERVER_CMD] = 0x3000 in the encrypted control stream's table (the same table
 * also has 0x3001 set clipboard and 0x3002 file transfer nonce, which the fork does over HTTPS or not
 * at all). This vendored copy has no such table slot, and the type is only valid in that one table,
 * so it is spelt out here, with the same channel Start B and the loss stats use. */
int mlxrSendExecServerCmd(uint8_t cmdId) {
    uint8_t payload[4] = { cmdId, 0, 0, 0 };

    // Before the control connection is up or after it is gone there is nothing to send on; and the type
    // exists only on the encrypted control stream of a Sunshine-family host (Apollo is one)
    if (!encryptedControlStream || !IS_SUNSHINE() || client == NULL || peer == NULL) {
        return 0;
    }

    return sendMessageAndForget(0x3000,
                                sizeof(payload),
                                payload,
                                CTRL_CHANNEL_GENERIC,
                                ENET_PACKET_FLAG_RELIABLE,
                                false) ? 1 : 0;
}
