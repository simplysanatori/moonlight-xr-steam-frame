/* Private helpers shared by the backend .c files. GPLv3. */
#ifndef MLXR_INTERNAL_H
#define MLXR_INTERNAL_H

#include "backend.h"
#include <sys/types.h>

#if defined(__GNUC__)
#define MLXR_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define MLXR_PRINTF(a, b)
#endif

mlxr_status mlxr_fail(mlxr_error *err, mlxr_status code, int status, const char *fmt, ...) MLXR_PRINTF(4, 5);
void mlxr_log(int level, const char *fmt, ...) MLXR_PRINTF(2, 3);

/* hex */
void mlxr_hex_encode(const uint8_t *in, size_t n, char *out);          /* out >= 2n+1 */
char *mlxr_hex_encode_alloc(const uint8_t *in, size_t n);
/* decodes into malloc'd buffer; NULL on odd length / non-hex / empty input */
uint8_t *mlxr_hex_decode_alloc(const char *hex, size_t *outlen);

/* random */
bool mlxr_random(void *buf, size_t n);

/* strings */
char *mlxr_strdup(const char *s);
size_t mlxr_strlcpy(char *dst, const char *src, size_t cap);
void mlxr_trim(char *s);

/* files */
mlxr_status mlxr_mkdir_p(const char *path, mode_t mode, mlxr_error *err);
mlxr_status mlxr_read_file(const char *path, char **data, size_t *len, mlxr_error *err);   /* NUL-terminated */
mlxr_status mlxr_write_file(const char *path, const void *data, size_t len, mode_t mode, mlxr_error *err); /* atomic */
bool mlxr_file_exists(const char *path);

/* crypto */
void *mlxr_x509_from_pem(const char *pem);          /* X509* (caller X509_free) */
void *mlxr_pkey_from_pem(const char *pem);          /* EVP_PKEY* (caller EVP_PKEY_free) */
char *mlxr_spki_pin(const char *cert_pem);          /* "sha256//base64" for CURLOPT_PINNEDPUBLICKEY */

/* hash selected by server generation: 32 for SHA-256 (gen>=7), 20 for SHA-1 */
size_t mlxr_pair_hash(int server_major, const uint8_t *in, size_t len, uint8_t out[32]);

/* http */
struct mlxr_server;
mlxr_status mlxr_http_get(struct mlxr_server *s, bool https, const char *command, const char *args,
                          long timeout_ms, char **body, mlxr_error *err);
/* parse body, require <root status_code="200"> */
mlxr_status mlxr_check_response(const char *body, mlxr_error *err);

#endif
