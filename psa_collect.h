#ifndef PSA_COLLECT_H
#define PSA_COLLECT_H

#include "psa_base.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PSA_COLLECT_RING_CAPACITY
#define PSA_COLLECT_RING_CAPACITY 256U
#endif

enum {
    PSA_SAMPLE_FLAG_VALID = 1u << 0,
    PSA_SAMPLE_FLAG_CLIPPED = 1u << 1,
    PSA_SAMPLE_FLAG_AFFINE = 1u << 2,
    PSA_SAMPLE_FLAG_DELTA_VALID = 1u << 3
};

typedef struct {
    psa_real_t gain;
    psa_real_t offset;
} psa_affine_map_t;

typedef struct {
    bool invert;
    bool emit_delta;
    psa_affine_map_t map;
} psa_collect_config_t;

typedef struct {
    uint32_t index;
    uint32_t timestamp_ticks;
    uint32_t flags;
    psa_real_t value;
    psa_real_t delta;
} psa_collect_packet_t;

typedef struct {
    psa_collect_config_t config;
    psa_collect_packet_t ring[PSA_COLLECT_RING_CAPACITY];
    size_t write_index;
    size_t count;
    uint32_t next_index;
    bool last_valid;
    psa_real_t last_value;
} psa_collect_state_t;

void psa_collect_default_config(psa_collect_config_t *config);
void psa_collect_init(psa_collect_state_t *state, const psa_collect_config_t *config);

psa_status_t psa_collect_push_real(psa_collect_state_t *state,
                                   psa_real_t raw_value,
                                   uint32_t timestamp_ticks,
                                   uint32_t input_flags,
                                   psa_collect_packet_t *packet_out);

size_t psa_collect_push_real_block(psa_collect_state_t *state,
                                   const psa_real_t *raw_values,
                                   size_t count,
                                   uint32_t first_timestamp_ticks,
                                   uint32_t tick_stride,
                                   uint32_t input_flags,
                                   psa_collect_packet_t *packets_out,
                                   size_t packets_capacity);

psa_status_t psa_collect_push_i16(psa_collect_state_t *state,
                                  int16_t raw_value,
                                  uint32_t timestamp_ticks,
                                  uint32_t input_flags,
                                  psa_collect_packet_t *packet_out);

size_t psa_collect_push_i16_block(psa_collect_state_t *state,
                                  const int16_t *raw_values,
                                  size_t count,
                                  uint32_t first_timestamp_ticks,
                                  uint32_t tick_stride,
                                  uint32_t input_flags,
                                  psa_collect_packet_t *packets_out,
                                  size_t packets_capacity);

psa_status_t psa_collect_push_u16(psa_collect_state_t *state,
                                  uint16_t raw_value,
                                  uint32_t timestamp_ticks,
                                  uint32_t input_flags,
                                  psa_collect_packet_t *packet_out);

size_t psa_collect_push_u16_block(psa_collect_state_t *state,
                                  const uint16_t *raw_values,
                                  size_t count,
                                  uint32_t first_timestamp_ticks,
                                  uint32_t tick_stride,
                                  uint32_t input_flags,
                                  psa_collect_packet_t *packets_out,
                                  size_t packets_capacity);

psa_status_t psa_collect_push_i32(psa_collect_state_t *state,
                                  int32_t raw_value,
                                  uint32_t timestamp_ticks,
                                  uint32_t input_flags,
                                  psa_collect_packet_t *packet_out);

size_t psa_collect_push_i32_block(psa_collect_state_t *state,
                                  const int32_t *raw_values,
                                  size_t count,
                                  uint32_t first_timestamp_ticks,
                                  uint32_t tick_stride,
                                  uint32_t input_flags,
                                  psa_collect_packet_t *packets_out,
                                  size_t packets_capacity);

bool psa_collect_latest(const psa_collect_state_t *state, psa_collect_packet_t *packet_out);
size_t psa_collect_snapshot(const psa_collect_state_t *state,
                            psa_collect_packet_t *out,
                            size_t max_count);

#ifdef __cplusplus
}
#endif

#endif
