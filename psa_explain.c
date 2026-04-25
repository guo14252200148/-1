#include "psa_explain.h"

#include <stdio.h>
#include <string.h>

static psa_real_t psa_explain_history_scale(const psa_real_t *values, uint32_t count) {
    psa_real_t scale = 1.0;
    uint32_t i;
    if (values == NULL) {
        return scale;
    }
    for (i = 0U; i < count; ++i) {
        scale = psa_real_max(scale, psa_real_abs(values[i]));
    }
    return scale;
}

static void psa_explain_capture_rule(psa_explain_rule_t *out,
                                     psa_explain_rule_kind_t kind,
                                     const psa_recurrence_state_t *state,
                                     const psa_real_t *history,
                                     uint32_t history_count,
                                     int32_t pow2_exponent,
                                     bool constant_tail,
                                     psa_real_t constant_value,
                                     uint32_t period_tail) {
    uint32_t order;
    uint32_t copy_count;
    uint32_t start;
    uint32_t i;
    if (out == NULL || state == NULL) {
        return;
    }
    psa_explain_rule_reset(out);
    out->kind = kind;
    out->defined = (history != NULL && history_count > 0U);
    out->order = state->L;
    out->pow2_exponent = pow2_exponent;
    out->constant_tail = constant_tail;
    out->constant_value = constant_value;
    out->period_tail = period_tail;
    for (i = 0U; i <= state->L && i <= PSA_CFG_MAX_ORDER; ++i) {
        out->coefficients[i] = state->C[i];
    }
    order = state->L;
    copy_count = (order == 0U && history_count > 0U) ? 1U : order;
    if (copy_count > history_count) {
        copy_count = history_count;
    }
    start = history_count - copy_count;
    out->seed_count = copy_count;
    for (i = 0U; i < copy_count; ++i) {
        out->seeds[i] = psa_scale_pow2_real(history[start + i], pow2_exponent);
    }
    if (history_count > 0U) {
        out->latest_value = psa_scale_pow2_real(history[history_count - 1U], pow2_exponent);
    }
    out->scale = psa_explain_history_scale(out->seeds, out->seed_count);
    out->signature = psa_signature_from_reals(out->coefficients, (size_t)out->order + 1U, out->scale);
}

static void psa_explain_copy_rule(psa_explain_rule_t *dst, const psa_explain_rule_t *src) {
    if (dst != NULL && src != NULL) {
        *dst = *src;
    }
}

static psa_status_t psa_explain_refresh_segment_rules(psa_explain_segment_record_t *segment) {
    uint32_t i;
    if (segment == NULL || !segment->active || segment->trace_count == 0U) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    if (segment->signal_rule.defined) {
        uint32_t copy = segment->signal_rule.order;
        if (copy == 0U && segment->trace_count > 0U) {
            copy = 1U;
        }
        if (copy > segment->trace_count) {
            copy = segment->trace_count;
        }
        segment->signal_rule.seed_count = copy;
        for (i = 0U; i < copy; ++i) {
            segment->signal_rule.seeds[i] = segment->sample_trace[i];
        }
        segment->signal_rule.latest_value = segment->sample_trace[segment->trace_count - 1U];
        segment->signal_rule.scale = psa_explain_history_scale(segment->signal_rule.seeds, copy);
    }
    if (segment->pascal_rule.defined) {
        uint32_t copy = segment->pascal_rule.order;
        if (copy == 0U && segment->trace_count > 0U) {
            copy = 1U;
        }
        if (copy > segment->trace_count) {
            copy = segment->trace_count;
        }
        segment->pascal_rule.seed_count = copy;
        for (i = 0U; i < copy; ++i) {
            segment->pascal_rule.seeds[i] = segment->pascal_trace[i];
        }
        segment->pascal_rule.latest_value = segment->pascal_trace[segment->trace_count - 1U];
        segment->pascal_rule.scale = psa_explain_history_scale(segment->pascal_rule.seeds, copy);
    }
    if (segment->q_rule.defined && segment->q_trace_count > 0U) {
        uint32_t copy = segment->q_rule.order;
        if (copy == 0U) {
            copy = 1U;
        }
        if (copy > segment->q_trace_count) {
            copy = segment->q_trace_count;
        }
        segment->q_rule.seed_count = copy;
        for (i = 0U; i < copy; ++i) {
            segment->q_rule.seeds[i] = segment->q_trace[i];
        }
        segment->q_rule.latest_value = segment->q_trace[segment->q_trace_count - 1U];
        segment->q_rule.scale = psa_explain_history_scale(segment->q_rule.seeds, copy);
    }
    return PSA_STATUS_OK;
}

static psa_status_t psa_explain_segment_init_from_record(psa_explain_segment_record_t *segment,
                                                         const psa_analysis_record_t *record,
                                                         const psa_explain_certificate_t *certificate,
                                                         uint32_t start_sample_index,
                                                         uint32_t start_q_index) {
    uint32_t i;
    if (segment == NULL || record == NULL || certificate == NULL || !record->ready) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    psa_explain_segment_record_reset(segment);
    segment->active = true;
    segment->certificate = *certificate;
    segment->start_sample_index = start_sample_index;
    segment->end_sample_index = start_sample_index;
    segment->start_q_index = start_q_index;
    segment->end_q_index = start_q_index;
    segment->signal_order = record->summary.signal_order;
    segment->pascal_order = record->summary.pascal_order;
    segment->q_order = record->summary.q_order;
    psa_explain_copy_rule(&segment->signal_rule, &record->signal_rule);
    psa_explain_copy_rule(&segment->pascal_rule, &record->pascal_rule);
    psa_explain_copy_rule(&segment->q_rule, &record->q_rule);
    if (start_sample_index > record->trace_count || start_q_index > record->q_trace_count) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    segment->trace_count = record->trace_count - start_sample_index;
    segment->sample_count = segment->trace_count;
    if (segment->trace_count > PSA_CFG_MAX_FRONTIER) {
        segment->overflowed = true;
        segment->trace_count = PSA_CFG_MAX_FRONTIER;
        segment->sample_count = PSA_CFG_MAX_FRONTIER;
    }
    for (i = 0U; i < segment->trace_count; ++i) {
        segment->sample_trace[i] = record->sample_trace[start_sample_index + i];
        segment->pascal_trace[i] = record->pascal_trace[start_sample_index + i];
    }
    segment->end_sample_index = start_sample_index + segment->trace_count;
    segment->q_trace_count = record->q_trace_count - start_q_index;
    if (segment->q_trace_count > PSA_CFG_MAX_FRONTIER) {
        segment->overflowed = true;
        segment->q_trace_count = PSA_CFG_MAX_FRONTIER;
    }
    for (i = 0U; i < segment->q_trace_count; ++i) {
        segment->q_trace[i] = record->q_trace[start_q_index + i];
        segment->q_step_trace[i] = record->q_step_trace[start_q_index + i];
        segment->kernel_trace[i] = record->kernel_trace[start_q_index + i];
        segment->kernel_step_trace[i] = record->kernel_step_trace[start_q_index + i];
    }
    segment->end_q_index = start_q_index + segment->q_trace_count;
    return psa_explain_refresh_segment_rules(segment);
}

static psa_status_t psa_explain_segment_append_from_record(psa_explain_segment_record_t *segment,
                                                           const psa_analysis_record_t *record,
                                                           uint32_t appended_sample_count,
                                                           uint32_t appended_q_count) {
    uint32_t i;
    uint32_t sample_base;
    uint32_t q_base;
    uint32_t sample_end;
    uint32_t q_end;
    if (segment == NULL || record == NULL || !segment->active) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    sample_base = segment->start_sample_index + appended_sample_count;
    q_base = segment->start_q_index + appended_q_count;
    sample_end = record->trace_count;
    q_end = record->q_trace_count;
    if (sample_end < sample_base || q_end < q_base) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    for (i = sample_base; i < sample_end; ++i) {
        if (segment->trace_count >= PSA_CFG_MAX_FRONTIER) {
            segment->overflowed = true;
            break;
        }
        segment->sample_trace[segment->trace_count] = record->sample_trace[i];
        segment->pascal_trace[segment->trace_count] = record->pascal_trace[i];
        segment->trace_count += 1U;
    }
    segment->sample_count = segment->trace_count;
    segment->end_sample_index = segment->start_sample_index + segment->trace_count;
    for (i = q_base; i < q_end; ++i) {
        if (segment->q_trace_count >= PSA_CFG_MAX_FRONTIER) {
            segment->overflowed = true;
            break;
        }
        segment->q_trace[segment->q_trace_count] = record->q_trace[i];
        segment->q_step_trace[segment->q_trace_count] = record->q_step_trace[i];
        segment->kernel_trace[segment->q_trace_count] = record->kernel_trace[i];
        segment->kernel_step_trace[segment->q_trace_count] = record->kernel_step_trace[i];
        segment->q_trace_count += 1U;
    }
    segment->end_q_index = segment->start_q_index + segment->q_trace_count;
    return psa_explain_refresh_segment_rules(segment);
}

static psa_status_t psa_explain_write_rule_series(const psa_explain_rule_t *rule,
                                                  uint32_t future_count,
                                                  psa_real_t *out_values,
                                                  uint32_t out_capacity,
                                                  uint32_t *written_count) {
    uint32_t total;
    uint32_t i;
    if (written_count != NULL) {
        *written_count = 0U;
    }
    if (rule == NULL || out_values == NULL || !rule->defined || rule->seed_count == 0U) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    if (rule->order == 0U && future_count > 0U) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    total = rule->seed_count + future_count;
    if (out_capacity < total) {
        return PSA_STATUS_CAPACITY;
    }
    for (i = 0U; i < rule->seed_count; ++i) {
        out_values[i] = rule->seeds[i];
    }
    for (i = rule->seed_count; i < total; ++i) {
        psa_kahan_t acc;
        uint32_t k;
        psa_kahan_reset(&acc);
        for (k = 1U; k <= rule->order; ++k) {
            psa_kahan_add(&acc, rule->coefficients[k] * out_values[i - k]);
        }
        out_values[i] = -acc.sum;
    }
    if (written_count != NULL) {
        *written_count = total;
    }
    return PSA_STATUS_OK;
}

static psa_status_t psa_explain_accumulate_rule_series(const psa_explain_rule_t *rule,
                                                       psa_real_t weight,
                                                       uint32_t future_count,
                                                       uint32_t max_seed,
                                                       psa_real_t *out_values,
                                                       uint32_t out_capacity) {
    psa_real_t state[PSA_CFG_MAX_ORDER];
    uint32_t offset;
    uint32_t total;
    uint32_t i;

    if (rule == NULL || out_values == NULL || !rule->defined || rule->seed_count == 0U) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    if (rule->order == 0U && future_count > 0U) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    total = max_seed + future_count;
    if (out_capacity < total) {
        return PSA_STATUS_CAPACITY;
    }
    offset = max_seed - rule->seed_count;
    for (i = 0U; i < rule->seed_count; ++i) {
        out_values[offset + i] += weight * rule->seeds[i];
    }
    if (future_count == 0U || rule->order == 0U) {
        return PSA_STATUS_OK;
    }
    for (i = 0U; i < rule->order; ++i) {
        state[i] = rule->seeds[rule->seed_count - rule->order + i];
    }
    for (i = 0U; i < future_count; ++i) {
        psa_kahan_t acc;
        psa_real_t next_value;
        uint32_t k;
        psa_kahan_reset(&acc);
        for (k = 1U; k <= rule->order; ++k) {
            psa_kahan_add(&acc, rule->coefficients[k] * state[rule->order - k]);
        }
        next_value = -acc.sum;
        out_values[offset + rule->seed_count + i] += weight * next_value;
        memmove(state, state + 1U, (size_t)(rule->order - 1U) * sizeof(*state));
        state[rule->order - 1U] = next_value;
    }
    return PSA_STATUS_OK;
}

static void psa_explain_accumulate_component_sequence(const psa_explain_component_t *component,
                                                      psa_real_t weight,
                                                      uint32_t sample_count,
                                                      psa_real_t *out_values) {
    uint32_t i;
    psa_real_t state_re;
    psa_real_t state_im;

    if (component == NULL || out_values == NULL || !component->active) {
        return;
    }
    state_re = component->amplitude_re;
    state_im = component->amplitude_im;
    for (i = 0U; i < sample_count; ++i) {
        psa_real_t next_re;
        psa_real_t next_im;
        psa_real_t value = component->oscillatory ? ((psa_real_t)2.0 * state_re) : state_re;
        out_values[i] += weight * value;
        next_re = state_re * component->alpha_re - state_im * component->alpha_im;
        next_im = state_re * component->alpha_im + state_im * component->alpha_re;
        state_re = next_re;
        state_im = next_im;
    }
}

void psa_explain_rule_reset(psa_explain_rule_t *rule) {
    if (rule != NULL) {
        memset(rule, 0, sizeof(*rule));
    }
}

void psa_explain_component_reset(psa_explain_component_t *component) {
    if (component != NULL) {
        memset(component, 0, sizeof(*component));
    }
}

void psa_analysis_record_reset(psa_analysis_record_t *record) {
    if (record != NULL) {
        memset(record, 0, sizeof(*record));
    }
}

void psa_precision_report_reset(psa_precision_report_t *report) {
    if (report != NULL) {
        memset(report, 0, sizeof(*report));
        report->machine_epsilon = psa_real_machine_epsilon();
    }
}

void psa_explain_pascal_workspace_reset(psa_explain_pascal_workspace_t *workspace) {
    if (workspace != NULL) {
        memset(workspace, 0, sizeof(*workspace));
    }
}

void psa_explain_certificate_reset(psa_explain_certificate_t *certificate) {
    if (certificate != NULL) {
        memset(certificate, 0, sizeof(*certificate));
    }
}

void psa_explain_segment_record_reset(psa_explain_segment_record_t *segment) {
    if (segment != NULL) {
        memset(segment, 0, sizeof(*segment));
    }
}

void psa_explain_segment_recorder_reset(psa_explain_segment_recorder_t *recorder) {
    if (recorder != NULL) {
        memset(recorder, 0, sizeof(*recorder));
    }
}

void psa_explain_stream_player_reset(psa_explain_stream_player_t *player) {
    if (player != NULL) {
        memset(player, 0, sizeof(*player));
    }
}

psa_status_t psa_explain_capture_record(const psa_core_state_t *state, psa_analysis_record_t *record) {
    uint32_t i;
    if (state == NULL || record == NULL) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    psa_analysis_record_reset(record);
    record->ready = (state->sample_count > 0U);
    psa_core_get_summary(state, &record->summary);
    psa_core_get_certificate(state, &record->certificate);
    psa_explain_capture_rule(&record->signal_rule, PSA_EXPLAIN_RULE_SIGNAL, &state->signal_rule, state->signal_history,
                             state->signal_history_count, state->signal_pow2_exponent, false, 0.0, 0U);
    psa_explain_capture_rule(&record->pascal_rule, PSA_EXPLAIN_RULE_PASCAL, &state->pascal_rule, state->pascal_history,
                             state->pascal_history_count, state->pascal_pow2_exponent, false, 0.0, 0U);
    psa_explain_capture_rule(&record->q_rule, PSA_EXPLAIN_RULE_Q, &state->q_rule_state, state->q_history,
                             state->q_history_count, 0, state->q_constant, state->q_constant_value, state->q_period);
    record->trace_count = state->sample_count;
    record->q_trace_count = state->q_count;
    record->q_constant = state->q_constant;
    record->q_constant_value = state->q_constant_value;
    record->q_period = state->q_period;
    record->kernel_constant = state->kernel_constant;
    record->kernel_constant_value = state->kernel_constant_value;
    record->kernel_period = state->kernel_period;
    for (i = 0U; i < state->sample_count; ++i) {
        record->sample_trace[i] = state->sample_trace[i];
        record->pascal_trace[i] = state->pascal_trace[i];
    }
    for (i = 0U; i < state->q_count; ++i) {
        record->q_trace[i] = state->q_trace[i];
        record->q_step_trace[i] = state->q_step_trace[i];
        record->kernel_trace[i] = state->kernel_trace[i];
        record->kernel_step_trace[i] = state->kernel_step_trace[i];
    }
    record->page_count = state->page_count;
    for (i = 0U; i < state->page_count; ++i) {
        uint32_t index = (state->page_write_index + PSA_CFG_MAX_PAGES - state->page_count + i) % PSA_CFG_MAX_PAGES;
        record->pages[i] = state->pages[index];
    }
    return PSA_STATUS_OK;
}

const psa_explain_rule_t *psa_explain_select_rule(const psa_analysis_record_t *record,
                                                  psa_explain_rule_kind_t kind) {
    if (record == NULL) {
        return NULL;
    }
    switch (kind) {
        case PSA_EXPLAIN_RULE_SIGNAL: return &record->signal_rule;
        case PSA_EXPLAIN_RULE_PASCAL: return &record->pascal_rule;
        case PSA_EXPLAIN_RULE_Q: return &record->q_rule;
        default: return NULL;
    }
}

psa_status_t psa_explain_replay_rule(const psa_explain_rule_t *rule,
                                     uint32_t future_count,
                                     psa_real_t *out_values,
                                     uint32_t out_capacity,
                                     uint32_t *written_count) {
    return psa_explain_write_rule_series(rule, future_count, out_values, out_capacity, written_count);
}

psa_status_t psa_explain_synthesize_rules(const psa_explain_rule_t *const *rules,
                                          const psa_real_t *weights,
                                          size_t rule_count,
                                          uint32_t future_count,
                                          psa_real_t *out_values,
                                          uint32_t out_capacity,
                                          uint32_t *written_count) {
    uint32_t max_seed = 0U;
    size_t i;
    if (written_count != NULL) {
        *written_count = 0U;
    }
    if (rules == NULL || weights == NULL || out_values == NULL || rule_count == 0U) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    for (i = 0U; i < rule_count; ++i) {
        if (rules[i] == NULL || !rules[i]->defined || rules[i]->seed_count == 0U) {
            return PSA_STATUS_BAD_ARGUMENT;
        }
        if (rules[i]->seed_count > max_seed) {
            max_seed = rules[i]->seed_count;
        }
    }
    if (out_capacity < max_seed + future_count) {
        return PSA_STATUS_CAPACITY;
    }
    memset(out_values, 0, (size_t)(max_seed + future_count) * sizeof(*out_values));
    for (i = 0U; i < rule_count; ++i) {
        psa_status_t status = psa_explain_accumulate_rule_series(rules[i],
                                                                 weights[i],
                                                                 future_count,
                                                                 max_seed,
                                                                 out_values,
                                                                 out_capacity);
        if (status != PSA_STATUS_OK) {
            return status;
        }
    }
    if (written_count != NULL) {
        *written_count = max_seed + future_count;
    }
    return PSA_STATUS_OK;
}

psa_status_t psa_explain_emit_component_sequence(const psa_explain_component_t *component,
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
    if (!component->active) {
        memset(out_values, 0, (size_t)sample_count * sizeof(*out_values));
        return PSA_STATUS_OK;
    }
    state_re = component->amplitude_re;
    state_im = component->amplitude_im;
    for (i = 0U; i < sample_count; ++i) {
        psa_real_t next_re;
        psa_real_t next_im;
        out_values[i] = component->oscillatory ? ((psa_real_t)2.0 * state_re) : state_re;
        next_re = state_re * component->alpha_re - state_im * component->alpha_im;
        next_im = state_re * component->alpha_im + state_im * component->alpha_re;
        state_re = next_re;
        state_im = next_im;
    }
    return PSA_STATUS_OK;
}

psa_status_t psa_explain_synthesize_components(const psa_explain_component_t *components,
                                               const psa_real_t *weights,
                                               size_t component_count,
                                               uint32_t sample_count,
                                               psa_real_t *out_values,
                                               uint32_t out_capacity) {
    size_t i;
    if (components == NULL || weights == NULL || out_values == NULL) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    if (out_capacity < sample_count) {
        return PSA_STATUS_CAPACITY;
    }
    memset(out_values, 0, (size_t)sample_count * sizeof(*out_values));
    for (i = 0U; i < component_count; ++i) {
        psa_explain_accumulate_component_sequence(&components[i], weights[i], sample_count, out_values);
    }
    return PSA_STATUS_OK;
}

psa_status_t psa_explain_measure_error(const psa_real_t *reference,
                                       const psa_real_t *candidate,
                                       uint32_t count,
                                       psa_precision_report_t *report) {
    uint32_t i;
    psa_real_t max_abs = 0.0;
    psa_real_t max_scaled = 0.0;
    if (reference == NULL || candidate == NULL || report == NULL) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    psa_precision_report_reset(report);
    report->sample_count = count;
    for (i = 0U; i < count; ++i) {
        psa_real_t err = psa_real_abs(reference[i] - candidate[i]);
        psa_real_t scale = psa_real_max(psa_real_abs(reference[i]), psa_real_abs(candidate[i]));
        psa_real_t scaled = err / psa_real_scaled_epsilon(scale);
        if (err > max_abs) {
            max_abs = err;
        }
        if (scaled > max_scaled) {
            max_scaled = scaled;
        }
    }
    report->max_abs_error = max_abs;
    report->max_scaled_error = max_scaled;
    return PSA_STATUS_OK;
}

int psa_explain_format_record(char *buffer, size_t buffer_size, const psa_analysis_record_t *record) {
    if (buffer == NULL || buffer_size == 0U || record == NULL) {
        return -1;
    }
    return snprintf(buffer, buffer_size,
                    "ready=%d samples=%lu q_class=%s signal(order=%lu seed=%lu) pascal(order=%lu seed=%lu) q(order=%lu seed=%lu) traces=(%lu,%lu)",
                    record->ready ? 1 : 0,
                    (unsigned long)record->summary.sample_count,
                    psa_q_rule_name(record->summary.q_class),
                    (unsigned long)record->signal_rule.order,
                    (unsigned long)record->signal_rule.seed_count,
                    (unsigned long)record->pascal_rule.order,
                    (unsigned long)record->pascal_rule.seed_count,
                    (unsigned long)record->q_rule.order,
                    (unsigned long)record->q_rule.seed_count,
                    (unsigned long)record->trace_count,
                    (unsigned long)record->q_trace_count);
}

int psa_explain_format_structural_report(char *buffer, size_t buffer_size, const psa_analysis_record_t *record) {
    if (buffer == NULL || buffer_size == 0U || record == NULL) {
        return -1;
    }
    return snprintf(buffer,
                    buffer_size,
                    "q_mode=%s q_value=%.12g q_period=%lu kernel_mode=%s kernel_value=%.12g kernel_period=%lu cert=%s "
                    "ret=(%lu,%ld) sigma=(ready=%d period=%lu count=%lu) block=(active=%d class=%s period=%lu) "
                    "lift=%s multiblock=(ready=%d count=%lu class=%s period=%lu) pages=%lu total_pages=%lu",
                    record->q_constant ? "constant" : ((record->q_period > 0U) ? "periodic" : "free"),
                    record->q_constant_value,
                    (unsigned long)record->q_period,
                    record->kernel_constant ? "constant" : ((record->kernel_period > 0U) ? "periodic" : "free"),
                    record->kernel_constant_value,
                    (unsigned long)record->kernel_period,
                    psa_q_certificate_name(&record->certificate),
                    (unsigned long)record->certificate.return_period,
                    (long)record->certificate.return_start_step,
                    record->certificate.sigma_radial_ready ? 1 : 0,
                    (unsigned long)record->certificate.sigma_period,
                    (unsigned long)record->certificate.sigma_lambda_count,
                    record->certificate.block_rule_active ? 1 : 0,
                    psa_q_rule_name(record->certificate.block_class),
                    (unsigned long)record->certificate.block_period,
                    record->certificate.kernel_lift_certified ? psa_q_rule_name(record->certificate.kernel_lift_class) : "none",
                    record->certificate.multiblock_ready ? 1 : 0,
                    (unsigned long)record->certificate.block_count,
                    psa_q_rule_name(record->certificate.multiblock_class),
                    (unsigned long)record->certificate.multiblock_period,
                    (unsigned long)record->summary.page_count,
                    (unsigned long)record->summary.total_page_count);
}

psa_status_t psa_explain_export_series(const psa_analysis_record_t *record,
                                       psa_explain_series_kind_t kind,
                                       psa_real_t *out_values,
                                       uint32_t out_capacity,
                                       uint32_t *written_count) {
    const psa_real_t *src = NULL;
    uint32_t count = 0U;
    uint32_t i;
    if (written_count != NULL) {
        *written_count = 0U;
    }
    if (record == NULL || out_values == NULL) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    switch (kind) {
        case PSA_EXPLAIN_SERIES_SAMPLE:
            src = record->sample_trace;
            count = record->trace_count;
            break;
        case PSA_EXPLAIN_SERIES_PASCAL:
            src = record->pascal_trace;
            count = record->trace_count;
            break;
        case PSA_EXPLAIN_SERIES_Q:
            src = record->q_trace;
            count = record->q_trace_count;
            break;
        case PSA_EXPLAIN_SERIES_KERNEL:
            src = record->kernel_trace;
            count = record->q_trace_count;
            break;
        default:
            return PSA_STATUS_BAD_ARGUMENT;
    }
    if (out_capacity < count) {
        return PSA_STATUS_CAPACITY;
    }
    for (i = 0U; i < count; ++i) {
        out_values[i] = src[i];
    }
    if (written_count != NULL) {
        *written_count = count;
    }
    return PSA_STATUS_OK;
}

psa_status_t psa_explain_rebuild_signal_from_pascal(const psa_analysis_record_t *record,
                                                    psa_explain_pascal_workspace_t *workspace,
                                                    psa_real_t *out_values,
                                                    uint32_t out_capacity,
                                                    uint32_t *written_count) {
    psa_dd_real_t *work;
    uint32_t step;
    if (written_count != NULL) {
        *written_count = 0U;
    }
    if (record == NULL || workspace == NULL || out_values == NULL) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    if (out_capacity < record->trace_count) {
        return PSA_STATUS_CAPACITY;
    }
    if (record->trace_count == 0U) {
        return PSA_STATUS_OK;
    }
    work = workspace->values;
    for (step = 0U; step < record->trace_count; ++step) {
        work[step] = psa_dd_from_real(record->pascal_trace[step]);
    }
    out_values[0] = psa_dd_to_real(work[0]);
    for (step = 1U; step < record->trace_count; ++step) {
        uint32_t k;
        for (k = 0U; k < record->trace_count - step; ++k) {
            work[k] = psa_dd_add(work[k], work[k + 1U]);
        }
        out_values[step] = psa_dd_to_real(work[0]);
    }
    if (written_count != NULL) {
        *written_count = record->trace_count;
    }
    return PSA_STATUS_OK;
}

psa_status_t psa_explain_build_certificate(const psa_analysis_record_t *record,
                                           psa_explain_certificate_t *certificate) {
    if (record == NULL || certificate == NULL || !record->ready) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    *certificate = record->certificate;
    return PSA_STATUS_OK;
}

psa_status_t psa_explain_segment_recorder_arm(psa_explain_segment_recorder_t *recorder,
                                              uint32_t min_streak) {
    if (recorder == NULL) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    psa_explain_segment_recorder_reset(recorder);
    recorder->armed = true;
    recorder->min_streak = (min_streak == 0U) ? 1U : min_streak;
    return PSA_STATUS_OK;
}

psa_status_t psa_explain_segment_recorder_arm_from_record(psa_explain_segment_recorder_t *recorder,
                                                          const psa_analysis_record_t *record,
                                                          uint32_t min_streak) {
    psa_status_t status = psa_explain_segment_recorder_arm(recorder, min_streak);
    psa_explain_certificate_t certificate;
    if (status != PSA_STATUS_OK) {
        return status;
    }
    if (record != NULL && record->ready) {
        recorder->arm_sample_index = record->trace_count;
        recorder->arm_q_index = record->q_trace_count;
        if (psa_explain_build_certificate(record, &certificate) == PSA_STATUS_OK &&
            psa_q_certificate_committable(&certificate)) {
            recorder->committed_ready = true;
            recorder->committed_signature = certificate.signature;
            recorder->committed_certificate = certificate;
        }
    }
    return PSA_STATUS_OK;
}

psa_status_t psa_explain_segment_recorder_update(psa_explain_segment_recorder_t *recorder,
                                                 const psa_analysis_record_t *record) {
    psa_explain_certificate_t certificate;
    if (recorder == NULL || record == NULL || !recorder->armed || !record->ready) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    if (recorder->completed) {
        return PSA_STATUS_OK;
    }
    if (psa_explain_build_certificate(record, &certificate) != PSA_STATUS_OK || !certificate.ready) {
        return PSA_STATUS_OK;
    }
    if (!psa_q_certificate_committable(&certificate)) {
        recorder->candidate_signature = 0ULL;
        recorder->candidate_streak = 0U;
        return PSA_STATUS_OK;
    }
    if (!recorder->committed_ready) {
        if (recorder->candidate_signature != certificate.signature) {
            recorder->candidate_signature = certificate.signature;
            recorder->candidate_streak = 1U;
            recorder->candidate_sample_index = (record->trace_count > 0U) ? (record->trace_count - 1U) : 0U;
            recorder->candidate_q_index = (record->q_trace_count > 0U) ? (record->q_trace_count - 1U) : 0U;
            return PSA_STATUS_OK;
        }
        recorder->candidate_streak += 1U;
        if (recorder->candidate_streak >= recorder->min_streak) {
            recorder->committed_ready = true;
            recorder->committed_signature = certificate.signature;
            recorder->committed_certificate = certificate;
            recorder->candidate_signature = 0ULL;
            recorder->candidate_streak = 0U;
            recorder->appended_sample_count = record->trace_count - recorder->candidate_sample_index;
            recorder->appended_q_count = record->q_trace_count - recorder->candidate_q_index;
            return psa_explain_segment_init_from_record(&recorder->segment,
                                                        record,
                                                        &certificate,
                                                        recorder->candidate_sample_index,
                                                        recorder->candidate_q_index);
        }
        return PSA_STATUS_OK;
    }
    if (certificate.signature == recorder->committed_signature) {
        recorder->candidate_signature = 0ULL;
        recorder->candidate_streak = 0U;
        if (!recorder->segment.active) {
            psa_status_t status = psa_explain_segment_init_from_record(&recorder->segment,
                                                                       record,
                                                                       &recorder->committed_certificate,
                                                                       recorder->arm_sample_index,
                                                                       recorder->arm_q_index);
            if (status == PSA_STATUS_OK) {
                recorder->appended_sample_count = record->trace_count - recorder->arm_sample_index;
                recorder->appended_q_count = record->q_trace_count - recorder->arm_q_index;
            }
            return status;
        }
        if (psa_explain_segment_append_from_record(&recorder->segment,
                                                   record,
                                                   recorder->appended_sample_count,
                                                   recorder->appended_q_count) != PSA_STATUS_OK) {
            return PSA_STATUS_NUMERIC;
        }
        recorder->appended_sample_count = record->trace_count - recorder->segment.start_sample_index;
        recorder->appended_q_count = record->q_trace_count - recorder->segment.start_q_index;
        return PSA_STATUS_OK;
    }
    if (recorder->candidate_signature != certificate.signature) {
        recorder->candidate_signature = certificate.signature;
        recorder->candidate_streak = 1U;
        recorder->candidate_sample_index = (record->trace_count > 0U) ? (record->trace_count - 1U) : 0U;
        recorder->candidate_q_index = (record->q_trace_count > 0U) ? (record->q_trace_count - 1U) : 0U;
        return PSA_STATUS_OK;
    }
    recorder->candidate_streak += 1U;
    if (recorder->candidate_streak >= recorder->min_streak) {
        recorder->completed = true;
        recorder->segment.completed = true;
    }
    return PSA_STATUS_OK;
}

bool psa_explain_segment_recorder_completed(const psa_explain_segment_recorder_t *recorder) {
    return (recorder != NULL) && recorder->completed;
}

const psa_explain_segment_record_t *psa_explain_segment_recorder_get_segment(const psa_explain_segment_recorder_t *recorder) {
    if (recorder == NULL || !recorder->segment.active) {
        return NULL;
    }
    return &recorder->segment;
}

psa_status_t psa_explain_stream_player_begin_rule(const psa_explain_rule_t *rule,
                                                  uint32_t sample_count,
                                                  psa_explain_stream_player_t *player) {
    uint32_t i;
    if (rule == NULL || player == NULL || !rule->defined || rule->seed_count == 0U) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    psa_explain_stream_player_reset(player);
    player->rule = *rule;
    player->remaining = sample_count;
    for (i = 0U; i < rule->order; ++i) {
        player->state[i] = rule->seeds[rule->seed_count - rule->order + i];
    }
    player->ready = true;
    return PSA_STATUS_OK;
}

psa_status_t psa_explain_stream_player_begin_segment_signal(const psa_explain_segment_record_t *segment,
                                                            psa_explain_stream_player_t *player) {
    if (segment == NULL || player == NULL || !segment->active) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    return psa_explain_stream_player_begin_rule(&segment->signal_rule, segment->trace_count, player);
}

psa_status_t psa_explain_stream_player_next(psa_explain_stream_player_t *player,
                                            psa_real_t *out_value) {
    if (player == NULL || out_value == NULL || !player->ready || player->remaining == 0U) {
        return PSA_STATUS_BAD_ARGUMENT;
    }
    if (player->sample_index < player->rule.seed_count) {
        *out_value = player->rule.seeds[player->sample_index];
    } else if (player->rule.order == 0U) {
        *out_value = player->rule.latest_value;
    } else {
        psa_kahan_t acc;
        psa_real_t next_value;
        uint32_t k;
        psa_kahan_reset(&acc);
        for (k = 1U; k <= player->rule.order; ++k) {
            psa_kahan_add(&acc, player->rule.coefficients[k] * player->state[player->rule.order - k]);
        }
        next_value = -acc.sum;
        memmove(player->state, player->state + 1U, (size_t)(player->rule.order - 1U) * sizeof(*player->state));
        player->state[player->rule.order - 1U] = next_value;
        *out_value = next_value;
    }
    player->sample_index += 1U;
    player->remaining -= 1U;
    return PSA_STATUS_OK;
}
