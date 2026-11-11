// Moonlight XR Native: the sealed clipboard hand-off (lane clipseal). GPLv3.
//
// Smart Clipboard Sync hands clipboard text between the flat launcher (which owns the desktop clipboard)
// and the VR client (which talks to Apollo) through two files in the VR client's config dir. Those files
// used to hold the text as it is. Now nothing copied or pasted is ever written unencrypted:
//
//   - The launcher makes a fresh random 256-bit key for every stream and gives it to the VR client on the
//     child's stdin pipe (one line, MLXR_CLIPSEAL_KEY_LINE and 64 hex digits). The key never touches the
//     disk, the command line or the environment, and both processes wipe it when the stream is over, so a
//     file left behind by a crash cannot be read afterwards.
//   - Each file is AES-256-GCM: MLXR_CLIPSEAL_MAGIC, a random 12-byte nonce, the ciphertext, a 16-byte
//     tag. The magic and the direction's label (MLXR_CLIPSEAL_TO_HOST or MLXR_CLIPSEAL_FROM_HOST) are
//     authenticated too, so a file is only accepted in the direction it was made for, and any change to
//     it is refused rather than read.
//   - The reader deletes a file as soon as it has read it, and plaintext buffers are wiped after use.
//
// The cost is one AES-GCM pass over the text (well under a millisecond for the largest clipboard), twice per
// stream. Header only, C and C++ (the launcher compiles it as C++, launcher/build.sh copies it in), with
// OpenSSL's libcrypto, which both programs already link.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#define MLXR_CLIPSEAL_KEY_BYTES 32
#define MLXR_CLIPSEAL_NONCE_BYTES 12
#define MLXR_CLIPSEAL_TAG_BYTES 16
#define MLXR_CLIPSEAL_MAGIC "MLXRCLP1"
#define MLXR_CLIPSEAL_MAGIC_BYTES 8
#define MLXR_CLIPSEAL_OVERHEAD (MLXR_CLIPSEAL_MAGIC_BYTES + MLXR_CLIPSEAL_NONCE_BYTES + MLXR_CLIPSEAL_TAG_BYTES)
// The most clipboard text either side hands over (Apollo's own clipboard is text, rarely more than a page)
#define MLXR_CLIPSEAL_MAX_TEXT (1024u * 1024u)
// The key's line on the VR client's stdin: this, 64 lowercase hex digits, a newline
#define MLXR_CLIPSEAL_KEY_LINE "MLXR_CLIPKEY "
#define MLXR_CLIPSEAL_KEY_LINE_BYTES (sizeof(MLXR_CLIPSEAL_KEY_LINE) - 1 + 2 * MLXR_CLIPSEAL_KEY_BYTES + 1)
// The two directions, and their files in the VR client's config dir
#define MLXR_CLIPSEAL_TO_HOST "to-host"
#define MLXR_CLIPSEAL_FROM_HOST "from-host"
#define MLXR_CLIPSEAL_TO_HOST_FILE "clipboard-to-host.sealed"
#define MLXR_CLIPSEAL_FROM_HOST_FILE "clipboard-from-host.sealed"
// The unencrypted files of 1.2.0 and before, removed wherever they are found
#define MLXR_CLIPSEAL_LEGACY_TO_HOST_FILE "clipboard-to-host.txt"
#define MLXR_CLIPSEAL_LEGACY_FROM_HOST_FILE "clipboard-from-host.txt"

// A fresh key from the system's random source. 0, or -1 when none could be had.
static inline int mlxr_clipseal_new_key(uint8_t key[MLXR_CLIPSEAL_KEY_BYTES]) {
    return RAND_bytes(key, MLXR_CLIPSEAL_KEY_BYTES) == 1 ? 0 : -1;
}

// The key as its stdin line, newline included, NUL terminated (out holds MLXR_CLIPSEAL_KEY_LINE_BYTES + 1)
static inline void mlxr_clipseal_key_line(const uint8_t key[MLXR_CLIPSEAL_KEY_BYTES], char* out) {
    static const char hex[] = "0123456789abcdef";
    size_t p = sizeof(MLXR_CLIPSEAL_KEY_LINE) - 1;
    memcpy(out, MLXR_CLIPSEAL_KEY_LINE, p);
    for (int i = 0; i < MLXR_CLIPSEAL_KEY_BYTES; i++) {
        out[p++] = hex[key[i] >> 4];
        out[p++] = hex[key[i] & 15];
    }
    out[p++] = '\n';
    out[p] = '\0';
}

// The key from its line (the newline is optional). 0, or -1 for anything else.
static inline int mlxr_clipseal_parse_key_line(const char* line, size_t len, uint8_t key[MLXR_CLIPSEAL_KEY_BYTES]) {
    size_t p = sizeof(MLXR_CLIPSEAL_KEY_LINE) - 1;
    if (len < p + 2 * MLXR_CLIPSEAL_KEY_BYTES || memcmp(line, MLXR_CLIPSEAL_KEY_LINE, p) != 0) return -1;
    for (int i = 0; i < 2 * MLXR_CLIPSEAL_KEY_BYTES; i++) {
        char c = line[p + i];
        int v = c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
        if (v < 0) {
            OPENSSL_cleanse(key, MLXR_CLIPSEAL_KEY_BYTES);
            return -1;
        }
        if (i & 1) key[i / 2] = (uint8_t)(key[i / 2] | v);
        else key[i / 2] = (uint8_t)(v << 4);
    }
    size_t end = p + 2 * MLXR_CLIPSEAL_KEY_BYTES;
    if (len > end && !(line[end] == '\n' || (line[end] == '\r' && len > end + 1 && line[end + 1] == '\n'))) {
        OPENSSL_cleanse(key, MLXR_CLIPSEAL_KEY_BYTES);
        return -1;
    }
    return 0;
}

static inline int mlxr_clipseal_aad_(EVP_CIPHER_CTX* c, int enc, const char* label) {
    int n = 0;
    const unsigned char* m = (const unsigned char*)MLXR_CLIPSEAL_MAGIC;
    const unsigned char* l = (const unsigned char*)label;
    int ll = (int)strlen(label);
    if (enc)
        return EVP_EncryptUpdate(c, NULL, &n, m, MLXR_CLIPSEAL_MAGIC_BYTES) == 1 && EVP_EncryptUpdate(c, NULL, &n, l, ll) == 1;
    return EVP_DecryptUpdate(c, NULL, &n, m, MLXR_CLIPSEAL_MAGIC_BYTES) == 1 && EVP_DecryptUpdate(c, NULL, &n, l, ll) == 1;
}

// Seals n bytes of text for one direction into out (cap at least n + MLXR_CLIPSEAL_OVERHEAD).
// 0 and *outLen, or -1 (too long, no room, or the cipher failed; nothing usable in out).
static inline int mlxr_clipseal_seal(const uint8_t key[MLXR_CLIPSEAL_KEY_BYTES], const char* label, const void* text,
                                     size_t n, uint8_t* out, size_t cap, size_t* outLen) {
    if (!key || !label || !out || (n && !text) || n > MLXR_CLIPSEAL_MAX_TEXT || cap < n + MLXR_CLIPSEAL_OVERHEAD) return -1;
    uint8_t* nonce = out + MLXR_CLIPSEAL_MAGIC_BYTES;
    uint8_t* body = nonce + MLXR_CLIPSEAL_NONCE_BYTES;
    memcpy(out, MLXR_CLIPSEAL_MAGIC, MLXR_CLIPSEAL_MAGIC_BYTES);
    if (RAND_bytes(nonce, MLXR_CLIPSEAL_NONCE_BYTES) != 1) return -1;
    EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
    if (!c) return -1;
    int len = 0, fin = 0;
    int ok = EVP_EncryptInit_ex(c, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1
          && EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, MLXR_CLIPSEAL_NONCE_BYTES, NULL) == 1
          && EVP_EncryptInit_ex(c, NULL, NULL, key, nonce) == 1
          && mlxr_clipseal_aad_(c, 1, label)
          && (n == 0 || EVP_EncryptUpdate(c, body, &len, (const unsigned char*)text, (int)n) == 1)
          && EVP_EncryptFinal_ex(c, body + len, &fin) == 1
          && (size_t)(len + fin) == n
          && EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, MLXR_CLIPSEAL_TAG_BYTES, body + n) == 1;
    EVP_CIPHER_CTX_free(c);
    if (!ok) {
        OPENSSL_cleanse(out, cap < n + MLXR_CLIPSEAL_OVERHEAD ? cap : n + MLXR_CLIPSEAL_OVERHEAD);
        return -1;
    }
    *outLen = n + MLXR_CLIPSEAL_OVERHEAD;
    return 0;
}

// Opens a sealed file's bytes made for this direction into text (cap at least n - MLXR_CLIPSEAL_OVERHEAD + 1;
// the text is NUL terminated). 0 and *textLen, or -1: not a sealed file, the wrong key or direction, or
// changed in any way. On -1 nothing of the text is left in the buffer.
static inline int mlxr_clipseal_open(const uint8_t key[MLXR_CLIPSEAL_KEY_BYTES], const char* label, const uint8_t* in,
                                     size_t n, char* text, size_t cap, size_t* textLen) {
    if (!key || !label || !in || !text || n < MLXR_CLIPSEAL_OVERHEAD) return -1;
    size_t tn = n - MLXR_CLIPSEAL_OVERHEAD;
    if (tn > MLXR_CLIPSEAL_MAX_TEXT || cap < tn + 1 || memcmp(in, MLXR_CLIPSEAL_MAGIC, MLXR_CLIPSEAL_MAGIC_BYTES) != 0)
        return -1;
    const uint8_t* nonce = in + MLXR_CLIPSEAL_MAGIC_BYTES;
    const uint8_t* body = nonce + MLXR_CLIPSEAL_NONCE_BYTES;
    uint8_t tag[MLXR_CLIPSEAL_TAG_BYTES];
    memcpy(tag, body + tn, sizeof(tag));
    EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
    if (!c) return -1;
    int len = 0, fin = 0;
    int ok = EVP_DecryptInit_ex(c, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1
          && EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, MLXR_CLIPSEAL_NONCE_BYTES, NULL) == 1
          && EVP_DecryptInit_ex(c, NULL, NULL, key, nonce) == 1
          && mlxr_clipseal_aad_(c, 0, label)
          && (tn == 0 || EVP_DecryptUpdate(c, (unsigned char*)text, &len, body, (int)tn) == 1)
          && EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, MLXR_CLIPSEAL_TAG_BYTES, tag) == 1
          && EVP_DecryptFinal_ex(c, (unsigned char*)text + len, &fin) == 1
          && (size_t)(len + fin) == tn;
    EVP_CIPHER_CTX_free(c);
    if (!ok) {
        OPENSSL_cleanse(text, tn + 1);
        return -1;
    }
    text[tn] = '\0';
    *textLen = tn;
    return 0;
}
