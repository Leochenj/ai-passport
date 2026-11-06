// Pure heart-rate parsing, session statistics, and zone logic.
//
// This module deliberately has no ESP-IDF or LVGL dependency so the protocol
// edge cases and the session arithmetic can be exercised by host tests.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define HEART_HR_MIN 25
#define HEART_HR_MAX 250
#define HEART_TREND_LEN 90
#define HEART_RR_WINDOW 16
#define HEART_MAX_RR_PER_PACKET 8
#define HEART_ZONE_COUNT 5

// One decoded Bluetooth SIG Heart Rate Measurement (0x2A37) notification.
typedef struct {
    uint8_t bpm;
    bool contact_supported;
    bool contact_detected;
    uint16_t rr_ms;
    uint8_t rr_count;
    uint16_t rr[HEART_MAX_RR_PER_PACKET];
} heart_packet_t;

typedef enum {
    HEART_ZONE_REST = 0,
    HEART_ZONE_WARMUP,
    HEART_ZONE_FAT_BURN,
    HEART_ZONE_CARDIO,
    HEART_ZONE_PEAK,
} heart_zone_t;

typedef struct {
    uint8_t age;
    uint32_t elapsed_ms;
    uint32_t zone_ms[HEART_ZONE_COUNT];
    uint16_t min_bpm;
    uint16_t max_bpm;
    uint32_t sum_bpm;
    uint32_t sample_count;
    uint16_t trend[HEART_TREND_LEN];
    uint8_t trend_count;
    uint8_t trend_head;
    uint16_t rr_window[HEART_RR_WINDOW];
    uint8_t rr_count;
    uint8_t rr_head;
    uint16_t last_bpm;
    uint16_t last_rr_ms;
    uint32_t last_sample_ms;
    uint8_t last_zone;
    bool paused;
    bool tick_started;
    uint32_t last_tick_ms;
} heart_session_t;

// Decode one Heart Rate Measurement notification. Returns false for a short,
// malformed, or physiologically implausible frame. RR values are converted from
// the protocol's 1/1024 s units to milliseconds.
bool heart_parse_measurement(const uint8_t *data, uint16_t len, heart_packet_t *out);

// Compatibility path for iPhone broadcasters that do not emit a complete
// standard Heart Rate Measurement frame. After the standard parser rejects a
// frame, accepts a plausible 8-bit BPM at byte 0 or 1, or a 16-bit BPM at
// bytes 0..1 in either byte order. A frame with no plausible BPM is rejected.
bool heart_parse_measurement_compat(const uint8_t *data, uint16_t len,
                                    heart_packet_t *out);

uint16_t heart_max_hr(uint8_t age);

// Age is clamped to 10..80 when computing maximum heart rate. Zone boundaries
// are 50/60/70/80 percent of that estimate.
heart_zone_t heart_session_zone(const heart_session_t *session, uint16_t bpm);

void heart_session_reset(heart_session_t *session, uint8_t age);
void heart_session_set_paused(heart_session_t *session, bool paused);

// Accumulate elapsed time and time-in-zone. The caller invokes this at least
// once per second; gaps longer than five seconds are not credited, so a paused
// or rebooted device cannot inflate the zone distribution.
void heart_session_tick(heart_session_t *session, uint32_t now_ms);

// Record one decoded packet. Duplicate packets closer than 700 ms are ignored
// for min/avg/max and the trend, while their RR values still update the HRV
// window. Returns true when a new sample was accepted.
bool heart_session_record(heart_session_t *session, const heart_packet_t *packet,
                          uint32_t now_ms);

// 0 when no sample has been recorded.
uint16_t heart_session_avg(const heart_session_t *session);

// Chronological trend access: index 0 is the oldest retained sample.
uint16_t heart_session_trend_at(const heart_session_t *session, uint8_t index);

// RMSSD in milliseconds, or -1 when fewer than two real RR intervals exist.
int16_t heart_session_rmssd(const heart_session_t *session);

// Percentage of measured time spent in a zone, 0..100. Zero when no time has
// been attributed yet.
uint8_t heart_session_zone_percent(const heart_session_t *session, heart_zone_t zone);

// Lifetime counters are 32-bit; this keeps the average computation safe.
uint32_t heart_session_measured_ms(const heart_session_t *session);
