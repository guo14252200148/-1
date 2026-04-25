#ifndef PSA_CORE_H
#define PSA_CORE_H

#include "psa_base.h"
#include "psa_collect.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t page_index;
    uint32_t sample_count;
    int32_t signal_pow2_exponent;
    int32_t pascal_pow2_exponent;
    uint32_t signal_order;
    uint32_t pascal_order;
    uint64_t combined_signature;
    psa_q_rule_class_t q_class;
} psa_page_summary_t;

typedef struct {
    psa_real_t C[PSA_CFG_MAX_ORDER + 1];
    psa_real_t B[PSA_CFG_MAX_ORDER + 1];
    psa_real_t T[PSA_CFG_MAX_ORDER + 1];
    psa_real_t b;
    uint32_t L;
    uint32_t m;
} psa_recurrence_state_t;

typedef struct {
    uint32_t sample_count;
    uint32_t max_samples;
    int32_t signal_pow2_exponent;
    int32_t pascal_pow2_exponent;
    psa_real_t sample_trace[PSA_CFG_MAX_FRONTIER];
    psa_real_t pascal_trace[PSA_CFG_MAX_FRONTIER];
    psa_dd_real_t frontier[PSA_CFG_MAX_FRONTIER];
    psa_real_t signal_history[PSA_CFG_MAX_ORDER + 1];
    psa_real_t pascal_history[PSA_CFG_MAX_ORDER + 1];
    uint32_t signal_history_count;
    uint32_t pascal_history_count;
    uint32_t q_count;
    psa_real_t q_trace[PSA_CFG_MAX_FRONTIER];
    uint32_t q_step_trace[PSA_CFG_MAX_FRONTIER];
    psa_real_t kernel_trace[PSA_CFG_MAX_FRONTIER];
    uint32_t kernel_step_trace[PSA_CFG_MAX_FRONTIER];
    psa_real_t q_history[PSA_CFG_MAX_ORDER + 1];
    uint32_t q_history_count;
    psa_real_t kernel_history[PSA_CFG_MAX_ORDER + 1];
    uint32_t kernel_history_count;
    bool q_last_defined;
    psa_real_t q_last;
    bool q_constant;
    psa_real_t q_constant_value;
    uint32_t q_period;
    bool kernel_constant;
    psa_real_t kernel_constant_value;
    uint32_t kernel_period;
    psa_recurrence_state_t signal_rule;
    psa_recurrence_state_t pascal_rule;
    psa_recurrence_state_t q_rule_state;
    psa_q_rule_class_t q_class;
    uint64_t sigma_signature;
    uint64_t state_signature;
    uint64_t combined_signature;
    uint64_t cert_history_signatures[PSA_CFG_CERT_HISTORY];
    int32_t cert_history_steps[PSA_CFG_CERT_HISTORY];
    uint32_t cert_history_count;
    psa_q_certificate_t certificate;
    psa_page_summary_t pages[PSA_CFG_MAX_PAGES];
    uint32_t page_count;
    uint32_t page_total_count;
    uint32_t page_write_index;
} psa_core_state_t;

void psa_core_init(psa_core_state_t *state);
psa_status_t psa_core_push_packet(psa_core_state_t *state, const psa_collect_packet_t *packet);
psa_status_t psa_core_push_value(psa_core_state_t *state, psa_real_t value);
void psa_core_get_summary(const psa_core_state_t *state, psa_summary_t *summary);
void psa_core_get_certificate(const psa_core_state_t *state, psa_q_certificate_t *certificate);
bool psa_core_get_last_page(const psa_core_state_t *state, psa_page_summary_t *summary);

#ifdef __cplusplus
}
#endif

#endif
