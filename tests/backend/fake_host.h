/* A fake Sunshine/GFE host for the backend tests: HTTP + HTTPS (client-cert aware)
 * listeners on 127.0.0.1 speaking the real pairing protocol with the same
 * primitives the client uses. GPLv3. */
#ifndef FAKE_HOST_H
#define FAKE_HOST_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *pin;            /* PIN the "user" typed on the host (default "1234") */
    int gen;                    /* server major version: 7 -> SHA-256, 6 -> SHA-1 (default 7) */
    bool gfe;                   /* state says MJOLNIR, uniqueid placeholder semantics */
    bool busy_pairing;          /* stage 1 returns an empty plaincert */
    bool bad_secret_signature;  /* stage 3 secret signature is corrupted (MITM) */
    bool cancel_is_noop;        /* /cancel succeeds but does not end the game (not the owner) */
    bool zero_mac;              /* serverinfo reports mac 00:00:00:00:00:00 */
    const char *app_title_extra;/* extra app with this title (e.g. with entities) */
    bool expired_cert;          /* server cert validity ended a year ago (pinning must still accept it)*/
    bool wait_for_pin;          /* stage 1 blocks until fake_host_set_pin(), like Sunshine waiting for the web UI */
    /* Native (lane artemis-settings): Apollo's serverinfo fields and actions/clipboard (raw text,
     * 403 unless fake_host_set_streaming(true), 401 with clipboard_denied) */
    bool apollo;
    bool clipboard_denied;
} fake_opts;

typedef struct fake_host fake_host;
fake_host *fake_host_start(const fake_opts *o);
void fake_host_stop(fake_host *h);
uint16_t fake_host_http_port(const fake_host *h);
uint16_t fake_host_https_port(const fake_host *h);
const char *fake_host_cert_pem(const fake_host *h);
void fake_host_set_pin(fake_host *h, const char *pin);
bool fake_host_is_paired(const fake_host *h);
int fake_host_current_game(const fake_host *h);
/* copy of the last launch/resume query ("" if none) */
void fake_host_last_launch(const fake_host *h, char *out, int cap);
/* uniqueid seen on the most recent request */
void fake_host_last_uniqueid(const fake_host *h, char *out, int cap);
int fake_host_count(const fake_host *h, const char *command);
/* Native (lane artemis-settings): Apollo clipboard state */
void fake_host_set_streaming(fake_host *h, bool on);
void fake_host_set_clipboard(fake_host *h, const char *text);
void fake_host_clipboard(fake_host *h, char *out, int cap);
#endif
