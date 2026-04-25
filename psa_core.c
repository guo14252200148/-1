#include "psa_core.h"

#include <math.h>
#include <string.h>

#define PSA_PERIOD_LIMIT 8U
#define PSA_RENORM_UPPER_EXP 96
#define PSA_RENORM_LOWER_EXP -96
#define PSA_RENORM_STEP 64
#define PSA_RULE_BLOCK_LIMIT 12U

typedef struct {
    bool active;
    bool oscillatory;
    uint32_t dim;
    psa_real_t matrix[4];
    psa_real_t lambda_re[2];
    psa_real_t lambda_im[2];
    psa_real_t weight;
    psa_q_rule_class_t q_class;
    uint32_t period;
} psa_rule_block_t;

static void psa_history_push(psa_real_t *history, uint32_t *count, psa_real_t value) {
    uint32_t i;
    if (*count < (uint32_t)(PSA_CFG_MAX_ORDER + 1)) {
        history[*count] = value;
        *count += 1U;
        return;
    }
    for (i = 1U; i < *count; ++i) {
        history[i - 1U] = history[i];
    }
    history[*count - 1U] = value;
}

static psa_real_t psa_history_back(const psa_real_t *history, uint32_t count, uint32_t back) {
    if (history == NULL || count == 0U || back == 0U || back > count) {
        return 0.0;
    }
    return history[count - back];
}

static void psa_recurrence_init(psa_recurrence_state_t *state) {
    memset(state, 0, sizeof(*state));
    state->C[0] = 1.0;
    state->B[0] = 1.0;
    state->b = 1.0;
    state->L = 0U;
    state->m = 1U;
}

static psa_status_t psa_update_recurrence(psa_recurrence_state_t *state,
                                          psa_real_t value,
                                          const psa_real_t *history,
                                          uint32_t history_count,
                                          psa_real_t scale) {
    psa_real_t tol;
    psa_real_t d;
    uint32_t i;
    if (state == NULL) {
        return PSA_STATUS_BAD_ARGUMENT;
    }

    tol = psa_real_max(1.0, scale) * 1e-12;
    d = value;
    for (i = 1U; i <= state->L; ++i) {
        d += state->C[i] * psa_history_back(history, history_count, i);
    }

    if (fabs(d) <= tol) {
        state->m += 1U;
        return PSA_STATUS_OK;
    }

    memcpy(state->T, state->C, sizeof(state->C));
    for (i = 0U; i + state->m <= PSA_CFG_MAX_ORDER; ++i) {
        state->C[i + state->m] -= (d / state->b) * state->B[i];
    }

    if (2U * state->L <= history_count) {
        state->L = history_count + 1U - state->L;
        if (state->L > PSA_CFG_MAX_ORDER) {
            return PSA_STATUS_CAPACITY;
        }
        memcpy(state->B, state->T, sizeof(state->B));
        state->b = d;
        state->m = 1U;
    } else {
        state->m += 1U;
    }
    return PSA_STATUS_OK;
}

static bool psa_detect_tail_constant(const psa_real_t *values,
                                     uint32_t count,
                                     psa_real_t tol,
                                     psa_real_t *constant_value) {
    uint32_t need;
    uint32_t start;
    uint32_t i;
    if (count == 0U || values == NULL) {
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

static uint32_t psa_detect_tail_period(const psa_real_t *values, uint32_t count, psa_real_t tol) {
    uint32_t p;
    if (values == NULL) {
        return 0U;
    }
    for (p = 1U; p <= PSA_PERIOD_LIMIT; ++p) {
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

static bool psa_tail_is_zero(const psa_real_t *values,
                             uint32_t count,
                             uint32_t len,
                             psa_real_t scale) {
    uint32_t start;
    uint32_t i;
    psa_real_t tol = psa_real_max(1.0, scale) * 1e-10;
    if (values == NULL || len == 0U || count < len) {
        return false;
    }
    start = count - len;
    for (i = start; i < count; ++i) {
        if (fabs(values[i]) > tol) {
            return false;
        }
    }
    return true;
}

static uint32_t psa_lcm_u32(uint32_t a, uint32_t b) {
    uint32_t x = a;
    uint32_t y = b;
    uint32_t t;
    if (a == 0U) {
        return b;
    }
    if (b == 0U) {
        return a;
    }
    while (y != 0U) {
        t = x % y;
        x = y;
        y = t;
    }
    return (a / x) * b;
}

static void psa_q_certificate_reset(psa_q_certificate_t *certificate) {
    if (certificate != NULL) {
        memset(certificate, 0, sizeof(*certificate));
        certificate->return_start_step = -1;
    }
}

static void psa_core_build_sigma_radial(psa_core_state_t *state) {
    uint32_t start;
    uint32_t end;
    uint32_t i;
    psa_q_certificate_t *cert;
    psa_real_t tol;
    if (state == NULL) {
        return;
    }
    cert = &state->certificate;
    cert->sigma_radial_ready = false;
    cert->sigma_radial_finite_certified = false;
    cert->sigma_start_level = 0U;
    cert->sigma_a = 1.0;
    cert->sigma_coeff_count = 0U;
    cert->sigma_lambda_count = 0U;
    cert->sigma_scale = 1.0;
    cert->sigma_constant = false;
    cert->sigma_constant_value = 0.0;
    cert->sigma_period = 0U;
    cert->sigma_radial_signature = 0ULL;
    memset(cert->sigma_coeff, 0, sizeof(cert->sigma_coeff));
    memset(cert->sigma_lambda, 0, sizeof(cert->sigma_lambda));
    if (state->q_count == 0U) {
        return;
    }
    end = state->q_count - 1U;
    start = end;
    while (start > 0U &&
           state->kernel_step_trace[start] == state->kernel_step_trace[start - 1U] + 1U &&
           end - start + 1U < PSA_CFG_MAX_ORDER) {
        start -= 1U;
    }
    cert->sigma_radial_ready = true;
    cert->sigma_start_level = state->kernel_step_trace[start];
    cert->sigma_a = state->pascal_trace[cert->sigma_start_level];
    cert->sigma_lambda_count = end - start + 1U;
    for (i = 0U; i < cert->sigma_lambda_count; ++i) {
        cert->sigma_lambda[i] = state->kernel_trace[start + i];
        cert->sigma_scale = psa_real_max(cert->sigma_scale, fabs(cert->sigma_lambda[i]));
    }
    cert->sigma_coeff_count = cert->sigma_lambda_count + 1U;
    cert->sigma_coeff[0] = 1.0;
    for (i = 0U; i + 1U < cert->sigma_coeff_count; ++i) {
        uint32_t level = cert->sigma_start_level + i;
        cert->sigma_coeff[i + 1U] = cert->sigma_coeff[i] * cert->sigma_lambda[i] / (psa_real_t)(level + 1U);
    }
    tol = psa_real_max(1.0, cert->sigma_scale) * 1e-12;
    if (cert->sigma_lambda_count == 0U || fabs(cert->sigma_a) <= tol) {
        return;
    }
    cert->sigma_constant = psa_detect_tail_constant(cert->sigma_lambda,
                                                    cert->sigma_lambda_count,
                                                    psa_real_max(1.0, cert->sigma_scale) * 1e-9,
                                                    &cert->sigma_constant_value);
    cert->sigma_period = psa_detect_tail_period(cert->sigma_lambda,
                                                cert->sigma_lambda_count,
                                                psa_real_max(1.0, cert->sigma_scale) * 1e-8);
    cert->sigma_radial_signature = psa_signature_from_reals(cert->sigma_lambda,
                                                            cert->sigma_lambda_count,
                                                            psa_real_max(1.0, cert->sigma_scale));
}

static psa_real_t psa_mat2_identity_error(const psa_real_t *a) {
    psa_real_t e = 0.0;
    e = psa_real_max(e, fabs(a[0] - 1.0));
    e = psa_real_max(e, fabs(a[1]));
    e = psa_real_max(e, fabs(a[2]));
    e = psa_real_max(e, fabs(a[3] - 1.0));
    return e;
}

static void psa_mat2_mul(const psa_real_t *a, const psa_real_t *b, psa_real_t *out) {
    psa_real_t r00 = a[0] * b[0] + a[1] * b[2];
    psa_real_t r01 = a[0] * b[1] + a[1] * b[3];
    psa_real_t r10 = a[2] * b[0] + a[3] * b[2];
    psa_real_t r11 = a[2] * b[1] + a[3] * b[3];
    out[0] = r00;
    out[1] = r01;
    out[2] = r10;
    out[3] = r11;
}

static bool psa_lambda_is_zero(psa_real_t re, psa_real_t im, psa_real_t scale) {
    psa_real_t tol = psa_real_max(1.0, scale) * 1e-10;
    return fabs(re) <= tol && fabs(im) <= tol;
}

static bool psa_lambda_is_unit_root(psa_real_t re, psa_real_t im, uint32_t *period_out) {
    psa_real_t radius2 = re * re + im * im;
    uint32_t p;
    if (fabs(radius2 - 1.0) > 1e-8) {
        return false;
    }
    for (p = 1U; p <= 32U; ++p) {
        psa_real_t zr = 1.0;
        psa_real_t zi = 0.0;
        uint32_t i;
        for (i = 0U; i < p; ++i) {
            psa_real_t nr = zr * re - zi * im;
            psa_real_t ni = zr * im + zi * re;
            zr = nr;
            zi = ni;
        }
        if (fabs(zr - 1.0) <= 1e-8 && fabs(zi) <= 1e-8) {
            *period_out = p;
            return true;
        }
    }
    return false;
}

static bool psa_block_has_finite_order(const psa_rule_block_t *block, uint32_t *period_out) {
    if (block == NULL || period_out == NULL) {
        return false;
    }
    if (block->dim == 1U) {
        return psa_lambda_is_unit_root(block->lambda_re[0], block->lambda_im[0], period_out);
    }
    if (block->dim == 2U) {
        psa_real_t power[4] = {1.0, 0.0, 0.0, 1.0};
        psa_real_t next[4];
        uint32_t m;
        for (m = 1U; m <= 32U; ++m) {
            psa_mat2_mul(power, block->matrix, next);
            memcpy(power, next, sizeof(power));
            if (psa_mat2_identity_error(power) <= 2e-4) {
                *period_out = m;
                return true;
            }
        }
        return false;
    }
    return false;
}

static void psa_rule_block_set_scalar(psa_rule_block_t *block,
                                      psa_real_t lambda_re,
                                      psa_real_t lambda_im,
                                      psa_real_t weight,
                                      psa_real_t scale) {
    uint32_t period = 0U;
    memset(block, 0, sizeof(*block));
    block->active = true;
    block->dim = 1U;
    block->oscillatory = fabs(lambda_im) > 1e-10;
    block->matrix[0] = lambda_re;
    block->lambda_re[0] = lambda_re;
    block->lambda_im[0] = lambda_im;
    block->weight = weight;
    if (psa_lambda_is_zero(lambda_re, lambda_im, scale)) {
        block->q_class = PSA_Q_RULE_ABSORBING;
    } else if (psa_lambda_is_unit_root(lambda_re, lambda_im, &period)) {
        block->q_class = PSA_Q_RULE_FINITE_RETURN;
        block->period = period;
    } else {
        block->q_class = PSA_Q_RULE_NONRETURN;
    }
}

static void psa_rule_block_set_pair(psa_rule_block_t *block,
                                    psa_real_t lambda_re,
                                    psa_real_t lambda_im,
                                    psa_real_t weight,
                                    psa_real_t scale) {
    uint32_t period = 0U;
    memset(block, 0, sizeof(*block));
    block->active = true;
    block->dim = 2U;
    block->oscillatory = fabs(lambda_im) > 1e-10;
    block->matrix[0] = lambda_re;
    block->matrix[1] = -lambda_im;
    block->matrix[2] = lambda_im;
    block->matrix[3] = lambda_re;
    block->lambda_re[0] = lambda_re;
    block->lambda_im[0] = lambda_im;
    block->lambda_re[1] = lambda_re;
    block->lambda_im[1] = -lambda_im;
    block->weight = weight;
    if (psa_lambda_is_zero(lambda_re, lambda_im, scale)) {
        block->q_class = PSA_Q_RULE_ABSORBING;
    } else if (psa_lambda_is_unit_root(lambda_re, lambda_im, &period) || psa_block_has_finite_order(block, &period)) {
        block->q_class = PSA_Q_RULE_FINITE_RETURN;
        block->period = period;
    } else {
        block->q_class = PSA_Q_RULE_NONRETURN;
    }
}

static void psa_core_update_block_certificate(psa_core_state_t *state) {
    psa_q_certificate_t *cert;
    if (state == NULL) {
        return;
    }
    cert = &state->certificate;
    cert->block_rule_active = false;
    cert->block_rule_oscillatory = false;
    cert->block_signature = 0ULL;
    cert->block_class = PSA_Q_RULE_UNKNOWN;
    cert->block_period = 0U;
    memset(cert->block_matrix, 0, sizeof(cert->block_matrix));
    memset(cert->block_state, 0, sizeof(cert->block_state));
    memset(cert->block_lambda_re, 0, sizeof(cert->block_lambda_re));
    memset(cert->block_lambda_im, 0, sizeof(cert->block_lambda_im));
    if (state->pascal_rule.L != 2U || state->pascal_history_count < 2U) {
        return;
    }
    cert->block_rule_active = true;
    cert->block_matrix[0] = 0.0;
    cert->block_matrix[1] = 1.0;
    cert->block_matrix[2] = -state->pascal_rule.C[2];
    cert->block_matrix[3] = -state->pascal_rule.C[1];
    cert->block_state[0] = psa_history_back(state->pascal_history, state->pascal_history_count, 2U);
    cert->block_state[1] = psa_history_back(state->pascal_history, state->pascal_history_count, 1U);
    cert->block_signature = psa_fnv1a_mix(psa_signature_from_reals(cert->block_matrix, 4U, 1.0),
                                          psa_signature_from_reals(cert->block_state, 2U, psa_real_max(1.0, fabs(cert->block_state[1]))));
    {
        psa_real_t c1 = state->pascal_rule.C[1];
        psa_real_t c2 = state->pascal_rule.C[2];
        psa_real_t disc = c1 * c1 - 4.0 * c2;
        if (disc >= 0.0) {
            psa_real_t root_disc = sqrt(disc);
            psa_real_t lambda0 = (-(c1) + root_disc) * 0.5;
            psa_real_t lambda1 = (-(c1) - root_disc) * 0.5;
            cert->block_lambda_re[0] = lambda0;
            cert->block_lambda_re[1] = lambda1;
            if (psa_lambda_is_zero(lambda0, 0.0, 1.0) && psa_lambda_is_zero(lambda1, 0.0, 1.0)) {
                cert->block_class = PSA_Q_RULE_ABSORBING;
            } else {
                uint32_t p0 = 0U;
                uint32_t p1 = 0U;
                if (psa_lambda_is_unit_root(lambda0, 0.0, &p0) && psa_lambda_is_unit_root(lambda1, 0.0, &p1)) {
                    cert->block_class = PSA_Q_RULE_FINITE_RETURN;
                    cert->block_period = psa_real_max((psa_real_t)p0, (psa_real_t)p1);
                } else {
                    cert->block_class = PSA_Q_RULE_NONRETURN;
                }
            }
        } else {
            psa_real_t lambda_re = -c1 * 0.5;
            psa_real_t lambda_im = sqrt(-disc) * 0.5;
            psa_rule_block_t block;
            uint32_t period = 0U;
            cert->block_rule_oscillatory = true;
            cert->block_lambda_re[0] = lambda_re;
            cert->block_lambda_re[1] = lambda_re;
            cert->block_lambda_im[0] = lambda_im;
            cert->block_lambda_im[1] = -lambda_im;
            psa_rule_block_set_pair(&block, lambda_re, lambda_im, 1.0, 1.0);
            cert->block_class = block.q_class;
            if (psa_block_has_finite_order(&block, &period)) {
                cert->block_class = PSA_Q_RULE_FINITE_RETURN;
                cert->block_period = period;
            } else {
                cert->block_period = block.period;
            }
        }
    }
}

static void psa_core_update_multiblock_certificate(psa_core_state_t *state) {
    psa_rule_block_t blocks[PSA_RULE_BLOCK_LIMIT];
    uint32_t block_count = 0U;
    uint32_t total_period = 1U;
    bool all_absorb = true;
    bool all_return = true;
    uint32_t i;
    psa_q_certificate_t *cert;
    if (state == NULL) {
        return;
    }
    cert = &state->certificate;
    cert->multiblock_ready = false;
    cert->multiblock_class = PSA_Q_RULE_UNKNOWN;
    cert->multiblock_period = 0U;
    if (state->q_constant && block_count < PSA_RULE_BLOCK_LIMIT) {
        psa_rule_block_set_scalar(&blocks[block_count],
                                  state->q_constant_value,
                                  0.0,
                                  1.0,
                                  psa_real_max(1.0, fabs(state->q_constant_value)));
        block_count += 1U;
    }
    if (cert->block_rule_active && cert->block_rule_oscillatory && block_count < PSA_RULE_BLOCK_LIMIT) {
        psa_rule_block_set_pair(&blocks[block_count],
                                cert->block_matrix[0],
                                fabs(cert->block_matrix[2]),
                                1.0,
                                1.0);
        block_count += 1U;
    } else if (cert->block_rule_active && cert->block_class != PSA_Q_RULE_UNKNOWN && block_count < PSA_RULE_BLOCK_LIMIT) {
        psa_rule_block_set_scalar(&blocks[block_count],
                                  cert->block_matrix[3],
                                  0.0,
                                  1.0,
                                  1.0);
        blocks[block_count].q_class = cert->block_class;
        blocks[block_count].period = cert->block_period;
        block_count += 1U;
    }
    if (block_count == 0U) {
        return;
    }
    cert->multiblock_ready = true;
    cert->block_count = block_count;
    for (i = 0U; i < block_count; ++i) {
        uint32_t period = 0U;
        if (blocks[i].q_class != PSA_Q_RULE_ABSORBING && psa_block_has_finite_order(&blocks[i], &period)) {
            blocks[i].q_class = PSA_Q_RULE_FINITE_RETURN;
            blocks[i].period = period;
        }
        if (blocks[i].q_class != PSA_Q_RULE_ABSORBING) {
            all_absorb = false;
        }
        if (blocks[i].q_class == PSA_Q_RULE_FINITE_RETURN) {
            total_period = psa_lcm_u32(total_period, blocks[i].period);
        } else if (blocks[i].q_class != PSA_Q_RULE_ABSORBING) {
            all_return = false;
        }
    }
    if (all_absorb) {
        cert->multiblock_class = PSA_Q_RULE_ABSORBING;
    } else if (all_return) {
        cert->multiblock_class = PSA_Q_RULE_FINITE_RETURN;
        cert->multiblock_period = total_period;
    } else {
        cert->multiblock_class = PSA_Q_RULE_NONRETURN;
    }
}

static int psa_rescale_step(psa_real_t max_abs) {
    int exponent;
    int step = 0;
    if (max_abs <= 0.0) {
        return 0;
    }
    (void)frexp(max_abs, &exponent);
    while (exponent > PSA_RENORM_UPPER_EXP) {
        exponent -= PSA_RENORM_STEP;
        step -= PSA_RENORM_STEP;
    }
    while (exponent < PSA_RENORM_LOWER_EXP) {
        exponent += PSA_RENORM_STEP;
        step += PSA_RENORM_STEP;
    }
    return step;
}

static void psa_core_rescale_signal(psa_core_state_t *state, int step) {
    if (state == NULL || step == 0) {
        return;
    }
    psa_scale_pow2_real_array(state->signal_history, state->signal_history_count, step);
    state->signal_rule.b = psa_scale_pow2_real(state->signal_rule.b, step);
    state->signal_pow2_exponent -= step;
}

static void psa_core_rescale_pascal(psa_core_state_t *state, int step) {
    if (state == NULL || step == 0) {
        return;
    }
    psa_scale_pow2_dd_array(state->frontier, state->sample_count, step);
    psa_scale_pow2_real_array(state->pascal_history, state->pascal_history_count, step);
    state->pascal_rule.b = psa_scale_pow2_real(state->pascal_rule.b, step);
    state->pascal_pow2_exponent -= step;
}

static void psa_core_update_signatures(psa_core_state_t *state) {
    psa_real_t normalized_state[PSA_CFG_MAX_ORDER + 1];
    psa_real_t denom = 1.0;
    uint32_t state_len;
    uint32_t i;
    if (state == NULL) {
        return;
    }

    state_len = state->pascal_rule.L;
    if (state_len > state->pascal_history_count) {
        state_len = state->pascal_history_count;
    }
    for (i = 0U; i < state_len; ++i) {
        psa_real_t value = psa_history_back(state->pascal_history, state->pascal_history_count, state_len - i);
        denom = psa_real_max(denom, fabs(value));
        normalized_state[i] = value;
    }
    for (i = 0U; i < state_len; ++i) {
        normalized_state[i] /= denom;
    }

    state->sigma_signature = psa_signature_from_reals(state->pascal_rule.C,
                                                      (size_t)state->pascal_rule.L + 1U,
                                                      1.0);
    state->state_signature = psa_signature_from_reals(normalized_state, state_len, 1.0);
    state->combined_signature = psa_fnv1a_mix(state->sigma_signature, state->state_signature);
}

static void psa_core_history_signature_push(psa_core_state_t *state, uint64_t signature, int32_t step) {
    uint32_t i;
    if (state == NULL) {
        return;
    }
    if (state->cert_history_count < PSA_CFG_CERT_HISTORY) {
        state->cert_history_signatures[state->cert_history_count] = signature;
        state->cert_history_steps[state->cert_history_count] = step;
        state->cert_history_count += 1U;
        return;
    }
    for (i = 1U; i < state->cert_history_count; ++i) {
        state->cert_history_signatures[i - 1U] = state->cert_history_signatures[i];
        state->cert_history_steps[i - 1U] = state->cert_history_steps[i];
    }
    state->cert_history_signatures[state->cert_history_count - 1U] = signature;
    state->cert_history_steps[state->cert_history_count - 1U] = step;
}

static void psa_core_update_q_mode(psa_core_state_t *state) {
    psa_real_t scale = 1.0;
    uint32_t i;
    if (state == NULL || state->q_history_count == 0U) {
        return;
    }
    for (i = 0U; i < state->q_history_count; ++i) {
        scale = psa_real_max(scale, fabs(state->q_history[i]));
    }
    state->q_constant = psa_detect_tail_constant(state->q_history,
                                                 state->q_history_count,
                                                 scale * 1e-10,
                                                 &state->q_constant_value);
    state->q_period = psa_detect_tail_period(state->q_history,
                                             state->q_history_count,
                                             scale * 1e-9);
}

static void psa_core_update_kernel_mode(psa_core_state_t *state) {
    psa_real_t scale = 1.0;
    uint32_t i;
    if (state == NULL || state->kernel_history_count == 0U) {
        return;
    }
    for (i = 0U; i < state->kernel_history_count; ++i) {
        scale = psa_real_max(scale, fabs(state->kernel_history[i]));
    }
    state->kernel_constant = psa_detect_tail_constant(state->kernel_history,
                                                      state->kernel_history_count,
                                                      scale * 1e-10,
                                                      &state->kernel_constant_value);
    state->kernel_period = psa_detect_tail_period(state->kernel_history,
                                                  state->kernel_history_count,
                                                  scale * 1e-9);
}

static void psa_core_update_certificate(psa_core_state_t *state) {
    psa_real_t pascal_scale = 1.0;
    psa_q_certificate_t *cert;
    uint64_t signature_hash = 1469598103934665603ULL;
    uint32_t i;
    if (state == NULL) {
        return;
    }
    cert = &state->certificate;
    psa_q_certificate_reset(cert);
    cert->ready = (state->sample_count > 0U);
    cert->sigma_signature = state->sigma_signature;
    cert->state_signature = state->state_signature;
    cert->combined_signature = state->combined_signature;

    for (i = 0U; i < state->pascal_history_count; ++i) {
        pascal_scale = psa_real_max(pascal_scale, fabs(state->pascal_history[i]));
    }
    psa_core_build_sigma_radial(state);
    state->combined_signature = psa_fnv1a_mix(state->combined_signature, cert->sigma_radial_signature);
    cert->combined_signature = state->combined_signature;
    psa_core_update_block_certificate(state);
    psa_core_update_multiblock_certificate(state);
    cert->q_signature = psa_signature_from_reals(state->q_rule_state.C,
                                                 (size_t)state->q_rule_state.L + 1U,
                                                 1.0);

    if (psa_tail_is_zero(state->pascal_history,
                         state->pascal_history_count,
                         (state->pascal_rule.L > 0U) ? state->pascal_rule.L : 1U,
                         pascal_scale)) {
        cert->q_class = PSA_Q_RULE_ABSORBING;
        cert->absorb_certified = true;
        state->q_class = cert->q_class;
        goto finalize;
    }
    if (cert->sigma_radial_ready &&
        cert->sigma_period > 0U &&
        cert->sigma_lambda_count >= 2U * cert->sigma_period + 1U) {
        cert->q_class = PSA_Q_RULE_FINITE_RETURN;
        cert->finite_return_certified = true;
        cert->sigma_radial_finite_certified = true;
        cert->return_period = cert->sigma_period;
        cert->return_start_step = (int32_t)(cert->sigma_start_level + cert->sigma_lambda_count - 2U * cert->sigma_period);
        state->q_class = cert->q_class;
        goto finalize;
    }
    if (cert->block_rule_active && cert->block_rule_oscillatory) {
        if (cert->block_class == PSA_Q_RULE_ABSORBING) {
            cert->q_class = PSA_Q_RULE_ABSORBING;
            cert->absorb_certified = true;
            cert->kernel_lift_certified = true;
            cert->kernel_lift_multiblock = false;
            cert->kernel_lift_class = PSA_Q_RULE_ABSORBING;
            state->q_class = cert->q_class;
            goto finalize;
        }
        if (cert->block_class == PSA_Q_RULE_FINITE_RETURN) {
            cert->q_class = PSA_Q_RULE_FINITE_RETURN;
            cert->finite_return_certified = true;
            cert->return_period = cert->block_period;
            cert->return_start_step = (int32_t)(state->sample_count - ((state->pascal_rule.L < state->sample_count) ? state->pascal_rule.L : state->sample_count));
            cert->kernel_lift_certified = true;
            cert->kernel_lift_multiblock = false;
            cert->kernel_lift_class = PSA_Q_RULE_FINITE_RETURN;
            state->q_class = cert->q_class;
            goto finalize;
        }
        if (cert->block_class == PSA_Q_RULE_NONRETURN) {
            cert->q_class = PSA_Q_RULE_NONRETURN;
            cert->nonreturn_certified = true;
            cert->kernel_lift_certified = true;
            cert->kernel_lift_multiblock = false;
            cert->kernel_lift_class = PSA_Q_RULE_NONRETURN;
            state->q_class = cert->q_class;
            goto finalize;
        }
    }
    if (state->pascal_history_count >= 3U) {
        uint32_t p_period = psa_detect_tail_period(state->pascal_trace,
                                                   state->sample_count,
                                                   psa_real_max(1.0, pascal_scale) * 5e-10);
        if (p_period > 0U && state->sample_count >= 3U * p_period) {
            cert->q_class = PSA_Q_RULE_FINITE_RETURN;
            cert->finite_return_certified = true;
            cert->return_period = p_period;
            cert->return_start_step = (int32_t)(state->sample_count - 2U * p_period);
            state->q_class = cert->q_class;
            goto finalize;
        }
    }
    if (state->pascal_rule.L > 0U && state->pascal_history_count >= state->pascal_rule.L) {
        for (i = 0U; i < state->cert_history_count; ++i) {
            if (state->cert_history_signatures[i] == state->combined_signature) {
                cert->q_class = PSA_Q_RULE_FINITE_RETURN;
                cert->finite_return_certified = true;
                cert->return_start_step = state->cert_history_steps[i];
                cert->return_period = state->sample_count - (uint32_t)state->cert_history_steps[i];
                state->q_class = cert->q_class;
                goto finalize;
            }
        }
        psa_core_history_signature_push(state, state->combined_signature, (int32_t)state->sample_count);
    }
    if (cert->multiblock_ready) {
        if (cert->multiblock_class == PSA_Q_RULE_ABSORBING) {
            cert->q_class = PSA_Q_RULE_ABSORBING;
            cert->absorb_certified = true;
            cert->kernel_lift_certified = true;
            cert->kernel_lift_multiblock = true;
            cert->kernel_lift_class = PSA_Q_RULE_ABSORBING;
            state->q_class = cert->q_class;
            goto finalize;
        }
        if (cert->multiblock_class == PSA_Q_RULE_FINITE_RETURN) {
            cert->q_class = PSA_Q_RULE_FINITE_RETURN;
            cert->finite_return_certified = true;
            cert->return_period = cert->multiblock_period;
            cert->kernel_lift_certified = true;
            cert->kernel_lift_multiblock = true;
            cert->kernel_lift_class = PSA_Q_RULE_FINITE_RETURN;
            state->q_class = cert->q_class;
            goto finalize;
        }
        if (cert->multiblock_class == PSA_Q_RULE_NONRETURN) {
            cert->q_class = PSA_Q_RULE_NONRETURN;
            cert->nonreturn_certified = true;
            cert->kernel_lift_certified = true;
            cert->kernel_lift_multiblock = true;
            cert->kernel_lift_class = PSA_Q_RULE_NONRETURN;
            state->q_class = cert->q_class;
            goto finalize;
        }
    }
    if (state->kernel_period > 0U && state->kernel_history_count >= 2U * state->kernel_period) {
        cert->q_class = PSA_Q_RULE_FINITE_RETURN;
        state->q_class = cert->q_class;
        goto finalize;
    }
    if (state->q_period > 0U && state->q_history_count >= 2U * state->q_period) {
        cert->q_class = PSA_Q_RULE_FINITE_RETURN;
        state->q_class = cert->q_class;
        goto finalize;
    }
    if (state->q_count > 0U) {
        cert->q_class = PSA_Q_RULE_NONRETURN;
        state->q_class = cert->q_class;
        goto finalize;
    }
    cert->q_class = PSA_Q_RULE_UNKNOWN;
    state->q_class = cert->q_class;
finalize:
    signature_hash = psa_fnv1a_mix(signature_hash, cert->q_signature);
    signature_hash = psa_fnv1a_mix(signature_hash, (uint64_t)cert->q_class);
    signature_hash = psa_fnv1a_mix(signature_hash, cert->absorb_certified ? 1ULL : 0ULL);
    signature_hash = psa_fnv1a_mix(signature_hash, cert->finite_return_certified ? 1ULL : 0ULL);
    signature_hash = psa_fnv1a_mix(signature_hash, cert->nonreturn_certified ? 1ULL : 0ULL);
    signature_hash = psa_fnv1a_mix(signature_hash, cert->kernel_lift_certified ? 1ULL : 0ULL);
    signature_hash = psa_fnv1a_mix(signature_hash, (uint64_t)cert->kernel_lift_class);
    signature_hash = psa_fnv1a_mix(signature_hash, (uint64_t)cert->return_period);
    signature_hash = psa_fnv1a_mix(signature_hash, (uint64_t)(cert->return_start_step + 1));
    signature_hash = psa_fnv1a_mix(signature_hash, cert->sigma_radial_signature);
    signature_hash = psa_fnv1a_mix(signature_hash, cert->block_signature);
    signature_hash = psa_fnv1a_mix(signature_hash, (uint64_t)cert->multiblock_class);
    signature_hash = psa_fnv1a_mix(signature_hash, (uint64_t)cert->multiblock_period);
    cert->signature = signature_hash;
}

static void psa_core_commit_page(psa_core_state_t *state) {
    psa_page_summary_t *page;
    uint32_t slot;
    if (state == NULL) {
        return;
    }
    if ((state->sample_count % PSA_CFG_PAGE_CAPACITY) != 0U) {
        return;
    }
    slot = state->page_write_index;
    page = &state->pages[slot];
    page->page_index = state->page_total_count;
    page->sample_count = state->sample_count;
    page->signal_pow2_exponent = state->signal_pow2_exponent;
    page->pascal_pow2_exponent = state->pascal_pow2_exponent;
    page->signal_order = state->signal_rule.L;
    page->pascal_order = state->pascal_rule.L;
    page->combined_signature = state->combined_signature;
    page->q_class = state->q_class;
    state->page_write_index = (slot + 1U) % PSA_CFG_MAX_PAGES;
    if (state->page_count < PSA_CFG_MAX_PAGES) {
        state->page_count += 1U;
    }
    state->page_total_count += 1U;
}

void psa_core_init(psa_core_state_t *state) {
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->max_samples = PSA_CFG_MAX_FRONTIER;
    psa_recurrence_init(&state->signal_rule);
    psa_recurrence_init(&state->pascal_rule);
    psa_recurrence_init(&state->q_rule_state);
}

psa_status_t psa_core_push_packet(psa_core_state_t *state, const psa_collect_packet_t *packet) {
    if (packet == NULL) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    return psa_core_push_value(state, packet->value);
}

psa_status_t psa_core_push_value(psa_core_state_t *state, psa_real_t value) {
    psa_real_t signal_value;
    psa_real_t frontier_input;
    psa_real_t pascal_value;
    psa_real_t frontier_max = 0.0;
    psa_real_t signal_scale = 1.0;
    psa_real_t pascal_scale = 1.0;
    uint32_t n;
    uint32_t i;
    psa_status_t status;

    if (state == NULL) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    if (state->sample_count >= state->max_samples) {
        return PSA_STATUS_CAPACITY;
    }

    signal_value = psa_scale_pow2_real(value, -state->signal_pow2_exponent);
    {
        int step = psa_rescale_step(fabs(signal_value));
        if (step != 0) {
            psa_core_rescale_signal(state, step);
            signal_value = psa_scale_pow2_real(signal_value, step);
        }
    }
    frontier_input = psa_scale_pow2_real(value, -state->pascal_pow2_exponent);
    n = state->sample_count;
    state->frontier[n] = psa_dd_from_real(frontier_input);
    frontier_max = fabs(frontier_input);
    while (n > 0U) {
        state->frontier[n - 1U] = psa_dd_sub(state->frontier[n], state->frontier[n - 1U]);
        frontier_max = psa_real_max(frontier_max, fabs(psa_dd_to_real(state->frontier[n - 1U])));
        n -= 1U;
    }
    pascal_value = psa_dd_to_real(state->frontier[0]);
    {
        int step = psa_rescale_step(frontier_max);
        if (step != 0) {
            psa_core_rescale_pascal(state, step);
            pascal_value = psa_scale_pow2_real(pascal_value, step);
            frontier_max = psa_scale_pow2_real(frontier_max, step);
        }
    }

    for (i = 0U; i < state->signal_history_count; ++i) {
        signal_scale = psa_real_max(signal_scale, fabs(state->signal_history[i]));
    }
    signal_scale = psa_real_max(signal_scale, fabs(signal_value));

    for (i = 0U; i < state->pascal_history_count; ++i) {
        pascal_scale = psa_real_max(pascal_scale, fabs(state->pascal_history[i]));
    }
    pascal_scale = psa_real_max(pascal_scale, fabs(pascal_value));

    status = psa_update_recurrence(&state->signal_rule,
                                   signal_value,
                                   state->signal_history,
                                   state->signal_history_count,
                                   signal_scale);
    if (status != PSA_STATUS_OK) {
        return status;
    }
    status = psa_update_recurrence(&state->pascal_rule,
                                   pascal_value,
                                   state->pascal_history,
                                   state->pascal_history_count,
                                   pascal_scale);
    if (status != PSA_STATUS_OK) {
        return status;
    }

    psa_history_push(state->signal_history, &state->signal_history_count, signal_value);
    psa_history_push(state->pascal_history, &state->pascal_history_count, pascal_value);
    state->sample_trace[state->sample_count] = psa_scale_pow2_real(signal_value, state->signal_pow2_exponent);
    state->pascal_trace[state->sample_count] = psa_scale_pow2_real(pascal_value, state->pascal_pow2_exponent);
    state->sample_count += 1U;

    if (state->sample_count >= 2U) {
        psa_real_t prev_pascal = psa_history_back(state->pascal_history, state->pascal_history_count, 2U);
        psa_real_t tol = psa_real_max(1.0, pascal_scale) * 1e-18;
        state->q_last_defined = false;
        if (fabs(prev_pascal) > tol) {
            psa_real_t q_value = pascal_value / prev_pascal;
            psa_real_t kernel_value = (psa_real_t)(state->sample_count - 1U) * q_value;
            psa_real_t q_scale = 1.0;
            uint32_t j;

            for (j = 0U; j < state->q_history_count; ++j) {
                q_scale = psa_real_max(q_scale, fabs(state->q_history[j]));
            }
            q_scale = psa_real_max(q_scale, fabs(q_value));
            status = psa_update_recurrence(&state->q_rule_state,
                                           q_value,
                                           state->q_history,
                                           state->q_history_count,
                                           q_scale);
            if (status != PSA_STATUS_OK) {
                return status;
            }

            psa_history_push(state->q_history, &state->q_history_count, q_value);
            psa_history_push(state->kernel_history, &state->kernel_history_count, kernel_value);
            state->q_trace[state->q_count] = q_value;
            state->q_step_trace[state->q_count] = state->sample_count - 1U;
            state->kernel_trace[state->q_count] = kernel_value;
            state->kernel_step_trace[state->q_count] = state->sample_count - 1U;
            state->q_count += 1U;
            state->q_last_defined = true;
            state->q_last = q_value;
        }
    }

    psa_core_update_q_mode(state);
    psa_core_update_kernel_mode(state);
    psa_core_update_signatures(state);
    psa_core_update_certificate(state);
    psa_core_commit_page(state);

    return PSA_STATUS_OK;
}

void psa_core_get_summary(const psa_core_state_t *state, psa_summary_t *summary) {
    if (state == NULL || summary == NULL) {
        return;
    }
    psa_summary_reset(summary);
    summary->ready = (state->sample_count > 0U);
    summary->sample_count = state->sample_count;
    summary->page_count = state->page_count;
    summary->total_page_count = state->page_total_count;
    summary->signal_order = state->signal_rule.L;
    summary->pascal_order = state->pascal_rule.L;
    summary->q_order = state->q_rule_state.L;
    summary->q_count = state->q_count;
    summary->kernel_count = state->kernel_history_count;
    summary->signal_pow2_exponent = state->signal_pow2_exponent;
    summary->pascal_pow2_exponent = state->pascal_pow2_exponent;
    summary->latest_sample = (state->signal_history_count > 0U)
        ? psa_scale_pow2_real(psa_history_back(state->signal_history, state->signal_history_count, 1U),
                              state->signal_pow2_exponent)
        : 0.0;
    summary->latest_pascal = (state->pascal_history_count > 0U)
        ? psa_scale_pow2_real(psa_history_back(state->pascal_history, state->pascal_history_count, 1U),
                              state->pascal_pow2_exponent)
        : 0.0;
    summary->latest_q = state->q_last;
    summary->q_class = state->q_class;
    summary->q_defined = state->q_last_defined;
    summary->q_constant = state->q_constant;
    summary->q_constant_value = state->q_constant_value;
    summary->q_period = state->q_period;
    summary->kernel_constant = state->kernel_constant;
    summary->kernel_constant_value = state->kernel_constant_value;
    summary->kernel_period = state->kernel_period;
    summary->sigma_signature = state->sigma_signature;
    summary->state_signature = state->state_signature;
    summary->combined_signature = state->combined_signature;
    summary->certificate = state->certificate;
}

void psa_core_get_certificate(const psa_core_state_t *state, psa_q_certificate_t *certificate) {
    if (state == NULL || certificate == NULL) {
        return;
    }
    *certificate = state->certificate;
}

bool psa_core_get_last_page(const psa_core_state_t *state, psa_page_summary_t *summary) {
    uint32_t index;
    if (state == NULL || summary == NULL || state->page_count == 0U) {
        return false;
    }
    index = (state->page_write_index + PSA_CFG_MAX_PAGES - 1U) % PSA_CFG_MAX_PAGES;
    *summary = state->pages[index];
    return true;
}
