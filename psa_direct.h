#ifndef PSA_DIRECT_H
#define PSA_DIRECT_H

#include "psa_explain.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PSA_DIRECT_KIND_UNKNOWN = 0,
    PSA_DIRECT_KIND_ORDER1_REAL = 1,
    PSA_DIRECT_KIND_ORDER2_REAL_DISTINCT = 2,
    PSA_DIRECT_KIND_ORDER2_REAL_REPEATED = 3,
    PSA_DIRECT_KIND_ORDER2_OSCILLATORY = 4
} psa_direct_kind_t;

typedef struct {
    bool valid;
    psa_direct_kind_t kind;
    bool oscillatory;
    psa_real_t alpha_re;
    psa_real_t alpha_im;
    psa_real_t lambda_re;
    psa_real_t lambda_im;
    psa_real_t amplitude_re;
    psa_real_t amplitude_im;
    psa_real_t jordan_re;
    psa_real_t jordan_im;
    psa_real_t rho;
    psa_real_t theta;
    psa_real_t phase;
    psa_real_t frequency_norm;
    psa_real_t frequency_hz;
    psa_real_t energy;
    uint32_t source_rule_index;
    bool harmonic;
    uint32_t harmonic_multiple;
} psa_direct_component_t;

typedef struct {
    bool ready;
    uint32_t source_order;
    uint32_t component_count;
    psa_direct_component_t components[2];
    int32_t dominant_index;
    uint32_t primary_count;
    uint32_t harmonic_count;
} psa_direct_result_t;

typedef struct {
    bool ready;
    uint32_t component_count;
    psa_direct_component_t components[PSA_CFG_MAX_ORDER];
    int32_t dominant_index;
    uint32_t primary_count;
    uint32_t harmonic_count;
} psa_direct_family_t;

typedef struct {
    bool ready;
    bool finite_certified;
    uint32_t start_level;
    psa_real_t a;
    uint32_t coeff_count;
    uint32_t lambda_count;
    psa_real_t coeff[PSA_CFG_MAX_ORDER + 1];
    psa_real_t lambda[PSA_CFG_MAX_ORDER];
    psa_real_t scale;
    bool constant;
    psa_real_t constant_value;
    uint32_t period;
    uint64_t signature;
} psa_direct_sigma_t;

typedef struct {
    bool active;
    bool oscillatory;
    uint32_t dim;
    psa_real_t matrix[4];
    psa_real_t lambda_re[2];
    psa_real_t lambda_im[2];
    psa_real_t alpha_re[2];
    psa_real_t alpha_im[2];
    psa_real_t weight;
    psa_q_rule_class_t q_class;
    uint32_t period;
    uint32_t source_rule_index;
    bool harmonic;
    uint32_t harmonic_multiple;
} psa_direct_block_t;

typedef struct {
    bool ready;
    bool committed_like;
    uint32_t block_count;
    uint32_t total_dimension;
    psa_q_rule_class_t q_class;
    uint32_t period;
    uint64_t signature;
    psa_direct_block_t blocks[PSA_CFG_MAX_ORDER];
    int32_t dominant_block;
    uint32_t primary_count;
    uint32_t harmonic_count;
} psa_direct_block_family_t;

typedef struct {
    bool ready;
    uint32_t dim;
    psa_q_rule_class_t q_class;
    uint32_t period;
    uint64_t signature;
    psa_real_t weight;
    psa_real_t matrix[PSA_CFG_MAX_ORDER * PSA_CFG_MAX_ORDER];
    psa_real_t state[PSA_CFG_MAX_ORDER];
    psa_real_t coefficients[PSA_CFG_MAX_ORDER + 1];
    psa_real_t seeds[PSA_CFG_MAX_ORDER];
} psa_direct_high_block_t;

typedef struct {
    bool ready;
    uint32_t start_level;
    psa_real_t a;
    uint32_t level_count;
    uint32_t total_dimension;
    uint32_t part_count;
    psa_q_rule_class_t q_class;
    uint32_t period;
    uint64_t signature;
    psa_direct_block_family_t base_family;
    psa_direct_high_block_t parts[PSA_CFG_MAX_ORDER];
    psa_real_t lambda[PSA_CFG_MAX_ORDER];
    psa_real_t coeff[PSA_CFG_MAX_ORDER + 1];
    psa_real_t level_weight[PSA_CFG_MAX_ORDER];
} psa_direct_connection_object_t;

void psa_direct_component_reset(psa_direct_component_t *component);
void psa_direct_result_reset(psa_direct_result_t *result);
void psa_direct_family_reset(psa_direct_family_t *family);
void psa_direct_sigma_reset(psa_direct_sigma_t *sigma);
void psa_direct_block_reset(psa_direct_block_t *block);
void psa_direct_block_family_reset(psa_direct_block_family_t *family);
void psa_direct_high_block_reset(psa_direct_high_block_t *block);
void psa_direct_connection_object_reset(psa_direct_connection_object_t *object);

psa_status_t psa_direct_decode_rule(const psa_explain_rule_t *rule,
                                    psa_real_t sample_rate_hz,
                                    psa_direct_result_t *result);

psa_status_t psa_direct_decode_signal_record(const psa_analysis_record_t *record,
                                             psa_real_t sample_rate_hz,
                                             psa_direct_result_t *result);

psa_status_t psa_direct_decode_rule_family(const psa_explain_rule_t *const *rules,
                                           size_t rule_count,
                                           psa_real_t sample_rate_hz,
                                           uint32_t energy_length,
                                           psa_direct_family_t *family);

psa_status_t psa_direct_extract_sigma(const psa_analysis_record_t *record,
                                      psa_direct_sigma_t *sigma);

psa_status_t psa_direct_blocks_from_result(const psa_direct_result_t *result,
                                           psa_direct_block_family_t *family);

psa_status_t psa_direct_blocks_from_family(const psa_direct_family_t *direct_family,
                                           psa_direct_block_family_t *block_family);

psa_status_t psa_direct_expand_sigma_connections(const psa_direct_sigma_t *sigma,
                                                 psa_direct_block_family_t *family);

psa_status_t psa_direct_high_block_from_rule(const psa_explain_rule_t *rule,
                                             uint32_t energy_length,
                                             psa_direct_high_block_t *block);

psa_status_t psa_direct_connection_object_from_sigma_and_family(const psa_direct_sigma_t *sigma,
                                                                const psa_direct_block_family_t *family,
                                                                psa_direct_connection_object_t *object);

psa_status_t psa_direct_connection_object_from_block_family(const psa_direct_block_family_t *family,
                                                            psa_direct_connection_object_t *object);

psa_status_t psa_direct_connection_object_from_high_block(const psa_direct_high_block_t *block,
                                                          psa_direct_connection_object_t *object);

psa_status_t psa_direct_connection_object_lift_sigma(const psa_direct_sigma_t *sigma,
                                                     const psa_direct_connection_object_t *base_object,
                                                     psa_direct_connection_object_t *object);

psa_status_t psa_direct_reconstruct_high_block(const psa_direct_high_block_t *block,
                                               uint32_t sample_count,
                                               psa_real_t *out_values,
                                               uint32_t out_capacity);

psa_status_t psa_direct_rebuild_connection_profile(const psa_direct_connection_object_t *object,
                                                   psa_real_t *out_values,
                                                   uint32_t out_capacity,
                                                   uint32_t *written_count);

psa_status_t psa_direct_emit_component_sequence(const psa_direct_component_t *component,
                                                uint32_t sample_count,
                                                psa_real_t *out_values,
                                                uint32_t out_capacity);

psa_status_t psa_direct_reconstruct(const psa_direct_result_t *result,
                                    uint32_t sample_count,
                                    psa_real_t *out_values,
                                    uint32_t out_capacity);

psa_status_t psa_direct_synthesize_family(const psa_direct_family_t *family,
                                          const psa_real_t *weights,
                                          uint32_t sample_count,
                                          psa_real_t *out_values,
                                          uint32_t out_capacity);

int psa_direct_format_result(char *buffer, size_t buffer_size, const psa_direct_result_t *result);
int psa_direct_format_family(char *buffer, size_t buffer_size, const psa_direct_family_t *family);
int psa_direct_format_sigma(char *buffer, size_t buffer_size, const psa_direct_sigma_t *sigma);
int psa_direct_format_block_family(char *buffer, size_t buffer_size, const psa_direct_block_family_t *family);
int psa_direct_format_high_block(char *buffer, size_t buffer_size, const psa_direct_high_block_t *block);
int psa_direct_format_connection_object(char *buffer, size_t buffer_size, const psa_direct_connection_object_t *object);

#ifdef __cplusplus
}
#endif

#endif
