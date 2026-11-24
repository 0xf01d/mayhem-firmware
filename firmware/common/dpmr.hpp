/*
 * Copyright (C) 2026 Mayhem fork contributors
 *
 * This file is part of PortaPack.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

/* dPMR446 layer-2 codec per ETSI TS 102 490 V1.9.1 (peer-to-peer / ISF mode).
 * Header-only, no STL, no floating point: usable from both the M0 application
 * and the M4 baseband core.
 *
 * Bit vectors are uint8_t arrays holding one bit per element, element 0 being
 * the first transmitted bit (clause 9.0: fields are sent MSB first).
 *
 * Implemented clauses:
 *   4.2.2  transmission format   5.1-5.16  frame contents
 *   6.1    sync words / channel code / preamble
 *   7.1    di-bit coding         7.2  CRC7/CRC8       7.3  Hamming (12,8)
 *   7.4    scrambling            7.5  interleavers    7.6-7.8 FEC chains
 *   9.1/9.5/9.6 coding chains    12.2 modulation (4FSK 2400 sym/s)
 *   table 6.1/6.2 channel codes, table 8.2 common ID, annex A.2 (B2 mapping)
 */

#ifndef __DPMR_H__
#define __DPMR_H__

#include <cstddef>
#include <cstdint>

namespace dpmr {

/* Sync words (6.1), MSB first as transmitted. */
constexpr uint64_t fs1 = 0x57FF5F75D577ULL; /* 48 bits, 6.1.1 */
constexpr uint32_t fs2 = 0x5FF77D;          /* 24 bits, 6.1.2 */
constexpr uint32_t fs3 = 0x7DDFF5;          /* 24 bits, 6.1.3 */
constexpr uint64_t fs4 = 0xFD55F5DF7FDDULL; /* 48 bits, 6.1.4 */
constexpr uint8_t preamble_byte = 0x5F;     /* repeated >= 72 bits, 6.1.6 */

constexpr size_t cch_bits = 41;   /* 5.1: FN+ID+M+V+F+RES+SLD */
constexpr size_t hi_bits = 72;    /* 5.2: HT+IDs+M+V+F+RES+CI */
constexpr size_t end_bits = 17;   /* 5.3: ET+ARQ+WAIT+STAT+RES */
constexpr size_t cc_bits = 12;    /* 6.1.5 channel code */
constexpr size_t tch_block_bits = 72;
constexpr size_t tch_blocks_per_frame = 4; /* 72 x 4 = 288 bits per frame */
constexpr size_t tch_frame_bits = tch_block_bits * tch_blocks_per_frame;

constexpr size_t cch_fec_bits = 72;   /* 6 x Hamming(12,8), 7.6 */
constexpr size_t hi_fec_bits = 120;   /* 10 x Hamming(12,8), 7.7 */
constexpr size_t end_fec_bits = 72;   /* 2 x (3 x Hamming(12,8)), 7.8 */

/* ---------------------------------------------------------------- CRC (7.2) */

/* CRC-7, G(x) = x^7 + x^3 + 1. MSB first, init 0. */
inline uint8_t crc7(const uint8_t* bits, size_t count) {
    uint8_t reg = 0;
    for (size_t i = 0; i < count; i++) {
        const uint8_t fb = ((reg >> 6) & 1u) ^ (bits[i] & 1u);
        reg = static_cast<uint8_t>((reg << 1) & 0x7Fu);
        if (fb)
            reg ^= 0x09u; /* x^3 + 1 */
    }
    return reg;
}

/* CRC-8, G(x) = x^8 + x^2 + x + 1. MSB first, init 0. */
inline uint8_t crc8(const uint8_t* bits, size_t count) {
    uint8_t reg = 0;
    for (size_t i = 0; i < count; i++) {
        const uint8_t fb = ((reg >> 7) & 1u) ^ (bits[i] & 1u);
        reg = static_cast<uint8_t>(reg << 1);
        if (fb)
            reg ^= 0x07u; /* x^2 + x + 1 */
    }
    return reg;
}

/* ------------------------------------------------- Hamming (12,8) (7.3) */

/* Block order: X7 X6 X5 X4 X3 X2 X1 X0 C3 C2 C1 C0 (table 7.1, left to right). */
inline void hamming_encode_block(const uint8_t data[8], uint8_t out[12]) {
    for (size_t i = 0; i < 8; i++)
        out[i] = data[i] & 1u;
    /* Parity rows of table 7.1. */
    out[8] = data[0] ^ data[2] ^ data[4] ^ data[5];                 /* C3 */
    out[9] = data[0] ^ data[1] ^ data[3] ^ data[5] ^ data[6];       /* C2 */
    out[10] = data[0] ^ data[1] ^ data[2] ^ data[4] ^ data[6] ^ data[7]; /* C1 */
    out[11] = data[1] ^ data[3] ^ data[4] ^ data[7];                /* C0 */
}

/* Syndrome column pattern of each of the 12 block bit positions. */
constexpr uint8_t hamming_syndrome[12] = {
    0xE, 0x7, 0xA, 0x5, 0xB, 0xC, 0x6, 0x3, /* X7..X0 */
    0x8, 0x4, 0x2, 0x1,                     /* C3,C2,C1,C0 */
};

/* Hamming decode result: 0 clean, 1 single-bit corrected, -1 uncorrectable. */
inline int hamming_decode_block(const uint8_t in[12], uint8_t data[8]) {
    uint8_t syndrome = 0;
    for (size_t i = 0; i < 12; i++)
        if (in[i] & 1u)
            syndrome ^= hamming_syndrome[i];

    if (syndrome == 0) {
        for (size_t i = 0; i < 8; i++)
            data[i] = in[i] & 1u;
        return 0;
    }

    int error_pos = -1;
    for (size_t i = 0; i < 12; i++) {
        if (hamming_syndrome[i] == syndrome) {
            error_pos = static_cast<int>(i);
            break;
        }
    }

    if (error_pos < 0) {
        for (size_t i = 0; i < 8; i++)
            data[i] = in[i] & 1u;
        return -1;
    }

    for (size_t i = 0; i < 8; i++)
        data[i] = in[i] & 1u;
    if (error_pos < 8)
        data[error_pos] ^= 1u; /* parity bit errors do not touch the data */
    return 1;
}

/* ---------------------------------------------------- Interleavers (7.5) */

/* Transmit interleave (table 7.2/7.3): data in by columns, out by rows.
 * out[i] = in[12 * (i % nblocks) + i / nblocks]; nblocks is 6 (12x6, CCH) or
 * 10 (12x10, HI). Output starts with in[0], in[12], in[24] ... i.e. the
 * 1-based order 1,13,25,37,... required by 7.5. */
inline void interleave(const uint8_t* in, uint8_t* out, size_t nblocks) {
    const size_t n = nblocks * 12;
    for (size_t i = 0; i < n; i++)
        out[i] = in[12 * (i % nblocks) + (i / nblocks)];
}

/* Receive de-interleave: the exact inverse permutation,
 * out[i] = in[nblocks * (i % 12) + i / 12]. */
inline void deinterleave(const uint8_t* in, uint8_t* out, size_t nblocks) {
    const size_t n = nblocks * 12;
    for (size_t i = 0; i < n; i++)
        out[i] = in[nblocks * (i % 12) + (i / 12)];
}

/* ------------------------------------------------------ Scrambler (7.4) */

/* x^9 + x^5 + 1, preset all ones. Restarted at the start of every coded
 * block (note under 7.4; per 9.6 each repeated 36-bit END copy is scrambled
 * with a fresh run). */
struct Scrambler {
    /* 10-bit shift register, bit 9 preset to one: the first nine outputs are
     * then all one, matching 7.4's all-ones preset. */
    uint32_t sr{0x3FF};

    void reset() {
        sr = 0x3FF;
    }

    uint8_t next() {
        sr >>= 1;
        const uint8_t out = sr & 1u;
        const uint8_t fb = ((sr >> 4) & 1u) ^ out; /* x^5 tap, insert at x^9 */
        sr = (sr & 0x1FFu) | (static_cast<uint32_t>(fb) << 9);
        return out;
    }
};

/* Scramble/descramble in place (addition and removal are the same XOR). */
inline void scramble(uint8_t* bits, size_t count) {
    Scrambler s;
    for (size_t i = 0; i < count; i++)
        bits[i] ^= s.next();
}

/* ------------------------------------------------------ FEC chains */

struct DecodeResult {
    uint8_t corrected{0};     /* bits fixed by Hamming */
    uint8_t failed_blocks{0}; /* blocks with uncorrectable syndromes */
    bool crc_ok{false};
};

/* 7.6 chain without the final scramble: data superframes scramble CCH and
 * TCH together as one 360-bit run (9.2/9.3), so their CCH is assembled
 * unscrambled and the whole frame scrambled afterwards. */
inline void encode_cch_coded(const uint8_t cch[cch_bits], uint8_t out[cch_fec_bits]) {
    uint8_t msg[48];
    for (size_t i = 0; i < cch_bits; i++)
        msg[i] = cch[i] & 1u;
    const uint8_t crc = crc7(msg, cch_bits);
    for (size_t i = 0; i < 7; i++)
        msg[cch_bits + i] = (crc >> (6 - i)) & 1u;

    uint8_t coded[cch_fec_bits];
    for (size_t b = 0; b < 6; b++)
        hamming_encode_block(&msg[b * 8], &coded[b * 12]);

    interleave(coded, out, 6);
}

/* 7.6: CCH 41 bits -> +CRC7 -> 6 x Hamming(12,8) -> 12x6 interleave -> scramble. */
inline void encode_cch(const uint8_t cch[cch_bits], uint8_t out[cch_fec_bits]) {
    encode_cch_coded(cch, out);
    scramble(out, cch_fec_bits);
}

/* FEC decoding of an already-descrambled CCH block (used by the combined
 * data-frame decode, 9.2/9.3). Fields are extracted with unpack_cch(). */
inline DecodeResult decode_cch_coded(const uint8_t in[cch_fec_bits], uint8_t cch[cch_bits]) {
    uint8_t work[cch_fec_bits];
    for (size_t i = 0; i < cch_fec_bits; i++)
        work[i] = in[i] & 1u;

    uint8_t coded[cch_fec_bits];
    deinterleave(work, coded, 6);

    DecodeResult res;
    uint8_t msg[48];
    for (size_t b = 0; b < 6; b++) {
        const int r = hamming_decode_block(&coded[b * 12], &msg[b * 8]);
        if (r > 0)
            res.corrected++;
        else if (r < 0)
            res.failed_blocks++;
    }

    for (size_t i = 0; i < cch_bits; i++)
        cch[i] = msg[i];
    uint8_t crc_rx = 0;
    for (size_t i = 0; i < 7; i++)
        crc_rx = static_cast<uint8_t>((crc_rx << 1) | (msg[cch_bits + i] & 1u));
    res.crc_ok = (crc7(msg, cch_bits) == crc_rx);
    return res;
}

/* Decoding of a voice-superframe CCH: it is scrambled on its own 72-bit
 * run (9.1), unlike the combined CCH+TCH run of the data superframes. */
inline DecodeResult decode_cch(const uint8_t in[cch_fec_bits], uint8_t cch[cch_bits]) {
    uint8_t work[cch_fec_bits];
    for (size_t i = 0; i < cch_fec_bits; i++)
        work[i] = in[i] & 1u;
    scramble(work, cch_fec_bits);
    return decode_cch_coded(work, cch);
}

/* 7.7: HI 72 bits -> +CRC8 -> 10 x Hamming(12,8) -> 12x10 interleave -> scramble. */
inline void encode_hi(const uint8_t hi[hi_bits], uint8_t out[hi_fec_bits]) {
    uint8_t msg[80];
    for (size_t i = 0; i < hi_bits; i++)
        msg[i] = hi[i] & 1u;
    const uint8_t crc = crc8(msg, hi_bits);
    for (size_t i = 0; i < 8; i++)
        msg[hi_bits + i] = (crc >> (7 - i)) & 1u;

    uint8_t coded[hi_fec_bits];
    for (size_t b = 0; b < 10; b++)
        hamming_encode_block(&msg[b * 8], &coded[b * 12]);

    interleave(coded, out, 10);
    scramble(out, hi_fec_bits);
}

/* Inverse of encode_hi. Fields are extracted with unpack_hi(). */
inline DecodeResult decode_hi(const uint8_t in[hi_fec_bits], uint8_t hi[hi_bits]) {
    uint8_t work[hi_fec_bits];
    for (size_t i = 0; i < hi_fec_bits; i++)
        work[i] = in[i] & 1u;
    scramble(work, hi_fec_bits);

    uint8_t coded[hi_fec_bits];
    deinterleave(work, coded, 10);

    DecodeResult res;
    uint8_t msg[80];
    for (size_t b = 0; b < 10; b++) {
        const int r = hamming_decode_block(&coded[b * 12], &msg[b * 8]);
        if (r > 0)
            res.corrected++;
        else if (r < 0)
            res.failed_blocks++;
    }

    for (size_t i = 0; i < hi_bits; i++)
        hi[i] = msg[i];
    uint8_t crc_rx = 0;
    for (size_t i = 0; i < 8; i++)
        crc_rx = static_cast<uint8_t>((crc_rx << 1) | (msg[hi_bits + i] & 1u));
    res.crc_ok = (crc8(msg, hi_bits) == crc_rx);
    return res;
}

/* 7.8/9.6: END 17 bits -> +CRC7 -> 3 x Hamming(12,8) = 36 bits, repeated,
 * each 36-bit copy scrambled with a fresh scrambler run. */
inline void encode_end(const uint8_t end[end_bits], uint8_t out[end_fec_bits]) {
    uint8_t msg[24];
    for (size_t i = 0; i < end_bits; i++)
        msg[i] = end[i] & 1u;
    const uint8_t crc = crc7(msg, end_bits);
    for (size_t i = 0; i < 7; i++)
        msg[end_bits + i] = (crc >> (6 - i)) & 1u;

    uint8_t coded[36];
    for (size_t b = 0; b < 3; b++)
        hamming_encode_block(&msg[b * 8], &coded[b * 12]);

    for (size_t copy_i = 0; copy_i < 2; copy_i++) {
        for (size_t i = 0; i < 36; i++)
            out[copy_i * 36 + i] = coded[i];
        scramble(&out[copy_i * 36], 36);
    }
}

/* Inverse of encode_end: both copies are decoded (9.6 has no interleaving
 * here); the first one whose CRC verifies wins. Fields use unpack_end(). */
inline DecodeResult decode_end(const uint8_t in[end_fec_bits], uint8_t end[end_bits]) {
    DecodeResult res;
    for (size_t copy_i = 0; copy_i < 2; copy_i++) {
        uint8_t coded[36];
        for (size_t i = 0; i < 36; i++)
            coded[i] = in[copy_i * 36 + i] & 1u;
        scramble(coded, 36);

        DecodeResult r;
        uint8_t msg[24];
        for (size_t b = 0; b < 3; b++) {
            const int hr = hamming_decode_block(&coded[b * 12], &msg[b * 8]);
            if (hr > 0)
                r.corrected++;
            else if (hr < 0)
                r.failed_blocks++;
        }
        uint8_t crc_rx = 0;
        for (size_t i = 0; i < 7; i++)
            crc_rx = static_cast<uint8_t>((crc_rx << 1) | (msg[end_bits + i] & 1u));
        r.crc_ok = (crc7(msg, end_bits) == crc_rx);

        if (copy_i == 0 || (r.crc_ok && !res.crc_ok))
            res = r;
        if (r.crc_ok) {
            for (size_t i = 0; i < end_bits; i++)
                end[i] = msg[i];
            return res;
        }
        for (size_t i = 0; i < end_bits; i++)
            end[i] = msg[i];
    }
    return res;
}

/* ------------------------------------------------- Field packing (5.x) */

/* CCH (5.1a): FN(2) ID(12) M(3) V(2) F(2) RES(2) SLD(18). */
struct CchFields {
    uint8_t frame_number{0}; /* FN, 5.6 */
    uint16_t id{0};          /* 12-bit address half, 9.1 */
    uint8_t comm_mode{0};    /* M, 5.7 */
    uint8_t version{0};      /* V, 5.16 */
    uint8_t comm_format{0};  /* F, 5.8 */
    uint8_t reserved{0};
    uint32_t sld{0}; /* 5.9 */
};

inline uint32_t bits_to_u32(const uint8_t* bits, size_t count) {
    uint32_t v = 0;
    for (size_t i = 0; i < count; i++)
        v = (v << 1) | (bits[i] & 1u);
    return v;
}

inline void u32_to_bits(uint32_t value, uint8_t* bits, size_t count) {
    for (size_t i = count; i > 0; i--) {
        bits[i - 1] = value & 1u;
        value >>= 1;
    }
}

inline void u64_to_bits(uint64_t value, uint8_t* bits, size_t count) {
    for (size_t i = count; i > 0; i--) {
        bits[i - 1] = value & 1u;
        value >>= 1;
    }
}

/* Bytes <-> bits, MSB of byte 0 transmitted first (clause 9.0). */
inline void bytes_to_bits(const uint8_t* bytes, uint8_t* bits, size_t byte_count) {
    for (size_t i = 0; i < byte_count; i++)
        for (size_t b = 0; b < 8; b++)
            bits[i * 8 + b] = (bytes[i] >> (7 - b)) & 1u;
}

inline void bits_to_bytes(const uint8_t* bits, uint8_t* bytes, size_t byte_count) {
    for (size_t i = 0; i < byte_count; i++) {
        uint8_t v = 0;
        for (size_t b = 0; b < 8; b++)
            v = static_cast<uint8_t>((v << 1) | (bits[i * 8 + b] & 1u));
        bytes[i] = v;
    }
}

inline void pack_cch(const CchFields& f, uint8_t bits[cch_bits]) {
    u32_to_bits(f.frame_number, &bits[0], 2);
    u32_to_bits(f.id, &bits[2], 12);
    u32_to_bits(f.comm_mode, &bits[14], 3);
    u32_to_bits(f.version, &bits[17], 2);
    u32_to_bits(f.comm_format, &bits[19], 2);
    u32_to_bits(f.reserved, &bits[21], 2);
    u32_to_bits(f.sld, &bits[23], 18);
}

inline CchFields unpack_cch(const uint8_t bits[cch_bits]) {
    CchFields f;
    f.frame_number = bits_to_u32(&bits[0], 2);
    f.id = bits_to_u32(&bits[2], 12);
    f.comm_mode = bits_to_u32(&bits[14], 3);
    f.version = bits_to_u32(&bits[17], 2);
    f.comm_format = bits_to_u32(&bits[19], 2);
    f.reserved = bits_to_u32(&bits[21], 2);
    f.sld = bits_to_u32(&bits[23], 18);
    return f;
}

/* HI (5.2): HT(4) called(24) own(24) M(3) V(2) F(2) RES(2) CI(11). */
struct HiFields {
    uint8_t header_type{0}; /* HT, 5.11 */
    uint32_t called_id{0};  /* 24-bit, 8.1.1 */
    uint32_t own_id{0};     /* 24-bit */
    uint8_t comm_mode{0};   /* M, 5.7 */
    uint8_t version{0};     /* V, 5.16 */
    uint8_t comm_format{0}; /* F, 5.8 */
    uint8_t reserved{0};
    uint8_t ci_type{0}; /* CI type, 3 bits, 5.10 */
    uint8_t ci_info{0}; /* CI information, 8 bits */
};

inline void pack_hi(const HiFields& f, uint8_t bits[hi_bits]) {
    u32_to_bits(f.header_type, &bits[0], 4);
    u32_to_bits(f.called_id, &bits[4], 24);
    u32_to_bits(f.own_id, &bits[28], 24);
    u32_to_bits(f.comm_mode, &bits[52], 3);
    u32_to_bits(f.version, &bits[55], 2);
    u32_to_bits(f.comm_format, &bits[57], 2);
    u32_to_bits(f.reserved, &bits[59], 2);
    u32_to_bits(f.ci_type, &bits[61], 3);
    u32_to_bits(f.ci_info, &bits[64], 8);
}

inline HiFields unpack_hi(const uint8_t bits[hi_bits]) {
    HiFields f;
    f.header_type = bits_to_u32(&bits[0], 4);
    f.called_id = bits_to_u32(&bits[4], 24);
    f.own_id = bits_to_u32(&bits[28], 24);
    f.comm_mode = bits_to_u32(&bits[52], 3);
    f.version = bits_to_u32(&bits[55], 2);
    f.comm_format = bits_to_u32(&bits[57], 2);
    f.reserved = bits_to_u32(&bits[59], 2);
    f.ci_type = bits_to_u32(&bits[61], 3);
    f.ci_info = bits_to_u32(&bits[64], 8);
    return f;
}

/* END (5.3): ET(2) ARQ(2) WAIT(4) STAT(5) RES(4). */
struct EndFields {
    uint8_t end_type{0}; /* ET, 5.12 */
    uint8_t arq{0};      /* 5.13 */
    uint8_t tx_wait{0};  /* 5.14 */
    uint8_t status{0};   /* STAT, 5.15 */
    uint8_t reserved{0};
};

inline void pack_end(const EndFields& f, uint8_t bits[end_bits]) {
    u32_to_bits(f.end_type, &bits[0], 2);
    u32_to_bits(f.arq, &bits[2], 2);
    u32_to_bits(f.tx_wait, &bits[4], 4);
    u32_to_bits(f.status, &bits[8], 5);
    u32_to_bits(f.reserved, &bits[13], 4);
}

inline EndFields unpack_end(const uint8_t bits[end_bits]) {
    EndFields f;
    f.end_type = bits_to_u32(&bits[0], 2);
    f.arq = bits_to_u32(&bits[2], 2);
    f.tx_wait = bits_to_u32(&bits[4], 4);
    f.status = bits_to_u32(&bits[8], 5);
    f.reserved = bits_to_u32(&bits[13], 4);
    return f;
}

/* ------------------------------------------- Channel code (7.1, 6.1.5) */

/* Di-bit coding of the channel code: 0 -> 01, 1 -> 11. The code bit is the
 * first (MSB) bit of each dibit; the second bit is always 1. */
inline void channel_code_encode(uint16_t code12, uint8_t bits24[24]) {
    for (size_t i = 0; i < 12; i++) {
        const uint8_t bit = (code12 >> (11 - i)) & 1u;
        bits24[2 * i] = bit;
        bits24[2 * i + 1] = 1;
    }
}

inline uint16_t channel_code_decode(const uint8_t bits24[24]) {
    uint16_t code = 0;
    for (size_t i = 0; i < 12; i++)
        code = (code << 1) | (bits24[2 * i] & 1u);
    return code;
}

/* Di-bit encode/decode of an arbitrary bit pair stream (table 12.1): a
 * dibit b1b0 carries bit b1 first. 01 -> +3, 00 -> +1, 10 -> -1, 11 -> -3. */
inline void bits_to_dibits(const uint8_t* bits, uint8_t* dibits, size_t dibit_count) {
    for (size_t i = 0; i < dibit_count; i++)
        dibits[i] = static_cast<uint8_t>(((bits[2 * i] & 1u) << 1) | (bits[2 * i + 1] & 1u));
}

inline void dibits_to_bits(const uint8_t* dibits, uint8_t* bits, size_t dibit_count) {
    for (size_t i = 0; i < dibit_count; i++) {
        bits[2 * i] = (dibits[i] >> 1) & 1u;
        bits[2 * i + 1] = dibits[i] & 1u;
    }
}

/* --------------------------------- PMR446 channel plan (table 6.1/6.2) */

/* 32 x 6.25 kHz channels from 446.003125 MHz (12.1.2). The Group A channel
 * codes of table 6.1 repeat on the upper half band (table 6.2). */
constexpr size_t channel_count = 32;

inline uint32_t channel_frequency_hz(size_t channel_1_to_32) {
    return 446003125u + static_cast<uint32_t>(channel_1_to_32 - 1) * 6250u;
}

/* Channel codes as transmitted: the 12-bit code di-bit coded into 24 bits
 * (hex column of table 6.1, Group A / ISF per 8.1.2). Table 6.2 repeats the
 * same codes on channels 17..32. */
constexpr uint32_t group_a_codes[16] = {
    0x577577, /* ch 1/17  */
    0x57DD75, /* ch 2/18  */
    0x57F775, /* ch 3/19  */
    0x55577D, /* ch 4/20  */
    0x557D7D, /* ch 5/21  */
    0x55D57F, /* ch 6/22  */
    0x55FF7F, /* ch 7/23  */
    0x5F555F, /* ch 8/24  */
    0x5F7F5F, /* ch 9/25  */
    0x5FD75D, /* ch 10/26 */
    0x5FFD5D, /* ch 11/27 */
    0x5D5D55, /* ch 12/28 */
    0x5D7755, /* ch 13/29 */
    0x5DDF57, /* ch 14/30 */
    0x5DF557, /* ch 15/31 */
    0x775DD7, /* ch 16/32 */
};

inline uint32_t channel_code_24(size_t channel_1_to_32) {
    return group_a_codes[(channel_1_to_32 - 1) & 15u];
}

/* ------------------------------------------------ Addressing (8.1, A.2) */

/* ISF addressing: 24-bit address = 8-bit common ID followed by 16 fixed
 * one-bits (8.1.1.0/8.1.1.2). Common ID per table 8.2: 0 reserved, 1..254
 * applicable, 255 all call. */
inline uint32_t isf_address(uint8_t common_id) {
    return (static_cast<uint32_t>(common_id) << 16) | 0xFFFFu;
}

/* Returns the common ID if addr is an ISF address, else -1. */
inline int isf_common_id(uint32_t addr) {
    if ((addr & 0xFFFFu) == 0xFFFFu)
        return static_cast<int>((addr >> 16) & 0xFFu);
    return -1;
}

/* B2 mapping of a 7-digit dialled string (annex A.2.1.1.6.1). Digits hold
 * values 0..9; K4..K7 also accept 10 for '*'. */
inline uint32_t b2_encode(const uint8_t digits[7]) {
    return static_cast<uint32_t>(digits[0]) * 1464100u +
           static_cast<uint32_t>(digits[1]) * 146410u +
           static_cast<uint32_t>(digits[2]) * 14641u +
           static_cast<uint32_t>(digits[3]) * 1331u +
           static_cast<uint32_t>(digits[4]) * 121u +
           static_cast<uint32_t>(digits[5]) * 11u +
           static_cast<uint32_t>(digits[6]);
}

/* Inverse B2. out holds 7 digit values 0..9 / 10 ('*'); returns false when
 * addr is outside the dialable range "0000001".."999****". */
inline bool b2_decode(uint32_t addr, uint8_t out[7]) {
    if (addr >= 14641000u || addr == 0)
        return false;
    const uint32_t low = addr % 14641u;
    const uint32_t high = addr / 14641u;
    out[6] = low % 11u;
    out[5] = (low / 11u) % 11u;
    out[4] = (low / 121u) % 11u;
    out[3] = (low / 1331u) % 11u;
    out[2] = high % 10u;
    out[1] = (high / 10u) % 10u;
    out[0] = (high / 100u) % 10u;
    return true;
}

/* SLD for data superframes (5.9.2): RES(5) DP(2) Format(4) Cont(1) Length(6).
 * DP = 11 marks a data superframe; Format 0010 is free text. */
inline uint32_t pack_sld_data(uint8_t dp, uint8_t format, uint8_t cont, uint8_t length) {
    return (0u << 13) | ((dp & 0x3u) << 11) | ((format & 0xFu) << 7) | ((cont & 0x1u) << 6) | (length & 0x3Fu);
}

/* ------------------------------------- Transmit frame assembly (4.2.2) */

constexpr size_t preamble_bits_min = 72; /* 6.1.6, repeated 0x5F */
constexpr size_t header_frame_bits = preamble_bits_min + 48 + hi_fec_bits + 24 + hi_fec_bits; /* 384 */
constexpr size_t payload_frame_bits = 24 + cch_fec_bits + tch_frame_bits;                    /* 384 */
constexpr size_t end_frame_total_bits = 24 + end_fec_bits;                                   /* 96 */

/* One call's addressing and mode fields, shared by header and payload CCH. */
struct TxCall {
    uint16_t cc12{0};      /* 12-bit channel code value, table 6.1 */
    uint32_t called_id{0}; /* 24-bit */
    uint32_t own_id{0};    /* 24-bit */
    uint8_t comm_mode{0};  /* M, 5.7 */
    uint8_t version{0};    /* V, 5.16 */
    uint8_t comm_format{0}; /* F, 5.8: 00 all call, 01 peer-to-peer */
};

inline void put_preamble(uint8_t* bits, size_t count) {
    for (size_t i = 0; i < count; i++)
        bits[i] = (preamble_byte >> (7 - (i % 8))) & 1u;
}

/* Header frame (9.5): preamble(72) FS1(48) HI0(120) CC(24) HI1(120). */
inline void encode_header_frame(
    const TxCall& c,
    uint8_t header_type,
    uint8_t ci_type,
    uint8_t ci_info,
    uint8_t out[header_frame_bits]) {
    HiFields f;
    f.header_type = header_type;
    f.called_id = c.called_id;
    f.own_id = c.own_id;
    f.comm_mode = c.comm_mode;
    f.version = c.version;
    f.comm_format = c.comm_format;
    f.ci_type = ci_type;
    f.ci_info = ci_info;
    uint8_t hi_bits_v[hi_bits], hi_coded[hi_fec_bits];
    pack_hi(f, hi_bits_v);
    encode_hi(hi_bits_v, hi_coded);

    put_preamble(out, preamble_bits_min);
    u64_to_bits(fs1, out + preamble_bits_min, 48);
    for (size_t i = 0; i < hi_fec_bits; i++)
        out[preamble_bits_min + 48 + i] = hi_coded[i];
    uint8_t cc24[24];
    channel_code_encode(c.cc12, cc24);
    for (size_t i = 0; i < 24; i++)
        out[preamble_bits_min + 48 + hi_fec_bits + i] = cc24[i];
    for (size_t i = 0; i < hi_fec_bits; i++)
        out[preamble_bits_min + 48 + hi_fec_bits + 24 + i] = hi_coded[i];
}

/* End frame (9.6): FS3(24) + two coded END copies. */
inline void encode_end_frame(uint8_t et, uint8_t arq, uint8_t wait, uint8_t stat, uint8_t out[end_frame_total_bits]) {
    EndFields f;
    f.end_type = et;
    f.arq = arq;
    f.tx_wait = wait;
    f.status = stat;
    uint8_t end_v[end_bits], end_coded[end_fec_bits];
    pack_end(f, end_v);
    encode_end(end_v, end_coded);
    u32_to_bits(fs3, out, 24);
    for (size_t i = 0; i < end_fec_bits; i++)
        out[24 + i] = end_coded[i];
}

/* CCH of a payload frame (9.1/9.2/9.3): FN + the ID slice for this frame
 * number + M + V + F + RES + SLD, coded but NOT scrambled (the caller
 * scrambles the voice CCH alone, or data CCH+TCH as one run). */
inline void encode_payload_cch(const TxCall& c, uint8_t fn, uint32_t sld, uint8_t out[cch_fec_bits]) {
    CchFields f;
    f.frame_number = fn;
    /* 9.1: FN 00/01 carry the called station ID's upper/lower 12 bits, FN
     * 10/11 the own ID's. */
    switch (fn & 3u) {
        case 0:
            f.id = (c.called_id >> 12) & 0xFFFu;
            break;
        case 1:
            f.id = c.called_id & 0xFFFu;
            break;
        case 2:
            f.id = (c.own_id >> 12) & 0xFFFu;
            break;
        default:
            f.id = c.own_id & 0xFFFu;
            break;
    }
    f.comm_mode = c.comm_mode;
    f.version = c.version;
    f.comm_format = c.comm_format;
    f.sld = sld;
    uint8_t cch_v[cch_bits];
    pack_cch(f, cch_v);
    encode_cch_coded(cch_v, out);
}

/* 24-bit frame prefix: FS2 on frames 0/2, colour code on frames 1/3 (9.1). */
inline void put_frame_prefix(const TxCall& c, uint8_t fn, uint8_t out[24]) {
    if (fn & 1u) {
        uint8_t cc24[24];
        channel_code_encode(c.cc12, cc24);
        for (size_t i = 0; i < 24; i++)
            out[i] = cc24[i];
    } else {
        u32_to_bits(fs2, out, 24);
    }
}

/* Voice payload frame (9.1): FS2/CC + coded CCH (scrambled on its own) +
 * four raw vocoder blocks, unscrambled. voc is 36 bytes = 288 bits, MSB of
 * byte 0 first (xombe's 9-byte AMBE+2 frames x 4). */
inline void encode_voice_frame(
    const TxCall& c,
    uint8_t fn,
    const uint8_t voc[tch_frame_bits / 8],
    uint8_t out[payload_frame_bits]) {
    uint8_t prefix[24], cch_coded[cch_fec_bits];
    put_frame_prefix(c, fn, prefix);
    encode_payload_cch(c, fn, 0, cch_coded);
    scramble(cch_coded, cch_fec_bits);

    for (size_t i = 0; i < 24; i++)
        out[i] = prefix[i];
    for (size_t i = 0; i < cch_fec_bits; i++)
        out[24 + i] = cch_coded[i];
    uint8_t voc_bits[tch_frame_bits];
    bytes_to_bits(voc, voc_bits, tch_frame_bits / 8);
    for (size_t i = 0; i < tch_frame_bits; i++)
        out[24 + cch_fec_bits + i] = voc_bits[i];
}

/* One coded type-2 data block (9.3): 40 data bits + null bit + CRC7 -> 48
 * bits -> 6 x Hamming(12,8) -> 12x6 interleave. */
inline void encode_t2_block(const uint8_t data5[5], uint8_t out[tch_block_bits]) {
    uint8_t msg[48];
    bytes_to_bits(data5, msg, 5);
    msg[40] = 0;
    const uint8_t crc = crc7(msg, 41);
    for (size_t i = 0; i < 7; i++)
        msg[41 + i] = (crc >> (6 - i)) & 1u;

    uint8_t coded[48];
    for (size_t b = 0; b < 6; b++)
        hamming_encode_block(&msg[b * 8], &coded[b * 12]);
    interleave(coded, out, 6);
}

/* Inverse of encode_t2_block. */
inline DecodeResult decode_t2_block(const uint8_t in[tch_block_bits], uint8_t data5[5]) {
    uint8_t work[tch_block_bits];
    for (size_t i = 0; i < tch_block_bits; i++)
        work[i] = in[i] & 1u;

    uint8_t coded[tch_block_bits];
    deinterleave(work, coded, 6);

    DecodeResult res;
    uint8_t msg[48];
    for (size_t b = 0; b < 6; b++) {
        const int r = hamming_decode_block(&coded[b * 12], &msg[b * 8]);
        if (r > 0)
            res.corrected++;
        else if (r < 0)
            res.failed_blocks++;
    }

    bits_to_bytes(msg, data5, 5);
    uint8_t crc_rx = 0;
    for (size_t i = 0; i < 7; i++)
        crc_rx = static_cast<uint8_t>((crc_rx << 1) | (msg[41 + i] & 1u));
    res.crc_ok = (crc7(msg, 41) == crc_rx);
    return res;
}

/* Type 1 data payload frame (9.2): FS2/CC + CCH(M=010) + 288 raw user data
 * bits, CCH and TCH scrambled together as one 360-bit run. payload is 36
 * bytes = 288 bits. */
inline void encode_data1_frame(
    const TxCall& c,
    uint8_t fn,
    uint32_t sld,
    const uint8_t payload[tch_frame_bits / 8],
    uint8_t out[payload_frame_bits]) {
    TxCall cd = c;
    cd.comm_mode = 0x2; /* 010 */
    uint8_t prefix[24], cch_coded[cch_fec_bits];
    put_frame_prefix(cd, fn, prefix);
    encode_payload_cch(cd, fn, sld, cch_coded);

    for (size_t i = 0; i < 24; i++)
        out[i] = prefix[i];
    for (size_t i = 0; i < cch_fec_bits; i++)
        out[24 + i] = cch_coded[i];
    uint8_t data_bits[tch_frame_bits];
    bytes_to_bits(payload, data_bits, tch_frame_bits / 8);
    for (size_t i = 0; i < tch_frame_bits; i++)
        out[24 + cch_fec_bits + i] = data_bits[i];
    scramble(out + 24, cch_fec_bits + tch_frame_bits);
}

constexpr size_t t2_payload_bytes = tch_blocks_per_frame * 5; /* 20 per frame */

/* Type 2 data payload frame (9.3): CCH(M=011) + 4 coded data blocks, all
 * scrambled as one run. payload is 20 bytes = 4 x 40 bits. */
inline void encode_data2_frame(
    const TxCall& c,
    uint8_t fn,
    uint32_t sld,
    const uint8_t payload[t2_payload_bytes],
    uint8_t out[payload_frame_bits]) {
    TxCall cd = c;
    cd.comm_mode = 0x3; /* 011 */
    uint8_t prefix[24], cch_coded[cch_fec_bits];
    put_frame_prefix(cd, fn, prefix);
    encode_payload_cch(cd, fn, sld, cch_coded);

    for (size_t i = 0; i < 24; i++)
        out[i] = prefix[i];
    for (size_t i = 0; i < cch_fec_bits; i++)
        out[24 + i] = cch_coded[i];
    for (size_t k = 0; k < tch_blocks_per_frame; k++)
        encode_t2_block(&payload[k * 5], out + 24 + cch_fec_bits + k * tch_block_bits);
    scramble(out + 24, cch_fec_bits + tch_frame_bits);
}

/* Combined decode of a data payload frame's CCH+TCH (9.2/9.3). type2 selects
 * the TCH coding; payload receives 36 bytes (type 1) or 20 bytes plus zero
 * padding (type 2). Returns the CCH result; block corrections of type 2
 * blocks are folded in. */
inline DecodeResult decode_data_frame(
    const uint8_t cch_coded[cch_fec_bits],
    const uint8_t tch_coded[tch_frame_bits],
    bool type2,
    uint8_t cch[cch_bits],
    uint8_t payload[tch_frame_bits / 8]) {
    uint8_t work[cch_fec_bits + tch_frame_bits];
    for (size_t i = 0; i < cch_fec_bits; i++)
        work[i] = cch_coded[i] & 1u;
    for (size_t i = 0; i < tch_frame_bits; i++)
        work[cch_fec_bits + i] = tch_coded[i] & 1u;
    scramble(work, cch_fec_bits + tch_frame_bits);

    DecodeResult res = decode_cch_coded(work, cch);
    if (!res.crc_ok)
        return res;

    if (type2) {
        for (size_t i = t2_payload_bytes; i < tch_frame_bits / 8; i++)
            payload[i] = 0;
        for (size_t k = 0; k < tch_blocks_per_frame; k++) {
            const DecodeResult b = decode_t2_block(&work[cch_fec_bits + k * tch_block_bits], &payload[k * 5]);
            res.corrected += b.corrected;
            res.failed_blocks += b.failed_blocks;
            if (!b.crc_ok)
                res.crc_ok = false;
        }
    } else {
        bits_to_bytes(&work[cch_fec_bits], payload, tch_frame_bits / 8);
    }
    return res;
}

} /* namespace dpmr */

#endif /*__DPMR_H__*/
