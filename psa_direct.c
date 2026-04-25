#include "psa_direct.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PSA_DIRECT_PI ((psa_real_t)3.14159265358979323846264338327950288)

static int psa_direct_gcd_int(int a, int b) {
    while (b != 0) {
        int t = a % b;
        a = b;
        b = t;
    }
    return (a < 0) ? -a : a;
}

static int psa_direct_lcm_int(int a, int b) {
    if (a <= 0) {
        return b;
    }
    if (b <= 0) {
        return a;
    }
    return (a / psa_direct_gcd_int(a, b)) * b;
}

static psa_real_t psa_direct_principal_angle(psa_real_t theta) {
    while (theta <= -PSA_DIRECT_PI) {
        theta += (psa_real_t)2.0 * PSA_DIRECT_PI;
    }
    while (theta > PSA_DIRECT_PI) {
        theta -= (psa_real_t)2.0 * PSA_DIRECT_PI;
    }
    return theta;
}

static psa_real_t psa_direct_hypot(psa_real_t x, psa_real_t y) {
    return sqrt(x * x + y * y);
}

static psa_real_t psa_direct_safe_scale(psa_real_t scale) {
    return psa_real_max((psa_real_t)1.0, scale);
}

static psa_real_t psa_direct_tol(psa_real_t scale, psa_real_t factor) {
    return psa_direct_safe_scale(scale) * factor * psa_real_machine_epsilon();
}

static psa_real_t psa_direct_seq_tol(psa_real_t scale, uint32_t count, psa_real_t factor) {
    return psa_direct_tol(scale, factor * (psa_real_t)psa_real_max((psa_real_t)4.0, (psa_real_t)count));
}

static psa_real_t psa_direct_unit_tol(psa_real_t factor) {
    return factor * psa_real_machine_epsilon();
}

static void psa_direct_finalize_component(psa_direct_component_t *component,
                                          psa_real_t sample_rate_hz,
                                          uint32_t source_rule_index) {
    if (component == NULL) {
        return;
    }
    component->valid = true;
    component->lambda_re = component->alpha_re - (psa_real_t)1.0;
    component->lambda_im = component->alpha_im;
    component->rho = psa_direct_hypot(component->alpha_re, component->alpha_im);
    component->theta = fabs(atan2(component->alpha_im, component->alpha_re));
    component->phase = psa_direct_principal_angle(atan2(component->amplitude_im, component->amplitude_re));
    component->frequency_norm = component->theta / ((psa_real_t)2.0 * PSA_DIRECT_PI);
    component->frequency_hz = sample_rate_hz * component->frequency_norm;
    component->source_rule_index = source_rule_index;
}

static psa_real_t psa_direct_rule_energy(const psa_explain_rule_t *rule, uint32_t sample_count) {
    psa_real_t state[PSA_CFG_MAX_ORDER];
    psa_real_t sum = 0.0;
    uint32_t i;

    if (rule == NULL || !rule->defined || rule->seed_count == 0U) {
        return 0.0;
    }
    for (i = 0U; i < sample_count && i < rule->seed_count; ++i) {
        sum += rule->seeds[i] * rule->seeds[i];
    }
    if (sample_count <= rule->seed_count || rule->order == 0U) {
        return sum;
    }
    for (i = 0U; i < rule->order; ++i) {
        state[i] = rule->seeds[rule->seed_count - rule->order + i];
    }
    for (i = rule->seed_count; i < sample_count; ++i) {
        psa_kahan_t acc;
        psa_real_t next_value;
        uint32_t k;
        psa_kahan_reset(&acc);
        for (k = 1U; k <= rule->order; ++k) {
            psa_kahan_add(&acc, rule->coefficients[k] * state[rule->order - k]);
        }
        next_value = -acc.sum;
        sum += next_value * next_value;
        memmove(state, state + 1U, (size_t)(rule->order - 1U) * sizeof(*state));
        state[rule->order - 1U] = next_value;
    }
    return sum;
}

static psa_real_t psa_direct_component_energy(const psa_direct_component_t *component, uint32_t sample_count) {
    psa_real_t sum = 0.0;
    psa_real_t state_re;
    psa_real_t state_im;
    uint32_t i;

    if (component == NULL || !component->valid) {
        return 0.0;
    }
    state_re = component->amplitude_re;
    state_im = component->amplitude_im;
    for (i = 0U; i < sample_count; ++i) {
        psa_real_t next_re;
        psa_real_t next_im;
        psa_real_t value = component->oscillatory ? ((psa_real_t)2.0 * state_re) : state_re;
        if (component->kind == PSA_DIRECT_KIND_ORDER2_REAL_REPEATED) {
            value += ((psa_real_t)i) * component->jordan_re * state_re;
        }
        sum += value * value;
        next_re = state_re * component->alpha_re - state_im * component->alpha_im;
        next_im = state_re * component->alpha_im + state_im * component->alpha_re;
        state_re = next_re;
        state_im = next_im;
    }
    return sum;
}

static void psa_direct_accumulate_component_sequence(const psa_direct_component_t *component,
                                                     psa_real_t weight,
                                                     uint32_t sample_count,
                                                     psa_real_t *out_values) {
    uint32_t i;
    psa_real_t state_re;
    psa_real_t state_im;

    if (component == NULL || out_values == NULL || !component->valid) {
        return;
    }
    state_re = component->amplitude_re;
    state_im = component->amplitude_im;
    for (i = 0U; i < sample_count; ++i) {
        psa_real_t next_re;
        psa_real_t next_im;
        psa_real_t value = component->oscillatory ? ((psa_real_t)2.0 * state_re) : state_re;
        if (component->kind == PSA_DIRECT_KIND_ORDER2_REAL_REPEATED) {
            value += ((psa_real_t)i) * component->jordan_re * state_re;
        }
        out_values[i] += weight * value;
        next_re = state_re * component->alpha_re - state_im * component->alpha_im;
        next_im = state_re * component->alpha_im + state_im * component->alpha_re;
        state_re = next_re;
        state_im = next_im;
    }
}

static bool psa_direct_is_harmonic_of(const psa_direct_component_t *comp,
                                      const psa_direct_component_t *base,
                                      uint32_t *multiple_out);

static bool psa_direct_detect_tail_constant(const psa_real_t *values,
                                            uint32_t count,
                                            psa_real_t tol,
                                            psa_real_t *constant_value) {
    uint32_t need;
    uint32_t start;
    uint32_t i;
    if (values == NULL || count == 0U) {
        return false;
    }
    need = (count < 4U) ? count : 4U;
    start = count - need;
    *constant_value = values[count - 1U];
    for (i = start; i < count; ++i) {
        if (fabs(values[i] - *constant_value) > tol) {
            return false;
        }
    }
    return true;
}

static uint32_t psa_direct_detect_tail_period(const psa_real_t *values,
                                              uint32_t count,
                                              psa_real_t tol) {
    uint32_t p;
    if (values == NULL) {
        return 0U;
    }
    for (p = 1U; p <= 8U; ++p) {
        uint32_t need = 2U * p;
        uint32_t i;
        if (count < need) {
            break;
        }
        for (i = count - p; i < count; ++i) {
            if (fabs(values[i] - values[i - p]) > tol) {
                break;
            }
        }
        if (i == count) {
            return p;
        }
    }
    return 0U;
}

static bool psa_direct_lambda_is_zero(psa_real_t re, psa_real_t im, psa_real_t scale) {
    psa_real_t norm = psa_direct_hypot(re, im);
    return norm <= psa_direct_tol(scale, (psa_real_t)256.0);
}

static bool psa_direct_lambda_is_unit_root(psa_real_t re, psa_real_t im, uint32_t *period_out) {
    uint32_t k;
    psa_real_t tol = psa_direct_unit_tol((psa_real_t)4096.0);
    psa_real_t rho = psa_direct_hypot(re, im);
    if (fabs(rho - (psa_real_t)1.0) > tol) {
        return false;
    }
    for (k = 1U; k <= 32U; ++k) {
        psa_real_t theta = atan2(im, re) * (psa_real_t)k;
        psa_real_t wr = cos(theta) * pow(rho, (psa_real_t)k);
        psa_real_t wi = sin(theta) * pow(rho, (psa_real_t)k);
        if (psa_direct_hypot(wr - (psa_real_t)1.0, wi) <= tol * (psa_real_t)8.0) {
            *period_out = k;
            return true;
        }
    }
    return false;
}

static void psa_direct_mat_identity(psa_real_t *a, uint32_t dim) {
    uint32_t i;
    uint32_t j;
    memset(a, 0, (size_t)dim * (size_t)dim * sizeof(*a));
    for (i = 0U; i < dim; ++i) {
        for (j = 0U; j < dim; ++j) {
            a[i * dim + j] = (i == j) ? (psa_real_t)1.0 : (psa_real_t)0.0;
        }
    }
}

static void psa_direct_mat_mul(const psa_real_t *a,
                               const psa_real_t *b,
                               psa_real_t *out,
                               uint32_t dim) {
    uint32_t i;
    uint32_t j;
    uint32_t k;
    for (i = 0U; i < dim; ++i) {
        for (j = 0U; j < dim; ++j) {
            psa_real_t sum = 0.0;
            for (k = 0U; k < dim; ++k) {
                sum += a[i * dim + k] * b[k * dim + j];
            }
            out[i * dim + j] = sum;
        }
    }
}

static psa_real_t psa_direct_matrix_max_abs(const psa_real_t *a, uint32_t dim) {
    psa_real_t best = 0.0;
    uint32_t i;
    uint32_t total = dim * dim;
    for (i = 0U; i < total; ++i) {
        best = psa_real_max(best, fabs(a[i]));
    }
    return best;
}

static psa_real_t psa_direct_mat_identity_error(const psa_real_t *a, uint32_t dim) {
    psa_real_t e = 0.0;
    uint32_t i;
    uint32_t j;
    for (i = 0U; i < dim; ++i) {
        for (j = 0U; j < dim; ++j) {
            psa_real_t target = (i == j) ? (psa_real_t)1.0 : (psa_real_t)0.0;
            e = psa_real_max(e, fabs(a[i * dim + j] - target));
        }
    }
    return e;
}

static psa_real_t psa_direct_mat_zero_error(const psa_real_t *a, uint32_t dim) {
    return psa_direct_matrix_max_abs(a, dim);
}

static psa_q_rule_class_t psa_direct_classify_matrix(const psa_real_t *matrix,
                                                     uint32_t dim,
                                                     psa_real_t scale,
                                                     uint32_t *period_out) {
    psa_real_t power[PSA_CFG_MAX_ORDER * PSA_CFG_MAX_ORDER];
    psa_real_t next[PSA_CFG_MAX_ORDER * PSA_CFG_MAX_ORDER];
    psa_real_t tol;
    uint32_t m;

    if (period_out != NULL) {
        *period_out = 0U;
    }
    if (matrix == NULL || dim == 0U || dim > PSA_CFG_MAX_ORDER) {
        return PSA_Q_RULE_UNKNOWN;
    }

    tol = psa_direct_tol(psa_real_max(scale, psa_direct_matrix_max_abs(matrix, dim)),
                         (psa_real_t)(32768.0 * dim * dim));
    psa_direct_mat_identity(power, dim);
    for (m = 1U; m <= 32U; ++m) {
        psa_direct_mat_mul(power, matrix, next, dim);
        memcpy(power, next, (size_t)dim * (size_t)dim * sizeof(*power));
        if (psa_direct_mat_zero_error(power, dim) <= tol) {
            return PSA_Q_RULE_ABSORBING;
        }
        if (psa_direct_mat_identity_error(power, dim) <= tol) {
            if (period_out != NULL) {
                *period_out = m;
            }
            return PSA_Q_RULE_FINITE_RETURN;
        }
    }
    return PSA_Q_RULE_NONRETURN;
}

static bool psa_direct_block_has_finite_order(const psa_direct_block_t *block, uint32_t *period_out) {
    if (block == NULL || !block->active) {
        return false;
    }
    if (block->dim == 1U) {
        return psa_direct_lambda_is_unit_root(block->lambda_re[0], block->lambda_im[0], period_out);
    }
    if (block->dim == 2U &&
        psa_direct_classify_matrix(block->matrix,
                                   2U,
                                   psa_direct_matrix_max_abs(block->matrix, 2U),
                                   period_out) == PSA_Q_RULE_FINITE_RETURN) {
        return true;
    }
    return false;
}

static bool psa_direct_block_can_merge(const psa_direct_block_t *lhs, const psa_direct_block_t *rhs) {
    psa_real_t scale0;
    psa_real_t err0;
    if (!lhs->active || !rhs->active || lhs->dim != rhs->dim) {
        return false;
    }
    scale0 = psa_real_max((psa_real_t)1.0,
                          psa_real_max(psa_direct_hypot(lhs->lambda_re[0], lhs->lambda_im[0]),
                                       psa_direct_hypot(rhs->lambda_re[0], rhs->lambda_im[0])));
    err0 = psa_direct_hypot(lhs->lambda_re[0] - rhs->lambda_re[0], lhs->lambda_im[0] - rhs->lambda_im[0]);
    if (lhs->dim == 1U) {
        return err0 <= psa_direct_tol(scale0, (psa_real_t)8192.0);
    }
    {
        psa_real_t scale1 = psa_real_max((psa_real_t)1.0,
                                         psa_real_max(psa_direct_hypot(lhs->lambda_re[1], lhs->lambda_im[1]),
                                                      psa_direct_hypot(rhs->lambda_re[1], rhs->lambda_im[1])));
        psa_real_t err1 = psa_direct_hypot(lhs->lambda_re[1] - rhs->lambda_re[1], lhs->lambda_im[1] - rhs->lambda_im[1]);
        psa_real_t swap_err0 = psa_direct_hypot(lhs->lambda_re[0] - rhs->lambda_re[1], lhs->lambda_im[0] - rhs->lambda_im[1]);
        psa_real_t swap_err1 = psa_direct_hypot(lhs->lambda_re[1] - rhs->lambda_re[0], lhs->lambda_im[1] - rhs->lambda_im[0]);
        return (err0 <= psa_direct_tol(scale0, (psa_real_t)8192.0) &&
                err1 <= psa_direct_tol(scale1, (psa_real_t)8192.0)) ||
               (swap_err0 <= psa_direct_tol(scale0, (psa_real_t)8192.0) &&
                swap_err1 <= psa_direct_tol(scale1, (psa_real_t)8192.0));
    }
}

static void psa_direct_merge_block(psa_direct_block_t *dst, const psa_direct_block_t *src) {
    dst->weight += src->weight;
    if (src->q_class == PSA_Q_RULE_NONRETURN) {
        dst->q_class = PSA_Q_RULE_NONRETURN;
    } else if (src->q_class == PSA_Q_RULE_FINITE_RETURN && dst->q_class == PSA_Q_RULE_FINITE_RETURN) {
        dst->period = (uint32_t)psa_direct_lcm_int((int)dst->period, (int)src->period);
    }
}

static void psa_direct_finalize_block(psa_direct_block_t *block) {
    uint32_t period = 0U;
    psa_real_t scale;
    if (block == NULL || !block->active) {
        return;
    }
    scale = psa_real_max((psa_real_t)1.0, psa_direct_matrix_max_abs(block->matrix, block->dim));
    if (psa_direct_lambda_is_zero(block->lambda_re[0], block->lambda_im[0], scale) &&
        (block->dim == 1U || psa_direct_lambda_is_zero(block->lambda_re[1], block->lambda_im[1], scale))) {
        block->q_class = PSA_Q_RULE_ABSORBING;
        return;
    }
    if (psa_direct_block_has_finite_order(block, &period)) {
        block->q_class = PSA_Q_RULE_FINITE_RETURN;
        block->period = period;
        return;
    }
    block->q_class = PSA_Q_RULE_NONRETURN;
}

static bool psa_direct_block_is_harmonic_of(const psa_direct_block_t *block,
                                            const psa_direct_block_t *base,
                                            uint32_t *multiple_out) {
    psa_direct_component_t lhs;
    psa_direct_component_t rhs;
    if (block == NULL || base == NULL || block->dim != 2U || base->dim != 2U) {
        return false;
    }
    memset(&lhs, 0, sizeof(lhs));
    memset(&rhs, 0, sizeof(rhs));
    lhs.valid = true;
    lhs.oscillatory = true;
    lhs.alpha_re = block->alpha_re[0];
    lhs.alpha_im = block->alpha_im[0];
    lhs.rho = psa_direct_hypot(lhs.alpha_re, lhs.alpha_im);
    lhs.theta = fabs(atan2(lhs.alpha_im, lhs.alpha_re));
    rhs.valid = true;
    rhs.oscillatory = true;
    rhs.alpha_re = base->alpha_re[0];
    rhs.alpha_im = base->alpha_im[0];
    rhs.rho = psa_direct_hypot(rhs.alpha_re, rhs.alpha_im);
    rhs.theta = fabs(atan2(rhs.alpha_im, rhs.alpha_re));
    return psa_direct_is_harmonic_of(&lhs, &rhs, multiple_out);
}

static void psa_direct_finalize_block_family(psa_direct_block_family_t *family) {
    uint32_t i;
    psa_real_t best = -1.0;
    bool all_absorb = true;
    bool all_return = true;
    uint32_t total_period = 1U;
    psa_direct_block_t compressed[PSA_CFG_MAX_ORDER];
    uint32_t out_count = 0U;

    if (family == NULL) {
        return;
    }
    memset(compressed, 0, sizeof(compressed));
    for (i = 0U; i < family->block_count; ++i) {
        uint32_t j;
        bool hit = false;
        if (!family->blocks[i].active) {
            continue;
        }
        for (j = 0U; j < out_count; ++j) {
            if (psa_direct_block_can_merge(&compressed[j], &family->blocks[i])) {
                psa_direct_merge_block(&compressed[j], &family->blocks[i]);
                hit = true;
                break;
            }
        }
        if (!hit && out_count < PSA_CFG_MAX_ORDER) {
            compressed[out_count] = family->blocks[i];
            out_count += 1U;
        }
    }
    memset(family->blocks, 0, sizeof(family->blocks));
    family->block_count = out_count;
    family->total_dimension = 0U;
    family->dominant_block = -1;
    family->primary_count = 0U;
    family->harmonic_count = 0U;
    family->signature = 1469598103934665603ULL;
    for (i = 0U; i < out_count; ++i) {
        family->blocks[i] = compressed[i];
        psa_direct_finalize_block(&family->blocks[i]);
        family->total_dimension += family->blocks[i].dim;
        family->signature = psa_fnv1a_mix(family->signature,
                                          psa_signature_from_reals(family->blocks[i].matrix,
                                                                   (family->blocks[i].dim == 2U) ? 4U : 1U,
                                                                   psa_real_max((psa_real_t)1.0, family->blocks[i].weight)));
        if (family->blocks[i].weight > best) {
            best = family->blocks[i].weight;
            family->dominant_block = (int32_t)i;
        }
        if (family->blocks[i].q_class != PSA_Q_RULE_ABSORBING) {
            all_absorb = false;
        }
        if (family->blocks[i].q_class == PSA_Q_RULE_FINITE_RETURN) {
            total_period = (uint32_t)psa_direct_lcm_int((int)total_period, (int)family->blocks[i].period);
        } else if (family->blocks[i].q_class != PSA_Q_RULE_ABSORBING) {
            all_return = false;
        }
    }
    if (out_count == 0U) {
        family->ready = false;
        return;
    }
    if (all_absorb) {
        family->q_class = PSA_Q_RULE_ABSORBING;
    } else if (all_return) {
        family->q_class = PSA_Q_RULE_FINITE_RETURN;
        family->period = total_period;
    } else {
        family->q_class = PSA_Q_RULE_NONRETURN;
    }
    if (family->dominant_block < 0) {
        family->dominant_block = 0;
    }
    for (i = 0U; i < out_count; ++i) {
        uint32_t multiple = 0U;
        if ((int32_t)i == family->dominant_block) {
            family->primary_count += 1U;
            continue;
        }
        if (psa_direct_block_is_harmonic_of(&family->blocks[i],
                                            &family->blocks[family->dominant_block],
                                            &multiple)) {
            family->blocks[i].harmonic = true;
            family->blocks[i].harmonic_multiple = multiple;
            family->harmonic_count += 1U;
        } else {
            family->primary_count += 1U;
        }
    }
    family->committed_like = (out_count <= 3U);
    family->ready = true;
}

static psa_status_t psa_direct_decode_order1(const psa_explain_rule_t *rule,
                                             psa_real_t sample_rate_hz,
                                             psa_direct_result_t *result) {
    psa_direct_component_t *component;
    if (rule->seed_count < 1U) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    component = &result->components[0];
    component->kind = PSA_DIRECT_KIND_ORDER1_REAL;
    component->oscillatory = false;
    component->alpha_re = -rule->coefficients[1];
    component->alpha_im = 0.0;
    component->amplitude_re = rule->seeds[0];
    component->amplitude_im = 0.0;
    component->jordan_re = 0.0;
    component->jordan_im = 0.0;
    psa_direct_finalize_component(component, sample_rate_hz, 0U);
    result->component_count = 1U;
    result->dominant_index = 0;
    result->primary_count = component->oscillatory ? 1U : 0U;
    result->harmonic_count = 0U;
    result->ready = true;
    result->source_order = 1U;
    return PSA_STATUS_OK;
}

static psa_status_t psa_direct_decode_order2(const psa_explain_rule_t *rule,
                                             psa_real_t sample_rate_hz,
                                             psa_direct_result_t *result) {
    psa_real_t c1;
    psa_real_t c2;
    psa_real_t disc;
    psa_real_t tol;
    result->source_order = 2U;
    if (rule->seed_count < 2U) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    c1 = rule->coefficients[1];
    c2 = rule->coefficients[2];
    tol = psa_real_max((psa_real_t)1.0, psa_real_abs(c1) + psa_real_abs(c2)) * (psa_real_t)32.0 * psa_real_machine_epsilon();
    disc = c1 * c1 - (psa_real_t)4.0 * c2;

    if (disc > tol) {
        psa_real_t sdisc = sqrt(disc);
        psa_real_t alpha0 = ((psa_real_t)(-c1) + sdisc) * (psa_real_t)0.5;
        psa_real_t alpha1 = ((psa_real_t)(-c1) - sdisc) * (psa_real_t)0.5;
        psa_real_t x0 = rule->seeds[0];
        psa_real_t x1 = rule->seeds[1];
        psa_real_t denom = alpha1 - alpha0;
        psa_direct_component_t *a = &result->components[0];
        psa_direct_component_t *b = &result->components[1];
        if (fabs(denom) <= tol) {
            return PSA_STATUS_NUMERIC;
        }
        a->kind = PSA_DIRECT_KIND_ORDER2_REAL_DISTINCT;
        a->oscillatory = false;
        a->alpha_re = alpha0;
        a->alpha_im = 0.0;
        a->amplitude_re = (x1 - alpha1 * x0) / (alpha0 - alpha1);
        a->amplitude_im = 0.0;
        a->jordan_re = 0.0;
        a->jordan_im = 0.0;
        psa_direct_finalize_component(a, sample_rate_hz, 0U);

        b->kind = PSA_DIRECT_KIND_ORDER2_REAL_DISTINCT;
        b->oscillatory = false;
        b->alpha_re = alpha1;
        b->alpha_im = 0.0;
        b->amplitude_re = (x1 - alpha0 * x0) / denom;
        b->amplitude_im = 0.0;
        b->jordan_re = 0.0;
        b->jordan_im = 0.0;
        psa_direct_finalize_component(b, sample_rate_hz, 0U);

        result->component_count = 2U;
        result->dominant_index = (fabs(a->amplitude_re) >= fabs(b->amplitude_re)) ? 0 : 1;
        result->primary_count = 0U;
        result->harmonic_count = 0U;
        result->ready = true;
        return PSA_STATUS_OK;
    }

    if (fabs(disc) <= tol) {
        psa_real_t alpha = -(psa_real_t)0.5 * c1;
        psa_real_t x0 = rule->seeds[0];
        psa_real_t x1 = rule->seeds[1];
        psa_direct_component_t *a = &result->components[0];
        psa_direct_component_t *b = &result->components[1];
        a->kind = PSA_DIRECT_KIND_ORDER2_REAL_REPEATED;
        a->oscillatory = false;
        a->alpha_re = alpha;
        a->alpha_im = 0.0;
        a->amplitude_re = x0;
        a->amplitude_im = 0.0;
        a->jordan_re = (fabs(alpha) <= tol) ? x1 : (x1 / alpha - x0);
        a->jordan_im = 0.0;
        psa_direct_finalize_component(a, sample_rate_hz, 0U);

        b->kind = PSA_DIRECT_KIND_ORDER2_REAL_REPEATED;
        b->oscillatory = false;
        b->alpha_re = alpha;
        b->alpha_im = 0.0;
        b->amplitude_re = 0.0;
        b->amplitude_im = 0.0;
        b->jordan_re = a->jordan_re;
        b->jordan_im = 0.0;
        psa_direct_finalize_component(b, sample_rate_hz, 0U);

        result->component_count = 1U;
        result->dominant_index = 0;
        result->primary_count = 0U;
        result->harmonic_count = 0U;
        result->ready = true;
        return PSA_STATUS_OK;
    }

    {
        psa_real_t x0 = rule->seeds[0];
        psa_real_t x1 = rule->seeds[1];
        psa_real_t alpha_re = -(psa_real_t)0.5 * c1;
        psa_real_t alpha_im = sqrt(-disc) * (psa_real_t)0.5;
        psa_real_t denom = (psa_real_t)2.0 * alpha_im;
        psa_real_t amp_re = (psa_real_t)0.5 * x0;
        psa_real_t amp_im = (alpha_im == 0.0) ? 0.0 : ((alpha_re * x0 - x1) / denom);
        psa_direct_component_t *a = &result->components[0];
        if (fabs(denom) <= tol) {
            return PSA_STATUS_NUMERIC;
        }
        a->kind = PSA_DIRECT_KIND_ORDER2_OSCILLATORY;
        a->oscillatory = true;
        a->alpha_re = alpha_re;
        a->alpha_im = alpha_im;
        a->amplitude_re = amp_re;
        a->amplitude_im = amp_im;
        a->jordan_re = 0.0;
        a->jordan_im = 0.0;
        psa_direct_finalize_component(a, sample_rate_hz, 0U);
        result->component_count = 1U;
        result->dominant_index = 0;
        result->primary_count = 1U;
        result->harmonic_count = 0U;
        result->ready = true;
        return PSA_STATUS_OK;
    }
}

static bool psa_direct_is_harmonic_of(const psa_direct_component_t *comp,
                                      const psa_direct_component_t *base,
                                      uint32_t *multiple_out) {
    uint32_t m;
    psa_real_t tol;
    if (comp == NULL || base == NULL || !comp->oscillatory || !base->oscillatory) {
        return false;
    }
    tol = psa_direct_unit_tol((psa_real_t)1024.0);
    for (m = 2U; m <= 8U; ++m) {
        psa_real_t rho = pow(base->rho, (psa_real_t)m);
        psa_real_t theta = base->theta * (psa_real_t)m;
        psa_real_t theta_err = fabs(psa_direct_principal_angle(comp->theta - theta));
        psa_real_t rho_err = fabs(comp->rho - rho);
        psa_real_t rho_scale = psa_real_max((psa_real_t)1.0, psa_real_max(comp->rho, rho));
        if (theta_err <= (psa_real_t)2048.0 * tol && rho_err <= rho_scale * (psa_real_t)2048.0 * tol) {
            *multiple_out = m;
            return true;
        }
    }
    return false;
}

void psa_direct_component_reset(psa_direct_component_t *component) {
    if (component != NULL) {
        memset(component, 0, sizeof(*component));
    }
}

void psa_direct_result_reset(psa_direct_result_t *result) {
    if (result != NULL) {
        memset(result, 0, sizeof(*result));
        result->dominant_index = -1;
    }
}

void psa_direct_family_reset(psa_direct_family_t *family) {
    if (family != NULL) {
        memset(family, 0, sizeof(*family));
        family->dominant_index = -1;
    }
}

void psa_direct_sigma_reset(psa_direct_sigma_t *sigma) {
    if (sigma != NULL) {
        memset(sigma, 0, sizeof(*sigma));
    }
}

void psa_direct_block_reset(psa_direct_block_t *block) {
    if (block != NULL) {
        memset(block, 0, sizeof(*block));
    }
}

void psa_direct_block_family_reset(psa_direct_block_family_t *family) {
    if (family != NULL) {
        memset(family, 0, sizeof(*family));
        family->dominant_block = -1;
    }
}

void psa_direct_high_block_reset(psa_direct_high_block_t *block) {
    if (block != NULL) {
        memset(block, 0, sizeof(*block));
    }
}

void psa_direct_connection_object_reset(psa_direct_connection_object_t *object) {
    if (object != NULL) {
        memset(object, 0, sizeof(*object));
        object->base_family.dominant_block = -1;
    }
}

static psa_real_t psa_direct_connection_base_weight(const psa_direct_connection_object_t *object) {
    psa_real_t total = 0.0;
    uint32_t i;
    if (object == NULL) {
        return (psa_real_t)1.0;
    }
    for (i = 0U; i < object->part_count; ++i) {
        total += object->parts[i].weight;
    }
    if (total == 0.0) {
        for (i = 0U; i < object->base_family.block_count; ++i) {
            total += object->base_family.blocks[i].weight;
        }
    }
    return psa_direct_safe_scale(total);
}

static void psa_direct_wrap_block_as_high(const psa_direct_block_t *src, psa_direct_high_block_t *dst) {
    psa_direct_high_block_reset(dst);
    if (src == NULL || dst == NULL || !src->active || src->dim == 0U || src->dim > 2U) {
        return;
    }
    dst->ready = true;
    dst->dim = src->dim;
    dst->q_class = src->q_class;
    dst->period = src->period;
    dst->weight = src->weight;
    if (src->dim == 1U) {
        dst->matrix[0] = src->matrix[0];
        dst->coefficients[0] = (psa_real_t)1.0;
        dst->coefficients[1] = -src->matrix[0];
        dst->state[0] = (psa_real_t)1.0;
        dst->seeds[0] = (psa_real_t)1.0;
    } else {
        memcpy(dst->matrix, src->matrix, 4U * sizeof(*dst->matrix));
        dst->coefficients[0] = (psa_real_t)1.0;
        dst->coefficients[1] = -(src->matrix[0] + src->matrix[3]);
        dst->coefficients[2] = src->matrix[0] * src->matrix[3] - src->matrix[1] * src->matrix[2];
        dst->state[0] = (psa_real_t)1.0;
        dst->state[1] = (psa_real_t)0.0;
        dst->seeds[0] = (psa_real_t)1.0;
        dst->seeds[1] = (psa_real_t)0.0;
    }
    dst->signature = psa_fnv1a_mix(psa_signature_from_reals(dst->matrix,
                                                            dst->dim * dst->dim,
                                                            psa_direct_safe_scale(dst->weight)),
                                   (uint64_t)(dst->dim * 17U + dst->period));
}

psa_status_t psa_direct_decode_rule(const psa_explain_rule_t *rule,
                                    psa_real_t sample_rate_hz,
                                    psa_direct_result_t *result) {
    if (rule == NULL || result == NULL) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    psa_direct_result_reset(result);
    if (!rule->defined || rule->order == 0U) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    if (rule->order == 1U) {
        return psa_direct_decode_order1(rule, sample_rate_hz, result);
    }
    if (rule->order == 2U) {
        return psa_direct_decode_order2(rule, sample_rate_hz, result);
    }
    return PSA_STATUS_NUMERIC;
}

psa_status_t psa_direct_decode_signal_record(const psa_analysis_record_t *record,
                                             psa_real_t sample_rate_hz,
                                             psa_direct_result_t *result) {
    psa_explain_rule_t local_rule;
    uint32_t i;
    if (record == NULL || result == NULL) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    if (!record->ready || !record->signal_rule.defined || record->signal_rule.order == 0U) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    if (record->trace_count < record->signal_rule.order) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    local_rule = record->signal_rule;
    local_rule.seed_count = local_rule.order;
    for (i = 0U; i < local_rule.order; ++i) {
        local_rule.seeds[i] = record->sample_trace[i];
    }
    return psa_direct_decode_rule(&local_rule, sample_rate_hz, result);
}

psa_status_t psa_direct_emit_component_sequence(const psa_direct_component_t *component,
                                                uint32_t sample_count,
                                                psa_real_t *out_values,
                                                uint32_t out_capacity) {
    uint32_t i;
    psa_real_t state_re;
    psa_real_t state_im;
    if (component == NULL || out_values == NULL) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    if (out_capacity < sample_count) {
        return PSA_STATUS_CAPACITY;
    }
    if (!component->valid) {
        memset(out_values, 0, (size_t)sample_count * sizeof(*out_values));
        return PSA_STATUS_OK;
    }
    state_re = component->amplitude_re;
    state_im = component->amplitude_im;
    for (i = 0U; i < sample_count; ++i) {
        psa_real_t next_re;
        psa_real_t next_im;
        psa_real_t value = component->oscillatory ? ((psa_real_t)2.0 * state_re) : state_re;
        if (component->kind == PSA_DIRECT_KIND_ORDER2_REAL_REPEATED) {
            value += ((psa_real_t)i) * component->jordan_re * state_re;
        }
        out_values[i] = value;
        next_re = state_re * component->alpha_re - state_im * component->alpha_im;
        next_im = state_re * component->alpha_im + state_im * component->alpha_re;
        state_re = next_re;
        state_im = next_im;
    }
    return PSA_STATUS_OK;
}

psa_status_t psa_direct_reconstruct(const psa_direct_result_t *result,
                                    uint32_t sample_count,
                                    psa_real_t *out_values,
                                    uint32_t out_capacity) {
    psa_real_t weights[2] = {1.0, 1.0};
    psa_direct_family_t family;
    uint32_t i;
    if (result == NULL || out_values == NULL) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    psa_direct_family_reset(&family);
    family.ready = result->ready;
    family.component_count = result->component_count;
    family.dominant_index = result->dominant_index;
    family.primary_count = result->primary_count;
    family.harmonic_count = result->harmonic_count;
    for (i = 0U; i < result->component_count; ++i) {
        family.components[i] = result->components[i];
    }
    return psa_direct_synthesize_family(&family, weights, sample_count, out_values, out_capacity);
}

psa_status_t psa_direct_decode_rule_family(const psa_explain_rule_t *const *rules,
                                           size_t rule_count,
                                           psa_real_t sample_rate_hz,
                                           uint32_t energy_length,
                                           psa_direct_family_t *family) {
    size_t i;
    if (rules == NULL || family == NULL || energy_length == 0U || energy_length > PSA_CFG_MAX_FRONTIER) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    psa_direct_family_reset(family);
    for (i = 0U; i < rule_count; ++i) {
        psa_direct_result_t result;
        uint32_t j;
        psa_status_t status;
        psa_direct_result_reset(&result);
        status = psa_direct_decode_rule(rules[i], sample_rate_hz, &result);
        if (status != PSA_STATUS_OK) {
            return status;
        }
        for (j = 0U; j < result.component_count; ++j) {
            uint32_t idx = family->component_count;
            if (idx >= PSA_CFG_MAX_ORDER) {
                return PSA_STATUS_CAPACITY;
            }
            family->components[idx] = result.components[j];
            family->components[idx].source_rule_index = (uint32_t)i;
            family->components[idx].energy = psa_direct_component_energy(&family->components[idx], energy_length);
            family->component_count += 1U;
        }
    }
    if (family->component_count == 0U) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    {
        psa_real_t best = -1.0;
        uint32_t i_comp;
        for (i_comp = 0U; i_comp < family->component_count; ++i_comp) {
            if (family->components[i_comp].oscillatory && family->components[i_comp].energy > best) {
                best = family->components[i_comp].energy;
                family->dominant_index = (int32_t)i_comp;
            }
        }
        if (family->dominant_index < 0) {
            family->dominant_index = 0;
        }
        for (i_comp = 0U; i_comp < family->component_count; ++i_comp) {
            uint32_t multiple = 0U;
            if ((int32_t)i_comp == family->dominant_index) {
                family->primary_count += 1U;
                continue;
            }
            if (psa_direct_is_harmonic_of(&family->components[i_comp],
                                          &family->components[family->dominant_index],
                                          &multiple)) {
                family->components[i_comp].harmonic = true;
                family->components[i_comp].harmonic_multiple = multiple;
                family->harmonic_count += 1U;
            } else {
                family->primary_count += 1U;
            }
        }
    }
    family->ready = true;
    return PSA_STATUS_OK;
}

psa_status_t psa_direct_extract_sigma(const psa_analysis_record_t *record,
                                      psa_direct_sigma_t *sigma) {
    uint32_t start;
    uint32_t end;
    uint32_t i;
    psa_real_t tol;
    if (record == NULL || sigma == NULL) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    psa_direct_sigma_reset(sigma);
    if (record->q_trace_count == 0U) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    end = record->q_trace_count - 1U;
    start = end;
    while (start > 0U &&
           record->kernel_step_trace[start] == record->kernel_step_trace[start - 1U] + 1U &&
           end - start + 1U < PSA_CFG_MAX_ORDER) {
        start -= 1U;
    }
    sigma->ready = true;
    sigma->start_level = record->kernel_step_trace[start];
    if (sigma->start_level >= record->trace_count) {
        return PSA_STATUS_NUMERIC;
    }
    sigma->a = record->pascal_trace[sigma->start_level];
    sigma->lambda_count = end - start + 1U;
    sigma->coeff_count = sigma->lambda_count + 1U;
    sigma->coeff[0] = (psa_real_t)1.0;
    for (i = 0U; i < sigma->lambda_count; ++i) {
        uint32_t level = sigma->start_level + i;
        sigma->lambda[i] = record->kernel_trace[start + i];
        sigma->scale = psa_real_max(sigma->scale, fabs(sigma->lambda[i]));
        if (i + 1U < sigma->coeff_count) {
            sigma->coeff[i + 1U] = sigma->coeff[i] * sigma->lambda[i] / (psa_real_t)(level + 1U);
        }
    }
    sigma->scale = psa_real_max(sigma->scale, (psa_real_t)1.0);
    tol = psa_direct_seq_tol(sigma->scale, sigma->lambda_count, (psa_real_t)128.0);
    if (fabs(sigma->a) > tol) {
        sigma->constant = psa_direct_detect_tail_constant(sigma->lambda,
                                                          sigma->lambda_count,
                                                          psa_direct_seq_tol(sigma->scale, sigma->lambda_count, (psa_real_t)512.0),
                                                          &sigma->constant_value);
        sigma->period = psa_direct_detect_tail_period(sigma->lambda,
                                                      sigma->lambda_count,
                                                      psa_direct_seq_tol(sigma->scale, sigma->lambda_count, (psa_real_t)2048.0));
        sigma->finite_certified = (sigma->period > 0U && sigma->lambda_count >= 2U * sigma->period + 1U);
    }
    sigma->signature = psa_signature_from_reals(sigma->lambda, sigma->lambda_count, sigma->scale);
    return PSA_STATUS_OK;
}

psa_status_t psa_direct_blocks_from_result(const psa_direct_result_t *result,
                                           psa_direct_block_family_t *family) {
    uint32_t i;
    if (result == NULL || family == NULL) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    psa_direct_block_family_reset(family);
    if (!result->ready) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    for (i = 0U; i < result->component_count; ++i) {
        psa_direct_block_t *block = &family->blocks[family->block_count];
        const psa_direct_component_t *component = &result->components[i];
        psa_direct_block_reset(block);
        block->active = component->valid;
        block->oscillatory = component->oscillatory;
        block->weight = psa_real_max((psa_real_t)1.0, component->energy);
        block->source_rule_index = component->source_rule_index;
        if (component->oscillatory) {
            block->dim = 2U;
            block->matrix[0] = component->lambda_re;
            block->matrix[1] = -component->lambda_im;
            block->matrix[2] = component->lambda_im;
            block->matrix[3] = component->lambda_re;
            block->lambda_re[0] = component->lambda_re;
            block->lambda_im[0] = component->lambda_im;
            block->lambda_re[1] = component->lambda_re;
            block->lambda_im[1] = -component->lambda_im;
            block->alpha_re[0] = component->alpha_re;
            block->alpha_im[0] = component->alpha_im;
            block->alpha_re[1] = component->alpha_re;
            block->alpha_im[1] = -component->alpha_im;
        } else {
            block->dim = 1U;
            block->matrix[0] = component->lambda_re;
            block->lambda_re[0] = component->lambda_re;
            block->lambda_im[0] = component->lambda_im;
            block->alpha_re[0] = component->alpha_re;
            block->alpha_im[0] = component->alpha_im;
        }
        family->block_count += 1U;
    }
    psa_direct_finalize_block_family(family);
    return PSA_STATUS_OK;
}

psa_status_t psa_direct_blocks_from_family(const psa_direct_family_t *direct_family,
                                           psa_direct_block_family_t *block_family) {
    uint32_t i;
    if (direct_family == NULL || block_family == NULL) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    psa_direct_block_family_reset(block_family);
    for (i = 0U; i < direct_family->component_count; ++i) {
        psa_direct_block_t *block = &block_family->blocks[block_family->block_count];
        const psa_direct_component_t *component = &direct_family->components[i];
        psa_direct_block_reset(block);
        block->active = component->valid;
        block->oscillatory = component->oscillatory;
        block->weight = psa_real_max((psa_real_t)1e-30, component->energy);
        block->source_rule_index = component->source_rule_index;
        if (component->oscillatory) {
            block->dim = 2U;
            block->matrix[0] = component->lambda_re;
            block->matrix[1] = -component->lambda_im;
            block->matrix[2] = component->lambda_im;
            block->matrix[3] = component->lambda_re;
            block->lambda_re[0] = component->lambda_re;
            block->lambda_im[0] = component->lambda_im;
            block->lambda_re[1] = component->lambda_re;
            block->lambda_im[1] = -component->lambda_im;
            block->alpha_re[0] = component->alpha_re;
            block->alpha_im[0] = component->alpha_im;
            block->alpha_re[1] = component->alpha_re;
            block->alpha_im[1] = -component->alpha_im;
        } else {
            block->dim = 1U;
            block->matrix[0] = component->lambda_re;
            block->lambda_re[0] = component->lambda_re;
            block->lambda_im[0] = component->lambda_im;
            block->alpha_re[0] = component->alpha_re;
            block->alpha_im[0] = component->alpha_im;
        }
        block_family->block_count += 1U;
    }
    psa_direct_finalize_block_family(block_family);
    return PSA_STATUS_OK;
}

psa_status_t psa_direct_expand_sigma_connections(const psa_direct_sigma_t *sigma,
                                                 psa_direct_block_family_t *family) {
    uint32_t i;
    if (sigma == NULL || family == NULL || !sigma->ready) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    psa_direct_block_family_reset(family);
    for (i = 0U; i < sigma->lambda_count && i < PSA_CFG_MAX_ORDER; ++i) {
        psa_direct_block_t *block = &family->blocks[family->block_count];
        psa_direct_block_reset(block);
        block->active = true;
        block->dim = 1U;
        block->matrix[0] = sigma->lambda[i];
        block->lambda_re[0] = sigma->lambda[i];
        block->alpha_re[0] = sigma->lambda[i] + (psa_real_t)1.0;
        block->weight = psa_real_max((psa_real_t)1e-30, fabs(sigma->coeff[i + 1U]));
        family->block_count += 1U;
    }
    psa_direct_finalize_block_family(family);
    return PSA_STATUS_OK;
}

psa_status_t psa_direct_high_block_from_rule(const psa_explain_rule_t *rule,
                                             uint32_t energy_length,
                                             psa_direct_high_block_t *block) {
    uint32_t i;
    uint32_t period = 0U;
    psa_real_t scale = 0.0;

    if (rule == NULL || block == NULL || !rule->defined || rule->order == 0U || rule->order > PSA_CFG_MAX_ORDER) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    if (rule->seed_count < rule->order || energy_length == 0U || energy_length > PSA_CFG_MAX_FRONTIER) {
        return PSA_STATUS_BAD_ARGUMENT;
    }

    psa_direct_high_block_reset(block);
    block->dim = rule->order;
    memcpy(block->coefficients, rule->coefficients, (size_t)(rule->order + 1U) * sizeof(*block->coefficients));
    memcpy(block->seeds, rule->seeds, (size_t)rule->order * sizeof(*block->seeds));
    memcpy(block->state, rule->seeds, (size_t)rule->order * sizeof(*block->state));

    for (i = 0U; i + 1U < rule->order; ++i) {
        block->matrix[i * block->dim + (i + 1U)] = (psa_real_t)1.0;
    }
    for (i = 0U; i < rule->order; ++i) {
        block->matrix[(rule->order - 1U) * block->dim + i] = -rule->coefficients[rule->order - i];
    }

    block->weight = psa_direct_rule_energy(rule, (energy_length < rule->order) ? rule->order : energy_length);
    scale = psa_real_max((psa_real_t)1.0,
                         psa_real_max(psa_direct_matrix_max_abs(block->matrix, block->dim),
                                      psa_direct_hypot(rule->scale, block->weight)));
    block->signature = psa_fnv1a_mix(psa_signature_from_reals(block->matrix,
                                                              block->dim * block->dim,
                                                              scale),
                                     psa_signature_from_reals(block->state,
                                                              block->dim,
                                                              scale));
    block->q_class = psa_direct_classify_matrix(block->matrix, block->dim, scale, &period);
    block->period = period;
    block->ready = true;
    return PSA_STATUS_OK;
}

psa_status_t psa_direct_reconstruct_high_block(const psa_direct_high_block_t *block,
                                               uint32_t sample_count,
                                               psa_real_t *out_values,
                                               uint32_t out_capacity) {
    psa_explain_rule_t local_rule;
    uint32_t i;
    if (block == NULL || out_values == NULL || !block->ready || block->dim == 0U) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    psa_explain_rule_reset(&local_rule);
    local_rule.defined = true;
    local_rule.kind = PSA_EXPLAIN_RULE_SIGNAL;
    local_rule.order = block->dim;
    local_rule.seed_count = block->dim;
    for (i = 0U; i <= block->dim; ++i) {
        local_rule.coefficients[i] = block->coefficients[i];
    }
    for (i = 0U; i < block->dim; ++i) {
        local_rule.seeds[i] = block->seeds[i];
    }
    return psa_explain_replay_rule(&local_rule,
                                   (sample_count > block->dim) ? (sample_count - block->dim) : 0U,
                                   out_values,
                                   out_capacity,
                                   NULL);
}

psa_status_t psa_direct_connection_object_from_high_block(const psa_direct_high_block_t *block,
                                                          psa_direct_connection_object_t *object) {
    if (block == NULL || object == NULL || !block->ready) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    psa_direct_connection_object_reset(object);
    object->ready = true;
    object->a = (psa_real_t)1.0;
    object->level_count = 1U;
    object->part_count = 1U;
    object->total_dimension = block->dim;
    object->q_class = block->q_class;
    object->period = block->period;
    object->signature = block->signature;
    object->parts[0] = *block;
    object->coeff[0] = (psa_real_t)1.0;
    object->level_weight[0] = block->weight;
    return PSA_STATUS_OK;
}

psa_status_t psa_direct_connection_object_from_block_family(const psa_direct_block_family_t *family,
                                                            psa_direct_connection_object_t *object) {
    uint32_t i;
    if (family == NULL || object == NULL || !family->ready) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    if (family->block_count > PSA_CFG_MAX_ORDER) {
        return PSA_STATUS_CAPACITY;
    }
    psa_direct_connection_object_reset(object);
    object->ready = true;
    object->a = (psa_real_t)1.0;
    object->level_count = 1U;
    object->part_count = family->block_count;
    object->total_dimension = family->total_dimension;
    object->q_class = family->q_class;
    object->period = family->period;
    object->signature = family->signature;
    object->base_family = *family;
    object->coeff[0] = (psa_real_t)1.0;
    for (i = 0U; i < family->block_count; ++i) {
        psa_direct_wrap_block_as_high(&family->blocks[i], &object->parts[i]);
        object->level_weight[0] += family->blocks[i].weight;
    }
    object->level_weight[0] = psa_direct_safe_scale(object->level_weight[0]);
    return PSA_STATUS_OK;
}

psa_status_t psa_direct_connection_object_lift_sigma(const psa_direct_sigma_t *sigma,
                                                     const psa_direct_connection_object_t *base_object,
                                                     psa_direct_connection_object_t *object) {
    psa_real_t base_weight;
    uint32_t i;
    if (sigma == NULL || base_object == NULL || object == NULL || !sigma->ready || !base_object->ready) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    if (base_object->part_count > PSA_CFG_MAX_ORDER) {
        return PSA_STATUS_CAPACITY;
    }
    psa_direct_connection_object_reset(object);
    object->start_level = sigma->start_level;
    object->a = sigma->a;
    object->level_count = sigma->lambda_count;
    object->part_count = base_object->part_count;
    object->total_dimension = sigma->lambda_count * base_object->total_dimension;
    object->q_class = base_object->q_class;
    object->period = base_object->period;
    object->signature = psa_fnv1a_mix(sigma->signature, base_object->signature);
    object->base_family = base_object->base_family;
    memcpy(object->parts, base_object->parts, (size_t)base_object->part_count * sizeof(*object->parts));
    memcpy(object->lambda, sigma->lambda, (size_t)sigma->lambda_count * sizeof(*object->lambda));
    memcpy(object->coeff, sigma->coeff, (size_t)sigma->coeff_count * sizeof(*object->coeff));

    base_weight = psa_direct_connection_base_weight(base_object);
    for (i = 0U; i < sigma->lambda_count; ++i) {
        object->level_weight[i] = base_weight * sigma->coeff[i + 1U];
    }
    if (base_object->q_class == PSA_Q_RULE_ABSORBING) {
        object->q_class = PSA_Q_RULE_ABSORBING;
        object->period = 0U;
    } else if (base_object->q_class == PSA_Q_RULE_FINITE_RETURN && sigma->finite_certified) {
        object->q_class = PSA_Q_RULE_FINITE_RETURN;
        object->period = (uint32_t)psa_direct_lcm_int((int)base_object->period, (int)sigma->period);
    }
    object->ready = true;
    return PSA_STATUS_OK;
}

psa_status_t psa_direct_connection_object_from_sigma_and_family(const psa_direct_sigma_t *sigma,
                                                                const psa_direct_block_family_t *family,
                                                                psa_direct_connection_object_t *object) {
    psa_direct_connection_object_t base_object;
    psa_status_t status;
    status = psa_direct_connection_object_from_block_family(family, &base_object);
    if (status != PSA_STATUS_OK) {
        return status;
    }
    return psa_direct_connection_object_lift_sigma(sigma, &base_object, object);
}

psa_status_t psa_direct_rebuild_connection_profile(const psa_direct_connection_object_t *object,
                                                   psa_real_t *out_values,
                                                   uint32_t out_capacity,
                                                   uint32_t *written_count) {
    uint32_t i;
    if (written_count != NULL) {
        *written_count = 0U;
    }
    if (object == NULL || out_values == NULL || !object->ready) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    if (out_capacity < object->level_count) {
        return PSA_STATUS_CAPACITY;
    }
    for (i = 0U; i < object->level_count; ++i) {
        out_values[i] = object->level_weight[i];
    }
    if (written_count != NULL) {
        *written_count = object->level_count;
    }
    return PSA_STATUS_OK;
}

psa_status_t psa_direct_synthesize_family(const psa_direct_family_t *family,
                                          const psa_real_t *weights,
                                          uint32_t sample_count,
                                          psa_real_t *out_values,
                                          uint32_t out_capacity) {
    uint32_t i;
    if (family == NULL || weights == NULL || out_values == NULL) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    if (out_capacity < sample_count) {
        return PSA_STATUS_CAPACITY;
    }
    memset(out_values, 0, (size_t)sample_count * sizeof(*out_values));
    for (i = 0U; i < family->component_count; ++i) {
        psa_direct_accumulate_component_sequence(&family->components[i], weights[i], sample_count, out_values);
    }
    return PSA_STATUS_OK;
}

int psa_direct_format_result(char *buffer, size_t buffer_size, const psa_direct_result_t *result) {
    if (buffer == NULL || buffer_size == 0U || result == NULL) {
        return -1;
    }
    if (!result->ready) {
        return snprintf(buffer, buffer_size, "ready=0");
    }
    return snprintf(buffer,
                    buffer_size,
                    "ready=1 order=%lu components=%lu dominant=%ld kind0=%d f0=%.12gHz theta0=%.12g",
                    (unsigned long)result->source_order,
                    (unsigned long)result->component_count,
                    (long)result->dominant_index,
                    (int)result->components[0].kind,
                    result->components[0].frequency_hz,
                    result->components[0].theta);
}

int psa_direct_format_family(char *buffer, size_t buffer_size, const psa_direct_family_t *family) {
    if (buffer == NULL || buffer_size == 0U || family == NULL) {
        return -1;
    }
    if (!family->ready) {
        return snprintf(buffer, buffer_size, "ready=0");
    }
    return snprintf(buffer,
                    buffer_size,
                    "ready=1 components=%lu dominant=%ld primary=%lu harmonic=%lu f_dom=%.12gHz",
                    (unsigned long)family->component_count,
                    (long)family->dominant_index,
                    (unsigned long)family->primary_count,
                    (unsigned long)family->harmonic_count,
                    family->components[family->dominant_index].frequency_hz);
}

int psa_direct_format_sigma(char *buffer, size_t buffer_size, const psa_direct_sigma_t *sigma) {
    if (buffer == NULL || buffer_size == 0U || sigma == NULL) {
        return -1;
    }
    if (!sigma->ready) {
        return snprintf(buffer, buffer_size, "ready=0");
    }
    return snprintf(buffer,
                    buffer_size,
                    "ready=1 start=%lu a=%.12g lambda_count=%lu coeff_count=%lu mode=%s period=%lu",
                    (unsigned long)sigma->start_level,
                    sigma->a,
                    (unsigned long)sigma->lambda_count,
                    (unsigned long)sigma->coeff_count,
                    sigma->constant ? "constant" : ((sigma->period > 0U) ? "periodic" : "free"),
                    (unsigned long)sigma->period);
}

int psa_direct_format_block_family(char *buffer, size_t buffer_size, const psa_direct_block_family_t *family) {
    if (buffer == NULL || buffer_size == 0U || family == NULL) {
        return -1;
    }
    if (!family->ready) {
        return snprintf(buffer, buffer_size, "ready=0");
    }
    return snprintf(buffer,
                    buffer_size,
                    "ready=1 blocks=%lu total_dim=%lu q_class=%s period=%lu dominant=%ld primary=%lu harmonic=%lu",
                    (unsigned long)family->block_count,
                    (unsigned long)family->total_dimension,
                    psa_q_rule_name(family->q_class),
                    (unsigned long)family->period,
                    (long)family->dominant_block,
                    (unsigned long)family->primary_count,
                    (unsigned long)family->harmonic_count);
}

int psa_direct_format_high_block(char *buffer, size_t buffer_size, const psa_direct_high_block_t *block) {
    if (buffer == NULL || buffer_size == 0U || block == NULL) {
        return -1;
    }
    if (!block->ready) {
        return snprintf(buffer, buffer_size, "ready=0");
    }
    return snprintf(buffer,
                    buffer_size,
                    "ready=1 dim=%lu q_class=%s period=%lu weight=%.12g state0=%.12g",
                    (unsigned long)block->dim,
                    psa_q_rule_name(block->q_class),
                    (unsigned long)block->period,
                    block->weight,
                    block->state[0]);
}

int psa_direct_format_connection_object(char *buffer,
                                        size_t buffer_size,
                                        const psa_direct_connection_object_t *object) {
    if (buffer == NULL || buffer_size == 0U || object == NULL) {
        return -1;
    }
    if (!object->ready) {
        return snprintf(buffer, buffer_size, "ready=0");
    }
    return snprintf(buffer,
                    buffer_size,
                    "ready=1 start=%lu levels=%lu parts=%lu total_dim=%lu q_class=%s period=%lu base_blocks=%lu",
                    (unsigned long)object->start_level,
                    (unsigned long)object->level_count,
                    (unsigned long)object->part_count,
                    (unsigned long)object->total_dimension,
                    psa_q_rule_name(object->q_class),
                    (unsigned long)object->period,
                    (unsigned long)object->base_family.block_count);
}
