#include <stdio.h>
#include <ttak/security/sha256.h>
#include <string.h>

#include <stdint.h>

// Helper function for byte swapping
static inline uint32_t bswap_32(uint32_t val) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap32(val);
#else
    return (((val & 0x000000FF) << 24) |
            ((val & 0x0000FF00) << 8)  |
            ((val & 0x00FF0000) >> 8)  |
            ((val & 0xFF000000) >> 24));
#endif
}

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    #define BYTESWAP32(x) (x)
#else
    #define BYTESWAP32(x) bswap_32(x)
#endif




#define ROTLEFT(a, b) (((a) << (b)) | ((a) >> (32 - (b))))
#define ROTRIGHT(a, b) (((a) >> (b)) | ((a) << (32 - (b))))

#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x, 2) ^ ROTRIGHT(x, 13) ^ ROTRIGHT(x, 22))
#define EP1(x) (ROTRIGHT(x, 6) ^ ROTRIGHT(x, 11) ^ ROTRIGHT(x, 25))
#define SIG0(x) (ROTRIGHT(x, 7) ^ ROTRIGHT(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x, 17) ^ ROTRIGHT(x, 19) ^ ((x) >> 10))

static const uint32_t k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static void sha256_transform(SHA256_CTX *ctx, const uint8_t data[]) {
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t w[64];
    uint32_t i;

    // Load initial hash values (already big-endian from ctx->state)
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for(int i = 0; i < 16; i++) {
        uint32_t temp;
        memcpy(&temp, &data[i * 4], 4);
        w[i] = BYTESWAP32(temp); // Ensure message words are big-endian
    }

    for (i = 16; i < 64; ++i) {
        w[i] = SIG1(w[i - 2]) + w[i - 7] + SIG0(w[i - 15]) + w[i - 16];
    }

    for (i = 0; i < 64; ++i) {
        uint32_t t1 = h + EP1(e) + CH(e, f, g) + k[i] + w[i];
        uint32_t t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    // Update ctx->state, ensuring values are stored in big-endian format
    ctx->state[0] = BYTESWAP32(BYTESWAP32(ctx->state[0]) + a);
    ctx->state[1] = BYTESWAP32(BYTESWAP32(ctx->state[1]) + b);
    ctx->state[2] = BYTESWAP32(BYTESWAP32(ctx->state[2]) + c);
    ctx->state[3] = BYTESWAP32(BYTESWAP32(ctx->state[3]) + d);
    ctx->state[4] = BYTESWAP32(BYTESWAP32(ctx->state[4]) + e);
    ctx->state[5] = BYTESWAP32(BYTESWAP32(ctx->state[5]) + f);
    ctx->state[6] = BYTESWAP32(BYTESWAP32(ctx->state[6]) + g);
    ctx->state[7] = BYTESWAP32(BYTESWAP32(ctx->state[7]) + h);
}

void sha256_init(SHA256_CTX *ctx) {
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = BYTESWAP32(0x6a09e667);
    ctx->state[1] = BYTESWAP32(0xbb67ae85);
    ctx->state[2] = BYTESWAP32(0x3c6ef372);
    ctx->state[3] = BYTESWAP32(0xa54ff53a);
    ctx->state[4] = BYTESWAP32(0x510e527f);
    ctx->state[5] = BYTESWAP32(0x9b05688c);
    ctx->state[6] = BYTESWAP32(0x1f83d9ab);
    ctx->state[7] = BYTESWAP32(0x5be0cd19);
}

void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len) {
    uint32_t i;

    for (i = 0; i < len; ++i) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        ctx->bitlen += 8;

        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->datalen = 0;
        }
    }
}

void sha256_final(SHA256_CTX *ctx, uint8_t hash[]) {
    uint32_t i;

    i = ctx->datalen;

    ctx->data[i++] = 0x80;

    if (i > 56) {
        while(i < 64) ctx->data[i++] = 0x00;
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
        i = 0;
    }

    while(i < 56) ctx->data[i++] = 0x00;

    for(int j = 0; j < 8; j++) {
        ctx->data[56 + j] = (uint8_t)(ctx->bitlen >> (56 - j * 8));
    }

    sha256_transform(ctx, ctx->data);

    for (i = 0; i < 8; ++i) {
        hash[i * 4] = (ctx->state[i] >> 24) & 0xFF;
        hash[i * 4 + 1] = (ctx->state[i] >> 16) & 0xFF;
        hash[i * 4 + 2] = (ctx->state[i] >> 8) & 0xFF;
        hash[i * 4 + 3] = (ctx->state[i]) & 0xFF;
    }
}
