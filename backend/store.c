/* hosts.ini + pinned server certs under the config dir. GPLv3.
 *
 *   [EXAMPLE0-example-id1-F307-92C54E613C61]
 *   name = DESKTOP-EXAMPLE
 *   address = 192.0.2.10
 *   extra_addresses = 203.0.113.5,203.0.113.7
 *   mac = 02:00:00:00:00:01
 *   http_port = 47989
 *   https_port = 47984
 *   cert = servercerts/EXAMPLE0-....pem
 */
#define _GNU_SOURCE
#include "internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <openssl/x509.h>

void mlxr_hosts_free(mlxr_hosts *h)
{
    free(h->v);
    h->v = NULL;
    h->n = 0;
}

static mlxr_host_record *append(mlxr_hosts *h)
{
    mlxr_host_record *nv = realloc(h->v, (h->n + 1) * sizeof *nv);
    if (!nv) return NULL;
    h->v = nv;
    memset(&nv[h->n], 0, sizeof *nv);
    nv[h->n].http_port = MLXR_DEFAULT_HTTP_PORT;
    nv[h->n].https_port = MLXR_DEFAULT_HTTPS_PORT;
    return &nv[h->n++];
}

static bool parse_mac(const char *s, uint8_t mac[6])
{
    unsigned b[6];
    if (sscanf(s, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) return false;
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)b[i];
    return true;
}

mlxr_status mlxr_hosts_load(const char *dir, mlxr_hosts *h, mlxr_error *err)
{
    memset(h, 0, sizeof *h);
    char path[1100], *data = NULL;
    snprintf(path, sizeof path, "%s/hosts.ini", dir);
    if (!mlxr_file_exists(path)) return MLXR_OK;
    mlxr_status st = mlxr_read_file(path, &data, NULL, err);
    if (st != MLXR_OK) return st;

    mlxr_host_record *cur = NULL;
    char *save = NULL;
    for (char *line = strtok_r(data, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        mlxr_trim(line);
        if (!*line || *line == '#' || *line == ';') continue;
        if (*line == '[') {
            char *e = strchr(line, ']');
            if (!e) continue;
            *e = 0;
            cur = append(h);
            if (!cur) { free(data); mlxr_hosts_free(h); return mlxr_fail(err, MLXR_E_NOMEM, 0, "oom"); }
            mlxr_strlcpy(cur->uuid, line + 1, sizeof cur->uuid);
            continue;
        }
        char *eq = strchr(line, '=');
        if (!eq || !cur) continue;
        *eq = 0;
        char *k = line, *v = eq + 1;
        mlxr_trim(k);
        mlxr_trim(v);
        if (!strcmp(k, "name")) mlxr_strlcpy(cur->name, v, sizeof cur->name);
        else if (!strcmp(k, "address")) mlxr_strlcpy(cur->address, v, sizeof cur->address);
        else if (!strcmp(k, "extra_addresses")) mlxr_strlcpy(cur->extra_addresses, v, sizeof cur->extra_addresses);
        else if (!strcmp(k, "mac")) cur->has_mac = parse_mac(v, cur->mac);
        else if (!strcmp(k, "http_port")) { int p = atoi(v); if (p > 0 && p < 65536) cur->http_port = (uint16_t)p; }
        else if (!strcmp(k, "https_port")) { int p = atoi(v); if (p > 0 && p < 65536) cur->https_port = (uint16_t)p; }
        else if (!strcmp(k, "cert")) mlxr_strlcpy(cur->cert_path, v, sizeof cur->cert_path);
    }
    free(data);
    return MLXR_OK;
}

static void clean(char *dst, size_t cap, const char *src)
{
    mlxr_strlcpy(dst, src, cap);
    for (char *p = dst; *p; p++)
        if (*p == '\n' || *p == '\r') *p = ' ';
}

mlxr_status mlxr_hosts_save(const char *dir, const mlxr_hosts *h, mlxr_error *err)
{
    size_t cap = 4096 + h->n * 1600;
    char *out = malloc(cap), *p = out;
    if (!out) return mlxr_fail(err, MLXR_E_NOMEM, 0, "oom");
    p += sprintf(p, "# Moonlight XR hosts. Pinned server certificates live in servercerts/.\n");
    for (size_t i = 0; i < h->n; i++) {
        const mlxr_host_record *r = &h->v[i];
        char name[128], addr[128], extra[256], cert[512], uuid[64];
        clean(name, sizeof name, r->name);
        clean(addr, sizeof addr, r->address);
        clean(extra, sizeof extra, r->extra_addresses);
        clean(cert, sizeof cert, r->cert_path);
        clean(uuid, sizeof uuid, r->uuid);
        for (char *q = uuid; *q; q++) if (*q == ']') *q = '_';
        p += sprintf(p, "\n[%s]\nname = %s\naddress = %s\n", uuid, name, addr);
        if (extra[0]) p += sprintf(p, "extra_addresses = %s\n", extra);
        if (r->has_mac)
            p += sprintf(p, "mac = %02x:%02x:%02x:%02x:%02x:%02x\n", r->mac[0], r->mac[1], r->mac[2], r->mac[3], r->mac[4], r->mac[5]);
        p += sprintf(p, "http_port = %u\nhttps_port = %u\ncert = %s\n", r->http_port, r->https_port, cert);
    }
    char path[1100];
    snprintf(path, sizeof path, "%s/hosts.ini", dir);
    mlxr_status st = mlxr_write_file(path, out, (size_t)(p - out), 0600, err);
    free(out);
    return st;
}

static bool in_csv(const char *csv, const char *key)
{
    size_t kl = strlen(key);
    for (const char *s = csv; *s;) {
        const char *e = strchr(s, ',');
        size_t n = e ? (size_t)(e - s) : strlen(s);
        while (n && *s == ' ') { s++; n--; }
        if (n == kl && strncasecmp(s, key, kl) == 0) return true;
        if (!e) break;
        s = e + 1;
    }
    return false;
}

mlxr_host_record *mlxr_hosts_find(mlxr_hosts *h, const char *key)
{
    for (size_t i = 0; i < h->n; i++) {
        mlxr_host_record *r = &h->v[i];
        if (!strcasecmp(r->address, key) || !strcasecmp(r->uuid, key) || !strcasecmp(r->name, key) ||
            in_csv(r->extra_addresses, key))
            return r;
    }
    return NULL;
}

mlxr_host_record *mlxr_hosts_find_uuid(mlxr_hosts *h, const char *uuid)
{
    if (!uuid || !*uuid) return NULL;
    for (size_t i = 0; i < h->n; i++)
        if (!strcasecmp(h->v[i].uuid, uuid)) return &h->v[i];
    return NULL;
}

mlxr_host_record *mlxr_hosts_upsert(mlxr_hosts *h, const mlxr_host_record *rec)
{
    mlxr_host_record *r = mlxr_hosts_find_uuid(h, rec->uuid);
    if (!r) r = append(h);
    if (r) *r = *rec;
    return r;
}

static void cert_abs(const char *dir, const mlxr_host_record *rec, char *out, size_t cap)
{
    if (rec->cert_path[0] == '/') snprintf(out, cap, "%s", rec->cert_path);
    else snprintf(out, cap, "%s/%s", dir, rec->cert_path);
}

mlxr_status mlxr_host_cert_load(const char *dir, const mlxr_host_record *rec, char **pem, mlxr_error *err)
{
    *pem = NULL;
    if (!rec->cert_path[0]) return mlxr_fail(err, MLXR_E_NOT_PAIRED, 0, "host %s has no pinned certificate", rec->name);
    if (strstr(rec->cert_path, "..")) return mlxr_fail(err, MLXR_E_ARG, 0, "refusing cert path with '..'");
    char path[1700];
    cert_abs(dir, rec, path, sizeof path);
    char *d;
    mlxr_status st = mlxr_read_file(path, &d, NULL, err);
    if (st != MLXR_OK) return st;
    X509 *x = mlxr_x509_from_pem(d);
    if (!x) {
        free(d);
        return mlxr_fail(err, MLXR_E_CRYPTO, 0, "pinned certificate %s is not a valid PEM certificate", rec->cert_path);
    }
    X509_free(x);
    *pem = d;
    return MLXR_OK;
}

mlxr_status mlxr_host_cert_save(const char *dir, mlxr_host_record *rec, const char *pem, mlxr_error *err)
{
    char sub[1100], path[1200], safe[64];
    snprintf(sub, sizeof sub, "%s/servercerts", dir);
    mlxr_status st = mlxr_mkdir_p(sub, 0700, err);
    if (st != MLXR_OK) return st;
    mlxr_strlcpy(safe, rec->uuid[0] ? rec->uuid : "host", sizeof safe);
    for (char *p = safe; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_') *p = '_';
    snprintf(path, sizeof path, "%s/%s.pem", sub, safe);
    st = mlxr_write_file(path, pem, strlen(pem), 0644, err);
    if (st != MLXR_OK) return st;
    snprintf(rec->cert_path, sizeof rec->cert_path, "servercerts/%s.pem", safe);
    return MLXR_OK;
}

void mlxr_host_record_from_computer(mlxr_host_record *rec, const mlxr_computer *c, const char *address)
{
    mlxr_strlcpy(rec->name, c->name, sizeof rec->name);
    mlxr_strlcpy(rec->uuid, c->uuid, sizeof rec->uuid);
    if (address) mlxr_strlcpy(rec->address, address, sizeof rec->address);
    rec->has_mac = c->has_mac;
    if (c->has_mac) memcpy(rec->mac, c->mac, 6);
    rec->http_port = c->http_port;
    rec->https_port = c->https_port;
}
