/* mlxr-cli: exercise the backend from a shell. GPLv3.
 *
 *   mlxr-cli identity [--show]
 *   mlxr-cli serverinfo <host> [--xml]
 *   mlxr-cli pair <host>
 *   mlxr-cli apps <host>
 *   mlxr-cli launch <host> <appid> <WxHxFPS> [--hdr]
 *   mlxr-cli quit <host>
 * <host> is an address[:port], a saved host name or a saved host uuid.
 * Set MLXR_VERBOSE=1 for request logging (never includes secrets). */
#define _GNU_SOURCE
#include "backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef MLXR_HAVE_LIMELIGHT
#include "backend_limelight.h"
#endif

struct ctx {
    char *dir;
    mlxr_identity id;
    mlxr_hosts hosts;
    mlxr_server *srv;
    char address[128];
    uint16_t http_port;
    mlxr_host_record *rec;   /* saved record (may be NULL) */
};

static void logfn(int lvl, const char *m, void *ud) { fprintf(stderr, "[mlxr %d] %s\n", lvl, m); }

static int fail(const char *what, const mlxr_error *e)
{
    if (strncmp(e->msg, what, strlen(what)) == 0)
        fprintf(stderr, "error: %s (%s", e->msg, mlxr_status_name(e->code));
    else
        fprintf(stderr, "error: %s: %s (%s", what, e->msg, mlxr_status_name(e->code));
    if (e->status) fprintf(stderr, ", %d", e->status);
    fprintf(stderr, ")\n");
    return e->code == MLXR_E_NET || e->code == MLXR_E_TIMEOUT ? 3 : 2;
}

/* "host", "host:port", "[v6]", "[v6]:port" */
static void split_host(const char *spec, char *host, size_t cap, uint16_t *port)
{
    *port = 0;
    if (spec[0] == '[') {
        const char *e = strchr(spec, ']');
        size_t n = e ? (size_t)(e - spec - 1) : strlen(spec) - 1;
        if (n >= cap) n = cap - 1;
        memcpy(host, spec + 1, n);
        host[n] = 0;
        if (e && e[1] == ':') *port = (uint16_t)atoi(e + 2);
        return;
    }
    snprintf(host, cap, "%s", spec);
    char *c = strchr(host, ':');
    if (c && !strchr(c + 1, ':')) {   /* exactly one colon: host:port */
        *port = (uint16_t)atoi(c + 1);
        *c = 0;
    }
}

static int ctx_open(struct ctx *c, mlxr_error *e)
{
    memset(c, 0, sizeof *c);
    c->dir = mlxr_config_dir(e);
    if (!c->dir) return fail("config dir", e);
    bool created;
    if (mlxr_identity_load_or_create(c->dir, &c->id, &created, e) != MLXR_OK) return fail("identity", e);
    if (created) fprintf(stderr, "note: created a new client identity in %s\n", c->dir);
    if (mlxr_hosts_load(c->dir, &c->hosts, e) != MLXR_OK) return fail("hosts.ini", e);
    return 0;
}

static void ctx_close(struct ctx *c)
{
    mlxr_server_free(c->srv);
    mlxr_hosts_free(&c->hosts);
    mlxr_identity_free(&c->id);
    free(c->dir);
}

static int pin_from_record(struct ctx *c, const mlxr_host_record *r, mlxr_error *e)
{
    char *pem;
    if (mlxr_host_cert_load(c->dir, r, &pem, e) != MLXR_OK) return fail("pinned certificate", e);
    mlxr_status st = mlxr_server_set_pinned_cert(c->srv, pem, e);
    free(pem);
    if (st != MLXR_OK) return fail("pinned certificate", e);
    mlxr_server_set_https_port(c->srv, r->https_port);
    return 0;
}

/* Resolve <host>: saved record by address/name/uuid, else probe over HTTP and match by uuid. */
static int open_server(struct ctx *c, const char *spec, mlxr_error *e)
{
    uint16_t port;
    split_host(spec, c->address, sizeof c->address, &port);
    c->rec = mlxr_hosts_find(&c->hosts, c->address);
    if (c->rec) {
        /* a saved name/uuid maps to its stored address */
        if (strcasecmp(c->rec->address, c->address) != 0 && c->rec->address[0] &&
            (!strcasecmp(c->rec->name, c->address) || !strcasecmp(c->rec->uuid, c->address)))
            snprintf(c->address, sizeof c->address, "%s", c->rec->address);
        c->http_port = port ? port : c->rec->http_port;
    } else {
        c->http_port = port ? port : MLXR_DEFAULT_HTTP_PORT;
    }
    c->srv = mlxr_server_new(&c->id, c->address, c->http_port);
    if (!c->srv) return fail("server", &(mlxr_error){MLXR_E_NOMEM, 0, "oom"});
    if (c->rec) return pin_from_record(c, c->rec, e);

    /* unknown address: it may still be a saved host reached on another address */
    mlxr_computer comp;
    if (mlxr_server_get_info(c->srv, false, &comp, NULL, e) != MLXR_OK) return fail("serverinfo", e);
    c->rec = mlxr_hosts_find_uuid(&c->hosts, comp.uuid);
    if (c->rec) {
        fprintf(stderr, "note: %s is saved host '%s' (matched by uuid)\n", c->address, c->rec->name);
        return pin_from_record(c, c->rec, e);
    }
    mlxr_server_set_https_port(c->srv, comp.https_port);
    return 0;
}

static void print_computer(const mlxr_computer *c)
{
    printf("hostname:        %s\n", c->name);
    printf("uuid:            %s\n", c->uuid);
    if (c->has_mac)
        printf("mac:             %02x:%02x:%02x:%02x:%02x:%02x\n", c->mac[0], c->mac[1], c->mac[2], c->mac[3], c->mac[4], c->mac[5]);
    printf("state:           %s\n", c->state);
    printf("software:        %s\n", c->is_nvidia ? "GeForce Experience" : "Sunshine-compatible");
    printf("appversion:      %s\n", c->app_version);
    if (c->gfe_version[0]) printf("GfeVersion:      %s\n", c->gfe_version);
    if (c->gpu[0]) printf("gpu:             %s\n", c->gpu);
    printf("channel:         %s\n", c->via_https ? "HTTPS (pinned cert)" : "HTTP");
    printf("paired:          %s%s\n", c->paired ? "yes" : "no", c->via_https ? "" : " (HTTP cannot tell; reported as no)");
    printf("current game:    %d\n", c->current_game);
    printf("http/https port: %u/%u (external %u)\n", c->http_port, c->https_port, c->external_port);
    if (c->local_ip[0]) printf("local ip:        %s\n", c->local_ip);
    if (c->external_ip[0]) printf("external ip:     %s\n", c->external_ip);
    printf("codec support:   0x%x%s%s%s%s%s%s\n", c->server_codec_mode_support,
           c->server_codec_mode_support & 0x1 ? " H264" : "", c->server_codec_mode_support & 0x100 ? " HEVC" : "",
           c->server_codec_mode_support & 0x200 ? " HEVC-10" : "", c->server_codec_mode_support & 0x10000 ? " AV1" : "",
           c->server_codec_mode_support & 0x20000 ? " AV1-10" : "", c->server_codec_mode_support & 0x7C0000 ? " 4:4:4" : "");
    printf("max luma HEVC:   %d\n", c->max_luma_pixels_hevc);
    for (int i = 0; i < c->n_modes; i++)
        printf("display mode:    %dx%dx%d\n", c->modes[i].width, c->modes[i].height, c->modes[i].refresh_rate);
}

static int cmd_identity(int argc, char **argv)
{
    mlxr_error e;
    struct ctx c;
    int rc = ctx_open(&c, &e);
    if (rc) return rc;
    char fp[65];
    mlxr_cert_fingerprint(c.id.cert_pem, fp);
    printf("config dir:   %s\n", c.dir);
    printf("unique id:    %s\n", c.id.unique_id);
    printf("cert sha256:  %s\n", fp);
    printf("files:        client.pem client.key uniqueid hosts.ini\n");
    printf("saved hosts:  %zu\n", c.hosts.n);
    ctx_close(&c);
    return 0;
}

static int cmd_serverinfo(int argc, char **argv)
{
    if (argc < 1) return fprintf(stderr, "usage: serverinfo <host> [--xml]\n"), 1;
    bool xml = argc > 1 && !strcmp(argv[1], "--xml");
    mlxr_error e;
    struct ctx c;
    int rc = ctx_open(&c, &e);
    if (rc) return rc;
    if ((rc = open_server(&c, argv[0], &e))) { ctx_close(&c); return rc; }
    mlxr_computer comp;
    char *raw = NULL;
    if (mlxr_server_get_info(c.srv, false, &comp, &raw, &e) != MLXR_OK) {
        rc = fail("serverinfo", &e);
    } else if (xml) {
        fputs(raw, stdout);
    } else {
        print_computer(&comp);
    }
    free(raw);
    ctx_close(&c);
    return rc;
}

static int cmd_pair(int argc, char **argv)
{
    if (argc < 1) return fprintf(stderr, "usage: pair <host>\n"), 1;
    mlxr_error e;
    struct ctx c;
    int rc = ctx_open(&c, &e);
    if (rc) return rc;
    uint16_t port;
    split_host(argv[0], c.address, sizeof c.address, &port);
    c.http_port = port ? port : MLXR_DEFAULT_HTTP_PORT;
    c.srv = mlxr_server_new(&c.id, c.address, c.http_port);
    mlxr_computer comp;
    if (mlxr_server_get_info(c.srv, false, &comp, NULL, &e) != MLXR_OK) { rc = fail("serverinfo", &e); goto out; }
    mlxr_server_set_gfe(c.srv, comp.is_nvidia);
    mlxr_server_set_https_port(c.srv, comp.https_port);

    char pin[5];
    if (mlxr_pair_generate_pin(pin) != MLXR_OK) { rc = 2; goto out; }
    printf("Pairing with %s (%s, %s).\n", comp.name, c.address, comp.app_version);
    if (comp.is_nvidia)
        printf("Type this PIN into the GeForce Experience dialog on the PC:\n\n    %s\n\n", pin);
    else
        printf("Type this PIN into Sunshine/Apollo's web UI (https://%s:%u/pin -> PIN tab):\n\n    %s\n\n", c.address,
               comp.http_port + 1, pin);
    printf("Waiting for the PIN to be entered (5 minutes)...\n");
    fflush(stdout);

    char *cert = NULL;
    mlxr_status st = mlxr_pair(c.srv, comp.app_version, pin, 0, &cert, &e);
    if (st != MLXR_OK) { rc = fail("pairing", &e); goto out; }

    mlxr_host_record rec = {0};
    mlxr_host_record *old = mlxr_hosts_find_uuid(&c.hosts, comp.uuid);
    if (old) rec = *old;
    mlxr_host_record_from_computer(&rec, &comp, c.address);
    if (mlxr_host_cert_save(c.dir, &rec, cert, &e) != MLXR_OK || !mlxr_hosts_upsert(&c.hosts, &rec) ||
        mlxr_hosts_save(c.dir, &c.hosts, &e) != MLXR_OK) {
        rc = fail("saving host", &e);
        free(cert);
        goto out;
    }
    char fp[65] = "";
    mlxr_cert_fingerprint(cert, fp);
    free(cert);
    printf("Paired. Host '%s' saved (server cert sha256 %.16s...).\n", comp.name, fp);
out:
    ctx_close(&c);
    return rc;
}

static int need_paired(struct ctx *c)
{
    if (!mlxr_server_pinned_cert(c->srv)) {
        fprintf(stderr, "error: %s is not paired with this client (no pinned certificate); run: mlxr-cli pair %s\n",
                c->address, c->address);
        return 4;
    }
    return 0;
}

static int cmd_apps(int argc, char **argv)
{
    if (argc < 1) return fprintf(stderr, "usage: apps <host>\n"), 1;
    mlxr_error e;
    struct ctx c;
    int rc = ctx_open(&c, &e);
    if (rc) return rc;
    if ((rc = open_server(&c, argv[0], &e)) || (rc = need_paired(&c))) { ctx_close(&c); return rc; }
    mlxr_computer comp;
    if (mlxr_server_get_info(c.srv, false, &comp, NULL, &e) != MLXR_OK) { rc = fail("serverinfo", &e); goto out; }
    mlxr_server_set_gfe(c.srv, comp.is_nvidia);
    mlxr_app *apps;
    size_t n;
    if (mlxr_server_get_apps(c.srv, &apps, &n, &e) != MLXR_OK) { rc = fail("applist", &e); goto out; }
    for (size_t i = 0; i < n; i++)
        printf("%d\t%s%s\n", apps[i].id, apps[i].name, apps[i].hdr_supported ? "\t[HDR]" : "");
    printf("(%zu apps; current game %d)\n", n, comp.current_game);
    mlxr_apps_free(apps, n);
out:
    ctx_close(&c);
    return rc;
}

static int cmd_launch(int argc, char **argv)
{
    if (argc < 3) return fprintf(stderr, "usage: launch <host> <appid> <WxHxFPS> [--hdr]\n"), 1;
    mlxr_launch_params p;
#ifdef MLXR_HAVE_LIMELIGHT
    mlxr_launch_params_init_ll(&p);
#else
    mlxr_launch_params_init(&p);
#endif
    int appid = atoi(argv[1]);
    if (mlxr_parse_mode(argv[2], &p.width, &p.height, &p.fps) != MLXR_OK)
        return fprintf(stderr, "error: mode must look like 1920x1080x60\n"), 1;
    p.hdr = argc > 3 && !strcmp(argv[3], "--hdr");
    mlxr_error e;
    struct ctx c;
    int rc = ctx_open(&c, &e);
    if (rc) return rc;
    if ((rc = open_server(&c, argv[0], &e)) || (rc = need_paired(&c))) { ctx_close(&c); return rc; }
    mlxr_computer comp;
    if (mlxr_server_get_info(c.srv, false, &comp, NULL, &e) != MLXR_OK) { rc = fail("serverinfo", &e); goto out; }
    mlxr_server_set_gfe(c.srv, comp.is_nvidia);
    p.is_gfe = comp.is_nvidia;
    char *url = NULL;
    bool resume = comp.current_game != 0;
    if (mlxr_server_launch_or_resume(c.srv, &comp, appid, &p, &url, &e) != MLXR_OK) { rc = fail(resume ? "resume" : "launch", &e); goto out; }
    printf("%s ok: app %d at %dx%dx%d\n", resume ? "resume" : "launch", appid, p.width, p.height, p.fps);
    printf("rtsp url:     %s\n", url);
    printf("rikeyid:      %d\n", (int)mlxr_launch_rikeyid(&p));
    printf("codec support 0x%x, appversion %s\n", comp.server_codec_mode_support, comp.app_version);
    free(url);
out:
    ctx_close(&c);
    return rc;
}

static int cmd_quit(int argc, char **argv)
{
    if (argc < 1) return fprintf(stderr, "usage: quit <host>\n"), 1;
    mlxr_error e;
    struct ctx c;
    int rc = ctx_open(&c, &e);
    if (rc) return rc;
    if ((rc = open_server(&c, argv[0], &e)) || (rc = need_paired(&c))) { ctx_close(&c); return rc; }
    mlxr_computer comp;
    if (mlxr_server_get_info(c.srv, false, &comp, NULL, &e) != MLXR_OK) { rc = fail("serverinfo", &e); goto out; }
    mlxr_server_set_gfe(c.srv, comp.is_nvidia);
    if (mlxr_server_quit_app(c.srv, &e) != MLXR_OK) rc = fail("quit", &e);
    else printf("quit ok: no session running\n");
out:
    ctx_close(&c);
    return rc;
}

int main(int argc, char **argv)
{
    if (getenv("MLXR_VERBOSE")) mlxr_set_log(logfn, NULL);
    if (argc < 2) {
        fprintf(stderr,
                "usage: mlxr-cli identity\n"
                "       mlxr-cli serverinfo <host> [--xml]\n"
                "       mlxr-cli pair <host>\n"
                "       mlxr-cli apps <host>\n"
                "       mlxr-cli launch <host> <appid> <WxHxFPS> [--hdr]\n"
                "       mlxr-cli quit <host>\n"
                "config dir: $MLXR_CONFIG_DIR or ~/.config/moonlight-xr\n");
        return 1;
    }
    const char *cmd = argv[1];
    argc -= 2;
    argv += 2;
    if (!strcmp(cmd, "identity")) return cmd_identity(argc, argv);
    if (!strcmp(cmd, "serverinfo")) return cmd_serverinfo(argc, argv);
    if (!strcmp(cmd, "pair")) return cmd_pair(argc, argv);
    if (!strcmp(cmd, "apps")) return cmd_apps(argc, argv);
    if (!strcmp(cmd, "launch")) return cmd_launch(argc, argv);
    if (!strcmp(cmd, "quit")) return cmd_quit(argc, argv);
    fprintf(stderr, "unknown command: %s\n", cmd);
    return 1;
}
