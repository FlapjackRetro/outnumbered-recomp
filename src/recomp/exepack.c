/*
 * exepack.c - undo Microsoft EXEPACK at load time, so the runtime can load the
 * original SSO.EXE as shipped. Port of pcrecomp tools/drm/unexepack.py.
 *
 * The packed load module is decoded from its end backwards (B0/B1 = fill,
 * B2/B3 = copy, low bit set = last command); the relocation table follows the
 * unpacker's "Packed file is corrupt" message as 16 frames of {count, offsets}.
 * The result is a plain MZ with a 512-byte-aligned header.
 */
#include "exepack.h"
#include <stdlib.h>
#include <string.h>

static unsigned rd16(const uint8_t *p) { return (unsigned)(p[0] | p[1] << 8); }
static void wr16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

int exepack_is_packed(const uint8_t *d, size_t n)
{
    if (n < 0x40 || d[0] != 'M' || d[1] != 'Z') return 0;
    size_t p = rd16(d + 8) * 16u + rd16(d + 0x16) * 16u;
    if (p + 18 > n) return 0;
    return (d[p + 14] == 'R' && d[p + 15] == 'B') || (d[p + 16] == 'R' && d[p + 17] == 'B');
}

int exepack_unpack(const uint8_t *d, size_t n, uint8_t **out_p, size_t *out_n)
{
    static const char msg[] = "Packed file is corrupt";
    if (!exepack_is_packed(d, n)) return -1;
    size_t hdr = rd16(d + 8) * 16u, seg = hdr + rd16(d + 0x16) * 16u;
    unsigned min_extra = rd16(d + 0x0A), max_extra = rd16(d + 0x0C);
    unsigned ip = rd16(d + seg), rcs = rd16(d + seg + 2), size = rd16(d + seg + 6);
    unsigned sp = rd16(d + seg + 8), ss = rd16(d + seg + 10), dest = rd16(d + seg + 12);
    int hlen18 = !(d[seg + 14] == 'R' && d[seg + 15] == 'B');
    unsigned skip = hlen18 ? rd16(d + seg + 14) : 1;

    const uint8_t *packed = d + hdr;
    size_t plen = seg - hdr, mlen = (size_t)dest * 16;
    size_t olen = mlen > plen ? mlen : plen;
    uint8_t *mod = calloc(1, olen);
    if (!mod) return -1;
    memcpy(mod, packed, plen);
    size_t src = plen, dst = skip ? (size_t)(dest - (skip - 1)) * 16 : mlen;
    while (src > 0 && packed[src - 1] == 0xFF) src--;
    for (;;) {
        if (src < 3) goto bad;
        unsigned cmd = packed[src - 1], cnt = (unsigned)(packed[src - 2] << 8 | packed[src - 3]);
        src -= 3;
        if (dst < cnt) goto bad;
        if ((cmd & 0xFE) == 0xB0) {
            if (src < 1) goto bad;
            memset(mod + dst - cnt, packed[--src], cnt);
        } else if ((cmd & 0xFE) == 0xB2) {
            if (src < cnt) goto bad;
            memmove(mod + dst - cnt, packed + src - cnt, cnt);
            src -= cnt;
        } else goto bad;
        dst -= cnt;
        if (cmd & 1) break;
    }

    /* relocation table after the message, inside the unpacker segment */
    if (seg + size > n) goto bad;
    const uint8_t *stub = d + seg;
    size_t at = 0, ml = sizeof msg - 1;
    for (; at + ml <= size; at++) if (!memcmp(stub + at, msg, ml)) break;
    if (at + ml > size) goto bad;
    size_t p = at + ml, nrel = 0, q = p;
    for (int f = 0; f < 16; f++) { if (q + 2 > size) goto bad; unsigned c = rd16(stub + q); nrel += c; q += 2 + 2u * c; }
    size_t nhdr = (0x1C + 4 * nrel + 511) / 512 * 512, total = nhdr + mlen;
    uint8_t *o = calloc(1, total);
    if (!o) goto bad;
    unsigned grown = (unsigned)((mlen > plen ? mlen - plen : 0) / 16);
    unsigned minx = min_extra ? (min_extra > grown ? min_extra - grown : 0) : 0;
    o[0] = 'M'; o[1] = 'Z';
    wr16(o + 2, (unsigned)(total % 512)); wr16(o + 4, (unsigned)((total + 511) / 512)); wr16(o + 6, (unsigned)nrel);
    wr16(o + 8, (unsigned)(nhdr / 16)); wr16(o + 0x0A, minx); wr16(o + 0x0C, max_extra);
    wr16(o + 0x0E, ss); wr16(o + 0x10, sp); wr16(o + 0x14, ip); wr16(o + 0x16, rcs); wr16(o + 0x18, 0x1C);
    size_t r = 0x1C;
    for (unsigned f = 0; f < 16; f++) {
        unsigned c = rd16(stub + p); p += 2;
        for (unsigned k = 0; k < c; k++, p += 2, r += 4) { wr16(o + r, rd16(stub + p)); wr16(o + r + 2, f * 0x1000u); }
    }
    memcpy(o + nhdr, mod, mlen);
    free(mod);
    *out_p = o; *out_n = total;
    return 0;
bad:
    free(mod);
    return -1;
}
