/*
 * GOST R 34.11-2012 core and API functions.
 */

#include "streebog-config.h"
#include <cstring>

#if defined _MSC_VER
#define ALIGN(x) __declspec(align(x))
#else
#define ALIGN(x) __attribute__ ((__aligned__(x)))
#endif

#include "streebog-ref.h"

//ALIGN(16) union uint512_u {
//    unsigned long long QWORD[8];
//} uint512_u;

extern union uint512_u {
    unsigned long long QWORD[8];
} uint512_u;

#include "streebog-consts.h"
#include "streebog-precalc.h"

ALIGN(16) typedef struct GOST34112012Context {
    ALIGN(16) unsigned char buffer[64];
    ALIGN(16) union uint512_u hash;
    ALIGN(16) union uint512_u h;
    ALIGN(16) union uint512_u N;
    ALIGN(16) union uint512_u Sigma;
    size_t bufsize;
    unsigned int digest_size;
} StreebogContext;

#define BSWAP64(x) \
    (((x & 0xFF00000000000000ULL) >> 56) | \
     ((x & 0x00FF000000000000ULL) >> 40) | \
     ((x & 0x0000FF0000000000ULL) >> 24) | \
     ((x & 0x000000FF00000000ULL) >>  8) | \
     ((x & 0x00000000FF000000ULL) <<  8) | \
     ((x & 0x0000000000FF0000ULL) << 24) | \
     ((x & 0x000000000000FF00ULL) << 40) | \
     ((x & 0x00000000000000FFULL) << 56))




static inline void pad(StreebogContext *CTX) {
    if (CTX->bufsize > 63) {
        return;
    }

    memset(CTX->buffer + CTX->bufsize,0x00, sizeof(CTX->buffer) - CTX->bufsize);

    CTX->buffer[CTX->bufsize] = 0x01;
}

static inline void add512(const union uint512_u *x, const union uint512_u *y, union uint512_u *r) {
#ifndef __GOST3411_BIG_ENDIAN__
    unsigned int CF;
    unsigned int i;

    CF = 0;
    for (i = 0; i < 8; i++) {
        const unsigned long long left = x->QWORD[i];
        unsigned long long sum;

        sum = left + y->QWORD[i] + CF;
        if (sum != left)
            CF = (sum < left);
        r->QWORD[i] = sum;
    }
#else
    const unsigned char *xp, *yp;
    unsigned char *rp;
    unsigned int i;
    int buf;

    xp = (const unsigned char *) &x[0];
    yp = (const unsigned char *) &y[0];
    rp = (unsigned char *) &r[0];

    buf = 0;
    for (i = 0; i < 64; i++)
    {
        buf = xp[i] + yp[i] + (buf >> 8);
        rp[i] = (unsigned char) buf & 0xFF;
    }
#endif
}

static void g(union uint512_u *h, const union uint512_u *N, const unsigned char *m) {
#ifdef __GOST3411_HAS_SSE2__
    __m128i xmm0, xmm2, xmm4, xmm6; /* XMMR0-quadruple */
    __m128i xmm1, xmm3, xmm5, xmm7; /* XMMR1-quadruple */
    unsigned int i;

    LOAD(N, xmm0, xmm2, xmm4, xmm6);
    XLPS128M(h, xmm0, xmm2, xmm4, xmm6);

    LOAD(m, xmm1, xmm3, xmm5, xmm7);
    XLPS128R(xmm0, xmm2, xmm4, xmm6, xmm1, xmm3, xmm5, xmm7);

    for (i = 0; i < 11; i++)
        ROUND128(i, xmm0, xmm2, xmm4, xmm6, xmm1, xmm3, xmm5, xmm7);

    XLPS128M((&C[11]), xmm0, xmm2, xmm4, xmm6);
    X128R(xmm0, xmm2, xmm4, xmm6, xmm1, xmm3, xmm5, xmm7);

    X128M(h, xmm0, xmm2, xmm4, xmm6);
    X128M(m, xmm0, xmm2, xmm4, xmm6);

    UNLOAD(h, xmm0, xmm2, xmm4, xmm6);

    /* Restore the Floating-point status on the CPU */
    _mm_empty();
#else
    union uint512_u Ki, data;
    unsigned int i;

    XLPS(h, N, (&data));

    /* Starting E() */
    Ki = data;
    XLPS((&Ki), ((const union uint512_u *) &m[0]), (&data));

    for (i = 0; i < 11; i++) ROUND(i, (&Ki), (&data));

    XLPS((&Ki), (&C[11]), (&Ki));
    X((&Ki), (&data), (&data));
    /* E() done */

    X((&data), h, (&data));
    X((&data), ((const union uint512_u *) &m[0]), h);
#endif
}

static inline void stage2(StreebogContext *CTX, const unsigned char *data) {
    union uint512_u m;

    memcpy(&m, data, sizeof(m));
    g(&(CTX->h), &(CTX->N), (const unsigned char *) &m);

    add512(&(CTX->N), &buffer512, &(CTX->N));
    add512(&(CTX->Sigma), &m, &(CTX->Sigma));
}

static inline void stage3(StreebogContext *CTX) {
    ALIGN(16) union uint512_u buf = {{0}};

#ifndef __GOST3411_BIG_ENDIAN__
    buf.QWORD[0] = CTX->bufsize << 3;
#else
    buf.QWORD[0] = BSWAP64(CTX->bufsize << 3);
#endif

    pad(CTX);

    g(&(CTX->h), &(CTX->N), (const unsigned char *) &(CTX->buffer));

    add512(&(CTX->N), &buf, &(CTX->N));
    add512(&(CTX->Sigma), (const union uint512_u *) &CTX->buffer[0],
           &(CTX->Sigma));

    g(&(CTX->h), &buffer0, (const unsigned char *) &(CTX->N));

    g(&(CTX->h), &buffer0, (const unsigned char *) &(CTX->Sigma));
    memcpy(&(CTX->hash), &(CTX->h), sizeof uint512_u);
}

