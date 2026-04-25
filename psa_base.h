#ifndef PSA_BASE_H
#define PSA_BASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PSA_CFG_MAX_ORDER
#define PSA_CFG_MAX_ORDER 24
#endif

#ifndef PSA_CFG_PAGE_CAPACITY
#define PSA_CFG_PAGE_CAPACITY 128U
#endif

#ifndef PSA_CFG_MAX_PAGES
#define PSA_CFG_MAX_PAGES 8U
#endif

#ifndef PSA_CFG_MAX_FRONTIER
#define PSA_CFG_MAX_FRONTIER 4096U
#endif

#ifndef PSA_CFG_TEXT_BUFFER
#define PSA_CFG_TEXT_BUFFER 256U
#endif

#ifndef PSA_CFG_CERT_HISTORY
#define PSA_CFG_CERT_HISTORY 256U
#endif

#ifndef PSA_REAL_TYPE
#define PSA_REAL_TYPE double
#endif

typedef PSA_REAL_TYPE psa_real_t;

typedef struct {
    psa_real_t hi;
    psa_real_t lo;
} psa_dd_real_t;

typedef enum {
    PSA_STATUS_OK = 0,
    PSA_STATUS_BAD_ARGUMENT = 1,
    PSA_STATUS_CAPACITY = 2,
    PSA_STATUS_NUMERIC = 3
} psa_status_t;

typedef enum {
    PSA_Q_RULE_UNKNOWN = 0,
    PSA_Q_RULE_ABSORBING = 1,
    PSA_Q_RULE_FINITE_RETURN = 2,
    PSA_Q_RULE_NONRETURN = 3
} psa_q_rule_class_t;

typedef struct {
    bool ready;
    psa_q_rule_class_t q_class;
    uint64_t sigma_signature;
    uint64_t state_signature;
    uint64_t combined_signature;
    uint64_t q_signature;
    uint32_t return_period;
    int32_t return_start_step;
    bool absorb_certified;
    bool finite_return_certified;
    bool nonreturn_certified;
    bool kernel_lift_certified;
    bool kernel_lift_multiblock;
    psa_q_rule_class_t kernel_lift_class;
    bool sigma_radial_ready;
    bool sigma_radial_finite_certified;
    uint32_t sigma_start_level;
    psa_real_t sigma_a;
    psa_real_t sigma_coeff[PSA_CFG_MAX_ORDER + 1];
    uint32_t sigma_coeff_count;
    psa_real_t sigma_lambda[PSA_CFG_MAX_ORDER];
    uint32_t sigma_lambda_count;
    psa_real_t sigma_scale;
    bool sigma_constant;
    psa_real_t sigma_constant_value;
    uint32_t sigma_period;
    uint64_t sigma_radial_signature;
    bool block_rule_active;
    bool block_rule_oscillatory;
    psa_real_t block_matrix[4];
    psa_real_t block_state[2];
    psa_real_t block_lambda_re[2];
    psa_real_t block_lambda_im[2];
    uint64_t block_signature;
    psa_q_rule_class_t block_class;
    uint32_t block_period;
    bool multiblock_ready;
    uint32_t block_count;
    psa_q_rule_class_t multiblock_class;
    uint32_t multiblock_period;
    uint64_t signature;
} psa_q_certificate_t;

typedef struct {
    psa_real_t sum;
    psa_real_t comp;
} psa_kahan_t;

typedef struct {
    bool ready;
    uint32_t sample_count;
    uint32_t page_count;
    uint32_t total_page_count;
    uint32_t signal_order;
    uint32_t pascal_order;
    uint32_t q_order;
    uint32_t q_count;
    uint32_t kernel_count;
    int32_t signal_pow2_exponent;
    int32_t pascal_pow2_exponent;
    psa_real_t latest_sample;
    psa_real_t latest_pascal;
    psa_real_t latest_q;
    psa_q_rule_class_t q_class;
    bool q_defined;
    bool q_constant;
    psa_real_t q_constant_value;
    uint32_t q_period;
    bool kernel_constant;
    psa_real_t kernel_constant_value;
    uint32_t kernel_period;
    uint64_t sigma_signature;
    uint64_t state_signature;
    uint64_t combined_signature;
    psa_q_certificate_t certificate;
} psa_summary_t;

psa_real_t psa_real_abs(psa_real_t value);
psa_real_t psa_real_max(psa_real_t a, psa_real_t b);
psa_real_t psa_real_machine_epsilon(void);
psa_real_t psa_real_scaled_epsilon(psa_real_t scale);

psa_dd_real_t psa_dd_from_real(psa_real_t value);
psa_dd_real_t psa_dd_add(psa_dd_real_t a, psa_dd_real_t b);
psa_dd_real_t psa_dd_sub(psa_dd_real_t a, psa_dd_real_t b);
psa_dd_real_t psa_dd_scale_pow2(psa_dd_real_t value, int exponent);
psa_real_t psa_dd_to_real(psa_dd_real_t value);

psa_real_t psa_scale_pow2_real(psa_real_t value, int exponent);
void psa_scale_pow2_real_array(psa_real_t *values, size_t count, int exponent);
void psa_scale_pow2_dd_array(psa_dd_real_t *values, size_t count, int exponent);

void psa_kahan_reset(psa_kahan_t *acc);
void psa_kahan_add(psa_kahan_t *acc, psa_real_t value);

uint64_t psa_fnv1a_mix(uint64_t hash, uint64_t chunk);
int64_t psa_quantize_value(psa_real_t value, psa_real_t scale);
uint64_t psa_signature_from_reals(const psa_real_t *values, size_t count, psa_real_t scale);

void psa_summary_reset(psa_summary_t *summary);
const char *psa_status_string(psa_status_t status);
const char *psa_q_rule_name(psa_q_rule_class_t q_class);
bool psa_q_certificate_committable(const psa_q_certificate_t *certificate);
const char *psa_q_certificate_name(const psa_q_certificate_t *certificate);
int psa_format_certificate(char *buffer, size_t buffer_size, const psa_q_certificate_t *certificate);
int psa_format_summary(char *buffer, size_t buffer_size, const psa_summary_t *summary);

#ifdef __cplusplus
}
#endif

#endif
