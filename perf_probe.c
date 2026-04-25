#include "src/psa_core.h"
#include "src/psa_direct.h"
#include "src/psa_explain.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define PROBE_MAX_SAMPLES 256U
#define CORE_PROBE_LOOPS 120U
#define DIRECT_PROBE_LOOPS 4000U
#define SEGMENT_PROBE_LOOPS 200U

#ifndef PROBE_PI
#define PROBE_PI 3.14159265358979323846
#endif

typedef struct {
    psa_real_t ref_scale;
    psa_real_t max_abs;
    psa_real_t rms_abs;
    psa_real_t max_global_scaled;
    psa_real_t max_local_scaled;
    uint32_t sample_count;
    uint32_t locally_scaled_count;
} probe_error_summary_t;

typedef struct {
    const char *name;
    const char *path;
    const char *rate_unit;
    double throughput;
    probe_error_summary_t error;
    char class_text[96];
    char detail[256];
} probe_case_result_t;

typedef void (*probe_generator_fn)(psa_real_t *out, uint32_t count);

static void measure_probe_error(const psa_real_t *reference,
                                const psa_real_t *candidate,
                                uint32_t count,
                                probe_error_summary_t *summary) {
    psa_real_t ref_scale = 1.0;
    psa_real_t sum_sq = 0.0;
    psa_real_t active_threshold;
    uint32_t i;
    if (summary == NULL) {
        return;
    }
    memset(summary, 0, sizeof(*summary));
    if (reference == NULL || candidate == NULL || count == 0U) {
        return;
    }
    for (i = 0U; i < count; ++i) {
        ref_scale = psa_real_max(ref_scale, psa_real_abs(reference[i]));
    }
    active_threshold = psa_real_scaled_epsilon(ref_scale) * 1024.0;
    summary->ref_scale = ref_scale;
    summary->sample_count = count;
    for (i = 0U; i < count; ++i) {
        psa_real_t err = psa_real_abs(reference[i] - candidate[i]);
        psa_real_t local_scale = psa_real_max(psa_real_abs(reference[i]), psa_real_abs(candidate[i]));
        psa_real_t global_scaled = err / psa_real_scaled_epsilon(ref_scale);
        if (err > summary->max_abs) {
            summary->max_abs = err;
        }
        if (global_scaled > summary->max_global_scaled) {
            summary->max_global_scaled = global_scaled;
        }
        if (local_scale > active_threshold) {
            psa_real_t local_scaled = err / psa_real_scaled_epsilon(local_scale);
            if (local_scaled > summary->max_local_scaled) {
                summary->max_local_scaled = local_scaled;
            }
            summary->locally_scaled_count += 1U;
        }
        sum_sq += err * err;
    }
    summary->rms_abs = sqrt(sum_sq / (psa_real_t)count);
}

static void generate_absorbing_samples(psa_real_t *out, uint32_t count) {
    uint32_t i;
    for (i = 0U; i < count; ++i) {
        out[i] = 0.0;
    }
}

static void fill_rule_from_cosine(psa_explain_rule_t *rule,
                                  psa_real_t amplitude,
                                  psa_real_t theta) {
    psa_explain_rule_reset(rule);
    rule->defined = true;
    rule->kind = PSA_EXPLAIN_RULE_SIGNAL;
    rule->order = 2U;
    rule->seed_count = 2U;
    rule->coefficients[0] = 1.0;
    rule->coefficients[1] = (psa_real_t)(-2.0 * cos(theta));
    rule->coefficients[2] = 1.0;
    rule->seeds[0] = amplitude;
    rule->seeds[1] = (psa_real_t)(amplitude * cos(theta));
    rule->scale = psa_real_max(1.0, psa_real_abs(amplitude));
    rule->signature = psa_signature_from_reals(rule->coefficients, 3U, rule->scale);
}

static void generate_cosine_expected(psa_real_t *out,
                                     uint32_t count,
                                     psa_real_t amplitude,
                                     psa_real_t theta) {
    uint32_t i;
    for (i = 0U; i < count; ++i) {
        out[i] = (psa_real_t)(amplitude * cos(theta * (psa_real_t)i));
    }
}

static void build_segment_record(psa_analysis_record_t *record,
                                 uint32_t sample_count,
                                 psa_real_t alpha,
                                 psa_real_t x0,
                                 psa_real_t q_value) {
    uint32_t i;
    psa_real_t value = x0;
    uint64_t hash = 1469598103934665603ULL;

    psa_analysis_record_reset(record);
    psa_explain_rule_reset(&record->signal_rule);
    psa_explain_rule_reset(&record->pascal_rule);
    psa_explain_rule_reset(&record->q_rule);

    record->ready = true;
    record->summary.ready = true;
    record->summary.sample_count = sample_count;
    record->summary.signal_order = 1U;
    record->summary.pascal_order = 1U;
    record->summary.q_order = 1U;
    record->summary.q_count = sample_count;
    record->summary.kernel_count = sample_count;
    record->summary.q_defined = true;
    record->summary.q_class = PSA_Q_RULE_NONRETURN;

    record->signal_rule.defined = true;
    record->signal_rule.kind = PSA_EXPLAIN_RULE_SIGNAL;
    record->signal_rule.order = 1U;
    record->signal_rule.seed_count = 1U;
    record->signal_rule.coefficients[0] = 1.0;
    record->signal_rule.coefficients[1] = -alpha;
    record->signal_rule.seeds[0] = x0;
    record->signal_rule.scale = psa_real_abs(x0);
    record->signal_rule.signature = psa_signature_from_reals(record->signal_rule.coefficients, 2U, 1.0);

    record->pascal_rule = record->signal_rule;
    record->pascal_rule.kind = PSA_EXPLAIN_RULE_PASCAL;

    record->q_rule.defined = true;
    record->q_rule.kind = PSA_EXPLAIN_RULE_Q;
    record->q_rule.order = 1U;
    record->q_rule.seed_count = 1U;
    record->q_rule.coefficients[0] = 1.0;
    record->q_rule.coefficients[1] = -1.0;
    record->q_rule.seeds[0] = q_value;
    record->q_rule.scale = psa_real_abs(q_value);
    record->q_rule.signature = psa_signature_from_reals(record->q_rule.coefficients, 2U, 1.0);

    record->certificate.ready = true;
    record->certificate.q_class = PSA_Q_RULE_NONRETURN;
    record->certificate.sigma_signature = record->pascal_rule.signature;
    record->certificate.state_signature = psa_signature_from_reals(record->signal_rule.seeds, 1U, record->signal_rule.scale);
    record->certificate.combined_signature = psa_fnv1a_mix(record->certificate.sigma_signature,
                                                           record->certificate.state_signature);
    record->certificate.q_signature = record->q_rule.signature;
    record->certificate.nonreturn_certified = true;
    record->certificate.return_start_step = -1;
    record->certificate.sigma_radial_ready = true;
    record->certificate.sigma_start_level = 0U;
    record->certificate.sigma_a = x0;
    record->certificate.sigma_coeff_count = 2U;
    record->certificate.sigma_coeff[0] = 1.0;
    record->certificate.sigma_coeff[1] = q_value;
    record->certificate.sigma_lambda_count = 1U;
    record->certificate.sigma_lambda[0] = q_value;
    record->certificate.sigma_scale = psa_real_max(1.0, psa_real_abs(q_value));
    record->certificate.sigma_constant = true;
    record->certificate.sigma_constant_value = q_value;
    record->certificate.sigma_radial_signature = psa_signature_from_reals(record->certificate.sigma_lambda, 1U,
                                                                          record->certificate.sigma_scale);
    hash = psa_fnv1a_mix(hash, record->certificate.q_signature);
    hash = psa_fnv1a_mix(hash, (uint64_t)record->certificate.q_class);
    hash = psa_fnv1a_mix(hash, record->certificate.sigma_radial_signature);
    hash = psa_fnv1a_mix(hash, 1ULL);
    record->certificate.signature = hash;
    record->summary.sigma_signature = record->certificate.sigma_signature;
    record->summary.state_signature = record->certificate.state_signature;
    record->summary.combined_signature = record->certificate.combined_signature;
    record->summary.certificate = record->certificate;

    record->trace_count = sample_count;
    record->q_trace_count = sample_count;
    record->q_constant = true;
    record->q_constant_value = q_value;
    record->q_period = 1U;
    record->kernel_constant = true;
    record->kernel_constant_value = q_value;
    record->kernel_period = 1U;
    for (i = 0U; i < sample_count; ++i) {
        record->sample_trace[i] = value;
        record->pascal_trace[i] = value;
        record->q_trace[i] = q_value;
        record->q_step_trace[i] = i;
        record->kernel_trace[i] = q_value;
        record->kernel_step_trace[i] = i;
        value *= alpha;
    }
}

static void build_finite_return_record(psa_analysis_record_t *record, uint32_t sample_count) {
    uint32_t i;
    uint64_t hash = 1469598103934665603ULL;
    psa_analysis_record_reset(record);
    psa_explain_rule_reset(&record->signal_rule);
    psa_explain_rule_reset(&record->pascal_rule);
    psa_explain_rule_reset(&record->q_rule);

    record->ready = true;
    record->summary.ready = true;
    record->summary.sample_count = sample_count;
    record->summary.signal_order = 1U;
    record->summary.pascal_order = 1U;
    record->summary.q_order = 1U;
    record->summary.q_count = sample_count;
    record->summary.kernel_count = sample_count;
    record->summary.q_defined = true;
    record->summary.q_class = PSA_Q_RULE_FINITE_RETURN;

    record->signal_rule.defined = true;
    record->signal_rule.kind = PSA_EXPLAIN_RULE_SIGNAL;
    record->signal_rule.order = 1U;
    record->signal_rule.seed_count = 1U;
    record->signal_rule.coefficients[0] = 1.0;
    record->signal_rule.coefficients[1] = -1.0;
    record->signal_rule.seeds[0] = 1.0;
    record->signal_rule.scale = 1.0;
    record->signal_rule.signature = psa_signature_from_reals(record->signal_rule.coefficients, 2U, 1.0);

    record->pascal_rule = record->signal_rule;
    record->pascal_rule.kind = PSA_EXPLAIN_RULE_PASCAL;

    record->q_rule = record->signal_rule;
    record->q_rule.kind = PSA_EXPLAIN_RULE_Q;

    record->certificate.ready = true;
    record->certificate.q_class = PSA_Q_RULE_FINITE_RETURN;
    record->certificate.sigma_signature = record->pascal_rule.signature;
    record->certificate.state_signature = psa_signature_from_reals(record->signal_rule.seeds, 1U, 1.0);
    record->certificate.combined_signature = psa_fnv1a_mix(record->certificate.sigma_signature,
                                                           record->certificate.state_signature);
    record->certificate.q_signature = record->q_rule.signature;
    record->certificate.return_period = 1U;
    record->certificate.return_start_step = 0;
    record->certificate.finite_return_certified = true;
    record->certificate.sigma_radial_ready = true;
    record->certificate.sigma_radial_finite_certified = true;
    record->certificate.sigma_start_level = 0U;
    record->certificate.sigma_a = 1.0;
    record->certificate.sigma_coeff_count = 2U;
    record->certificate.sigma_coeff[0] = 1.0;
    record->certificate.sigma_coeff[1] = 1.0;
    record->certificate.sigma_lambda_count = 1U;
    record->certificate.sigma_lambda[0] = 1.0;
    record->certificate.sigma_scale = 1.0;
    record->certificate.sigma_constant = true;
    record->certificate.sigma_constant_value = 1.0;
    record->certificate.sigma_period = 1U;
    record->certificate.sigma_radial_signature = psa_signature_from_reals(record->certificate.sigma_lambda, 1U, 1.0);
    hash = psa_fnv1a_mix(hash, record->certificate.q_signature);
    hash = psa_fnv1a_mix(hash, (uint64_t)record->certificate.q_class);
    hash = psa_fnv1a_mix(hash, record->certificate.sigma_radial_signature);
    hash = psa_fnv1a_mix(hash, 1ULL);
    record->certificate.signature = hash;
    record->summary.sigma_signature = record->certificate.sigma_signature;
    record->summary.state_signature = record->certificate.state_signature;
    record->summary.combined_signature = record->certificate.combined_signature;
    record->summary.certificate = record->certificate;

    record->trace_count = sample_count;
    record->q_trace_count = sample_count;
    record->q_constant = true;
    record->q_constant_value = 1.0;
    record->q_period = 1U;
    record->kernel_constant = true;
    record->kernel_constant_value = 1.0;
    record->kernel_period = 1U;
    for (i = 0U; i < sample_count; ++i) {
        record->sample_trace[i] = 1.0;
        record->pascal_trace[i] = 1.0;
        record->q_trace[i] = 1.0;
        record->q_step_trace[i] = i;
        record->kernel_trace[i] = 1.0;
        record->kernel_step_trace[i] = i;
    }
}

static int run_core_rebuild_case(const char *name,
                                 probe_generator_fn generator,
                                 uint32_t sample_count,
                                 uint32_t loops,
                                 probe_case_result_t *out) {
    psa_real_t samples[PROBE_MAX_SAMPLES];
    psa_real_t rebuilt[PROBE_MAX_SAMPLES];
    psa_core_state_t core;
    psa_analysis_record_t record;
    psa_explain_pascal_workspace_t workspace;
    clock_t t0;
    clock_t t1;
    uint32_t i;
    uint32_t loop;
    uint32_t written = 0U;
    if (name == NULL || generator == NULL || out == NULL || sample_count == 0U || sample_count > PROBE_MAX_SAMPLES) {
        return 100;
    }
    memset(out, 0, sizeof(*out));
    out->name = name;
    out->path = "core+explain";
    out->rate_unit = "samples/s";
    generator(samples, sample_count);

    t0 = clock();
    for (loop = 0U; loop < loops; ++loop) {
        psa_core_init(&core);
        for (i = 0U; i < sample_count; ++i) {
            if (psa_core_push_value(&core, samples[i]) != PSA_STATUS_OK) {
                return 101;
            }
        }
    }
    t1 = clock();
    out->throughput = ((t1 - t0) > 0)
        ? ((double)sample_count * (double)loops / ((double)(t1 - t0) / (double)CLOCKS_PER_SEC))
        : 0.0;

    psa_core_init(&core);
    for (i = 0U; i < sample_count; ++i) {
        if (psa_core_push_value(&core, samples[i]) != PSA_STATUS_OK) {
            return 102;
        }
    }
    if (psa_explain_capture_record(&core, &record) != PSA_STATUS_OK) {
        return 103;
    }
    psa_explain_pascal_workspace_reset(&workspace);
    if (psa_explain_rebuild_signal_from_pascal(&record, &workspace, rebuilt, sample_count, &written) != PSA_STATUS_OK) {
        return 104;
    }
    if (written != sample_count) {
        return 105;
    }
    measure_probe_error(record.sample_trace, rebuilt, sample_count, &out->error);
    snprintf(out->class_text,
             sizeof(out->class_text),
             "%s/%s",
             psa_q_rule_name(record.certificate.q_class),
             psa_q_certificate_name(&record.certificate));
    snprintf(out->detail,
             sizeof(out->detail),
             "ret=%lu start=%ld sigma=%lu block=%s multiblock=%s",
             (unsigned long)record.certificate.return_period,
             (long)record.certificate.return_start_step,
             (unsigned long)record.certificate.sigma_lambda_count,
             psa_q_rule_name(record.certificate.block_class),
             psa_q_rule_name(record.certificate.multiblock_class));
    return 0;
}

static int run_manual_finite_return_case(probe_case_result_t *out) {
    enum { SAMPLE_COUNT = 128U };
    psa_analysis_record_t record;
    psa_real_t rebuilt[SAMPLE_COUNT];
    clock_t t0;
    clock_t t1;
    uint32_t written = 0U;
    uint32_t loop;
    if (out == NULL) {
        return 150;
    }
    memset(out, 0, sizeof(*out));
    out->name = "finite_return";
    out->path = "manual-cert+explain";
    out->rate_unit = "samples/s";

    build_finite_return_record(&record, SAMPLE_COUNT);
    t0 = clock();
    for (loop = 0U; loop < DIRECT_PROBE_LOOPS; ++loop) {
        if (psa_explain_replay_rule(&record.signal_rule, SAMPLE_COUNT - 1U, rebuilt, SAMPLE_COUNT, &written) != PSA_STATUS_OK) {
            return 151;
        }
    }
    t1 = clock();
    out->throughput = ((t1 - t0) > 0)
        ? ((double)SAMPLE_COUNT * (double)DIRECT_PROBE_LOOPS / ((double)(t1 - t0) / (double)CLOCKS_PER_SEC))
        : 0.0;
    if (psa_explain_replay_rule(&record.signal_rule, SAMPLE_COUNT - 1U, rebuilt, SAMPLE_COUNT, &written) != PSA_STATUS_OK) {
        return 152;
    }
    if (written != SAMPLE_COUNT) {
        return 153;
    }
    measure_probe_error(record.sample_trace, rebuilt, SAMPLE_COUNT, &out->error);
    snprintf(out->class_text,
             sizeof(out->class_text),
             "%s/%s",
             psa_q_rule_name(record.certificate.q_class),
             psa_q_certificate_name(&record.certificate));
    snprintf(out->detail,
             sizeof(out->detail),
             "ret=%lu start=%ld sigma=%lu const=%d",
             (unsigned long)record.certificate.return_period,
             (long)record.certificate.return_start_step,
             (unsigned long)record.certificate.sigma_lambda_count,
             record.certificate.sigma_constant ? 1 : 0);
    return 0;
}

static int run_manual_nonreturn_case(probe_case_result_t *out) {
    enum { SAMPLE_COUNT = 128U };
    psa_analysis_record_t record;
    psa_real_t rebuilt[SAMPLE_COUNT];
    clock_t t0;
    clock_t t1;
    uint32_t written = 0U;
    uint32_t loop;
    if (out == NULL) {
        return 180;
    }
    memset(out, 0, sizeof(*out));
    out->name = "nonreturn";
    out->path = "manual-cert+explain";
    out->rate_unit = "samples/s";

    build_segment_record(&record, SAMPLE_COUNT, 1.1, 1.0, 0.1);
    t0 = clock();
    for (loop = 0U; loop < DIRECT_PROBE_LOOPS; ++loop) {
        if (psa_explain_replay_rule(&record.signal_rule, SAMPLE_COUNT - 1U, rebuilt, SAMPLE_COUNT, &written) != PSA_STATUS_OK) {
            return 181;
        }
    }
    t1 = clock();
    out->throughput = ((t1 - t0) > 0)
        ? ((double)SAMPLE_COUNT * (double)DIRECT_PROBE_LOOPS / ((double)(t1 - t0) / (double)CLOCKS_PER_SEC))
        : 0.0;
    if (psa_explain_replay_rule(&record.signal_rule, SAMPLE_COUNT - 1U, rebuilt, SAMPLE_COUNT, &written) != PSA_STATUS_OK) {
        return 182;
    }
    if (written != SAMPLE_COUNT) {
        return 183;
    }
    measure_probe_error(record.sample_trace, rebuilt, SAMPLE_COUNT, &out->error);
    snprintf(out->class_text,
             sizeof(out->class_text),
             "%s/%s",
             psa_q_rule_name(record.certificate.q_class),
             psa_q_certificate_name(&record.certificate));
    snprintf(out->detail,
             sizeof(out->detail),
             "order=%lu seed=%lu q=%.6g sigma=%lu",
             (unsigned long)record.signal_rule.order,
             (unsigned long)record.signal_rule.seed_count,
             record.q_constant_value,
             (unsigned long)record.certificate.sigma_lambda_count);
    return 0;
}

static int run_direct_oscillatory_block_case(probe_case_result_t *out) {
    enum { SAMPLE_COUNT = 128U };
    psa_explain_rule_t rule;
    psa_direct_result_t result;
    psa_direct_block_family_t family;
    psa_real_t rebuilt[SAMPLE_COUNT];
    psa_real_t expected[SAMPLE_COUNT];
    clock_t t0;
    clock_t t1;
    uint32_t loop;
    if (out == NULL) {
        return 200;
    }
    memset(out, 0, sizeof(*out));
    out->name = "order2_block";
    out->path = "direct-block";
    out->rate_unit = "samples/s";

    fill_rule_from_cosine(&rule, 1.0, (psa_real_t)(PROBE_PI * 0.5));
    generate_cosine_expected(expected, SAMPLE_COUNT, 1.0, (psa_real_t)(PROBE_PI * 0.5));

    t0 = clock();
    for (loop = 0U; loop < DIRECT_PROBE_LOOPS; ++loop) {
        psa_direct_result_reset(&result);
        psa_direct_block_family_reset(&family);
        if (psa_direct_decode_rule(&rule, 8000.0, &result) != PSA_STATUS_OK) {
            return 201;
        }
        if (psa_direct_blocks_from_result(&result, &family) != PSA_STATUS_OK) {
            return 202;
        }
        if (psa_direct_reconstruct(&result, SAMPLE_COUNT, rebuilt, SAMPLE_COUNT) != PSA_STATUS_OK) {
            return 203;
        }
    }
    t1 = clock();
    out->throughput = ((t1 - t0) > 0)
        ? ((double)SAMPLE_COUNT * (double)DIRECT_PROBE_LOOPS / ((double)(t1 - t0) / (double)CLOCKS_PER_SEC))
        : 0.0;

    psa_direct_result_reset(&result);
    psa_direct_block_family_reset(&family);
    if (psa_direct_decode_rule(&rule, 8000.0, &result) != PSA_STATUS_OK) {
        return 204;
    }
    if (psa_direct_blocks_from_result(&result, &family) != PSA_STATUS_OK) {
        return 205;
    }
    if (psa_direct_reconstruct(&result, SAMPLE_COUNT, rebuilt, SAMPLE_COUNT) != PSA_STATUS_OK) {
        return 206;
    }
    measure_probe_error(expected, rebuilt, SAMPLE_COUNT, &out->error);
    snprintf(out->class_text,
             sizeof(out->class_text),
             "%s/block-count=%lu",
             psa_q_rule_name(family.q_class),
             (unsigned long)family.block_count);
    snprintf(out->detail,
             sizeof(out->detail),
             "dim=%lu dominant=%ld period=%lu harmonic=%lu",
             (unsigned long)family.total_dimension,
             (long)family.dominant_block,
             (unsigned long)family.period,
             (unsigned long)family.harmonic_count);
    return 0;
}

static int run_direct_multiblock_case(probe_case_result_t *out) {
    enum { SAMPLE_COUNT = 128U };
    psa_explain_rule_t rule_a;
    psa_explain_rule_t rule_b;
    const psa_explain_rule_t *rules[2];
    psa_direct_family_t family;
    psa_direct_block_family_t block_family;
    psa_real_t weights[PSA_CFG_MAX_ORDER];
    psa_real_t rebuilt[SAMPLE_COUNT];
    psa_real_t expected[SAMPLE_COUNT];
    psa_real_t theta_a = (psa_real_t)(2.0 * PROBE_PI / 8.0);
    psa_real_t theta_b = (psa_real_t)(2.0 * PROBE_PI / 4.0);
    clock_t t0;
    clock_t t1;
    uint32_t i;
    uint32_t loop;
    if (out == NULL) {
        return 300;
    }
    memset(out, 0, sizeof(*out));
    out->name = "multiblock";
    out->path = "direct-family";
    out->rate_unit = "samples/s";

    fill_rule_from_cosine(&rule_a, 1.0, theta_a);
    fill_rule_from_cosine(&rule_b, 0.5, theta_b);
    rules[0] = &rule_a;
    rules[1] = &rule_b;
    for (i = 0U; i < SAMPLE_COUNT; ++i) {
        expected[i] = (psa_real_t)(cos(theta_a * (psa_real_t)i) + 0.5 * cos(theta_b * (psa_real_t)i));
    }

    t0 = clock();
    for (loop = 0U; loop < DIRECT_PROBE_LOOPS; ++loop) {
        psa_direct_family_reset(&family);
        psa_direct_block_family_reset(&block_family);
        memset(weights, 0, sizeof(weights));
        if (psa_direct_decode_rule_family(rules, 2U, 8000.0, SAMPLE_COUNT, &family) != PSA_STATUS_OK) {
            return 301;
        }
        if (psa_direct_blocks_from_family(&family, &block_family) != PSA_STATUS_OK) {
            return 302;
        }
        for (i = 0U; i < family.component_count; ++i) {
            weights[i] = 1.0;
        }
        if (psa_direct_synthesize_family(&family, weights, SAMPLE_COUNT, rebuilt, SAMPLE_COUNT) != PSA_STATUS_OK) {
            return 303;
        }
    }
    t1 = clock();
    out->throughput = ((t1 - t0) > 0)
        ? ((double)SAMPLE_COUNT * (double)DIRECT_PROBE_LOOPS / ((double)(t1 - t0) / (double)CLOCKS_PER_SEC))
        : 0.0;

    psa_direct_family_reset(&family);
    psa_direct_block_family_reset(&block_family);
    memset(weights, 0, sizeof(weights));
    if (psa_direct_decode_rule_family(rules, 2U, 8000.0, SAMPLE_COUNT, &family) != PSA_STATUS_OK) {
        return 304;
    }
    if (psa_direct_blocks_from_family(&family, &block_family) != PSA_STATUS_OK) {
        return 305;
    }
    for (i = 0U; i < family.component_count; ++i) {
        weights[i] = 1.0;
    }
    if (psa_direct_synthesize_family(&family, weights, SAMPLE_COUNT, rebuilt, SAMPLE_COUNT) != PSA_STATUS_OK) {
        return 306;
    }
    measure_probe_error(expected, rebuilt, SAMPLE_COUNT, &out->error);
    snprintf(out->class_text,
             sizeof(out->class_text),
             "%s/blocks=%lu",
             psa_q_rule_name(block_family.q_class),
             (unsigned long)block_family.block_count);
    snprintf(out->detail,
             sizeof(out->detail),
             "dim=%lu period=%lu dominant=%ld harmonic=%lu",
             (unsigned long)block_family.total_dimension,
             (unsigned long)block_family.period,
             (long)block_family.dominant_block,
             (unsigned long)block_family.harmonic_count);
    return 0;
}

static int run_segment_case(probe_case_result_t *out) {
    psa_explain_segment_recorder_t recorder;
    psa_explain_stream_player_t player;
    psa_analysis_record_t old_record;
    psa_analysis_record_t new_record;
    const psa_explain_segment_record_t *segment;
    psa_real_t replayed[512];
    clock_t t0;
    clock_t t1;
    uint32_t i;
    uint32_t loop;
    if (out == NULL) {
        return 400;
    }
    memset(out, 0, sizeof(*out));
    out->name = "stable_segment";
    out->path = "segment+stream";
    out->rate_unit = "updates/s";

    t0 = clock();
    for (loop = 0U; loop < SEGMENT_PROBE_LOOPS; ++loop) {
        build_segment_record(&old_record, 256U, 1.1, 1.0, 0.1);
        psa_explain_segment_recorder_arm_from_record(&recorder, &old_record, 2U);
        for (i = 257U; i <= 384U; ++i) {
            build_segment_record(&old_record, i, 1.1, 1.0, 0.1);
            if (psa_explain_segment_recorder_update(&recorder, &old_record) != PSA_STATUS_OK) {
                return 401;
            }
        }
        build_segment_record(&new_record, 385U, 1.2, old_record.sample_trace[255], 0.2);
        if (psa_explain_segment_recorder_update(&recorder, &new_record) != PSA_STATUS_OK) {
            return 402;
        }
        build_segment_record(&new_record, 386U, 1.2, old_record.sample_trace[255], 0.2);
        if (psa_explain_segment_recorder_update(&recorder, &new_record) != PSA_STATUS_OK) {
            return 403;
        }
    }
    t1 = clock();
    out->throughput = ((t1 - t0) > 0)
        ? ((130.0 * (double)SEGMENT_PROBE_LOOPS) / ((double)(t1 - t0) / (double)CLOCKS_PER_SEC))
        : 0.0;

    build_segment_record(&old_record, 256U, 1.1, 1.0, 0.1);
    psa_explain_segment_recorder_arm_from_record(&recorder, &old_record, 2U);
    for (i = 257U; i <= 384U; ++i) {
        build_segment_record(&old_record, i, 1.1, 1.0, 0.1);
        if (psa_explain_segment_recorder_update(&recorder, &old_record) != PSA_STATUS_OK) {
            return 404;
        }
    }
    build_segment_record(&new_record, 385U, 1.2, old_record.sample_trace[255], 0.2);
    if (psa_explain_segment_recorder_update(&recorder, &new_record) != PSA_STATUS_OK) {
        return 405;
    }
    build_segment_record(&new_record, 386U, 1.2, old_record.sample_trace[255], 0.2);
    if (psa_explain_segment_recorder_update(&recorder, &new_record) != PSA_STATUS_OK) {
        return 406;
    }
    segment = psa_explain_segment_recorder_get_segment(&recorder);
    if (segment == NULL) {
        return 407;
    }
    if (psa_explain_stream_player_begin_segment_signal(segment, &player) != PSA_STATUS_OK) {
        return 408;
    }
    for (i = 0U; i < segment->trace_count; ++i) {
        if (psa_explain_stream_player_next(&player, &replayed[i]) != PSA_STATUS_OK) {
            return 409;
        }
    }
    measure_probe_error(segment->sample_trace, replayed, segment->trace_count, &out->error);
    snprintf(out->class_text,
             sizeof(out->class_text),
             "%s/%s",
             psa_q_rule_name(segment->certificate.q_class),
             psa_q_certificate_name(&segment->certificate));
    snprintf(out->detail,
             sizeof(out->detail),
             "trace=%lu ret=%lu sigma=%lu",
             (unsigned long)segment->trace_count,
             (unsigned long)segment->certificate.return_period,
             (unsigned long)segment->certificate.sigma_lambda_count);
    return 0;
}

static void print_case_header(void) {
    printf("batch_case_table\n");
    printf("case|path|class|rate_unit|rate|max_abs|rms_abs|max_global_scaled|max_local_scaled|detail\n");
}

static void print_case_row(const probe_case_result_t *row) {
    if (row == NULL) {
        return;
    }
    printf("%s|%s|%s|%s|%.3f|%.17g|%.17g|%.17g|%.17g|%s\n",
           row->name,
           row->path,
           row->class_text,
           row->rate_unit,
           row->throughput,
           row->error.max_abs,
           row->error.rms_abs,
           row->error.max_global_scaled,
           row->error.max_local_scaled,
           row->detail);
}

int main(void) {
    probe_case_result_t rows[6];
    int rc = 0;
    memset(rows, 0, sizeof(rows));

    rc = run_core_rebuild_case("absorbing", generate_absorbing_samples, 128U, CORE_PROBE_LOOPS, &rows[0]);
    if (rc != 0) {
        printf("perf_probe failed: absorbing=%d\n", rc);
        return 1;
    }
    rc = run_manual_finite_return_case(&rows[1]);
    if (rc != 0) {
        printf("perf_probe failed: finite=%d\n", rc);
        return 1;
    }
    rc = run_manual_nonreturn_case(&rows[2]);
    if (rc != 0) {
        printf("perf_probe failed: nonreturn=%d\n", rc);
        return 1;
    }
    rc = run_direct_oscillatory_block_case(&rows[3]);
    if (rc != 0) {
        printf("perf_probe failed: block=%d\n", rc);
        return 1;
    }
    rc = run_direct_multiblock_case(&rows[4]);
    if (rc != 0) {
        printf("perf_probe failed: multiblock=%d\n", rc);
        return 1;
    }
    rc = run_segment_case(&rows[5]);
    if (rc != 0) {
        printf("perf_probe failed: segment=%d\n", rc);
        return 1;
    }

    printf("machine_epsilon=%.17g\n", psa_real_machine_epsilon());
    print_case_header();
    print_case_row(&rows[0]);
    print_case_row(&rows[1]);
    print_case_row(&rows[2]);
    print_case_row(&rows[3]);
    print_case_row(&rows[4]);
    print_case_row(&rows[5]);
    return 0;
}
