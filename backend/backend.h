/* Moonlight XR Native - host backend (C port of moonlight-qt app/backend).
 * GPLv3.
 *
 * Everything here is blocking and thread-compatible (no global state except a
 * one-time curl init and an optional log hook). Functions return an
 * mlxr_status (MLXR_OK == 0) and, when the optional mlxr_error* is non-NULL,
 * fill it with a human-readable message. Secrets (private key, rikey, PIN) are
 * never logged.
 */
#ifndef MLXR_BACKEND_H
#define MLXR_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MLXR_DEFAULT_HTTP_PORT  47989
#define MLXR_DEFAULT_HTTPS_PORT 47984

typedef enum {
    MLXR_OK = 0,
    MLXR_E_ARG,           /* bad argument / missing prerequisite (e.g. no pinned cert) */
    MLXR_E_NOMEM,
    MLXR_E_IO,            /* config file I/O */
    MLXR_E_CRYPTO,        /* OpenSSL failure */
    MLXR_E_NET,           /* connect / transfer failure */
    MLXR_E_TIMEOUT,
    MLXR_E_TLS,           /* handshake failed or server cert != pinned cert (moonlight-qt maps this to 401) */
    MLXR_E_HTTP,          /* host answered with status_code != 200; error.status holds it */
    MLXR_E_XML,           /* malformed / unexpected response */
    MLXR_E_PIN_WRONG,     /* pairing: the PIN did not match */
    MLXR_E_PAIR_BUSY,     /* pairing: host already has a pairing in progress */
    MLXR_E_PAIR_FAILED,   /* pairing: host refused at some stage (message says which) */
    MLXR_E_MITM,          /* pairing: server secret signature did not verify */
    MLXR_E_NOT_PAIRED,
    MLXR_E_BUSY_OTHER     /* quit: another client owns the running session (moonlight-qt status 599) */
} mlxr_status;

typedef struct {
    mlxr_status code;
    int status;           /* MLXR_E_HTTP: host status_code; MLXR_E_NET/TLS: libcurl code */
    char msg[256];
} mlxr_error;

const char *mlxr_status_name(mlxr_status s);

/* Optional log hook (level 0 error, 1 info, 2 debug). Default: silent. */
typedef void (*mlxr_log_fn)(int level, const char *msg, void *ud);
void mlxr_set_log(mlxr_log_fn fn, void *ud);

/* ------------------------------------------------------------------ */
/* Identity: RSA-2048 client cert + key + unique id (identitymanager)  */
/* ------------------------------------------------------------------ */

typedef struct {
    char *cert_pem;       /* "-----BEGIN CERTIFICATE-----..." */
    char *key_pem;        /* PKCS#8 "-----BEGIN PRIVATE KEY-----..." */
    char unique_id[17];   /* 16 hex chars, sent as uniqueid= on every request */
} mlxr_identity;

/* Config dir: $MLXR_CONFIG_DIR, else ~/.config/moonlight-xr. Created 0700.
 * Returned string is malloc'd (caller frees); NULL on failure. */
char *mlxr_config_dir(mlxr_error *err);

/* Generate a fresh identity in memory (CN "NVIDIA GameStream Client", 20 years). */
mlxr_status mlxr_identity_generate(mlxr_identity *out, mlxr_error *err);
/* Load <dir>/client.pem, client.key, uniqueid; create (and persist, key 0600)
 * whatever is missing or unreadable, like IdentityManager. *created (nullable)
 * is set when a new keypair was generated. */
mlxr_status mlxr_identity_load_or_create(const char *dir, mlxr_identity *out, bool *created, mlxr_error *err);
void mlxr_identity_free(mlxr_identity *id);
/* SHA-256 fingerprint of the certificate DER as lowercase hex (65 bytes incl. NUL). */
mlxr_status mlxr_cert_fingerprint(const char *cert_pem, char out[65]);

/* ------------------------------------------------------------------ */
/* serverinfo (computermanager / nvcomputer)                           */
/* ------------------------------------------------------------------ */

typedef struct { int width, height, refresh_rate; } mlxr_display_mode;

#define MLXR_MAX_DISPLAY_MODES 32

typedef struct {
    char name[128];             /* <hostname>, "UNKNOWN" if missing */
    char uuid[64];              /* <uniqueid> */
    uint8_t mac[6];
    bool has_mac;               /* false when absent or 00:00:00:00:00:00 */
    char local_ip[64];          /* <LocalIP> ("" if loopback or absent) */
    char external_ip[64];       /* <ExternalIP> */
    uint16_t http_port;         /* port the request was made on */
    uint16_t https_port;        /* <HttpsPort>, default 47984 */
    uint16_t external_port;     /* <ExternalPort>, default http_port */
    bool is_nvidia;             /* <state> contains MJOLNIR (real GFE) */
    bool paired;                /* <PairStatus> == 1 (only meaningful over HTTPS) */
    bool via_https;             /* this answer came over the pinned HTTPS channel */
    int current_game;           /* app id of running session, 0 if none (BUSY semantics) */
    char state[64];             /* e.g. SUNSHINE_SERVER_FREE */
    char app_version[32];       /* <appversion> */
    char gfe_version[32];       /* <GfeVersion> (may be empty) */
    char gpu[64];               /* <gputype> */
    int server_codec_mode_support; /* SCM_* bitmask; SCM_H264 (1) if absent */
    int max_luma_pixels_hevc;   /* 0 if absent */
    mlxr_display_mode modes[MLXR_MAX_DISPLAY_MODES];
    int n_modes;
} mlxr_computer;

/* Parse a serverinfo response (checks <root status_code>). http_port is what
 * the request used (stored in out->http_port). */
mlxr_status mlxr_computer_parse(const char *xml, size_t len, uint16_t http_port,
                                mlxr_computer *out, mlxr_error *err);
/* "a.b.c.d" -> first component (server generation); 0 for empty/garbage. */
int mlxr_parse_major_version(const char *quad);

/* ------------------------------------------------------------------ */
/* HTTP(S) client to one host (nvhttp)                                 */
/* ------------------------------------------------------------------ */

typedef struct mlxr_server mlxr_server;

/* address: hostname, IPv4 or IPv6 literal (no brackets). The identity is copied. */
mlxr_server *mlxr_server_new(const mlxr_identity *id, const char *address, uint16_t http_port);
void mlxr_server_free(mlxr_server *s);
void mlxr_server_set_https_port(mlxr_server *s, uint16_t port);
uint16_t mlxr_server_https_port(const mlxr_server *s);
/* Pin the host's self-signed certificate (PEM). HTTPS is refused until set.
 * Pass NULL to clear. The TLS handshake succeeds only if the server presents
 * exactly this certificate's key (trust store = this cert only, plus a
 * public-key pin). */
mlxr_status mlxr_server_set_pinned_cert(mlxr_server *s, const char *pem, mlxr_error *err);
const char *mlxr_server_pinned_cert(const mlxr_server *s);
/* Real GFE hosts get the shared placeholder uid 0123456789ABCDEF (default false = true uid). */
void mlxr_server_set_gfe(mlxr_server *s, bool is_gfe);

/* GET /serverinfo: HTTPS first when a cert is pinned and the HTTPS port known
 * (falls back to HTTP on a TLS error), else HTTP then re-query over HTTPS.
 * raw_xml (nullable) receives the malloc'd response body. */
mlxr_status mlxr_server_get_info(mlxr_server *s, bool fast_fail, mlxr_computer *out,
                                 char **raw_xml, mlxr_error *err);

typedef struct {
    char *name;                 /* <AppTitle> (malloc'd, may be "") */
    int id;                     /* <ID> */
    bool hdr_supported;         /* <IsHdrSupported> */
    bool app_collector_game;    /* <IsAppCollectorGame> */
} mlxr_app;

/* GET /applist over pinned HTTPS. Sorted case-insensitively by name. */
mlxr_status mlxr_parse_applist(const char *xml, size_t len, mlxr_app **apps, size_t *n, mlxr_error *err);
mlxr_status mlxr_server_get_apps(mlxr_server *s, mlxr_app **apps, size_t *n, mlxr_error *err);
void mlxr_apps_free(mlxr_app *apps, size_t n);

/* ---- launch / resume / quit ---- */

/* Audio config as in Limelight.h: MAKE_AUDIO_CONFIGURATION(count, mask). */
#define MLXR_AUDIO_CONFIG(count, mask) ((((mask) & 0xFFFF) << 16) | ((count) << 8) | 0xCA)
#define MLXR_AUDIO_STEREO MLXR_AUDIO_CONFIG(2, 0x3)
#define MLXR_AUDIO_51     MLXR_AUDIO_CONFIG(6, 0x3F)
#define MLXR_AUDIO_71     MLXR_AUDIO_CONFIG(8, 0x63F)

typedef struct {
    int width, height, fps;
    bool sops;                  /* let the host change its desktop resolution */
    bool local_audio;           /* play audio on the host too */
    int audio_configuration;    /* MLXR_AUDIO_* */
    bool hdr;                   /* adds hdrMode=1 (10-bit formats requested) */
    bool is_gfe;                /* real GFE: fps>60 is sent as 0 */
    int gamepad_mask;           /* remoteControllersBitmap / gcmap */
    bool persist_gamepads;
    uint8_t ri_key[16];         /* rikey (also STREAM_CONFIGURATION.remoteInputAesKey) */
    uint8_t ri_iv[16];          /* bytes 0..3 = rikeyid big-endian (also ...AesIv) */
    const char *extra_query;    /* NULL -> "&corever=1" (LiGetLaunchUrlQueryParameters) */
} mlxr_launch_params;

/* Zero + defaults (1920x1080x60, stereo, sops on) + fresh random ri_key/ri_iv. */
mlxr_status mlxr_launch_params_init(mlxr_launch_params *p);
/* Parse "WxHxFPS" (e.g. "2560x1440x90"). */
mlxr_status mlxr_parse_mode(const char *s, int *w, int *h, int *fps);
/* rikeyid as sent on the wire (signed int32 from ri_iv[0..3], big endian). */
int32_t mlxr_launch_rikeyid(const mlxr_launch_params *p);
/* Build the query string (without uniqueid/uuid) for launch/resume; tests use it. */
mlxr_status mlxr_launch_query(int appid, const mlxr_launch_params *p, char *out, size_t cap);

/* /launch (resume=false) or /resume (resume=true) over pinned HTTPS.
 * *rtsp_url receives <sessionUrl0> (malloc'd; may be rtsp:// or rtspenc://). */
mlxr_status mlxr_server_start_app(mlxr_server *s, bool resume, int appid,
                                  const mlxr_launch_params *p, char **rtsp_url, mlxr_error *err);
/* Convenience: resume if comp->current_game != 0 (resuming that game), else launch appid. */
mlxr_status mlxr_server_launch_or_resume(mlxr_server *s, const mlxr_computer *comp, int appid,
                                         const mlxr_launch_params *p, char **rtsp_url, mlxr_error *err);
/* /cancel, then verifies via serverinfo that no game is running
 * (else MLXR_E_BUSY_OTHER, like moonlight-qt's synthetic 599). */
mlxr_status mlxr_server_quit_app(mlxr_server *s, mlxr_error *err);
/* /unpair over HTTP. */
mlxr_status mlxr_server_unpair(mlxr_server *s, mlxr_error *err);

/* ------------------------------------------------------------------ */
/* Pairing (nvpairingmanager)                                          */
/* ------------------------------------------------------------------ */

/* 4 random decimal digits (+NUL). */
mlxr_status mlxr_pair_generate_pin(char pin[5]);
/* Full handshake. Stage 1 (getservercert) blocks on the host until the PIN is
 * entered in Sunshine/Apollo's web UI, up to timeout_ms (0 = default 5 min).
 * app_version: serverinfo <appversion> (picks SHA-256 for gen >= 7 else SHA-1).
 * On MLXR_OK *server_cert_pem is the host cert (malloc'd) and the server handle
 * is left pinned to it. On failure the pin is restored and /unpair is sent. */
mlxr_status mlxr_pair(mlxr_server *s, const char *app_version, const char *pin,
                      unsigned timeout_ms, char **server_cert_pem, mlxr_error *err);

/* Primitives (exposed for tests). */
/* SHA-256(salt||pin) or SHA-1 for gen < 7, truncated to 16 bytes. */
void mlxr_pair_derive_key(int server_major, const uint8_t salt[16], const char *pin, uint8_t key[16]);
/* AES-128-ECB, no padding; len must be a multiple of 16. out may equal in. */
mlxr_status mlxr_aes128_ecb(bool encrypt, const uint8_t key[16], const uint8_t *in, size_t len, uint8_t *out);
/* The signature BIT STRING bytes of a PEM cert. Returns length or -1; cap must be >= 512 for RSA-4096. */
int mlxr_cert_signature(const char *cert_pem, uint8_t *out, size_t cap);
/* RSA PKCS#1 v1.5 / SHA-256. sign returns length (<= cap) or -1. */
int mlxr_sign_sha256(const char *key_pem, const uint8_t *msg, size_t len, uint8_t *sig, size_t cap);
bool mlxr_verify_sha256(const char *cert_pem, const uint8_t *msg, size_t len, const uint8_t *sig, size_t siglen);

/* ------------------------------------------------------------------ */
/* Persistent host list: <dir>/hosts.ini (+ servercerts/<uuid>.pem)    */
/* ------------------------------------------------------------------ */

typedef struct {
    char name[128];
    char uuid[64];
    char address[128];          /* last known good address */
    char extra_addresses[256];  /* comma list (imported local/remote/manual addrs) */
    uint8_t mac[6];
    bool has_mac;
    uint16_t http_port;
    uint16_t https_port;
    char cert_path[512];        /* relative to the config dir unless absolute */
} mlxr_host_record;

typedef struct {
    mlxr_host_record *v;
    size_t n;
} mlxr_hosts;

mlxr_status mlxr_hosts_load(const char *dir, mlxr_hosts *h, mlxr_error *err);   /* missing file == empty */
mlxr_status mlxr_hosts_save(const char *dir, const mlxr_hosts *h, mlxr_error *err);
void mlxr_hosts_free(mlxr_hosts *h);
/* Match by address (also extra_addresses), uuid or name (case-insensitive). */
mlxr_host_record *mlxr_hosts_find(mlxr_hosts *h, const char *key);
mlxr_host_record *mlxr_hosts_find_uuid(mlxr_hosts *h, const char *uuid);
/* Insert or replace by uuid; returns the stored record. */
mlxr_host_record *mlxr_hosts_upsert(mlxr_hosts *h, const mlxr_host_record *rec);
/* Read a host's pinned cert (malloc'd PEM) / write it (sets rec->cert_path). */
mlxr_status mlxr_host_cert_load(const char *dir, const mlxr_host_record *rec, char **pem, mlxr_error *err);
mlxr_status mlxr_host_cert_save(const char *dir, mlxr_host_record *rec, const char *pem, mlxr_error *err);
/* Fill a record from a serverinfo answer + address (does not touch cert_path). */
void mlxr_host_record_from_computer(mlxr_host_record *rec, const mlxr_computer *c, const char *address);

/* ------------------------------------------------------------------ */
/* Tiny XML access (exposed for tests): first element named `tag`.      */
/* ------------------------------------------------------------------ */
typedef struct mlxr_xml mlxr_xml;
mlxr_xml *mlxr_xml_parse(const char *xml, size_t len);
void mlxr_xml_free(mlxr_xml *x);
char *mlxr_xml_get(const mlxr_xml *x, const char *tag);   /* malloc'd text or NULL if absent */
/* <root status_code= status_message=>; returns false if no root element. */
bool mlxr_xml_status(const mlxr_xml *x, int *code, char *msg, size_t cap);
size_t mlxr_xml_count(const mlxr_xml *x, const char *tag);
/* text of child `child` of the idx-th element named `tag`, or NULL */
char *mlxr_xml_get_nth(const mlxr_xml *x, const char *tag, size_t idx, const char *child);

#ifdef __cplusplus
}
#endif
#endif
