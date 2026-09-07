/* =============================================================================
 * ZeruX OS — Minimal DEFLATE (RFC 1951) Decompressor
 * File: kernel/include/inflate.h
 * =============================================================================
 * A small, self-contained, freestanding DEFLATE decoder — the algorithm
 * PNG's IDAT stream is compressed with (via zlib, RFC 1950, which is just
 * a 2-byte header + DEFLATE + a 4-byte Adler32 trailer around this).
 *
 * Structurally this follows the well-known public-domain reference
 * decoder "puff.c" by Mark Adler (canonical Huffman decode via
 * count[]/symbol[] tables, no bit-reversal tricks, no lookup-table
 * acceleration) — it favors being obviously correct and easy to audit
 * over being fast, which is the right tradeoff for a hobby OS's first
 * real compression code.
 * =============================================================================
 */

#ifndef INFLATE_H
#define INFLATE_H

#include <stdint.h>

/* Decompresses a raw DEFLATE byte stream (no zlib/gzip wrapper — see
 * png.c for that layer) from src[0..src_len) into out_buf, which must
 * already be sized to hold the full decompressed output (out_cap bytes;
 * the PNG IHDR chunk tells you exactly how big that needs to be before
 * you call this).
 *
 * Returns the number of bytes written on success, or a negative error
 * code on failure (corrupt stream, unsupported block type, or the
 * output/input didn't fit the given buffers). */
int32_t inflate_raw(const uint8_t *src, uint32_t src_len,
                     uint8_t *out_buf, uint32_t out_cap);

#define INFLATE_ERR_INPUT_EXHAUSTED   -1
#define INFLATE_ERR_OUTPUT_FULL       -2
#define INFLATE_ERR_INVALID_BTYPE     -3
#define INFLATE_ERR_INVALID_STORED    -4
#define INFLATE_ERR_INVALID_CODE      -5
#define INFLATE_ERR_INVALID_DISTANCE  -6
#define INFLATE_ERR_INVALID_HUFFMAN   -7
#define INFLATE_ERR_TOO_MANY_CODES    -8

#endif /* INFLATE_H */
