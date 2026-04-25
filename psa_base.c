#include "psa_base.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

psa_real_t psa_real_abs(psa_real_t value) {
    return fabs(value);
}

psa_real_t psa_real_max(psa_real_t a, psa_real_t b) {
    return (a > b) ? a : b;
}

psa_real_t psa_real_machine_epsilon(void) {
    if (sizeof(psa_real_t) == sizeof(float)) {
        return (psa_real_t)FLT_EPSILON;
    }
    if (sizeof(psa_real_t) == sizeof(double)) {
        return (psa_real_t)DBL_EPSILON;
    }
    return (psa_real_t)LDBL_EPSILON;
}

psa_real_t psa_real_scaled_epsilon(psa_real_t scale) {
    return psa_real_max((psa_real_t)1.0, scale) * psa_real_machine_epsilon();
}

psa_dd_real_t psa_dd_from_real(psa_real_t value) {
    psa_dd_real_t out;
    out.hi = value;
    out.lo = 0.0;
    return out;
}

psa_dd_real_t psa_dd_add(psa_dd_real_t a, psa_dd_real_t b) {
    psa_dd_real_t out;
    psa_real_t s = a.hi + b.hi;
    psa_real_t v = s - a.hi;
    psa_real_t t = ((b.hi - v) + (a.hi - (s - v))) + a.lo + b.lo;
    out.hi = s + t;
    out.lo = t - (out.hi - s);
    return out;
}

psa_dd_real_t psa_dd_sub(psa_dd_real_t a, psa_dd_real_t b) {
    psa_dd_real_t neg_b;
    neg_b.hi = -b.hi;
    neg_b.lo = -b.lo;
    return psa_dd_add(a, neg_b);
}

psa_dd_real_t psa_dd_scale_pow2(psa_dd_real_t value, int exponent) {
    psa_dd_real_t out;
    out.hi = ldexp(value.hi, exponent);
    out.lo = ldexp(value.lo, exponent);
    return out;
}

psa_real_t psa_dd_to_real(psa_dd_real_t value) {
    return value.hi + value.lo;
}

psa_real_t psa_scale_pow2_real(psa_real_t value, int exponent) {
    return ldexp(value, exponent);
}

void psa_scale_pow2_real_array(psa_real_t *values, size_t count, int exponent) {
    size_t i;
    if (values == NULL || exponent == 0) {
        return;
    }
    for (i = 0; i < count; ++i) {
        values[i] = ldexp(values[i], exponent);
    }
}

void psa_scale_pow2_dd_array(psa_dd_real_t *values, size_t count, int exponent) {
    size_t i;
    if (values == NULL || exponent == 0) {
        return;
    }
    for (i = 0; i < count; ++i) {
        values[i] = psa_dd_scale_pow2(values[i], exponent);
    }
}

void psa_kahan_reset(psa_kahan_t *acc) {
    if (acc == NULL) {
        return;
    }
    acc->sum = 0.0;
    acc->comp = 0.0;
}

void psa_kahan_add(psa_kahan_t *acc, psa_real_t value) {
    psa_real_t y;
    psa_real_t t;
    if (acc == NULL) {
        return;
    }
    y = value - acc->comp;
    t = acc->sum + y;
    acc->comp = (t - acc->sum) - y;
    acc->sum = t;
}

uint64_t psa_fnv1a_mix(uint64_t hash, uint64_t chunk) {
    hash ^= chunk;
    hash *= 1099511628211ULL;
    return hash;
}

int64_t psa_quantize_value(psa_real_t value, psa_real_t scale) {
    psa_real_t unit = psa_real_scaled_epsilon(scale) * (psa_real_t)4096.0;
    if (fabs(value) <= unit) {
        return 0;
    }
    return (int64_t)llround(value / unit);
}

uint64_t psa_signature_from_reals(const psa_real_t *values, size_t count, psa_real_t scale) {
    size_t i;
    uint64_t hash = 1469598103934665603ULL;
    if (values == NULL) {
        return hash;
    }
    for (i = 0; i < count; ++i) {
        hash = psa_fnv1a_mix(hash, (uint64_t)psa_quantize_value(values[i], scale));
    }
    return hash;
}

void psa_summary_reset(psa_summary_t *summary) {
    if (summary == NULL) {
        return;
    }
    memset(summary, 0, sizeof(*summary));
}

const char *psa_status_string(psa_status_t status) {
    switch (status) {
        case PSA_STATUS_OK:
            return "ok";
        case PSA_STATUS_BAD_ARGUMENT:
            return "bad-argument";
        case PSA_STATUS_CAPACITY:
            return "capacity";
        case PSA_STATUS_NUMERIC:
            return "numeric";
        default:
            return "unknown";
    }
}

const char *psa_q_rule_name(psa_q_rule_class_t q_class) {
    switch (q_class) {
        case PSA_Q_RULE_ABSORBING:
            return "absorbing";
        case PSA_Q_RULE_FINITE_RETURN:
            return "finite-return";
        case PSA_Q_RULE_NONRETURN:
            return "non-finite-return";
        default:
            return "unknown";
    }
}

bool psa_q_certificate_committable(const psa_q_certificate_t *certificate) {
    if (certificate == NULL || !certificate->ready) {
        return false;
    }
    if (certificate->absorb_certified) {
        return true;
    }
    if (certificate->sigma_radial_finite_certified) {
        return true;
    }
    if (certificate->finite_return_certified) {
        return true;
    }
    if (certificate->nonreturn_certified) {
        return true;
    }
    if (certificate->kernel_lift_certified) {
        return true;
    }
    return false;
}

const char *psa_q_certificate_name(const psa_q_certificate_t *certificate) {
    if (certificate == NULL || !certificate->ready) {
        return "none";
    }
    if (certificate->kernel_lift_certified) {
        switch (certificate->kernel_lift_class) {
            case PSA_Q_RULE_ABSORBING:
                return certificate->kernel_lift_multiblock ? "kernel-lift-multiblock-absorbing"
                                                           : "kernel-lift-block-absorbing";
            case PSA_Q_RULE_FINITE_RETURN:
                return certificate->kernel_lift_multiblock ? "kernel-lift-multiblock-finite-return"
                                                           : "kernel-lift-block-finite-return";
            case PSA_Q_RULE_NONRETURN:
                return certificate->kernel_lift_multiblock ? "kernel-lift-multiblock-nonreturn"
                                                           : "kernel-lift-block-nonreturn";
            default:
                break;
        }
    }
    if (certificate->absorb_certified) {
        return "absorbing-tail";
    }
    if (certificate->sigma_radial_finite_certified) {
        return "normalized-kernel-finite-return";
    }
    if (certificate->finite_return_certified) {
        return "finite-return";
    }
    if (certificate->nonreturn_certified) {
        return "non-finite-return";
    }
    return "uncertified";
}

int psa_format_certificate(char *buffer, size_t buffer_size, const psa_q_certificate_t *certificate) {
    if (buffer == NULL || buffer_size == 0U) {
        return -1;
    }
    if (certificate == NULL) {
        return snprintf(buffer, buffer_size, "none");
    }
    return snprintf(buffer,
                    buffer_size,
                    "ready=%d q=%s name=%s ret=(%lu,%ld) sig=(%llu,%llu,%llu) sigma=(ready=%d period=%lu count=%lu) "
                    "block=(active=%d osc=%d class=%s period=%lu) multiblock=(ready=%d count=%lu class=%s period=%lu)",
                    certificate->ready ? 1 : 0,
                    psa_q_rule_name(certificate->q_class),
                    psa_q_certificate_name(certificate),
                    (unsigned long)certificate->return_period,
                    (long)certificate->return_start_step,
                    (unsigned long long)certificate->sigma_signature,
                    (unsigned long long)certificate->state_signature,
                    (unsigned long long)certificate->combined_signature,
                    certificate->sigma_radial_ready ? 1 : 0,
                    (unsigned long)certificate->sigma_period,
                    (unsigned long)certificate->sigma_lambda_count,
                    certificate->block_rule_active ? 1 : 0,
                    certificate->block_rule_oscillatory ? 1 : 0,
                    psa_q_rule_name(certificate->block_class),
                    (unsigned long)certificate->block_period,
                    certificate->multiblock_ready ? 1 : 0,
                    (unsigned long)certificate->block_count,
                    psa_q_rule_name(certificate->multiblock_class),
                    (unsigned long)certificate->multiblock_period);
}

int psa_format_summary(char *buffer, size_t buffer_size, const psa_summary_t *summary) {
    if (buffer == NULL || buffer_size == 0U || summary == NULL) {
        return -1;
    }
    return snprintf(buffer,
                    buffer_size,
                    "samples=%lu pages=%lu q_class=%s cert=%s sig_order=%lu pascal_order=%lu q_order=%lu "
                    "ret=(%lu,%ld) total_pages=%lu pow2=(%ld,%ld) q=%s%.12f kernel=%s sig=%llu/%llu/%llu certsig=%llu",
                    (unsigned long)summary->sample_count,
                    (unsigned long)summary->page_count,
                    psa_q_rule_name(summary->q_class),
                    psa_q_certificate_name(&summary->certificate),
                    (unsigned long)summary->signal_order,
                    (unsigned long)summary->pascal_order,
                    (unsigned long)summary->q_order,
                    (unsigned long)summary->certificate.return_period,
                    (long)summary->certificate.return_start_step,
                    (unsigned long)summary->total_page_count,
                    (long)summary->signal_pow2_exponent,
                    (long)summary->pascal_pow2_exponent,
                    summary->q_defined ? "" : "undef:",
                    summary->q_defined ? summary->latest_q : 0.0,
                    summary->kernel_constant ? "const" : (summary->kernel_period > 0U ? "periodic" : "free"),
                    (unsigned long long)summary->sigma_signature,
                    (unsigned long long)summary->state_signature,
                    (unsigned long long)summary->combined_signature,
                    (unsigned long long)summary->certificate.signature);
}
