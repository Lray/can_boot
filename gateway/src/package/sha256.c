#include "sha256.h"

#include <string.h>

#include "byte_order.h"

#define ROTR32(x, n) (((x) >> (n)) | ((x) << (32u - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define BSIG0(x) (ROTR32((x), 2u) ^ ROTR32((x), 13u) ^ ROTR32((x), 22u))
#define BSIG1(x) (ROTR32((x), 6u) ^ ROTR32((x), 11u) ^ ROTR32((x), 25u))
#define SSIG0(x) (ROTR32((x), 7u) ^ ROTR32((x), 18u) ^ ((x) >> 3u))
#define SSIG1(x) (ROTR32((x), 17u) ^ ROTR32((x), 19u) ^ ((x) >> 10u))

static const uint32_t k_table[64] = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
    0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
    0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
    0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
    0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u,
};

static void transform(Sha256 *ctx, const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    int i;

    for (i = 0; i < 16; ++i) {
        w[i] = byte_order_get_u32_be(block + (size_t)i * 4u);
    }
    for (i = 16; i < 64; ++i) {
        w[i] = SSIG1(w[i - 2]) + w[i - 7] + SSIG0(w[i - 15]) + w[i - 16];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; ++i) {
        uint32_t t1 = h + BSIG1(e) + CH(e, f, g) + k_table[i] + w[i];
        uint32_t t2 = BSIG0(a) + MAJ(a, b, c);

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void sha256_init(Sha256 *ctx)
{
    ctx->state[0] = 0x6A09E667u;
    ctx->state[1] = 0xBB67AE85u;
    ctx->state[2] = 0x3C6EF372u;
    ctx->state[3] = 0xA54FF53Au;
    ctx->state[4] = 0x510E527Fu;
    ctx->state[5] = 0x9B05688Cu;
    ctx->state[6] = 0x1F83D9ABu;
    ctx->state[7] = 0x5BE0CD19u;
    ctx->total_len = 0u;
    ctx->buffer_len = 0u;
}

void sha256_update(Sha256 *ctx, const uint8_t *data, size_t len)
{
    size_t offset = 0u;

    if (len == 0u) {
        return;
    }

    ctx->total_len += (uint64_t)len;

    if (ctx->buffer_len > 0u) {
        size_t need = 64u - ctx->buffer_len;
        size_t copy_len = len < need ? len : need;

        memcpy(ctx->buffer + ctx->buffer_len, data, copy_len);
        ctx->buffer_len += copy_len;
        offset += copy_len;
        if (ctx->buffer_len == 64u) {
            transform(ctx, ctx->buffer);
            ctx->buffer_len = 0u;
        }
    }

    while (offset + 64u <= len) {
        transform(ctx, data + offset);
        offset += 64u;
    }

    if (offset < len) {
        ctx->buffer_len = len - offset;
        memcpy(ctx->buffer, data + offset, ctx->buffer_len);
    }
}

void sha256_final(Sha256 *ctx, uint8_t digest[PACKAGE_SHA256_SIZE])
{
    uint64_t bit_len = ctx->total_len * 8u;
    uint8_t len_bytes[8];
    size_t i;

    for (i = 0; i < 8u; ++i) {
        len_bytes[7u - i] = (uint8_t)(bit_len >> (i * 8u));
    }

    ctx->buffer[ctx->buffer_len++] = 0x80u;
    if (ctx->buffer_len > 56u) {
        while (ctx->buffer_len < 64u) {
            ctx->buffer[ctx->buffer_len++] = 0u;
        }
        transform(ctx, ctx->buffer);
        ctx->buffer_len = 0u;
    }
    while (ctx->buffer_len < 56u) {
        ctx->buffer[ctx->buffer_len++] = 0u;
    }
    memcpy(ctx->buffer + 56u, len_bytes, sizeof(len_bytes));
    transform(ctx, ctx->buffer);

    for (i = 0; i < 8u; ++i) {
        byte_order_put_u32_be(digest + i * 4u, ctx->state[i]);
    }
}

void sha256_compute(const uint8_t *data, size_t len, uint8_t digest[PACKAGE_SHA256_SIZE])
{
    Sha256 ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}
