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

#include "doctest.h"
#include "dpmr.hpp"

using namespace dpmr;

/* Fixed pseudo-random-ish vectors (xorshift) so tests are deterministic. */
static void fill_pattern(uint8_t* bits, size_t count, uint32_t seed) {
    uint32_t s = seed ? seed : 1;
    for (size_t i = 0; i < count; i++) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        bits[i] = s & 1u;
    }
}

TEST_CASE("CRC7 matches augmented-division reference vectors") {
    uint8_t bits[24];

    u32_to_bits(0xD75, bits, 12);
    CHECK(crc7(bits, 12) == 0x1D);

    u32_to_bits(0x5FF77D, bits, 24);
    CHECK(crc7(bits, 24) == 0x7B);

    /* Zero message -> zero remainder. */
    for (size_t i = 0; i < 41; i++)
        bits[i] = 0;
    CHECK(crc7(bits, 41) == 0);
}

TEST_CASE("CRC8 matches augmented-division reference vectors") {
    uint8_t bits[24];

    u32_to_bits(0x31, bits, 8);
    CHECK(crc8(bits, 8) == 0x97);

    u32_to_bits(0x1B91FD, bits, 24);
    CHECK(crc8(bits, 24) == 0x47);
}

TEST_CASE("Hamming encode reproduces every generator row of table 7.1") {
    /* Rows of table 7.1: data bit X7..X0 set alone, columns 12..1. */
    static const uint8_t rows[8][12] = {
        {1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0}, /* X7 */
        {0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1}, /* X6 */
        {0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0}, /* X5 */
        {0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1}, /* X4 */
        {0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1}, /* X3 */
        {0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0}, /* X2 */
        {0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0}, /* X1 */
        {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1}, /* X0 */
    };

    for (size_t d = 0; d < 8; d++) {
        uint8_t data[8]{};
        uint8_t out[12]{};
        data[d] = 1;
        hamming_encode_block(data, out);
        for (size_t i = 0; i < 12; i++)
            CHECK(out[i] == rows[d][i]);
    }
}

TEST_CASE("Hamming decode corrects every single-bit error in a block") {
    uint8_t data[8];
    fill_pattern(data, 8, 0x1234);

    uint8_t encoded[12];
    hamming_encode_block(data, encoded);

    for (size_t pos = 0; pos < 12; pos++) {
        uint8_t corrupted[12];
        for (size_t i = 0; i < 12; i++)
            corrupted[i] = encoded[i];
        corrupted[pos] ^= 1;

        uint8_t decoded[8];
        const int result = hamming_decode_block(corrupted, decoded);
        CHECK(result == 1);
        for (size_t i = 0; i < 8; i++)
            CHECK(decoded[i] == data[i]);
    }

    /* Clean block decodes without correction. */
    uint8_t decoded[8];
    CHECK(hamming_decode_block(encoded, decoded) == 0);
    for (size_t i = 0; i < 8; i++)
        CHECK(decoded[i] == data[i]);
}

TEST_CASE("Hamming flags double-bit errors") {
    uint8_t data[8];
    fill_pattern(data, 8, 0xBEEF);

    uint8_t encoded[12];
    hamming_encode_block(data, encoded);

    /* Minimum distance is 3: every 2-bit error either reports uncorrectable
     * or miscorrects, but must never return the original data as valid. */
    for (size_t p1 = 0; p1 < 12; p1++) {
        for (size_t p2 = p1 + 1; p2 < 12; p2++) {
            uint8_t corrupted[12];
            for (size_t i = 0; i < 12; i++)
                corrupted[i] = encoded[i];
            corrupted[p1] ^= 1;
            corrupted[p2] ^= 1;

            uint8_t decoded[8];
            const int result = hamming_decode_block(corrupted, decoded);
            const bool uncorrectable_or_miscorrected = (result == -1) || (result == 1);
            CHECK(uncorrectable_or_miscorrected);
            if (result == 1) {
                bool differs = false;
                for (size_t i = 0; i < 8; i++)
                    if (decoded[i] != data[i])
                        differs = true;
                CHECK(differs);
            } else {
                CHECK(result == -1);
            }
        }
    }
}

TEST_CASE("Interleaver permutation starts 1,13,25,37,... and is invertible") {
    uint8_t in[120];
    fill_pattern(in, 120, 0x55AA);

    SUBCASE("12x6 (CCH/TCH)") {
        uint8_t out[72];
        uint8_t back[72];
        interleave(in, out, 6);

        /* out[1..5] take in[12], in[24], in[36], in[48], in[60] (1-based 13,25,37,49,61). */
        for (size_t j = 0; j < 6; j++)
            CHECK(out[j] == in[12 * j]);

        deinterleave(out, back, 6);
        for (size_t i = 0; i < 72; i++)
            CHECK(back[i] == in[i]);
    }

    SUBCASE("12x10 (HI)") {
        uint8_t out[120];
        uint8_t back[120];
        interleave(in, out, 10);

        for (size_t j = 0; j < 10; j++)
            CHECK(out[j] == in[12 * j]);

        deinterleave(out, back, 10);
        for (size_t i = 0; i < 120; i++)
            CHECK(back[i] == in[i]);
    }
}

TEST_CASE("Scrambler is deterministic and restarts from all ones") {
    uint8_t a[120];
    uint8_t b[120];

    Scrambler s1;
    for (size_t i = 0; i < 120; i++)
        a[i] = s1.next();

    Scrambler s2;
    for (size_t i = 0; i < 120; i++)
        b[i] = s2.next();

    for (size_t i = 0; i < 120; i++)
        CHECK(a[i] == b[i]);

    /* Preset all ones: the first nine outputs are one (7.4). */
    Scrambler s3;
    for (size_t i = 0; i < 9; i++)
        CHECK(s3.next() == 1);

    /* Scramble is an involution. */
    uint8_t bits[120];
    fill_pattern(bits, 120, 0xCAFE);
    uint8_t orig[120];
    for (size_t i = 0; i < 120; i++)
        orig[i] = bits[i];
    scramble(bits, 120);
    scramble(bits, 120);
    for (size_t i = 0; i < 120; i++)
        CHECK(bits[i] == orig[i]);
}

TEST_CASE("CCH encodes and decodes back to the same fields") {
    CchFields f;
    f.frame_number = 2;
    f.id = 0x123;
    f.comm_mode = 1;
    f.version = 0;
    f.comm_format = 1;
    f.reserved = 0;
    f.sld = 0x2ABCD;

    uint8_t bits[cch_bits];
    pack_cch(f, bits);

    uint8_t fec[cch_fec_bits];
    encode_cch(bits, fec);

    uint8_t decoded[cch_bits];
    const DecodeResult res = decode_cch(fec, decoded);
    CHECK(res.crc_ok);
    CHECK(res.corrected == 0);
    CHECK(res.failed_blocks == 0);

    const CchFields g = unpack_cch(decoded);
    CHECK(g.frame_number == f.frame_number);
    CHECK(g.id == f.id);
    CHECK(g.comm_mode == f.comm_mode);
    CHECK(g.version == f.version);
    CHECK(g.comm_format == f.comm_format);
    CHECK(g.reserved == f.reserved);
    CHECK(g.sld == f.sld);

    /* One flipped bit anywhere still recovers with a single correction. */
    for (size_t pos = 0; pos < cch_fec_bits; pos += 17) {
        uint8_t corrupted[cch_fec_bits];
        for (size_t i = 0; i < cch_fec_bits; i++)
            corrupted[i] = fec[i];
        corrupted[pos] ^= 1;
        const DecodeResult r2 = decode_cch(corrupted, decoded);
        CHECK(r2.crc_ok);
        CHECK(r2.corrected == 1);
    }
}

TEST_CASE("Header HI encodes and decodes and survives one flipped bit") {
    HiFields f;
    f.header_type = 0;
    f.called_id = isf_address(0xCD);
    f.own_id = 0x1B91FD;
    f.comm_mode = 0;
    f.version = 0;
    f.comm_format = 1;
    f.reserved = 0;
    f.ci_type = 1;
    f.ci_info = 0;

    uint8_t bits[hi_bits];
    pack_hi(f, bits);

    uint8_t fec[hi_fec_bits];
    encode_hi(bits, fec);

    uint8_t decoded[hi_bits];
    const DecodeResult res = decode_hi(fec, decoded);
    CHECK(res.crc_ok);
    CHECK(res.corrected == 0);

    const HiFields g = unpack_hi(decoded);
    CHECK(g.header_type == f.header_type);
    CHECK(g.called_id == f.called_id);
    CHECK(g.own_id == f.own_id);
    CHECK(g.comm_mode == f.comm_mode);
    CHECK(g.version == f.version);
    CHECK(g.comm_format == f.comm_format);
    CHECK(g.ci_type == f.ci_type);
    CHECK(g.ci_info == f.ci_info);

    /* Single flipped bit: corrected == 1, CRC still verifies. */
    uint8_t corrupted[hi_fec_bits];
    for (size_t i = 0; i < hi_fec_bits; i++)
        corrupted[i] = fec[i];
    corrupted[63] ^= 1;
    const DecodeResult r2 = decode_hi(corrupted, decoded);
    CHECK(r2.crc_ok);
    CHECK(r2.corrected == 1);
    CHECK(unpack_hi(decoded).called_id == f.called_id);
}

TEST_CASE("End frame round trip over both repeated copies") {
    EndFields f;
    f.end_type = 1;
    f.arq = 1;
    f.tx_wait = 4;
    f.status = 17;
    f.reserved = 0;

    uint8_t bits[end_bits];
    pack_end(f, bits);

    uint8_t fec[end_fec_bits];
    encode_end(bits, fec);

    uint8_t decoded[end_bits];
    const DecodeResult res = decode_end(fec, decoded);
    CHECK(res.crc_ok);
    CHECK(res.corrected == 0);

    const EndFields g = unpack_end(decoded);
    CHECK(g.end_type == f.end_type);
    CHECK(g.arq == f.arq);
    CHECK(g.tx_wait == f.tx_wait);
    CHECK(g.status == f.status);

    /* Corrupt the first copy: the second (freshly scrambled) copy recovers. */
    uint8_t corrupted[end_fec_bits];
    for (size_t i = 0; i < end_fec_bits; i++)
        corrupted[i] = fec[i];
    corrupted[5] ^= 1;
    const DecodeResult r2 = decode_end(corrupted, decoded);
    CHECK(r2.crc_ok);
    CHECK(unpack_end(decoded).status == f.status);
}

TEST_CASE("PMR446 channel table maps channels per table 6.1/6.2") {
    CHECK(channel_count == 32);
    CHECK(channel_frequency_hz(1) == 446003125);
    CHECK(channel_frequency_hz(2) == 446009375);
    CHECK(channel_frequency_hz(16) == 446096875);
    CHECK(channel_frequency_hz(17) == 446103125);
    CHECK(channel_frequency_hz(32) == 446196875);

    CHECK(channel_code_24(1) == 0x577577);
    CHECK(channel_code_24(16) == 0x775DD7);
    CHECK(channel_code_24(17) == 0x577577); /* table 6.2 repeats table 6.1 */
    CHECK(channel_code_24(32) == 0x775DD7);

    /* Di-bit round trip of the codes (7.1). */
    for (size_t ch = 1; ch <= 16; ch++) {
        uint8_t bits24[24];
        const uint32_t code24 = channel_code_24(ch);
        u32_to_bits(code24, bits24, 24);
        const uint16_t code12 = channel_code_decode(bits24);
        channel_code_encode(code12, bits24);
        CHECK(bits_to_u32(bits24, 24) == code24);
    }
}

TEST_CASE("Addressing: ISF common ID and B2 mapping") {
    CHECK(isf_address(1) == 0x01FFFF);
    CHECK(isf_address(254) == 0xFEFFFF);
    CHECK(isf_address(255) == 0xFFFFFF);
    CHECK(isf_common_id(0xCDFFFF) == 0xCD);
    CHECK(isf_common_id(0x1B91FD) == -1);

    /* Examples from table A.2. */
    uint8_t digits[7] = {1, 2, 3, 4, 5, 6, 7};
    CHECK(b2_encode(digits) == 0x1B91FD);
    uint8_t digits2[7] = {4, 6, 8, 9, 5, 6, 10}; /* 468956* */
    CHECK(b2_encode(digits2) == 0x68BF08);

    uint8_t out[7];
    CHECK(b2_decode(0x1B91FD, out));
    for (size_t i = 0; i < 7; i++)
        CHECK(out[i] == digits[i]);
    CHECK(b2_decode(0x68BF08, out));
    for (size_t i = 0; i < 7; i++)
        CHECK(out[i] == digits2[i]);

    /* All-call broadcast address 999**** (A.2.3.3) decodes with stars. */
    uint8_t allcall[7] = {9, 9, 9, 10, 10, 10, 10};
    const uint32_t addr = b2_encode(allcall);
    CHECK(b2_decode(addr, out));
    for (size_t i = 0; i < 7; i++)
        CHECK(out[i] == allcall[i]);

    CHECK_FALSE(b2_decode(14641000, out));
}
