/* =============================================================================
 * ZeruX OS — Minimal DEFLATE (RFC 1951) Decompressor
 * File: kernel/drivers/inflate.c
 * =============================================================================
 */

#include "inflate.h"
#include <stddef.h>

#define MAXBITS   15   /* max Huffman code length in DEFLATE                */
#define MAXLCODES 286  /* max literal/length codes                          */
#define MAXDCODES 30   /* max distance codes                                */
#define MAXCODES  (MAXLCODES + MAXDCODES)
#define FIXLCODES 288  /* number of fixed literal/length codes              */

typedef struct {
    const uint8_t *in;
    uint32_t in_len;
    uint32_t in_pos;
    uint32_t bitbuf;
    int32_t  bitcnt;

    uint8_t  *out;
    uint32_t out_cap;
    uint32_t out_pos;
} inf_state_t;

/* Canonical Huffman decode table: count[len] = how many codes of that
 * length exist; symbol[] lists the symbols in canonical order. */
typedef struct {
    int16_t count[MAXBITS + 1];
    int16_t symbol[MAXLCODES]; /* big enough for either literal/length or distance use */
} inf_huffman_t;

/* ── Bit-level input ─────────────────────────────────────────────────── */
static int32_t inf_bits(inf_state_t *s, int32_t need) {
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->in_pos >= s->in_len) return INFLATE_ERR_INPUT_EXHAUSTED;
        val |= (uint32_t)s->in[s->in_pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int32_t)(val & ((1u << need) - 1));
}

/* ── Stored (uncompressed) block ─────────────────────────────────────── */
static int32_t inf_stored(inf_state_t *s) {
    /* Discard any partial byte left in the bit buffer */
    s->bitbuf = 0;
    s->bitcnt = 0;

    if (s->in_pos + 4 > s->in_len) return INFLATE_ERR_INPUT_EXHAUSTED;
    uint32_t len  = (uint32_t)s->in[s->in_pos] | ((uint32_t)s->in[s->in_pos + 1] << 8);
    uint32_t nlen = (uint32_t)s->in[s->in_pos + 2] | ((uint32_t)s->in[s->in_pos + 3] << 8);
    s->in_pos += 4;
    if ((len ^ 0xFFFFu) != nlen) return INFLATE_ERR_INVALID_STORED;

    if (s->in_pos + len > s->in_len) return INFLATE_ERR_INPUT_EXHAUSTED;
    if (s->out_pos + len > s->out_cap) return INFLATE_ERR_OUTPUT_FULL;

    for (uint32_t i = 0; i < len; i++) s->out[s->out_pos++] = s->in[s->in_pos++];
    return 0;
}

/* ── Build a canonical Huffman table from an array of code lengths ──────
 * length[i] = bit length of the code for symbol i (0 = symbol unused). */
static int32_t inf_construct(inf_huffman_t *h, const int16_t *length, int32_t n) {
    for (int32_t len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (int32_t sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0; /* no codes at all (empty table, e.g. unused distance tree) */

    /* Sanity check: codes must not over-subscribe the code space */
    int32_t left = 1;
    for (int32_t len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return INFLATE_ERR_INVALID_HUFFMAN; /* over-subscribed */
    }

    int16_t offs[MAXBITS + 1];
    offs[1] = 0;
    for (int32_t len = 1; len < MAXBITS; len++) offs[len + 1] = (int16_t)(offs[len] + h->count[len]);

    for (int32_t sym = 0; sym < n; sym++) {
        if (length[sym] != 0) h->symbol[offs[length[sym]]++] = (int16_t)sym;
    }
    return 0;
}

/* ── Decode one symbol using a canonical Huffman table ──────────────── */
static int32_t inf_decode(inf_state_t *s, const inf_huffman_t *h) {
    int32_t code = 0, first = 0, index = 0;
    for (int32_t len = 1; len <= MAXBITS; len++) {
        int32_t bit = inf_bits(s, 1);
        if (bit < 0) return bit;
        code |= bit;
        int32_t count = h->count[len];
        if (code - first < count) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return INFLATE_ERR_INVALID_CODE;
}

/* ── Length/distance extra-bit tables (RFC 1951 §3.2.5) ─────────────── */
static const int16_t LEN_BASE[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const int16_t LEN_EXTRA[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const int16_t DIST_BASE[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
    1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const int16_t DIST_EXTRA[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

/* ── Decode a compressed block (fixed or dynamic Huffman) ───────────── */
static int32_t inf_codes(inf_state_t *s, const inf_huffman_t *lencode, const inf_huffman_t *distcode) {
    for (;;) {
        int32_t sym = inf_decode(s, lencode);
        if (sym < 0) return sym;

        if (sym < 256) {
            if (s->out_pos >= s->out_cap) return INFLATE_ERR_OUTPUT_FULL;
            s->out[s->out_pos++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0; /* end of block */
        } else {
            sym -= 257;
            if (sym >= 29) return INFLATE_ERR_INVALID_CODE;
            int32_t extra = inf_bits(s, LEN_EXTRA[sym]);
            if (extra < 0) return extra;
            int32_t length = LEN_BASE[sym] + extra;

            int32_t dsym = inf_decode(s, distcode);
            if (dsym < 0) return dsym;
            if (dsym >= 30) return INFLATE_ERR_INVALID_DISTANCE;
            int32_t dextra = inf_bits(s, DIST_EXTRA[dsym]);
            if (dextra < 0) return dextra;
            int32_t distance = DIST_BASE[dsym] + dextra;

            if ((uint32_t)distance > s->out_pos) return INFLATE_ERR_INVALID_DISTANCE;
            if (s->out_pos + (uint32_t)length > s->out_cap) return INFLATE_ERR_OUTPUT_FULL;

            uint32_t from = s->out_pos - (uint32_t)distance;
            for (int32_t i = 0; i < length; i++) {
                s->out[s->out_pos] = s->out[from];
                s->out_pos++;
                from++;
            }
        }
    }
}

/* ── Fixed Huffman tables (RFC 1951 §3.2.6) ──────────────────────────── */
static int32_t inf_fixed_block(inf_state_t *s) {
    static int16_t lenlen[FIXLCODES];
    static int16_t distlen[MAXDCODES];
    int32_t sym = 0;
    for (; sym < 144; sym++) lenlen[sym] = 8;
    for (; sym < 256; sym++) lenlen[sym] = 9;
    for (; sym < 280; sym++) lenlen[sym] = 7;
    for (; sym < FIXLCODES; sym++) lenlen[sym] = 8;
    for (sym = 0; sym < MAXDCODES; sym++) distlen[sym] = 5;

    inf_huffman_t lencode, distcode;
    inf_construct(&lencode, lenlen, FIXLCODES);
    inf_construct(&distcode, distlen, MAXDCODES);
    return inf_codes(s, &lencode, &distcode);
}

/* ── Dynamic Huffman tables (RFC 1951 §3.2.7) ────────────────────────── */
static const uint8_t CLEN_ORDER[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };

static int32_t inf_dynamic_block(inf_state_t *s) {
    int32_t hlit  = inf_bits(s, 5); if (hlit  < 0) return hlit;  hlit  += 257;
    int32_t hdist = inf_bits(s, 5); if (hdist < 0) return hdist; hdist += 1;
    int32_t hclen = inf_bits(s, 4); if (hclen < 0) return hclen; hclen += 4;

    if (hlit > MAXLCODES || hdist > MAXDCODES) return INFLATE_ERR_TOO_MANY_CODES;

    int16_t clen_lengths[19];
    for (int32_t i = 0; i < 19; i++) clen_lengths[i] = 0;
    for (int32_t i = 0; i < hclen; i++) {
        int32_t v = inf_bits(s, 3);
        if (v < 0) return v;
        clen_lengths[CLEN_ORDER[i]] = (int16_t)v;
    }

    inf_huffman_t clencode;
    if (inf_construct(&clencode, clen_lengths, 19) != 0) return INFLATE_ERR_INVALID_HUFFMAN;

    int16_t lengths[MAXCODES];
    int32_t idx = 0;
    int32_t total = hlit + hdist;
    while (idx < total) {
        int32_t sym = inf_decode(s, &clencode);
        if (sym < 0) return sym;

        if (sym < 16) {
            lengths[idx++] = (int16_t)sym;
        } else if (sym == 16) {
            if (idx == 0) return INFLATE_ERR_INVALID_HUFFMAN;
            int32_t rep = inf_bits(s, 2); if (rep < 0) return rep; rep += 3;
            if (idx + rep > total) return INFLATE_ERR_TOO_MANY_CODES;
            int16_t prev = lengths[idx - 1];
            while (rep--) lengths[idx++] = prev;
        } else if (sym == 17) {
            int32_t rep = inf_bits(s, 3); if (rep < 0) return rep; rep += 3;
            if (idx + rep > total) return INFLATE_ERR_TOO_MANY_CODES;
            while (rep--) lengths[idx++] = 0;
        } else { /* sym == 18 */
            int32_t rep = inf_bits(s, 7); if (rep < 0) return rep; rep += 11;
            if (idx + rep > total) return INFLATE_ERR_TOO_MANY_CODES;
            while (rep--) lengths[idx++] = 0;
        }
    }

    inf_huffman_t lencode, distcode;
    if (inf_construct(&lencode, lengths, hlit) != 0) return INFLATE_ERR_INVALID_HUFFMAN;
    if (inf_construct(&distcode, lengths + hlit, hdist) != 0) return INFLATE_ERR_INVALID_HUFFMAN;

    return inf_codes(s, &lencode, &distcode);
}

/* ── Public entry point ──────────────────────────────────────────────── */
int32_t inflate_raw(const uint8_t *src, uint32_t src_len, uint8_t *out_buf, uint32_t out_cap) {
    if (!src || !out_buf) return INFLATE_ERR_INPUT_EXHAUSTED;

    inf_state_t s;
    s.in = src; s.in_len = src_len; s.in_pos = 0;
    s.bitbuf = 0; s.bitcnt = 0;
    s.out = out_buf; s.out_cap = out_cap; s.out_pos = 0;

    int32_t is_final;
    do {
        is_final = inf_bits(&s, 1);
        if (is_final < 0) return is_final;

        int32_t btype = inf_bits(&s, 2);
        if (btype < 0) return btype;

        int32_t rc;
        switch (btype) {
            case 0: rc = inf_stored(&s); break;
            case 1: rc = inf_fixed_block(&s); break;
            case 2: rc = inf_dynamic_block(&s); break;
            default: return INFLATE_ERR_INVALID_BTYPE;
        }
        if (rc != 0) return rc;
    } while (!is_final);

    return (int32_t)s.out_pos;
}
