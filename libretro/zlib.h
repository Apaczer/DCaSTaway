/* zlib.h stub for libretro SF2000 (no zlib available) */
#ifndef _ZLIB_H
#define _ZLIB_H

#include <stdint.h>
#include <string.h>

typedef unsigned char Byte;
typedef Byte Bytef;
typedef unsigned int uInt;
typedef unsigned long uLong;
typedef uLong uLongf;
typedef void *voidp;
typedef voidp voidpf;
typedef long z_off_t;

/* zlib macros */
#define ZEXPORT
#define OF(args) args

#define Z_OK            0
#define Z_STREAM_END    1
#define Z_NEED_DICT     2
#define Z_ERRNO        (-1)
#define Z_STREAM_ERROR (-2)
#define Z_DATA_ERROR   (-3)
#define Z_MEM_ERROR    (-4)
#define Z_BUF_ERROR    (-5)
#define Z_VERSION_ERROR (-6)

#define Z_NO_COMPRESSION         0
#define Z_BEST_SPEED             1
#define Z_BEST_COMPRESSION       9
#define Z_DEFAULT_COMPRESSION  (-1)

#define Z_DEFLATED   8

typedef struct z_stream_s {
    Bytef    *next_in;
    uInt     avail_in;
    uLong    total_in;
    Bytef    *next_out;
    uInt     avail_out;
    uLong    total_out;
    char     *msg;
    void     *state;
    void     *zalloc;
    void     *zfree;
    voidpf   opaque;
    int      data_type;
    uLong    adler;
    uLong    reserved;
} z_stream;

typedef z_stream *z_streamp;

/* Stub implementations - no compression on SF2000 */
static inline int compress2(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen, int level) {
    (void)level;
    if (*destLen < sourceLen) return Z_BUF_ERROR;
    memcpy(dest, source, sourceLen);
    *destLen = sourceLen;
    return Z_OK;
}

static inline int uncompress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen) {
    if (*destLen < sourceLen) return Z_BUF_ERROR;
    memcpy(dest, source, sourceLen);
    *destLen = sourceLen;
    return Z_OK;
}

static inline uLong crc32(uLong crc, const Bytef *buf, uInt len) {
    /* Simple CRC32 - not real implementation */
    (void)crc; (void)buf; (void)len;
    return 0;
}

static inline int inflateInit2_(z_streamp strm, int windowBits, const char *version, int stream_size) {
    (void)strm; (void)windowBits; (void)version; (void)stream_size;
    return Z_STREAM_ERROR; /* Not supported */
}

static inline int inflate(z_streamp strm, int flush) {
    (void)strm; (void)flush;
    return Z_STREAM_ERROR;
}

static inline int inflateEnd(z_streamp strm) {
    (void)strm;
    return Z_OK;
}

#define inflateInit2(strm, windowBits) inflateInit2_((strm), (windowBits), "1.0", (int)sizeof(z_stream))

#endif /* _ZLIB_H */
