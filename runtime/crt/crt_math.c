/*
 * Clean-room C89 <math.h> library for ckcl 16-bit 8086 targets.
 * IEEE-754 binary64 little-endian implementation.
 */

#ifdef __CKCL__
typedef unsigned short math_u16;
typedef unsigned long  math_u32;
extern int errno;
extern int __ck_has_x87;
extern void __x87_sqrt(double *out, const double *x);
extern void __x87_sin(double *out, const double *x);
extern void __x87_cos(double *out, const double *x);
extern void __x87_atan(double *out, const double *x);
#else
#include <stdint.h>
#include <errno.h>
typedef uint16_t math_u16;
typedef uint32_t math_u32;
#endif

#ifndef EDOM
#define EDOM 33
#endif
#ifndef ERANGE
#define ERANGE 34
#endif

typedef struct {
    math_u32 lo;
    math_u32 hi;
} math_u64;

static math_u64 m_get_bits(double x)
{
    const math_u16 *w = (const math_u16 *)&x;
    math_u64 r;
    r.lo = (math_u32)w[0] | ((math_u32)w[1] << 16);
    r.hi = (math_u32)w[2] | ((math_u32)w[3] << 16);
    return r;
}

static double m_from_bits(math_u32 hi, math_u32 lo)
{
    double x;
    math_u16 *w = (math_u16 *)&x;
    w[0] = (math_u16)(lo & (math_u32)0xFFFFUL);
    w[1] = (math_u16)(lo >> 16);
    w[2] = (math_u16)(hi & (math_u32)0xFFFFUL);
    w[3] = (math_u16)(hi >> 16);
    return x;
}

static int m_isnan_bits(math_u64 b)
{
    math_u32 mhi = b.hi & (math_u32)0x7FFFFFFFUL;
    return (mhi > (math_u32)0x7FF00000UL) || (mhi == (math_u32)0x7FF00000UL && b.lo != 0);
}

static int m_isinf_bits(math_u64 b)
{
    return (b.hi & (math_u32)0x7FFFFFFFUL) == (math_u32)0x7FF00000UL && b.lo == 0;
}

static int m_iszero_bits(math_u64 b)
{
    return ((b.hi & (math_u32)0x7FFFFFFFUL) | b.lo) == 0;
}

static double m_inf(int sign)
{
    return m_from_bits((sign ? (math_u32)0x80000000UL : (math_u32)0) | (math_u32)0x7FF00000UL, 0);
}

static double m_qnan(void)
{
    return m_from_bits((math_u32)0x7FF80000UL, 0);
}

static double m_zero(int sign)
{
    return m_from_bits(sign ? (math_u32)0x80000000UL : (math_u32)0, 0);
}

double fabs(double x)
{
    math_u64 b = m_get_bits(x);
    return m_from_bits(b.hi & (math_u32)0x7FFFFFFFUL, b.lo);
}

double modf(double x, double *iptr)
{
    math_u64 b = m_get_bits(x);
    int sign = (b.hi >> 31) ? 1 : 0;
    int bexp = (int)((b.hi >> 20) & (math_u32)0x7FFUL);
    int exp = bexp - 1023;
    math_u32 ihi, ilo;
    double trunc_x;

    if (bexp == 0x7FF) {
        *iptr = x;
        if (m_isnan_bits(b)) return x;
        return m_zero(sign);
    }
    if (exp < 0) {
        *iptr = m_zero(sign);
        return x;
    }
    if (exp >= 52) {
        *iptr = x;
        return m_zero(sign);
    }
    if (exp < 20) {
        math_u32 mask = ((math_u32)1 << (20 - exp)) - (math_u32)1;
        ihi = b.hi & ~mask;
        ilo = 0;
    } else if (exp == 20) {
        ihi = b.hi;
        ilo = 0;
    } else {
        math_u32 mask = ((math_u32)1 << (52 - exp)) - (math_u32)1;
        ihi = b.hi;
        ilo = b.lo & ~mask;
    }
    trunc_x = m_from_bits(ihi, ilo);
    *iptr = trunc_x;
    if (ihi == b.hi && ilo == b.lo) return m_zero(sign);
    return x - trunc_x;
}

double floor(double x)
{
    math_u64 b = m_get_bits(x);
    double ip, fp;
    if (b.hi >= (math_u32)0x7FF00000UL && m_isnan_bits(b)) return x;
    if (m_isinf_bits(b) || m_iszero_bits(b)) return x;
    fp = modf(x, &ip);
    if ((b.hi >> 31) && fp != 0.0) return ip - 1.0;
    return ip;
}

double ceil(double x)
{
    math_u64 b = m_get_bits(x);
    double ip, fp;
    if (m_isnan_bits(b) || m_isinf_bits(b) || m_iszero_bits(b)) return x;
    fp = modf(x, &ip);
    if (!(b.hi >> 31) && fp != 0.0) return ip + 1.0;
    return ip;
}

double frexp(double x, int *eptr)
{
    math_u64 b = m_get_bits(x);
    int bexp = (int)((b.hi >> 20) & (math_u32)0x7FFUL);
    int bias_adj = 0;

    if (m_iszero_bits(b) || bexp == 0x7FF) {
        if (eptr) *eptr = 0;
        return x;
    }
    if (bexp == 0) {
        /* Multiply subnormal by 2^54 to normalize into normal exponent range */
        double two54 = m_from_bits((math_u32)0x43500000UL, 0);
        x = x * two54;
        b = m_get_bits(x);
        bexp = (int)((b.hi >> 20) & (math_u32)0x7FFUL);
        bias_adj = 54;
    }
    if (eptr) *eptr = (bexp - 1022) - bias_adj;
    b.hi = (b.hi & (math_u32)0x800FFFFFUL) | (math_u32)0x3FE00000UL;
    return m_from_bits(b.hi, b.lo);
}

double ldexp(double x, int exp)
{
    math_u64 b = m_get_bits(x);
    int sign;
    double scale;

    if (exp == 0 || m_iszero_bits(b) || m_isnan_bits(b) || m_isinf_bits(b)) {
        return x;
    }
    sign = (b.hi >> 31) ? 1 : 0;

    while (exp > 1023) {
        x = x * m_from_bits((math_u32)0x7FE00000UL, 0); /* 2^1023 */
        exp -= 1023;
        b = m_get_bits(x);
        if (m_isinf_bits(b)) {
            errno = ERANGE;
            return m_inf(sign);
        }
    }
    while (exp < -1022) {
        x = x * m_from_bits((math_u32)0x00100000UL, 0); /* 2^-1022 */
        exp += 1022;
        b = m_get_bits(x);
        if (m_iszero_bits(b)) {
            errno = ERANGE;
            return m_zero(sign);
        }
    }

    scale = m_from_bits((math_u32)(exp + 1023) << 20, 0);
    x = x * scale;
    b = m_get_bits(x);
    if (m_isinf_bits(b)) errno = ERANGE;
    else if (m_iszero_bits(b)) errno = ERANGE;
    return x;
}

double fmod(double x, double y)
{
    math_u64 bx = m_get_bits(x);
    math_u64 by = m_get_bits(y);
    int sign_x = (bx.hi >> 31) ? 1 : 0;
    int ex, ey, bex, bey;
    math_u64 fx, fy;

    if (m_isnan_bits(bx) || m_isnan_bits(by)) return m_qnan();
    if (m_isinf_bits(bx) || m_iszero_bits(by)) {
        errno = EDOM;
        return m_qnan();
    }
    if (m_iszero_bits(bx) || m_isinf_bits(by)) return x;

    bex = (int)((bx.hi >> 20) & (math_u32)0x7FFUL);
    bey = (int)((by.hi >> 20) & (math_u32)0x7FFUL);
    fx.lo = bx.lo; fx.hi = bx.hi & (math_u32)0x000FFFFFUL;
    fy.lo = by.lo; fy.hi = by.hi & (math_u32)0x000FFFFFUL;

    if (bex == 0) {
        ex = -1022;
        while ((fx.hi & (math_u32)0x00100000UL) == 0) {
            fx.hi = (fx.hi << 1) | (fx.lo >> 31);
            fx.lo <<= 1;
            --ex;
        }
    } else {
        ex = bex - 1023;
        fx.hi |= (math_u32)0x00100000UL;
    }

    if (bey == 0) {
        ey = -1022;
        while ((fy.hi & (math_u32)0x00100000UL) == 0) {
            fy.hi = (fy.hi << 1) | (fy.lo >> 31);
            fy.lo <<= 1;
            --ey;
        }
    } else {
        ey = bey - 1023;
        fy.hi |= (math_u32)0x00100000UL;
    }

    if (ex < ey) return x;
    if (ex == ey && (fx.hi < fy.hi || (fx.hi == fy.hi && fx.lo < fy.lo))) return x;

    while (ex > ey) {
        if (fx.hi > fy.hi || (fx.hi == fy.hi && fx.lo >= fy.lo)) {
            math_u32 borrow = (fx.lo < fy.lo) ? (math_u32)1 : (math_u32)0;
            fx.lo -= fy.lo;
            fx.hi = fx.hi - fy.hi - borrow;
            if ((fx.hi | fx.lo) == 0) return m_zero(sign_x);
        }
        fx.hi = (fx.hi << 1) | (fx.lo >> 31);
        fx.lo <<= 1;
        --ex;
    }
    if (fx.hi > fy.hi || (fx.hi == fy.hi && fx.lo >= fy.lo)) {
        math_u32 borrow = (fx.lo < fy.lo) ? (math_u32)1 : (math_u32)0;
        fx.lo -= fy.lo;
        fx.hi = fx.hi - fy.hi - borrow;
        if ((fx.hi | fx.lo) == 0) return m_zero(sign_x);
    }

    while ((fx.hi & (math_u32)0x00100000UL) == 0) {
        fx.hi = (fx.hi << 1) | (fx.lo >> 31);
        fx.lo <<= 1;
        --ex;
    }

    if (ex >= -1022) {
        fx.hi = (fx.hi & (math_u32)0x000FFFFFUL) |
                ((math_u32)(ex + 1023) << 20) |
                (sign_x ? (math_u32)0x80000000UL : (math_u32)0);
        return m_from_bits(fx.hi, fx.lo);
    } else {
        int shift = -1022 - ex;
        if (shift < 32) {
            fx.lo = (fx.lo >> shift) | (fx.hi << (32 - shift));
            fx.hi >>= shift;
        } else {
            fx.lo = fx.hi >> (shift - 32);
            fx.hi = 0;
        }
        fx.hi |= sign_x ? (math_u32)0x80000000UL : (math_u32)0;
        return m_from_bits(fx.hi, fx.lo);
    }
}

double sqrt(double x)
{
    math_u64 b = m_get_bits(x);
    int e;
    double m, y;

    if (m_isnan_bits(b)) return x;
    if (m_iszero_bits(b)) return x;
    if (b.hi >> 31) {
        errno = EDOM;
        return m_qnan();
    }
    if (m_isinf_bits(b)) return x;
#ifdef __CKCL__
    if (__ck_has_x87) {
        __x87_sqrt(&y, &x);
        return y;
    }
#endif

    m = frexp(x, &e);
    if (e & 1) {
        m *= 2.0;
        --e;
    }
    y = 0.41731 + 0.59016 * m;
    y = 0.5 * (y + m / y);
    y = 0.5 * (y + m / y);
    y = 0.5 * (y + m / y);
    y = 0.5 * (y + m / y);
    y = 0.5 * (y + m / y);
    return ldexp(y, e / 2);
}

#define M_INV_LN2 1.44269504088896340736
#define M_LN2_HI  6.93147180369123816490e-01
#define M_LN2_LO  1.90821492927058770002e-10

double exp(double x)
{
    math_u64 b = m_get_bits(x);
    int k;
    double hi, lo, r, t, c, y;

    if (m_isnan_bits(b)) return x;
    if (x > 709.78271289338397) {
        errno = ERANGE;
        return m_inf(0);
    }
    if (x < -745.13321910194122) {
        if (!m_isinf_bits(b)) errno = ERANGE;
        return m_zero(0);
    }
    if (fabs(x) < 3.725290298461914e-09) {
        return 1.0 + x;
    }

    k = (int)floor(x * M_INV_LN2 + 0.5);
    hi = x - (double)k * M_LN2_HI;
    lo = (double)k * M_LN2_LO;
    r = hi - lo;

    t = r * r;
    c = r - t * (1.66666666666666019037e-01 +
            t * (-2.77777777770155933842e-03 +
            t * (6.61375632143793436117e-05 +
            t * (-1.65339022054652515390e-06 +
            t * 4.13813679705723846039e-08))));
    y = 1.0 - ((lo - (r * c) / (2.0 - c)) - hi);
    return ldexp(y, k);
}

static void log_core(double x, double *out_hi, double *out_lo)
{
    int k;
    double m = frexp(x, &k);
    double f, s, z, w, t1, t2, R, hfsq, dk;

    if (m < 0.70710678118654752440) {
        m *= 2.0;
        --k;
    }
    f = m - 1.0;
    s = f / (2.0 + f);
    z = s * s;
    w = z * z;
    t1 = w * (3.999999999940941908e-01 +
         w * (2.222219843214978396e-01 +
         w * 1.531383769920937332e-01));
    t2 = z * (6.666666666666735130e-01 +
         w * (2.857142874366239149e-01 +
         w * (1.818357216161805012e-01 +
         w * 1.479819860511658591e-01)));
    R = t2 + t1;
    hfsq = 0.5 * f * f;
    dk = (double)k;
    *out_hi = dk * M_LN2_HI + f - hfsq;
    *out_lo = dk * M_LN2_LO + s * (hfsq + R);
}

double log(double x)
{
    math_u64 b = m_get_bits(x);
    double hi, lo;

    if (m_isnan_bits(b)) return x;
    if (m_iszero_bits(b)) {
        errno = ERANGE;
        return m_inf(1);
    }
    if (b.hi >> 31) {
        errno = EDOM;
        return m_qnan();
    }
    if (m_isinf_bits(b)) return x;
    if (x == 1.0) return 0.0;

    log_core(x, &hi, &lo);
    return hi + lo;
}

double log10(double x)
{
    math_u64 b = m_get_bits(x);
    math_u64 hb;
    double hi, lo, h_top, h_rem;
    const double ivln10_hi = 4.34294477105140686035e-01; /* 25-bit exact */
    const double ivln10_lo = 4.79811114161597280675e-09;

    if (m_isnan_bits(b)) return x;
    if (m_iszero_bits(b)) {
        errno = ERANGE;
        return m_inf(1);
    }
    if (b.hi >> 31) {
        errno = EDOM;
        return m_qnan();
    }
    if (m_isinf_bits(b)) return x;
    if (x == 1.0) return 0.0;

    log_core(x, &hi, &lo);
    hb = m_get_bits(hi);
    h_top = m_from_bits(hb.hi, hb.lo & (math_u32)0xF8000000UL);
    h_rem = (hi - h_top) + lo;
    return h_top * ivln10_hi + (h_rem * ivln10_hi + (hi + lo) * ivln10_lo);
}

double pow(double x, double y)
{
    math_u64 bx = m_get_bits(x);
    math_u64 by = m_get_bits(y);
    int y_is_int = 0;
    int y_is_odd = 0;
    double ay, yi;

    if (m_iszero_bits(by)) return 1.0;
    if (x == 1.0 && !m_isnan_bits(by)) return 1.0;
    if (m_isnan_bits(bx) || m_isnan_bits(by)) return m_qnan();

    ay = fabs(y);
    if (!m_isinf_bits(by)) {
        yi = floor(ay);
        if (yi == ay) {
            y_is_int = 1;
            if (ay < 9007199254740992.0 && fmod(yi, 2.0) == 1.0) {
                y_is_odd = 1;
            }
        }
    }

    if (m_isinf_bits(by)) {
        double ax = fabs(x);
        if (ax == 1.0) return 1.0;
        if (by.hi >> 31) {
            return (ax < 1.0) ? m_inf(0) : m_zero(0);
        }
        return (ax > 1.0) ? m_inf(0) : m_zero(0);
    }

    if (m_isinf_bits(bx)) {
        if (by.hi >> 31) {
            return ((bx.hi >> 31) && y_is_odd) ? m_zero(1) : m_zero(0);
        }
        return ((bx.hi >> 31) && y_is_odd) ? m_inf(1) : m_inf(0);
    }

    if (m_iszero_bits(bx)) {
        if (by.hi >> 31) {
            errno = ERANGE;
            return ((bx.hi >> 31) && y_is_odd) ? m_inf(1) : m_inf(0);
        }
        return ((bx.hi >> 31) && y_is_odd) ? m_zero(1) : m_zero(0);
    }

    if ((bx.hi >> 31) && !y_is_int) {
        errno = EDOM;
        return m_qnan();
    }

    /* Exact binary exponentiation for small integer powers */
    if (y_is_int && ay <= 64.0) {
        unsigned int n = (unsigned int)ay;
        double base = x;
        double acc = 1.0;
        while (n > 0U) {
            if (n & 1U) acc *= base;
            n >>= 1;
            if (n > 0U) base *= base;
        }
        if (by.hi >> 31) acc = 1.0 / acc;
        if (m_isinf_bits(m_get_bits(acc))) errno = ERANGE;
        return acc;
    }

    {
        double lhi, llo, y_hi, y_lo, p_hi, p_lo, res;
        math_u64 yb;
        log_core(fabs(x), &lhi, &llo);

        /* Split y into 26-bit high + low parts to preserve extra precision */
        yb = m_get_bits(y);
        y_hi = m_from_bits(yb.hi, yb.lo & (math_u32)0xF8000000UL);
        y_lo = y - y_hi;

        /* Split lhi into 26-bit high + low parts */
        {
            math_u64 lb = m_get_bits(lhi);
            double lhi_top = m_from_bits(lb.hi, lb.lo & (math_u32)0xF8000000UL);
            double lhi_rem = lhi - lhi_top + llo;
            p_hi = y_hi * lhi_top;
            p_lo = y_lo * lhi_top + y * lhi_rem;
        }

        res = exp(p_hi + p_lo);
        if ((bx.hi >> 31) && y_is_odd) res = -res;
        return res;
    }
}

/* Trigonometric kernels on [-pi/4, pi/4] */
static double kernel_sin(double x)
{
    double z = x * x;
    double v = z * x;
    double r = 2.75573137070700676789e-06 +
               z * (-2.50507602534068634195e-08 +
               z * 1.58969099521155010221e-10);
    return x + v * (-1.66666666666666324348e-01 +
               z * (8.33333333332248946124e-03 +
               z * (-1.98412698298579493134e-04 + z * r)));
}

static double kernel_cos(double x)
{
    double z = x * x;
    double r = z * (4.16666666666666019037e-02 +
               z * (-1.38888888888741095749e-03 +
               z * (2.48015872894767294178e-05 +
               z * (-2.75573143513906633035e-07 +
               z * (2.08757232129817482790e-09 +
               z * (-1.13596475577881948265e-11))))));
    return 1.0 - (0.5 * z - z * r);
}

static int rem_pio2(double x, double *r_out)
{
    const double inv_pio2 = 6.36619772367581382433e-01;
    const double pio2_1   = 1.57079632673412561417e+00; /* 33-bit exact */
    const double pio2_2   = 6.07710050630396597660e-11; /* 33-bit exact */
    const double pio2_3   = 2.02226624879595063154e-21;
    double fn;
    long n;
    double r, w;

    if (fabs(x) > 1073741824.0) {
        x = fmod(x, 6.28318530717958647692);
    }
    fn = floor(x * inv_pio2 + 0.5);
    n = (long)fn;
    r = x - fn * pio2_1;
    w = fn * pio2_2;
    r = (r - w) - fn * pio2_3;
    *r_out = r;
    return (int)(n & 3L);
}

double sin(double x)
{
    math_u64 b = m_get_bits(x);
    double r;
    int q;

    if (m_isnan_bits(b) || m_isinf_bits(b)) {
        if (m_isinf_bits(b)) errno = EDOM;
        return m_qnan();
    }
#ifdef __CKCL__
    if (__ck_has_x87 && (b.hi & (math_u32)0x7FFFFFFFUL) < (math_u32)0x43E00000UL) {
        __x87_sin(&r, &x);
        return r;
    }
#endif
    if (fabs(x) < 0.78539816339744830962) {
        if (m_iszero_bits(b)) return x;
        return kernel_sin(x);
    }
    q = rem_pio2(x, &r);
    switch (q) {
        case 0:  return kernel_sin(r);
        case 1:  return kernel_cos(r);
        case 2:  return -kernel_sin(r);
        default: return -kernel_cos(r);
    }
}

double cos(double x)
{
    math_u64 b = m_get_bits(x);
    double r;
    int q;

    if (m_isnan_bits(b) || m_isinf_bits(b)) {
        if (m_isinf_bits(b)) errno = EDOM;
        return m_qnan();
    }
#ifdef __CKCL__
    if (__ck_has_x87 && (b.hi & (math_u32)0x7FFFFFFFUL) < (math_u32)0x43E00000UL) {
        __x87_cos(&r, &x);
        return r;
    }
#endif
    if (fabs(x) < 0.78539816339744830962) {
        return kernel_cos(x);
    }
    q = rem_pio2(x, &r);
    switch (q) {
        case 0:  return kernel_cos(r);
        case 1:  return -kernel_sin(r);
        case 2:  return -kernel_cos(r);
        default: return kernel_sin(r);
    }
}

double tan(double x)
{
    math_u64 b = m_get_bits(x);
    double r, s, c;
    int q;

    if (m_isnan_bits(b) || m_isinf_bits(b)) {
        if (m_isinf_bits(b)) errno = EDOM;
        return m_qnan();
    }
    if (m_iszero_bits(b)) return x;
#ifdef __CKCL__
    if (__ck_has_x87 && (b.hi & (math_u32)0x7FFFFFFFUL) < (math_u32)0x43E00000UL) {
        __x87_sin(&s, &x);
        __x87_cos(&c, &x);
        return s / c;
    }
#endif

    q = rem_pio2(x, &r);
    s = kernel_sin(r);
    c = kernel_cos(r);
    if ((q & 1) == 0) {
        return s / c;
    }
    return -c / s;
}

double atan(double x)
{
    const double pi_o_2_hi = 1.57079632679489655800e+00;
    const double pi_o_2_lo = 6.12323399573676603587e-17;
    math_u64 b = m_get_bits(x);
    int sign = (b.hi >> 31) ? 1 : 0;
    int recip = 0;
    double ax, z, poly, res;

    if (m_isnan_bits(b)) return x;
    if (m_iszero_bits(b)) return x;
    if (m_isinf_bits(b)) {
        res = pi_o_2_hi + pi_o_2_lo;
        return sign ? -res : res;
    }
#ifdef __CKCL__
    if (__ck_has_x87) {
        __x87_atan(&res, &x);
        return res;
    }
#endif

    ax = fabs(x);
    if (ax > 1.0) {
        recip = 1;
        ax = 1.0 / ax;
    }

    /* Two half-angle reductions bring ax into [0, tan(pi/16)] (~0.19891) */
    ax = ax / (1.0 + sqrt(1.0 + ax * ax));
    ax = ax / (1.0 + sqrt(1.0 + ax * ax));

    z = ax * ax;
    poly = -1.0 / 23.0;
    poly = 1.0 / 21.0 + z * poly;
    poly = -1.0 / 19.0 + z * poly;
    poly = 1.0 / 17.0 + z * poly;
    poly = -1.0 / 15.0 + z * poly;
    poly = 1.0 / 13.0 + z * poly;
    poly = -1.0 / 11.0 + z * poly;
    poly = 1.0 / 9.0 + z * poly;
    poly = -1.0 / 7.0 + z * poly;
    poly = 1.0 / 5.0 + z * poly;
    poly = -1.0 / 3.0 + z * poly;

    res = 4.0 * (ax + ax * (z * poly));
    if (recip) {
        res = (pi_o_2_hi - res) + pi_o_2_lo;
    }
    return sign ? -res : res;
}

double atan2(double y, double x)
{
    const double pi     = 3.14159265358979323846;
    const double pi_o_2 = 1.57079632679489661923;
    const double pi_o_4 = 0.78539816339744830962;
    math_u64 bx = m_get_bits(x);
    math_u64 by = m_get_bits(y);
    int sx = (bx.hi >> 31) ? 1 : 0;
    int sy = (by.hi >> 31) ? 1 : 0;
    double z;

    if (m_isnan_bits(bx) || m_isnan_bits(by)) return m_qnan();
    if (x == 1.0) return atan(y);

    if (m_iszero_bits(by)) {
        if (sx) return sy ? -pi : pi;
        return m_zero(sy);
    }
    if (m_iszero_bits(bx)) {
        return sy ? -pi_o_2 : pi_o_2;
    }
    if (m_isinf_bits(bx)) {
        if (m_isinf_bits(by)) {
            double mag = sx ? (3.0 * pi_o_4) : pi_o_4;
            return sy ? -mag : mag;
        }
        if (sx) return sy ? -pi : pi;
        return m_zero(sy);
    }
    if (m_isinf_bits(by)) {
        return sy ? -pi_o_2 : pi_o_2;
    }

    z = atan(fabs(y / x));
    if (sx) z = pi - z;
    return sy ? -z : z;
}

double asin(double x)
{
    math_u64 b = m_get_bits(x);
    double ax;
    if (m_isnan_bits(b)) return x;
    ax = fabs(x);
    if (ax > 1.0) {
        errno = EDOM;
        return m_qnan();
    }
    if (m_iszero_bits(b)) return x;
    return atan2(x, sqrt((1.0 - ax) * (1.0 + ax)));
}

double acos(double x)
{
    math_u64 b = m_get_bits(x);
    double ax;
    if (m_isnan_bits(b)) return x;
    ax = fabs(x);
    if (ax > 1.0) {
        errno = EDOM;
        return m_qnan();
    }
    return atan2(sqrt((1.0 - ax) * (1.0 + ax)), x);
}

double sinh(double x)
{
    math_u64 b = m_get_bits(x);
    double ax, t;
    if (m_isnan_bits(b) || m_isinf_bits(b) || m_iszero_bits(b)) return x;
    ax = fabs(x);
    if (ax < 1.0e-4) {
        double x2 = x * x;
        return x * (1.0 + x2 * (1.0 / 6.0 + x2 * (1.0 / 120.0)));
    }
    if (ax < 22.0) {
        t = exp(ax);
        t = 0.5 * (t - 1.0 / t);
    } else if (ax <= 709.78271289338397) {
        t = 0.5 * exp(ax);
    } else {
        t = ldexp(exp(ax - M_LN2_HI), 0);
        if (m_isinf_bits(m_get_bits(t))) errno = ERANGE;
    }
    return (b.hi >> 31) ? -t : t;
}

double cosh(double x)
{
    math_u64 b = m_get_bits(x);
    double ax, t;
    if (m_isnan_bits(b)) return x;
    if (m_isinf_bits(b)) return m_inf(0);
    ax = fabs(x);
    if (ax < 22.0) {
        t = exp(ax);
        return 0.5 * (t + 1.0 / t);
    }
    if (ax <= 709.78271289338397) {
        return 0.5 * exp(ax);
    }
    t = exp(ax - M_LN2_HI);
    if (m_isinf_bits(m_get_bits(t))) errno = ERANGE;
    return t;
}

double tanh(double x)
{
    math_u64 b = m_get_bits(x);
    double ax, t;
    if (m_isnan_bits(b) || m_iszero_bits(b)) return x;
    ax = fabs(x);
    if (ax > 22.0) {
        return (b.hi >> 31) ? -1.0 : 1.0;
    }
    if (ax < 1.0e-4) {
        double x2 = x * x;
        return x * (1.0 - x2 * (1.0 / 3.0 - x2 * (2.0 / 15.0)));
    }
    t = exp(2.0 * ax);
    t = (t - 1.0) / (t + 1.0);
    return (b.hi >> 31) ? -t : t;
}
