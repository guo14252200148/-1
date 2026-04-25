#include "psa_collect.h"

#include <string.h>

static psa_real_t psa_collect_apply_map(const psa_collect_config_t *config, psa_real_t raw_value) {
    psa_real_t value = raw_value;
    if (config->invert) {
        value = -value;
    }
    value = config->map.gain * value + config->map.offset;
    return value;
}

void psa_collect_default_config(psa_collect_config_t *config) {
    if (config == NULL) {
        return;
    }
    config->invert = false;
    config->emit_delta = true;
    config->map.gain = 1.0;
    config->map.offset = 0.0;
}

void psa_collect_init(psa_collect_state_t *state, const psa_collect_config_t *config) {
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    if (config != NULL) {
        state->config = *config;
    } else {
        psa_collect_default_config(&state->config);
    }
}

psa_status_t psa_collect_push_real(psa_collect_state_t *state,
                                   psa_real_t raw_value,
                                   uint32_t timestamp_ticks,
                                   uint32_t input_flags,
                                   psa_collect_packet_t *packet_out) {
    psa_collect_packet_t packet;
    size_t slot;
    if (state == NULL) {
        return PSA_STATUS_BAD_ARGUMENT;
    }

    packet.index = state->next_index;
    packet.timestamp_ticks = timestamp_ticks;
    packet.flags = input_flags | PSA_SAMPLE_FLAG_VALID;
    packet.value = psa_collect_apply_map(&state->config, raw_value);
    packet.delta = 0.0;
    if (state->config.map.gain != 1.0 || state->config.map.offset != 0.0 || state->config.invert) {
        packet.flags |= PSA_SAMPLE_FLAG_AFFINE;
    }
    if (state->config.emit_delta && state->last_valid) {
        packet.delta = packet.value - state->last_value;
        packet.flags |= PSA_SAMPLE_FLAG_DELTA_VALID;
    }

    slot = state->write_index;
    state->ring[slot] = packet;
    state->write_index = (slot + 1U) % PSA_COLLECT_RING_CAPACITY;
    if (state->count < PSA_COLLECT_RING_CAPACITY) {
        state->count += 1U;
    }
    state->next_index += 1U;
    state->last_valid = true;
    state->last_value = packet.value;

    if (packet_out != NULL) {
        *packet_out = packet;
    }
    return PSA_STATUS_OK;
}

size_t psa_collect_push_real_block(psa_collect_state_t *state,
                                   const psa_real_t *raw_values,
                                   size_t count,
                                   uint32_t first_timestamp_ticks,
                                   uint32_t tick_stride,
                                   uint32_t input_flags,
                                   psa_collect_packet_t *packets_out,
                                   size_t packets_capacity) {
    size_t i;
    size_t emitted = 0U;
    if (state == NULL || raw_values == NULL) {
        return 0U;
    }
    for (i = 0U; i < count; ++i) {
        psa_collect_packet_t packet;
        psa_collect_packet_t *packet_out = NULL;
        if (packets_out != NULL && emitted < packets_capacity) {
            packet_out = &packet;
        }
        if (psa_collect_push_real(state,
                                  raw_values[i],
                                  first_timestamp_ticks + (uint32_t)i * tick_stride,
                                  input_flags,
                                  packet_out) != PSA_STATUS_OK) {
            break;
        }
        if (packet_out != NULL) {
            packets_out[emitted] = packet;
            emitted += 1U;
        }
    }
    return emitted;
}

psa_status_t psa_collect_push_i16(psa_collect_state_t *state,
                                  int16_t raw_value,
                                  uint32_t timestamp_ticks,
                                  uint32_t input_flags,
                                  psa_collect_packet_t *packet_out) {
    return psa_collect_push_real(state, (psa_real_t)raw_value, timestamp_ticks, input_flags, packet_out);
}

size_t psa_collect_push_i16_block(psa_collect_state_t *state,
                                  const int16_t *raw_values,
                                  size_t count,
                                  uint32_t first_timestamp_ticks,
                                  uint32_t tick_stride,
                                  uint32_t input_flags,
                                  psa_collect_packet_t *packets_out,
                                  size_t packets_capacity) {
    size_t i;
    size_t emitted = 0U;
    if (state == NULL || raw_values == NULL) {
        return 0U;
    }
    for (i = 0U; i < count; ++i) {
        psa_collect_packet_t packet;
        psa_collect_packet_t *packet_out = NULL;
        if (packets_out != NULL && emitted < packets_capacity) {
            packet_out = &packet;
        }
        if (psa_collect_push_i16(state,
                                 raw_values[i],
                                 first_timestamp_ticks + (uint32_t)i * tick_stride,
                                 input_flags,
                                 packet_out) != PSA_STATUS_OK) {
            break;
        }
        if (packet_out != NULL) {
            packets_out[emitted] = packet;
            emitted += 1U;
        }
    }
    return emitted;
}

psa_status_t psa_collect_push_u16(psa_collect_state_t *state,
                                  uint16_t raw_value,
                                  uint32_t timestamp_ticks,
                                  uint32_t input_flags,
                                  psa_collect_packet_t *packet_out) {
    return psa_collect_push_real(state, (psa_real_t)raw_value, timestamp_ticks, input_flags, packet_out);
}

size_t psa_collect_push_u16_block(psa_collect_state_t *state,
                                  const uint16_t *raw_values,
                                  size_t count,
                                  uint32_t first_timestamp_ticks,
                                  uint32_t tick_stride,
                                  uint32_t input_flags,
                                  psa_collect_packet_t *packets_out,
                                  size_t packets_capacity) {
    size_t i;
    size_t emitted = 0U;
    if (state == NULL || raw_values == NULL) {
        return 0U;
    }
    for (i = 0U; i < count; ++i) {
        psa_collect_packet_t packet;
        psa_collect_packet_t *packet_out = NULL;
        if (packets_out != NULL && emitted < packets_capacity) {
            packet_out = &packet;
        }
        if (psa_collect_push_u16(state,
                                 raw_values[i],
                                 first_timestamp_ticks + (uint32_t)i * tick_stride,
                                 input_flags,
                                 packet_out) != PSA_STATUS_OK) {
            break;
        }
        if (packet_out != NULL) {
            packets_out[emitted] = packet;
            emitted += 1U;
        }
    }
    return emitted;
}

psa_status_t psa_collect_push_i32(psa_collect_state_t *state,
                                  int32_t raw_value,
                                  uint32_t timestamp_ticks,
                                  uint32_t input_flags,
                                  psa_collect_packet_t *packet_out) {
    return psa_collect_push_real(state, (psa_real_t)raw_value, timestamp_ticks, input_flags, packet_out);
}

size_t psa_collect_push_i32_block(psa_collect_state_t *state,
                                  const int32_t *raw_values,
                                  size_t count,
                                  uint32_t first_timestamp_ticks,
                                  uint32_t tick_stride,
                                  uint32_t input_flags,
                                  psa_collect_packet_t *packets_out,
                                  size_t packets_capacity) {
    size_t i;
    size_t emitted = 0U;
    if (state == NULL || raw_values == NULL) {
        return 0U;
    }
    for (i = 0U; i < count; ++i) {
        psa_collect_packet_t packet;
        psa_collect_packet_t *packet_out = NULL;
        if (packets_out != NULL && emitted < packets_capacity) {
            packet_out = &packet;
        }
        if (psa_collect_push_i32(state,
                                 raw_values[i],
                                 first_timestamp_ticks + (uint32_t)i * tick_stride,
                                 input_flags,
                                 packet_out) != PSA_STATUS_OK) {
            break;
        }
        if (packet_out != NULL) {
            packets_out[emitted] = packet;
            emitted += 1U;
        }
    }
    return emitted;
}

bool psa_collect_latest(const psa_collect_state_t *state, psa_collect_packet_t *packet_out) {
    size_t index;
    if (state == NULL || packet_out == NULL || state->count == 0U) {
        return false;
    }
    index = (state->write_index + PSA_COLLECT_RING_CAPACITY - 1U) % PSA_COLLECT_RING_CAPACITY;
    *packet_out = state->ring[index];
    return true;
}

size_t psa_collect_snapshot(const psa_collect_state_t *state,
                            psa_collect_packet_t *out,
                            size_t max_count) {
    size_t copy_count;
    size_t i;
    if (state == NULL || out == NULL || max_count == 0U) {
        return 0U;
    }
    copy_count = (state->count < max_count) ? state->count : max_count;
    for (i = 0; i < copy_count; ++i) {
        size_t src = (state->write_index + PSA_COLLECT_RING_CAPACITY - copy_count + i) % PSA_COLLECT_RING_CAPACITY;
        out[i] = state->ring[src];
    }
    return copy_count;
}
