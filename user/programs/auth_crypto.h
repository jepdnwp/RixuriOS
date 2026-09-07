#ifndef RIXURI_AUTH_CRYPTO_H
#define RIXURI_AUTH_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t bit_length;
    uint32_t data_length;
    uint8_t data[64];
} rix_sha256_ctx_t;

static const uint32_t rix_sha256_constants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t rix_rotr(uint32_t value, unsigned shift) {
    return (value >> shift) | (value << (32u - shift));
}

static void rix_sha256_transform(rix_sha256_ctx_t *context) {
    uint32_t schedule[64];
    uint32_t a, b, c, d, e, f, g, h;
    for (unsigned i = 0; i < 16u; ++i) {
        unsigned offset = i * 4u;
        schedule[i] = ((uint32_t)context->data[offset] << 24) |
                      ((uint32_t)context->data[offset + 1u] << 16) |
                      ((uint32_t)context->data[offset + 2u] << 8) |
                      (uint32_t)context->data[offset + 3u];
    }
    for (unsigned i = 16u; i < 64u; ++i) {
        uint32_t s0 = rix_rotr(schedule[i - 15u], 7u) ^
                      rix_rotr(schedule[i - 15u], 18u) ^ (schedule[i - 15u] >> 3u);
        uint32_t s1 = rix_rotr(schedule[i - 2u], 17u) ^
                      rix_rotr(schedule[i - 2u], 19u) ^ (schedule[i - 2u] >> 10u);
        schedule[i] = schedule[i - 16u] + s0 + schedule[i - 7u] + s1;
    }
    a = context->state[0]; b = context->state[1]; c = context->state[2]; d = context->state[3];
    e = context->state[4]; f = context->state[5]; g = context->state[6]; h = context->state[7];
    for (unsigned i = 0; i < 64u; ++i) {
        uint32_t s1 = rix_rotr(e, 6u) ^ rix_rotr(e, 11u) ^ rix_rotr(e, 25u);
        uint32_t choose = (e & f) ^ ((~e) & g);
        uint32_t temporary1 = h + s1 + choose + rix_sha256_constants[i] + schedule[i];
        uint32_t s0 = rix_rotr(a, 2u) ^ rix_rotr(a, 13u) ^ rix_rotr(a, 22u);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temporary2 = s0 + majority;
        h = g; g = f; f = e; e = d + temporary1;
        d = c; c = b; b = a; a = temporary1 + temporary2;
    }
    context->state[0] += a; context->state[1] += b; context->state[2] += c; context->state[3] += d;
    context->state[4] += e; context->state[5] += f; context->state[6] += g; context->state[7] += h;
}

static void rix_sha256_init(rix_sha256_ctx_t *context) {
    context->data_length = 0;
    context->bit_length = 0;
    context->state[0] = 0x6a09e667u; context->state[1] = 0xbb67ae85u;
    context->state[2] = 0x3c6ef372u; context->state[3] = 0xa54ff53au;
    context->state[4] = 0x510e527fu; context->state[5] = 0x9b05688cu;
    context->state[6] = 0x1f83d9abu; context->state[7] = 0x5be0cd19u;
}

static void rix_sha256_update(rix_sha256_ctx_t *context, const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        context->data[context->data_length++] = data[i];
        if (context->data_length == 64u) {
            rix_sha256_transform(context);
            context->bit_length += 512u;
            context->data_length = 0;
        }
    }
}

static void rix_sha256_final(rix_sha256_ctx_t *context, uint8_t digest[32]) {
    uint32_t index = context->data_length;
    context->data[index++] = 0x80u;
    if (index > 56u) {
        while (index < 64u) context->data[index++] = 0;
        rix_sha256_transform(context);
        index = 0;
    }
    while (index < 56u) context->data[index++] = 0;
    context->bit_length += (uint64_t)context->data_length * 8u;
    for (unsigned i = 0; i < 8u; ++i)
        context->data[63u - i] = (uint8_t)(context->bit_length >> (i * 8u));
    rix_sha256_transform(context);
    for (unsigned i = 0; i < 8u; ++i) {
        digest[i * 4u] = (uint8_t)(context->state[i] >> 24);
        digest[i * 4u + 1u] = (uint8_t)(context->state[i] >> 16);
        digest[i * 4u + 2u] = (uint8_t)(context->state[i] >> 8);
        digest[i * 4u + 3u] = (uint8_t)context->state[i];
    }
}

static void rix_password_digest(const char *salt, const char *password,
                                uint32_t rounds, uint8_t digest[32]) {
    rix_sha256_ctx_t context;
    rix_sha256_init(&context);
    rix_sha256_update(&context, (const uint8_t *)salt, 32u);
    size_t password_length = 0;
    while (password[password_length]) ++password_length;
    rix_sha256_update(&context, (const uint8_t *)password, password_length);
    rix_sha256_final(&context, digest);
    for (uint32_t round = 1u; round < rounds; ++round) {
        rix_sha256_init(&context);
        rix_sha256_update(&context, digest, 32u);
        rix_sha256_update(&context, (const uint8_t *)password, password_length);
        rix_sha256_final(&context, digest);
    }
}

static __attribute__((unused)) int rix_constant_digest_equal(const uint8_t left[32], const uint8_t right[32]) {
    uint8_t difference = 0;
    for (unsigned i = 0; i < 32u; ++i) difference |= left[i] ^ right[i];
    return difference == 0;
}

#endif
