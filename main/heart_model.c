#include "heart_model.h"

#include <math.h>
#include <string.h>

#define HEART_RR_MIN_MS 250
#define HEART_RR_MAX_MS 3000
#define HEART_DUPLICATE_MS 700
#define HEART_FRESH_MS 6000
#define HEART_MAX_TICK_GAP_MS 5000

bool heart_parse_measurement(const uint8_t *data, uint16_t len, heart_packet_t *out)
{
    if (data == NULL || out == NULL || len < 2) return false;

    memset(out, 0, sizeof(*out));

    const uint8_t flags = data[0];
    uint16_t index = 1;
    uint16_t bpm;

    if ((flags & 0x01u) != 0) {
        if ((uint16_t)(index + 2) > len) return false;
        bpm = (uint16_t)data[index] | ((uint16_t)data[index + 1] << 8);
        index = (uint16_t)(index + 2);
    } else {
        if ((uint16_t)(index + 1) > len) return false;
        bpm = data[index];
        index = (uint16_t)(index + 1);
    }

    if (bpm < HEART_HR_MIN || bpm > HEART_HR_MAX) return false;

    // Bits 1..2: 0/1 = sensor contact unsupported, 2 = supported but no
    // contact, 3 = supported and in contact.
    const uint8_t contact = (uint8_t)((flags >> 1) & 0x03u);
    out->bpm = (uint8_t)bpm;
    out->contact_supported = contact >= 2;
    out->contact_detected = contact == 3;

    if ((flags & 0x08u) != 0) {
        if ((uint16_t)(index + 2) > len) return false;
        index = (uint16_t)(index + 2);  // Energy expended, not displayed.
    }

    if ((flags & 0x10u) != 0) {
        while ((uint16_t)(index + 2) <= len && out->rr_count < HEART_MAX_RR_PER_PACKET) {
            const uint16_t raw = (uint16_t)data[index] | ((uint16_t)data[index + 1] << 8);
            index = (uint16_t)(index + 2);

            const uint32_t rr_ms = ((uint32_t)raw * 1000u + 512u) / 1024u;
            if (rr_ms >= HEART_RR_MIN_MS && rr_ms <= HEART_RR_MAX_MS) {
                out->rr[out->rr_count++] = (uint16_t)rr_ms;
            }
        }
    }

    out->rr_ms = out->rr_count > 0 ? out->rr[0] : (uint16_t)(60000u / bpm);
    return true;
}

bool heart_parse_measurement_compat(const uint8_t *data, uint16_t len,
                                    heart_packet_t *out)
{
    if (data == NULL || out == NULL || len == 0) return false;
    if (heart_parse_measurement(data, len, out)) return true;

    uint16_t bpm = 0;
    if (len == 1) {
        bpm = data[0];
    } else if (len == 2) {
        const uint16_t le = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
        const uint16_t be = ((uint16_t)data[0] << 8) | (uint16_t)data[1];
        if (le >= HEART_HR_MIN && le <= HEART_HR_MAX) {
            bpm = le;
        } else if (be >= HEART_HR_MIN && be <= HEART_HR_MAX) {
            bpm = be;
        } else if (data[1] >= HEART_HR_MIN && data[1] <= HEART_HR_MAX) {
            // Common malformed variant: valid flags-like byte followed by an
            // 8-bit BPM, or a 16-bit value whose leading byte is not flags.
            bpm = data[1];
        }
    }

    if (bpm < HEART_HR_MIN || bpm > HEART_HR_MAX) return false;

    memset(out, 0, sizeof(*out));
    out->bpm = (uint8_t)bpm;
    out->rr_ms = (uint16_t)(60000u / bpm);
    return true;
}

uint16_t heart_max_hr(uint8_t age)
{
    if (age < 10) age = 10;
    if (age > 80) age = 80;
    return (uint16_t)(220 - age);
}

heart_zone_t heart_session_zone(const heart_session_t *session, uint16_t bpm)
{
    const uint8_t age = session != NULL ? session->age : 30;
    const uint16_t maximum = heart_max_hr(age);
    const uint32_t scaled = (uint32_t)bpm * 100u;

    if (scaled < (uint32_t)maximum * 50u) return HEART_ZONE_REST;
    if (scaled < (uint32_t)maximum * 60u) return HEART_ZONE_WARMUP;
    if (scaled < (uint32_t)maximum * 70u) return HEART_ZONE_FAT_BURN;
    if (scaled < (uint32_t)maximum * 80u) return HEART_ZONE_CARDIO;
    return HEART_ZONE_PEAK;
}

void heart_session_reset(heart_session_t *session, uint8_t age)
{
    if (session == NULL) return;

    memset(session, 0, sizeof(*session));
    session->age = age;
    session->last_zone = (uint8_t)HEART_ZONE_REST;
}

void heart_session_set_paused(heart_session_t *session, bool paused)
{
    if (session == NULL) return;
    session->paused = paused;
}

void heart_session_tick(heart_session_t *session, uint32_t now_ms)
{
    if (session == NULL) return;

    if (!session->tick_started) {
        session->tick_started = true;
        session->last_tick_ms = now_ms;
        return;
    }

    uint32_t delta = now_ms - session->last_tick_ms;
    session->last_tick_ms = now_ms;
    if (delta > HEART_MAX_TICK_GAP_MS) delta = HEART_MAX_TICK_GAP_MS;
    if (session->paused) return;

    session->elapsed_ms += delta;

    // Only credit zone time while the current reading is fresh. This prevents
    // a disconnected sensor from accumulating hours in the last known zone.
    const bool fresh = session->sample_count > 0 &&
                       (uint32_t)(now_ms - session->last_sample_ms) <= HEART_FRESH_MS;
    if (fresh && session->last_zone < HEART_ZONE_COUNT) {
        session->zone_ms[session->last_zone] += delta;
    }
}

static void push_rr(heart_session_t *session, uint16_t rr_ms)
{
    if (rr_ms < HEART_RR_MIN_MS || rr_ms > HEART_RR_MAX_MS) return;

    session->rr_window[session->rr_head] = rr_ms;
    session->rr_head = (uint8_t)((session->rr_head + 1u) % HEART_RR_WINDOW);
    if (session->rr_count < HEART_RR_WINDOW) session->rr_count++;
}

static void push_trend(heart_session_t *session, uint16_t bpm)
{
    session->trend[session->trend_head] = bpm;
    session->trend_head = (uint8_t)((session->trend_head + 1u) % HEART_TREND_LEN);
    if (session->trend_count < HEART_TREND_LEN) session->trend_count++;
}

bool heart_session_record(heart_session_t *session, const heart_packet_t *packet,
                          uint32_t now_ms)
{
    if (session == NULL || packet == NULL || session->paused) return false;
    if (packet->bpm < HEART_HR_MIN || packet->bpm > HEART_HR_MAX) return false;

    for (uint8_t i = 0; i < packet->rr_count; i++) {
        push_rr(session, packet->rr[i]);
    }

    if (session->sample_count > 0 &&
        (uint32_t)(now_ms - session->last_sample_ms) < HEART_DUPLICATE_MS) {
        return false;
    }

    const uint16_t bpm = packet->bpm;
    if (session->sample_count == 0 || bpm < session->min_bpm) session->min_bpm = bpm;
    if (session->sample_count == 0 || bpm > session->max_bpm) session->max_bpm = bpm;
    session->sum_bpm += bpm;
    session->sample_count++;
    session->last_bpm = bpm;
    session->last_rr_ms = packet->rr_ms;
    session->last_sample_ms = now_ms;
    session->last_zone = (uint8_t)heart_session_zone(session, bpm);
    push_trend(session, bpm);
    return true;
}

uint16_t heart_session_avg(const heart_session_t *session)
{
    if (session == NULL || session->sample_count == 0) return 0;
    return (uint16_t)((session->sum_bpm + session->sample_count / 2u) /
                      session->sample_count);
}

uint16_t heart_session_trend_at(const heart_session_t *session, uint8_t index)
{
    if (session == NULL || index >= session->trend_count) return 0;
    const uint8_t start =
        (uint8_t)((session->trend_head + HEART_TREND_LEN - session->trend_count) %
                  HEART_TREND_LEN);
    return session->trend[(uint8_t)((start + index) % HEART_TREND_LEN)];
}

static uint16_t rr_at(const heart_session_t *session, uint8_t index)
{
    const uint8_t start =
        session->rr_count < HEART_RR_WINDOW ? 0 : session->rr_head;
    return session->rr_window[(uint8_t)((start + index) % HEART_RR_WINDOW)];
}

int16_t heart_session_rmssd(const heart_session_t *session)
{
    if (session == NULL || session->rr_count < 2) return -1;

    uint64_t sum_squares = 0;
    uint16_t previous = rr_at(session, 0);
    for (uint8_t i = 1; i < session->rr_count; i++) {
        const uint16_t current = rr_at(session, i);
        const int32_t difference = (int32_t)current - (int32_t)previous;
        sum_squares += (uint64_t)(difference * difference);
        previous = current;
    }

    const uint32_t count = (uint32_t)session->rr_count - 1u;
    const float value = sqrtf((float)((double)sum_squares / (double)count));
    return (int16_t)(value + 0.5f);
}

uint32_t heart_session_measured_ms(const heart_session_t *session)
{
    if (session == NULL) return 0;
    uint32_t total = 0;
    for (uint8_t i = 0; i < HEART_ZONE_COUNT; i++) total += session->zone_ms[i];
    return total;
}

uint8_t heart_session_zone_percent(const heart_session_t *session, heart_zone_t zone)
{
    if (session == NULL || zone >= HEART_ZONE_COUNT) return 0;
    const uint32_t total = heart_session_measured_ms(session);
    if (total == 0) return 0;

    const uint32_t percent =
        (session->zone_ms[zone] * 100u + total / 2u) / total;
    return (uint8_t)(percent > 100u ? 100u : percent);
}
