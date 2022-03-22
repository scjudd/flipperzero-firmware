#include "decoder_hid37.h"
#include <furi_hal.h>

constexpr uint32_t clocks_in_us = 64;

constexpr uint32_t jitter_time_us = 20;
constexpr uint32_t min_time_us = 64;
constexpr uint32_t max_time_us = 80;

constexpr uint32_t min_clocks = (min_time_us - jitter_time_us) * clocks_in_us;
constexpr uint32_t mid_clocks = ((max_time_us - min_time_us) / 2 + min_time_us) * clocks_in_us;
constexpr uint32_t max_clocks = (max_time_us + jitter_time_us) * clocks_in_us;

bool DecoderHID37::read(uint8_t* data, uint8_t data_size) {
    bool result = false;
    furi_assert(data_size >= 4);

    if(ready) {
        result = true;
        hid.decode(
            reinterpret_cast<const uint8_t*>(&stored_data), sizeof(uint32_t) * 4, data, data_size);
        ready = false;
    }

    return result;
}

// This is called on every rising and falling edge, with the number of CPU
// 'clocks' which occurred since the last edge transition.
void DecoderHID37::process_front(bool rising_edge, uint32_t edge_clocks) {
    // If we have already read all the data, don't process anymore
    if(ready) return;

    // Determine the number of CPU clocks since our last pulse. Since this
    // method is called on both the rising and falling edge, all we do for the
    // rising edge is record the number of edge_clocks to be summed with the
    // edge_clocks from the falling edge on the next call, giving us the total
    // pulse_clocks.
    if(rising_edge) {
        pulse_clocks = edge_clocks;
        return;
    }
    pulse_clocks += edge_clocks;

    if(pulse_clocks > min_clocks && pulse_clocks < max_clocks) {

        // If the pulse duration < 72us, it's a 64us (short) pulse; otherwise,
        // it's an 80us (long) pulse.
        bool pulse_length = pulse_clocks >= mid_clocks;

        if(pulse_length == last_pulse_length) {
            pulse_count++;

            if(pulse_length == 1 && pulse_count > 4) {
                // We have at least 5 long pulses, that's a logical 1.
                pulse_count = 0;
                store_data(1);
            } else if(pulse_length == 0 && pulse_count > 5) {
                // We have at least 6 short pulses, that's a logical 0.
                pulse_count = 0;
                store_data(0);
            }

            return;
        }

        if(last_pulse_length == 1 && pulse_count > 2) {
            // If our last 3 or more pulses have been long, we caught the tail
            // end of a logical 1.
            store_data(1);
        } else if(last_pulse_length == 0 && pulse_count > 3) {
            // If our last 4 or more pulses have been short, we caught the tail
            // end of a logical 0.
            store_data(0);
        }

        pulse_count = 0;
        last_pulse_length = pulse_length;
    }
}

DecoderHID37::DecoderHID37() {
    reset_state();
}

void DecoderHID37::store_data(bool data) {
    stored_data[0] = (stored_data[0] << 1) | ((stored_data[1] >> 31) & 1);
    stored_data[1] = (stored_data[1] << 1) | ((stored_data[2] >> 31) & 1);
    stored_data[2] = (stored_data[2] << 1) | ((stored_data[3] >> 31) & 1);
    stored_data[3] = (stored_data[3] << 1) | data;

    if(hid.can_be_decoded(reinterpret_cast<const uint8_t*>(&stored_data), sizeof(uint32_t) * 4)) {
        ready = true;
    }
}

void DecoderHID37::reset_state() {
    last_pulse_length = false;
    pulse_count = 0;
    ready = false;
    pulse_clocks = 0;
}
