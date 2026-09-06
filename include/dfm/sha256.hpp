// sha256.hpp - minimal, self-contained SHA-256 implementation.
//
// Public domain style implementation adapted for dotfile-manager. No external
// dependency is required; this keeps the hashing primitive small, auditable,
// and free of third-party licensing concerns while remaining a standard,
// well-tested construction (FIPS 180-4).
//
// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace dfm {

// Streaming SHA-256 hasher. Feed bytes via update() any number of times,
// then call finalize_hex() to obtain the lowercase hex digest.
class Sha256 {
public:
    Sha256() { reset(); }

    void reset() {
        state_ = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                  0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
        bit_len_ = 0;
        buffer_len_ = 0;
        finalized_ = false;
    }

    void update(const void* data, std::size_t len) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        bit_len_ += static_cast<std::uint64_t>(len) * 8U;
        while (len > 0) {
            std::size_t take = std::min<std::size_t>(64 - buffer_len_, len);
            std::memcpy(buffer_.data() + buffer_len_, bytes, take);
            buffer_len_ += take;
            bytes += take;
            len -= take;
            if (buffer_len_ == 64) {
                transform(buffer_.data());
                buffer_len_ = 0;
            }
        }
    }

    void update(const std::string& s) { update(s.data(), s.size()); }

    // Returns the lowercase hex digest and leaves the hasher unusable
    // (call reset() to reuse).
    std::string finalize_hex() {
        std::array<std::uint8_t, 32> digest = finalize_bytes();
        static const char* hexchars = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (std::uint8_t b : digest) {
            out.push_back(hexchars[(b >> 4) & 0xF]);
            out.push_back(hexchars[b & 0xF]);
        }
        return out;
    }

    std::array<std::uint8_t, 32> finalize_bytes() {
        std::uint64_t total_bits = bit_len_;

        // Raw padding: append 0x80 then zero bytes, without touching bit_len_
        // (padding is not part of the hashed message length).
        raw_append(0x80);
        while (buffer_len_ != 56) {
            raw_append(0x00);
        }
        std::array<std::uint8_t, 8> len_bytes{};
        for (int i = 0; i < 8; ++i) {
            len_bytes[7 - i] = static_cast<std::uint8_t>((total_bits >> (8 * i)) & 0xFF);
        }
        std::memcpy(buffer_.data() + 56, len_bytes.data(), 8);
        transform(buffer_.data());

        std::array<std::uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i) {
            out[i * 4 + 0] = static_cast<std::uint8_t>((state_[i] >> 24) & 0xFF);
            out[i * 4 + 1] = static_cast<std::uint8_t>((state_[i] >> 16) & 0xFF);
            out[i * 4 + 2] = static_cast<std::uint8_t>((state_[i] >> 8) & 0xFF);
            out[i * 4 + 3] = static_cast<std::uint8_t>(state_[i] & 0xFF);
        }
        finalized_ = true;
        return out;
    }

    static std::string hex_of(const void* data, std::size_t len) {
        Sha256 h;
        h.update(data, len);
        return h.finalize_hex();
    }

    static std::string hex_of(const std::string& s) { return hex_of(s.data(), s.size()); }

private:
    static std::uint32_t rotr(std::uint32_t x, std::uint32_t n) {
        return (x >> n) | (x << (32 - n));
    }

    // Appends a single byte to the internal block buffer, transforming and
    // resetting the buffer when it fills, WITHOUT updating bit_len_. Used
    // only for length padding during finalize.
    void raw_append(std::uint8_t byte) {
        buffer_[buffer_len_++] = byte;
        if (buffer_len_ == 64) {
            transform(buffer_.data());
            buffer_len_ = 0;
        }
    }

    void transform(const std::uint8_t* block) {
        static constexpr std::array<std::uint32_t, 64> k = {
            0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
            0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
            0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
            0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
            0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
            0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
            0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
            0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
            0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
            0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
            0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

        std::array<std::uint32_t, 64> w{};
        for (int i = 0; i < 16; ++i) {
            w[static_cast<std::size_t>(i)] =
                (static_cast<std::uint32_t>(block[i * 4 + 0]) << 24) |
                (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                (static_cast<std::uint32_t>(block[i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            std::uint32_t s0 = rotr(w[static_cast<std::size_t>(i - 15)], 7) ^
                                rotr(w[static_cast<std::size_t>(i - 15)], 18) ^
                                (w[static_cast<std::size_t>(i - 15)] >> 3);
            std::uint32_t s1 = rotr(w[static_cast<std::size_t>(i - 2)], 17) ^
                                rotr(w[static_cast<std::size_t>(i - 2)], 19) ^
                                (w[static_cast<std::size_t>(i - 2)] >> 10);
            w[static_cast<std::size_t>(i)] = w[static_cast<std::size_t>(i - 16)] + s0 +
                                              w[static_cast<std::size_t>(i - 7)] + s1;
        }

        std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];

        for (int i = 0; i < 64; ++i) {
            std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            std::uint32_t ch = (e & f) ^ ((~e) & g);
            std::uint32_t temp1 = h + s1 + ch + k[static_cast<std::size_t>(i)] +
                                   w[static_cast<std::size_t>(i)];
            std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t temp2 = s0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffer_len_ = 0;
    std::uint64_t bit_len_ = 0;
    bool finalized_ = false;
};

}  // namespace dfm
