/*
 * Clean-room IEEE-754 binary64/binary32 software floating-point runtime
 * for ckcl 16-bit 8086 targets.
 *
 * Uses pure 16/32-bit integer arithmetic so this file compiles self-contained
 * under ckcl without recursing into soft-float helpers.
 */

#ifdef __CKCL__
typedef unsigned short fp_u16;
typedef unsigned long  fp_u32;
typedef long           fp_i32;
#else
#include <stdint.h>
typedef uint16_t fp_u16;
typedef uint32_t fp_u32;
typedef int32_t  fp_i32;
#endif

typedef struct {
    fp_u32 lo;
    fp_u32 hi;
} fp_u64;

#define FP_CLASS_ZERO 0
#define FP_CLASS_NORM 1
#define FP_CLASS_INF  2
#define FP_CLASS_NAN  3

typedef struct {
    int cls;
    int sign;
    int exp;
    fp_u64 frac; /* 53-bit normalized significand (bit 52 = 0x00100000 in hi) */
} fp_unpacked;

static fp_u64 u64_make(fp_u32 hi, fp_u32 lo)
{
    fp_u64 r;
    r.lo = lo;
    r.hi = hi;
    return r;
}

static int u64_is_zero(fp_u64 a)
{
    return (a.lo | a.hi) == (fp_u32)0;
}

static int u64_cmp(fp_u64 a, fp_u64 b)
{
    if (a.hi != b.hi) return (a.hi < b.hi) ? -1 : 1;
    if (a.lo != b.lo) return (a.lo < b.lo) ? -1 : 1;
    return 0;
}

static fp_u64 u64_add(fp_u64 a, fp_u64 b)
{
    fp_u64 r;
    r.lo = a.lo + b.lo;
    r.hi = a.hi + b.hi + ((r.lo < a.lo) ? (fp_u32)1 : (fp_u32)0);
    return r;
}

static fp_u64 u64_sub(fp_u64 a, fp_u64 b)
{
    fp_u64 r;
    fp_u32 borrow = (a.lo < b.lo) ? (fp_u32)1 : (fp_u32)0;
    r.lo = a.lo - b.lo;
    r.hi = a.hi - b.hi - borrow;
    return r;
}

static fp_u64 u64_shl1(fp_u64 a)
{
    fp_u64 r;
    r.hi = (a.hi << 1) | (a.lo >> 31);
    r.lo = a.lo << 1;
    return r;
}

static fp_u64 u64_shl(fp_u64 a, int n)
{
    fp_u64 r;
    if (n <= 0) return a;
    if (n >= 64) return u64_make(0, 0);
    if (n >= 32) {
        r.hi = a.lo << (n - 32);
        r.lo = 0;
        return r;
    }
    r.hi = (a.hi << n) | (a.lo >> (32 - n));
    r.lo = a.lo << n;
    return r;
}

static fp_u64 u64_shr(fp_u64 a, int n)
{
    fp_u64 r;
    if (n <= 0) return a;
    if (n >= 64) return u64_make(0, 0);
    if (n >= 32) {
        r.lo = a.hi >> (n - 32);
        r.hi = 0;
        return r;
    }
    r.lo = (a.lo >> n) | (a.hi << (32 - n));
    r.hi = a.hi >> n;
    return r;
}

static fp_u64 u64_shr_sticky(fp_u64 a, int n)
{
    fp_u64 r;
    fp_u32 lost = 0;
    if (n <= 0) return a;
    if (n >= 64) {
        return u64_make(0, u64_is_zero(a) ? (fp_u32)0 : (fp_u32)1);
    }
    if (n == 32) {
        lost = a.lo;
        r.lo = a.hi;
        r.hi = 0;
    } else if (n > 32) {
        int s = n - 32;
        lost = a.lo | (a.hi & (((fp_u32)1 << s) - (fp_u32)1));
        r.lo = a.hi >> s;
        r.hi = 0;
    } else {
        lost = a.lo & (((fp_u32)1 << n) - (fp_u32)1);
        r.lo = (a.lo >> n) | (a.hi << (32 - n));
        r.hi = a.hi >> n;
    }
    if (lost != 0) r.lo |= (fp_u32)1;
    return r;
}

static fp_u64 fp_load(const double *p)
{
    const fp_u16 *w = (const fp_u16 *)p;
    fp_u64 r;
    r.lo = (fp_u32)w[0] | ((fp_u32)w[1] << 16);
    r.hi = (fp_u32)w[2] | ((fp_u32)w[3] << 16);
    return r;
}

static void fp_store(double *p, fp_u64 v)
{
    fp_u16 *w = (fp_u16 *)p;
    w[0] = (fp_u16)(v.lo & (fp_u32)0xFFFFUL);
    w[1] = (fp_u16)(v.lo >> 16);
    w[2] = (fp_u16)(v.hi & (fp_u32)0xFFFFUL);
    w[3] = (fp_u16)(v.hi >> 16);
}

static fp_u64 fp_make_zero(int sign)
{
    return u64_make(sign ? (fp_u32)0x80000000UL : (fp_u32)0, 0);
}

static fp_u64 fp_make_inf(int sign)
{
    return u64_make((sign ? (fp_u32)0x80000000UL : (fp_u32)0) | (fp_u32)0x7FF00000UL, 0);
}

static fp_u64 fp_make_qnan(void)
{
    return u64_make((fp_u32)0x7FF80000UL, 0);
}

static fp_u64 fp_quiet_nan(fp_u64 raw)
{
    raw.hi |= (fp_u32)0x7FF80000UL;
    return raw;
}

static void fp_unpack(fp_u64 raw, fp_unpacked *u)
{
    int bexp;
    u->sign = (raw.hi >> 31) ? 1 : 0;
    bexp = (int)((raw.hi >> 20) & (fp_u32)0x7FFUL);
    u->frac.lo = raw.lo;
    u->frac.hi = raw.hi & (fp_u32)0x000FFFFFUL;

    if (bexp == 0x7FF) {
        u->exp = 0;
        u->cls = u64_is_zero(u->frac) ? FP_CLASS_INF : FP_CLASS_NAN;
        return;
    }
    if (bexp == 0) {
        if (u64_is_zero(u->frac)) {
            u->exp = 0;
            u->cls = FP_CLASS_ZERO;
            return;
        }
        u->cls = FP_CLASS_NORM;
        u->exp = -1022;
        while ((u->frac.hi & (fp_u32)0x00100000UL) == 0) {
            u->frac = u64_shl1(u->frac);
            --u->exp;
        }
        return;
    }
    u->cls = FP_CLASS_NORM;
    u->exp = bexp - 1023;
    u->frac.hi |= (fp_u32)0x00100000UL;
}

/*
 * Pack a nonzero value given sign, unbiased exponent `exp`, and significand
 * `sig` scaled so bit 55 (0x00800000 in sig.hi) corresponds to 1.0 * 2^exp
 * (low 3 bits of `sig` are Guard, Round, Sticky).
 */
static fp_u64 fp_pack_round(int sign, int exp, fp_u64 sig)
{
    unsigned int r3;
    int bexp;

    if (u64_is_zero(sig)) return fp_make_zero(sign);

    while (sig.hi >= (fp_u32)0x01000000UL) {
        sig = u64_shr_sticky(sig, 1);
        ++exp;
    }
    while (sig.hi < (fp_u32)0x00800000UL) {
        if (sig.hi == 0 && sig.lo != 0 && exp > -1200) {
            if ((sig.lo & (fp_u32)0xFFFF0000UL) == 0) {
                sig = u64_shl(sig, 16);
                exp -= 16;
                continue;
            }
        }
        sig = u64_shl1(sig);
        --exp;
    }

    if (exp < -1022) {
        int shift = -1022 - exp;
        if (shift >= 57) {
            sig = u64_make(0, 1);
        } else {
            sig = u64_shr_sticky(sig, shift);
        }
        exp = -1022;
    }

    r3 = (unsigned int)(sig.lo & (fp_u32)7);
    sig = u64_shr(sig, 3);
    if (r3 > 4U || (r3 == 4U && (sig.lo & (fp_u32)1) != 0)) {
        sig = u64_add(sig, u64_make(0, 1));
        if (sig.hi >= (fp_u32)0x00200000UL) {
            sig = u64_shr(sig, 1);
            ++exp;
        }
    }

    if (u64_is_zero(sig)) return fp_make_zero(sign);
    if (exp > 1023) return fp_make_inf(sign);

    if ((sig.hi & (fp_u32)0x00100000UL) == 0) {
        bexp = 0;
    } else {
        bexp = exp + 1023;
        sig.hi &= (fp_u32)0x000FFFFFUL;
    }

    return u64_make(((fp_u32)(sign ? 1 : 0) << 31) |
                    ((fp_u32)bexp << 20) | sig.hi,
                    sig.lo);
}

static fp_u64 fp_add_core(fp_u64 ra, fp_u64 rb, int negate_b)
{
    fp_unpacked a, b;
    fp_u64 sa, sb, sr;
    int ea, eb, sign_a, sign_b;

    if (negate_b) rb.hi ^= (fp_u32)0x80000000UL;

    fp_unpack(ra, &a);
    fp_unpack(rb, &b);

    if (a.cls == FP_CLASS_NAN) return fp_quiet_nan(ra);
    if (b.cls == FP_CLASS_NAN) return fp_quiet_nan(rb);

    if (a.cls == FP_CLASS_INF) {
        if (b.cls == FP_CLASS_INF && a.sign != b.sign) return fp_make_qnan();
        return fp_make_inf(a.sign);
    }
    if (b.cls == FP_CLASS_INF) {
        return fp_make_inf(b.sign);
    }

    if (a.cls == FP_CLASS_ZERO && b.cls == FP_CLASS_ZERO) {
        return fp_make_zero((a.sign && b.sign) ? 1 : 0);
    }
    if (a.cls == FP_CLASS_ZERO) return rb;
    if (b.cls == FP_CLASS_ZERO) return ra;

    sa = u64_shl(a.frac, 3);
    sb = u64_shl(b.frac, 3);
    ea = a.exp;
    eb = b.exp;
    sign_a = a.sign;
    sign_b = b.sign;

    if (ea < eb || (ea == eb && u64_cmp(sa, sb) < 0)) {
        fp_u64 ts = sa;
        int te = ea;
        int tsg = sign_a;
        sa = sb; ea = eb; sign_a = sign_b;
        sb = ts; eb = te; sign_b = tsg;
    }

    if (ea > eb) {
        int diff = ea - eb;
        if (diff >= 57) {
            sb = u64_make(0, 1);
        } else {
            sb = u64_shr_sticky(sb, diff);
        }
    }

    if (sign_a == sign_b) {
        sr = u64_add(sa, sb);
        return fp_pack_round(sign_a, ea, sr);
    }

    sr = u64_sub(sa, sb);
    if (u64_is_zero(sr)) return fp_make_zero(0);
    return fp_pack_round(sign_a, ea, sr);
}

void __dadd(double *out, const double *a, const double *b)
{
    fp_store(out, fp_add_core(fp_load(a), fp_load(b), 0));
}

void __dsub(double *out, const double *a, const double *b)
{
    fp_store(out, fp_add_core(fp_load(a), fp_load(b), 1));
}

void __dneg(double *out, const double *a)
{
    fp_u64 r = fp_load(a);
    r.hi ^= (fp_u32)0x80000000UL;
    fp_store(out, r);
}

void __dmul(double *out, const double *a, const double *b)
{
    fp_u64 ra = fp_load(a);
    fp_u64 rb = fp_load(b);
    fp_unpacked ua, ub;
    int rsign;
    fp_u16 pa[4], pb[4], prod[8];
    int i, j;
    fp_u32 sticky;
    fp_u64 sig;

    fp_unpack(ra, &ua);
    fp_unpack(rb, &ub);
    rsign = ua.sign ^ ub.sign;

    if (ua.cls == FP_CLASS_NAN) { fp_store(out, fp_quiet_nan(ra)); return; }
    if (ub.cls == FP_CLASS_NAN) { fp_store(out, fp_quiet_nan(rb)); return; }

    if (ua.cls == FP_CLASS_INF) {
        fp_store(out, (ub.cls == FP_CLASS_ZERO) ? fp_make_qnan() : fp_make_inf(rsign));
        return;
    }
    if (ub.cls == FP_CLASS_INF) {
        fp_store(out, (ua.cls == FP_CLASS_ZERO) ? fp_make_qnan() : fp_make_inf(rsign));
        return;
    }
    if (ua.cls == FP_CLASS_ZERO || ub.cls == FP_CLASS_ZERO) {
        fp_store(out, fp_make_zero(rsign));
        return;
    }

    pa[0] = (fp_u16)(ua.frac.lo & (fp_u32)0xFFFFUL);
    pa[1] = (fp_u16)(ua.frac.lo >> 16);
    pa[2] = (fp_u16)(ua.frac.hi & (fp_u32)0xFFFFUL);
    pa[3] = (fp_u16)(ua.frac.hi >> 16);

    pb[0] = (fp_u16)(ub.frac.lo & (fp_u32)0xFFFFUL);
    pb[1] = (fp_u16)(ub.frac.lo >> 16);
    pb[2] = (fp_u16)(ub.frac.hi & (fp_u32)0xFFFFUL);
    pb[3] = (fp_u16)(ub.frac.hi >> 16);

    for (i = 0; i < 8; ++i) prod[i] = 0U;
    for (i = 0; i < 4; ++i) {
        fp_u32 carry = 0;
        for (j = 0; j < 4; ++j) {
            fp_u32 cur = (fp_u32)pa[i] * (fp_u32)pb[j] +
                         (fp_u32)prod[i + j] + carry;
            prod[i + j] = (fp_u16)(cur & (fp_u32)0xFFFFUL);
            carry = cur >> 16;
        }
        prod[i + 4] = (fp_u16)((fp_u32)prod[i + 4] + carry);
    }

    sticky = (fp_u32)prod[0] | (fp_u32)prod[1] |
             (fp_u32)prod[2] | (fp_u32)(prod[3] & 1U);
    sig.lo = ((fp_u32)prod[3] >> 1) |
             ((fp_u32)prod[4] << 15) |
             (((fp_u32)(prod[5] & 1U)) << 31);
    sig.hi = ((fp_u32)prod[5] >> 1) |
             ((fp_u32)prod[6] << 15);
    if (sticky != 0) sig.lo |= (fp_u32)1;

    fp_store(out, fp_pack_round(rsign, ua.exp + ub.exp, sig));
}

void __ddiv(double *out, const double *a, const double *b)
{
    fp_u64 ra = fp_load(a);
    fp_u64 rb = fp_load(b);
    fp_unpacked ua, ub;
    int rsign, rexp, i;
    fp_u64 rem, den, q;

    fp_unpack(ra, &ua);
    fp_unpack(rb, &ub);
    rsign = ua.sign ^ ub.sign;

    if (ua.cls == FP_CLASS_NAN) { fp_store(out, fp_quiet_nan(ra)); return; }
    if (ub.cls == FP_CLASS_NAN) { fp_store(out, fp_quiet_nan(rb)); return; }

    if (ua.cls == FP_CLASS_INF) {
        fp_store(out, (ub.cls == FP_CLASS_INF) ? fp_make_qnan() : fp_make_inf(rsign));
        return;
    }
    if (ub.cls == FP_CLASS_INF) {
        fp_store(out, fp_make_zero(rsign));
        return;
    }
    if (ub.cls == FP_CLASS_ZERO) {
        fp_store(out, (ua.cls == FP_CLASS_ZERO) ? fp_make_qnan() : fp_make_inf(rsign));
        return;
    }
    if (ua.cls == FP_CLASS_ZERO) {
        fp_store(out, fp_make_zero(rsign));
        return;
    }

    rexp = ua.exp - ub.exp;
    rem = ua.frac;
    den = ub.frac;
    if (u64_cmp(rem, den) < 0) {
        rem = u64_shl1(rem);
        --rexp;
    }

    q = u64_make(0, 0);
    for (i = 55; i >= 0; --i) {
        q = u64_shl1(q);
        if (u64_cmp(rem, den) >= 0) {
            rem = u64_sub(rem, den);
            q.lo |= (fp_u32)1;
        }
        if (i > 0) rem = u64_shl1(rem);
    }
    if (!u64_is_zero(rem)) q.lo |= (fp_u32)1;

    fp_store(out, fp_pack_round(rsign, rexp, q));
}

int __dcmp(const double *a, const double *b)
{
    fp_u64 ra = fp_load(a);
    fp_u64 rb = fp_load(b);
    fp_u32 ma_hi = ra.hi & (fp_u32)0x7FFFFFFFUL;
    fp_u32 mb_hi = rb.hi & (fp_u32)0x7FFFFFFFUL;
    int sa, sb, mag_cmp;

    if ((ma_hi > (fp_u32)0x7FF00000UL) || (ma_hi == (fp_u32)0x7FF00000UL && ra.lo != 0)) return 2;
    if ((mb_hi > (fp_u32)0x7FF00000UL) || (mb_hi == (fp_u32)0x7FF00000UL && rb.lo != 0)) return 2;

    if ((ma_hi | ra.lo) == 0 && (mb_hi | rb.lo) == 0) return 0;

    sa = (ra.hi >> 31) ? 1 : 0;
    sb = (rb.hi >> 31) ? 1 : 0;
    if (sa != sb) return sa ? -1 : 1;

    if (ma_hi != mb_hi) mag_cmp = (ma_hi < mb_hi) ? -1 : 1;
    else if (ra.lo != rb.lo) mag_cmp = (ra.lo < rb.lo) ? -1 : 1;
    else return 0;

    return sa ? -mag_cmp : mag_cmp;
}

static void fp_from_u32(double *out, int sign, fp_u32 v)
{
    fp_u64 sig;
    if (v == 0) {
        fp_store(out, fp_make_zero(0));
        return;
    }
    sig = u64_make(0, v);
    fp_store(out, fp_pack_round(sign, 55, sig));
}

void __ultod(double *out, fp_u32 v)
{
    fp_from_u32(out, 0, v);
}

void __ltod(double *out, fp_i32 v)
{
    if (v < (fp_i32)0) {
        fp_from_u32(out, 1, (fp_u32)0 - (fp_u32)v);
    } else {
        fp_from_u32(out, 0, (fp_u32)v);
    }
}

void __utod(double *out, unsigned int v)
{
    fp_from_u32(out, 0, (fp_u32)v);
}

void __itod(double *out, int v)
{
    __ltod(out, (fp_i32)v);
}

fp_u32 __dtoul(const double *a)
{
    fp_unpacked u;
    fp_u64 shifted;
    fp_unpack(fp_load(a), &u);
    if (u.cls == FP_CLASS_ZERO || u.cls == FP_CLASS_NAN || u.exp < 0) return 0;
    if (u.cls == FP_CLASS_INF || u.exp >= 32) {
        return u.sign ? (fp_u32)0 : (fp_u32)0xFFFFFFFFUL;
    }
    shifted = u64_shr(u.frac, 52 - u.exp);
    return u.sign ? ((fp_u32)0 - shifted.lo) : shifted.lo;
}

fp_i32 __dtol(const double *a)
{
    fp_unpacked u;
    fp_u64 shifted;
    fp_unpack(fp_load(a), &u);
    if (u.cls == FP_CLASS_ZERO || u.cls == FP_CLASS_NAN || u.exp < 0) return 0;
    if (u.sign) {
        if (u.cls == FP_CLASS_INF || u.exp > 31) return (fp_i32)0x80000000UL;
        shifted = u64_shr(u.frac, 52 - u.exp);
        if (u.exp == 31 && shifted.lo >= (fp_u32)0x80000000UL) return (fp_i32)0x80000000UL;
        return (fp_i32)((fp_u32)0 - shifted.lo);
    }
    if (u.cls == FP_CLASS_INF || u.exp >= 31) return (fp_i32)0x7FFFFFFFUL;
    shifted = u64_shr(u.frac, 52 - u.exp);
    return (fp_i32)shifted.lo;
}

int __dtoi(const double *a)
{
    fp_i32 v = __dtol(a);
    if (v > (fp_i32)32767L) return 32767;
    if (v < (fp_i32)(-32767L - 1L)) return -32767 - 1;
    return (int)v;
}

unsigned int __dtou(const double *a)
{
    fp_u32 v = __dtoul(a);
    if (v > (fp_u32)65535UL) return 65535U;
    return (unsigned int)v;
}

fp_u32 __dtos(const double *a)
{
    fp_u64 raw = fp_load(a);
    fp_unpacked u;
    fp_u32 sbit, fsig, r3;
    int exp, fbexp;

    fp_unpack(raw, &u);
    sbit = u.sign ? (fp_u32)0x80000000UL : (fp_u32)0;

    if (u.cls == FP_CLASS_NAN) {
        fp_u64 pl = u64_shr(u.frac, 29);
        return sbit | (fp_u32)0x7FC00000UL | (pl.lo & (fp_u32)0x003FFFFFUL);
    }
    if (u.cls == FP_CLASS_INF) return sbit | (fp_u32)0x7F800000UL;
    if (u.cls == FP_CLASS_ZERO) return sbit;

    {
        fp_u64 s27 = u64_shr_sticky(u.frac, 26);
        fsig = s27.lo;
    }
    exp = u.exp;

    if (exp < -126) {
        int shift = -126 - exp;
        if (shift >= 29) {
            fsig = (fp_u32)1;
        } else {
            fp_u32 lost = fsig & (((fp_u32)1 << shift) - (fp_u32)1);
            fsig >>= shift;
            if (lost != 0) fsig |= (fp_u32)1;
        }
        exp = -126;
    }

    r3 = fsig & (fp_u32)7;
    fsig >>= 3;
    if (r3 > (fp_u32)4 || (r3 == (fp_u32)4 && (fsig & (fp_u32)1) != 0)) {
        ++fsig;
        if (fsig >= (fp_u32)0x01000000UL) {
            fsig >>= 1;
            ++exp;
        }
    }

    if (fsig == 0) return sbit;
    if (exp > 127) return sbit | (fp_u32)0x7F800000UL;

    if ((fsig & (fp_u32)0x00800000UL) == 0) {
        fbexp = 0;
    } else {
        fbexp = exp + 127;
        fsig &= (fp_u32)0x007FFFFFUL;
    }
    return sbit | ((fp_u32)fbexp << 23) | fsig;
}

void __stod(double *out, fp_u32 fbits)
{
    int sign = (fbits >> 31) ? 1 : 0;
    int fbexp = (int)((fbits >> 23) & (fp_u32)0xFFUL);
    fp_u32 ffrac = fbits & (fp_u32)0x007FFFFFUL;
    fp_u32 sbit = sign ? (fp_u32)0x80000000UL : (fp_u32)0;
    int exp, bexp;
    fp_u64 dfrac;

    if (fbexp == 0xFF) {
        if (ffrac == 0) {
            fp_store(out, fp_make_inf(sign));
        } else {
            dfrac = u64_shl(u64_make(0, ffrac), 29);
            dfrac.hi |= sbit | (fp_u32)0x7FF80000UL;
            fp_store(out, dfrac);
        }
        return;
    }
    if (fbexp == 0) {
        if (ffrac == 0) {
            fp_store(out, fp_make_zero(sign));
            return;
        }
        exp = -126;
        while ((ffrac & (fp_u32)0x00800000UL) == 0) {
            ffrac <<= 1;
            --exp;
        }
        ffrac &= (fp_u32)0x007FFFFFUL;
        bexp = exp + 1023;
    } else {
        bexp = (fbexp - 127) + 1023;
    }

    dfrac = u64_shl(u64_make(0, ffrac), 29);
    dfrac.hi |= sbit | ((fp_u32)bexp << 20);
    fp_store(out, dfrac);
}
