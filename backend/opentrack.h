/* Native (lane wave4-headinput, PLAN 4.5): the head pose to opentrack on the PC, over UDP. A small
 * non-blocking sender for the frame loop: one socket, the target resolved once (a name on a thread
 * of its own, so a slow DNS answer never holds a frame up), and a send that drops the packet rather
 * than wait. The packet itself is xr/core/xr_opentrack.h's. GPLv3. */
#ifndef MLXR_OPENTRACK_H
#define MLXR_OPENTRACK_H

#include <stddef.h>
#include <stdint.h>

/* Any thread: the address of the PC the stream is connected to ("" or NULL when none). The sender's
 * target when opentrack_host is empty. */
void mlxr_opentrack_set_stream_host(const char *addr);

/* Frame loop: one packet to host:port, host NULL or "" for the stream's PC. Never blocks: the packet
 * is dropped while the target is being resolved, is not known, or the socket is busy. Returns 1 when
 * it went out, 0 when it was dropped, -1 when the target cannot be used (logged once per target). */
int mlxr_opentrack_send(const char *host, int port, const uint8_t *pkt, size_t len);

/* The socket closed and the target forgotten (the stream stopped, or the setting went off) */
void mlxr_opentrack_close(void);

/* What the sender resolved its target to, "address:port", for the log; 0 when nothing yet */
int mlxr_opentrack_target(char *out, size_t cap);

#endif
