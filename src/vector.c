/*
 * vector.c - SIMD-friendly vector math for Racecar
 *
 * All hot loops are unrolled by 4 or 8 and written for
 * auto-vectorization with -O3 -march=native -ffast-math.
 */
#include "racecar.h"
#include <math.h>
#include <string.h>

/* ----------------------------------------------------------------
 * rc_vec_dot  --  dot product, 4x unrolled
 * ---------------------------------------------------------------- */
float
rc_vec_dot(const float *__restrict__ a,
           const float *__restrict__ b,
           uint32_t dim)
{
    float sum0 = 0.0f, sum1 = 0.0f, sum2 = 0.0f, sum3 = 0.0f;
    uint32_t i = 0;
    uint32_t end4 = dim & ~3u;           /* round down to multiple of 4 */

    for (; i < end4; i += 4) {
        sum0 += a[i]     * b[i];
        sum1 += a[i + 1] * b[i + 1];
        sum2 += a[i + 2] * b[i + 2];
        sum3 += a[i + 3] * b[i + 3];
    }
    for (; i < dim; i++) {
        sum0 += a[i] * b[i];
    }
    return sum0 + sum1 + sum2 + sum3;
}

/* ----------------------------------------------------------------
 * rc_vec_norm  --  L2 norm = sqrt(dot(v, v))
 * ---------------------------------------------------------------- */
float
rc_vec_norm(const float *__restrict__ v, uint32_t dim)
{
    return sqrtf(rc_vec_dot(v, v, dim));
}

/* ----------------------------------------------------------------
 * rc_vec_normalize  --  in-place L2 normalization
 * ---------------------------------------------------------------- */
void
rc_vec_normalize(float *__restrict__ v, uint32_t dim)
{
    float n = rc_vec_norm(v, dim);
    if (n < 1e-30f) return;              /* guard against zero vector */
    float inv = 1.0f / n;
    uint32_t i = 0;
    uint32_t end4 = dim & ~3u;

    for (; i < end4; i += 4) {
        v[i]     *= inv;
        v[i + 1] *= inv;
        v[i + 2] *= inv;
        v[i + 3] *= inv;
    }
    for (; i < dim; i++) {
        v[i] *= inv;
    }
}

/* ----------------------------------------------------------------
 * rc_vec_cosine_dist  --  1 - cos(a, b)
 * ---------------------------------------------------------------- */
float
rc_vec_cosine_dist(const float *__restrict__ a,
                   const float *__restrict__ b,
                   uint32_t dim)
{
    /* Single-pass: accumulate dot, norm_a^2, norm_b^2 together.
     * This keeps all three reductions in the same loop for better
     * cache / prefetch behaviour.                                   */
    float dot  = 0.0f, na = 0.0f, nb = 0.0f;
    float dot1 = 0.0f, na1 = 0.0f, nb1 = 0.0f;
    float dot2 = 0.0f, na2 = 0.0f, nb2 = 0.0f;
    float dot3 = 0.0f, na3 = 0.0f, nb3 = 0.0f;
    uint32_t i = 0;
    uint32_t end4 = dim & ~3u;

    for (; i < end4; i += 4) {
        float a0 = a[i],     b0 = b[i];
        float a1 = a[i + 1], b1 = b[i + 1];
        float a2 = a[i + 2], b2 = b[i + 2];
        float a3 = a[i + 3], b3 = b[i + 3];
        dot  += a0 * b0;  na  += a0 * a0;  nb  += b0 * b0;
        dot1 += a1 * b1;  na1 += a1 * a1;  nb1 += b1 * b1;
        dot2 += a2 * b2;  na2 += a2 * a2;  nb2 += b2 * b2;
        dot3 += a3 * b3;  na3 += a3 * a3;  nb3 += b3 * b3;
    }
    for (; i < dim; i++) {
        dot += a[i] * b[i];
        na  += a[i] * a[i];
        nb  += b[i] * b[i];
    }

    dot += dot1 + dot2 + dot3;
    na  += na1  + na2  + na3;
    nb  += nb1  + nb2  + nb3;

    float denom = sqrtf(na) * sqrtf(nb);
    if (denom < 1e-30f) return 1.0f;     /* treat zero vectors as maximally dissimilar */
    return 1.0f - dot / denom;
}

/* ----------------------------------------------------------------
 * rc_vec_euclidean_dist  --  L2 distance, 4x unrolled
 * ---------------------------------------------------------------- */
float
rc_vec_euclidean_dist(const float *__restrict__ a,
                      const float *__restrict__ b,
                      uint32_t dim)
{
    float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
    uint32_t i = 0;
    uint32_t end4 = dim & ~3u;

    for (; i < end4; i += 4) {
        float d0 = a[i]     - b[i];
        float d1 = a[i + 1] - b[i + 1];
        float d2 = a[i + 2] - b[i + 2];
        float d3 = a[i + 3] - b[i + 3];
        s0 += d0 * d0;
        s1 += d1 * d1;
        s2 += d2 * d2;
        s3 += d3 * d3;
    }
    for (; i < dim; i++) {
        float d = a[i] - b[i];
        s0 += d * d;
    }
    return sqrtf(s0 + s1 + s2 + s3);
}

/* ----------------------------------------------------------------
 * rc_vec_distance  --  metric dispatch
 * ---------------------------------------------------------------- */
float
rc_vec_distance(const float *__restrict__ a,
                const float *__restrict__ b,
                uint32_t dim,
                rc_metric_t metric)
{
    switch (metric) {
    case RC_METRIC_COSINE:    return rc_vec_cosine_dist(a, b, dim);
    case RC_METRIC_EUCLIDEAN: return rc_vec_euclidean_dist(a, b, dim);
    case RC_METRIC_DOT:       return -rc_vec_dot(a, b, dim);
    default:                  return rc_vec_euclidean_dist(a, b, dim);
    }
}
