#ifndef PSA_EXPLAIN_H
#define PSA_EXPLAIN_H

#include "psa_base.h"
#include "psa_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PSA_EXPLAIN_RULE_SIGNAL = 1,
    PSA_EXPLAIN_RULE_PASCAL = 2,
    PSA_EXPLAIN_RULE_Q = 3
} psa_explain_rule_kind_t;

typedef struct {
    psa_explain_rule_kind_t kind;
    bool defined;
    uint32_t order;
    uint32_t seed_count;
    int32_t pow2_exponent;
    psa_real_t latest_value;
    psa_real_t scale;
    psa_real_t coefficients[PSA_CFG_MAX_ORDER + 1];
    psa_real_t seeds[PSA_CFG_MAX_ORDER + 1];
    uint64_t signature;
    bool constant_tail;
    psa_real_t constant_value;
    uint32_t period_tail;
} psa_explain_rule_t;

typedef struct {
    bool active;
    bool oscillatory;
    psa_real_t alpha_re;
    psa_real_t alpha_im;
    psa_real_t amplitude_re;
    psa_real_t amplitude_im;
    psa_real_t rho;
    psa_real_t theta;
    psa_real_t phase;
    psa_real_t energy;
} psa_explain_component_t;

typedef enum {
    PSA_EXPLAIN_SERIES_SAMPLE = 1,
    PSA_EXPLAIN_SERIES_PASCAL = 2,
    PSA_EXPLAIN_SERIES_Q = 3,
    PSA_EXPLAIN_SERIES_KERNEL = 4
} psa_explain_series_kind_t;

typedef struct {
    bool ready;
    psa_summary_t summary;
    psa_q_certificate_t certificate;
    psa_explain_rule_t signal_rule;
    psa_explain_rule_t pascal_rule;
    psa_explain_rule_t q_rule;
    uint32_t trace_count;
    uint32_t q_trace_count;
    psa_real_t sample_trace[PSA_CFG_MAX_FRONTIER];
    psa_real_t pascal_trace[PSA_CFG_MAX_FRONTIER];
    psa_real_t q_trace[PSA_CFG_MAX_FRONTIER];
    uint32_t q_step_trace[PSA_CFG_MAX_FRONTIER];
    psa_real_t kernel_trace[PSA_CFG_MAX_FRONTIER];
    uint32_t kernel_step_trace[PSA_CFG_MAX_FRONTIER];
    bool q_constant;
    psa_real_t q_constant_value;
    uint32_t q_period;
    bool kernel_constant;
    psa_real_t kernel_constant_value;
    uint32_t kernel_period;
    psa_page_summary_t pages[PSA_CFG_MAX_PAGES];
    uint32_t page_count;
} psa_analysis_record_t;

typedef struct {
    uint32_t sample_count;
    psa_real_t max_abs_error;
    psa_real_t max_scaled_error;
    psa_real_t machine_epsilon;
} psa_precision_report_t;

typedef struct {
    psa_dd_real_t values[PSA_CFG_MAX_FRONTIER];
} psa_explain_pascal_workspace_t;

typedef psa_q_certificate_t psa_explain_certificate_t;

typedef struct {
    bool active;
    bool completed;
    bool overflowed;
    uint32_t start_sample_index;
    uint32_t end_sample_index;
    uint32_t start_q_index;
    uint32_t end_q_index;
    psa_explain_certificate_t certificate;
    uint32_t sample_count;
    uint32_t signal_order;
    uint32_t pascal_order;
    uint32_t q_order;
    uint32_t trace_count;
    uint32_t q_trace_count;
    psa_explain_rule_t signal_rule;
    psa_explain_rule_t pascal_rule;
    psa_explain_rule_t q_rule;
    psa_real_t sample_trace[PSA_CFG_MAX_FRONTIER];
    psa_real_t pascal_trace[PSA_CFG_MAX_FRONTIER];
    psa_real_t q_trace[PSA_CFG_MAX_FRONTIER];
    uint32_t q_step_trace[PSA_CFG_MAX_FRONTIER];
    psa_real_t kernel_trace[PSA_CFG_MAX_FRONTIER];
    uint32_t kernel_step_trace[PSA_CFG_MAX_FRONTIER];
} psa_explain_segment_record_t;

typedef struct {
    bool armed;
    bool completed;
    bool committed_ready;
    uint32_t arm_sample_index;
    uint32_t arm_q_index;
    uint32_t appended_sample_count;
    uint32_t appended_q_count;
    uint32_t candidate_sample_index;
    uint32_t candidate_q_index;
    uint64_t committed_signature;
    uint64_t candidate_signature;
    uint32_t candidate_streak;
    uint32_t min_streak;
    psa_explain_certificate_t committed_certificate;
    psa_explain_segment_record_t segment;
} psa_explain_segment_recorder_t;

typedef struct {
    bool ready;
    uint32_t sample_index;
    uint32_t remaining;
    psa_explain_rule_t rule;
    psa_real_t state[PSA_CFG_MAX_ORDER];
} psa_explain_stream_player_t;

void psa_explain_rule_reset(psa_explain_rule_t *rule);
void psa_explain_component_reset(psa_explain_component_t *component);
void psa_analysis_record_reset(psa_analysis_record_t *record);
void psa_precision_report_reset(psa_precision_report_t *report);
void psa_explain_pascal_workspace_reset(psa_explain_pascal_workspace_t *workspace);
void psa_explain_certificate_reset(psa_explain_certificate_t *certificate);
void psa_explain_segment_record_reset(psa_explain_segment_record_t *segment);
void psa_explain_segment_recorder_reset(psa_explain_segment_recorder_t *recorder);
void psa_explain_stream_player_reset(psa_explain_stream_player_t *player);

psa_status_t psa_explain_capture_record(const psa_core_state_t *state, psa_analysis_record_t *record);
const psa_explain_rule_t *psa_explain_select_rule(const psa_analysis_record_t *record,
                                                  psa_explain_rule_kind_t kind);

psa_status_t psa_explain_replay_rule(const psa_explain_rule_t *rule,
                                     uint32_t future_count,
                                     psa_real_t *out_values,
                                     uint32_t out_capacity,
                                     uint32_t *written_count);

psa_status_t psa_explain_synthesize_rules(const psa_explain_rule_t *const *rules,
                                          const psa_real_t *weights,
                                          size_t rule_count,
                                          uint32_t future_count,
                                          psa_real_t *out_values,
                                          uint32_t out_capacity,
                                          uint32_t *written_count);

psa_status_t psa_explain_emit_component_sequence(const psa_explain_component_t *component,
                                                 uint32_t sample_count,
                                                 psa_real_t *out_values,
                                                 uint32_t out_capacity);

psa_status_t psa_explain_synthesize_components(const psa_explain_component_t *components,
                                               const psa_real_t *weights,
                                               size_t component_count,
                                               uint32_t sample_count,
                                               psa_real_t *out_values,
                                               uint32_t out_capacity);

psa_status_t psa_explain_measure_error(const psa_real_t *reference,
                                       const psa_real_t *candidate,
                                       uint32_t count,
                                       psa_precision_report_t *report);

int psa_explain_format_record(char *buffer, size_t buffer_size, const psa_analysis_record_t *record);
int psa_explain_format_structural_report(char *buffer,
                                         size_t buffer_size,
                                         const psa_analysis_record_t *record);

psa_status_t psa_explain_export_series(const psa_analysis_record_t *record,
                                       psa_explain_series_kind_t kind,
                                       psa_real_t *out_values,
                                       uint32_t out_capacity,
                                       uint32_t *written_count);

psa_status_t psa_explain_rebuild_signal_from_pascal(const psa_analysis_record_t *record,
                                                    psa_explain_pascal_workspace_t *workspace,
                                                    psa_real_t *out_values,
                                                    uint32_t out_capacity,
                                                    uint32_t *written_count);

psa_status_t psa_explain_segment_recorder_arm(psa_explain_segment_recorder_t *recorder,
                                              uint32_t min_streak);

psa_status_t psa_explain_segment_recorder_arm_from_record(psa_explain_segment_recorder_t *recorder,
                                                          const psa_analysis_record_t *record,
                                                          uint32_t min_streak);

psa_status_t psa_explain_segment_recorder_update(psa_explain_segment_recorder_t *recorder,
                                                 const psa_analysis_record_t *record);

bool psa_explain_segment_recorder_completed(const psa_explain_segment_recorder_t *recorder);

const psa_explain_segment_record_t *psa_explain_segment_recorder_get_segment(const psa_explain_segment_recorder_t *recorder);

psa_status_t psa_explain_build_certificate(const psa_analysis_record_t *record,
                                           psa_explain_certificate_t *certificate);

psa_status_t psa_explain_stream_player_begin_rule(const psa_explain_rule_t *rule,
                                                  uint32_t sample_count,
                                                  psa_explain_stream_player_t *player);

psa_status_t psa_explain_stream_player_begin_segment_signal(const psa_explain_segment_record_t *segment,
                                                            psa_explain_stream_player_t *player);

psa_status_t psa_explain_stream_player_next(psa_explain_stream_player_t *player,
                                            psa_real_t *out_value);

#ifdef __cplusplus
}
#endif

#endif
