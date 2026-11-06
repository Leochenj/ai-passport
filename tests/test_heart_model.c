#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "heart_model.h"

static void test_parse_8bit(void)
{
    const uint8_t frame[] = {0x00, 72};
    heart_packet_t packet;
    assert(heart_parse_measurement(frame, sizeof(frame), &packet));
    assert(packet.bpm == 72);
    assert(!packet.contact_supported);
    assert(!packet.contact_detected);
    assert(packet.rr_count == 0);
    assert(packet.rr_ms == 833);
}

static void test_parse_contact(void)
{
    const uint8_t contact[] = {0x06, 64};        // supported, in contact
    const uint8_t no_contact[] = {0x04, 64};     // supported, no contact
    heart_packet_t packet;

    assert(heart_parse_measurement(contact, sizeof(contact), &packet));
    assert(packet.contact_supported);
    assert(packet.contact_detected);

    assert(heart_parse_measurement(no_contact, sizeof(no_contact), &packet));
    assert(packet.contact_supported);
    assert(!packet.contact_detected);
}

static void test_parse_16bit_energy_rr(void)
{
    // 16-bit HR, energy expended, RR present. RR raw 1024 = 1000 ms.
    const uint8_t frame[] = {
        0x19, 0x96, 0x00, 0x34, 0x12, 0x00, 0x04,
    };
    heart_packet_t packet;
    assert(heart_parse_measurement(frame, sizeof(frame), &packet));
    assert(packet.bpm == 150);
    assert(!packet.contact_supported);
    assert(packet.rr_count == 1);
    assert(packet.rr[0] == 1000);
    assert(packet.rr_ms == 1000);
}

static void test_parse_rejects_bad_frames(void)
{
    heart_packet_t packet;
    const uint8_t too_short[] = {0x00};
    const uint8_t truncated_hr16[] = {0x01, 0x50};
    const uint8_t truncated_energy[] = {0x08, 0x50};
    const uint8_t low[] = {0x00, 24};
    const uint8_t high[] = {0x00, 251};
    const uint8_t invalid_rr[] = {0x10, 0x50, 0x00, 0x00};

    assert(!heart_parse_measurement(too_short, sizeof(too_short), &packet));
    assert(!heart_parse_measurement(truncated_hr16, sizeof(truncated_hr16), &packet));
    assert(!heart_parse_measurement(truncated_energy, sizeof(truncated_energy), &packet));
    assert(!heart_parse_measurement(low, sizeof(low), &packet));
    assert(!heart_parse_measurement(high, sizeof(high), &packet));
    assert(heart_parse_measurement(invalid_rr, sizeof(invalid_rr), &packet));
    assert(packet.bpm == 80);
    assert(packet.rr_count == 0);
    assert(packet.rr_ms == 750);
    assert(!heart_parse_measurement(NULL, 2, &packet));
}

static void test_parse_multiple_rr(void)
{
    // Raw 512 = 500 ms, 768 = 750 ms, 0 is invalid and skipped, 4096 = 4000 ms
    // is outside the plausible window and skipped.
    const uint8_t frame[] = {
        0x10, 0x50, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10,
    };
    heart_packet_t packet;
    assert(heart_parse_measurement(frame, sizeof(frame), &packet));
    assert(packet.rr_count == 2);
    assert(packet.rr[0] == 500);
    assert(packet.rr[1] == 750);
}

static void test_zones(void)
{
    heart_session_t session;
    heart_session_reset(&session, 30);
    assert(heart_max_hr(30) == 190);
    assert(heart_max_hr(5) == 210);
    assert(heart_max_hr(95) == 140);

    assert(heart_session_zone(&session, 94) == HEART_ZONE_REST);
    assert(heart_session_zone(&session, 95) == HEART_ZONE_WARMUP);
    assert(heart_session_zone(&session, 113) == HEART_ZONE_WARMUP);
    assert(heart_session_zone(&session, 114) == HEART_ZONE_FAT_BURN);
    assert(heart_session_zone(&session, 132) == HEART_ZONE_FAT_BURN);
    assert(heart_session_zone(&session, 133) == HEART_ZONE_CARDIO);
    assert(heart_session_zone(&session, 151) == HEART_ZONE_CARDIO);
    assert(heart_session_zone(&session, 152) == HEART_ZONE_PEAK);
    assert(heart_session_zone(&session, 200) == HEART_ZONE_PEAK);
}

static heart_packet_t packet(uint8_t bpm, uint16_t rr_ms)
{
    heart_packet_t result;
    memset(&result, 0, sizeof(result));
    result.bpm = bpm;
    result.rr_ms = rr_ms;
    if (rr_ms > 0) {
        result.rr_count = 1;
        result.rr[0] = rr_ms;
    }
    return result;
}

static void test_session_statistics(void)
{
    heart_session_t session;
    heart_session_reset(&session, 30);

    heart_packet_t first = packet(70, 850);
    heart_packet_t second = packet(90, 670);
    heart_packet_t third = packet(80, 750);
    assert(heart_session_record(&session, &first, 1000));
    assert(heart_session_record(&session, &second, 2000));
    assert(heart_session_record(&session, &third, 3000));

    assert(session.sample_count == 3);
    assert(session.min_bpm == 70);
    assert(session.max_bpm == 90);
    assert(heart_session_avg(&session) == 80);
    assert(session.last_bpm == 80);
    assert(session.last_zone == HEART_ZONE_REST);
    assert(session.trend_count == 3);
    assert(heart_session_trend_at(&session, 0) == 70);
    assert(heart_session_trend_at(&session, 1) == 90);
    assert(heart_session_trend_at(&session, 2) == 80);
    assert(heart_session_trend_at(&session, 3) == 0);

    // A duplicate BPM inside 700 ms must not move the statistics, but its RR
    // interval is still valid for RMSSD.
    const uint32_t rr_before = session.rr_count;
    heart_packet_t duplicate = packet(80, 800);
    assert(!heart_session_record(&session, &duplicate, 3400));
    assert(session.sample_count == 3);
    assert(session.rr_count == rr_before + 1);
}

static void test_pause_and_tick(void)
{
    heart_session_t session;
    heart_session_reset(&session, 30);
    heart_session_tick(&session, 0);
    const heart_packet_t p = packet(100, 600);
    assert(heart_session_record(&session, &p, 100));

    heart_session_tick(&session, 1000);
    assert(session.elapsed_ms == 1000);
    assert(heart_session_measured_ms(&session) == 1000);
    assert(session.zone_ms[HEART_ZONE_WARMUP] == 1000);

    heart_session_set_paused(&session, true);
    heart_session_tick(&session, 2000);
    assert(session.elapsed_ms == 1000);
    assert(heart_session_measured_ms(&session) == 1000);
    assert(!heart_session_record(&session, &p, 2100));

    heart_session_set_paused(&session, false);
    heart_session_tick(&session, 3000);
    assert(session.elapsed_ms == 2000);

    // After a long gap with no fresh data the elapsed time still advances (the
    // device was on), but no zone time is credited.
    heart_session_tick(&session, 20000);
    assert(session.elapsed_ms == 7000);
    assert(heart_session_measured_ms(&session) == 2000);
}

static void test_trend_wraps(void)
{
    heart_session_t session;
    heart_session_reset(&session, 30);
    for (uint32_t i = 0; i < 100; i++) {
        heart_packet_t p = packet((uint8_t)(60 + i % 40), 0);
        assert(heart_session_record(&session, &p, 1000 + i * 1000));
    }
    assert(session.trend_count == HEART_TREND_LEN);
    // The first retained sample is number 10 (BPM 70); the last is number 99.
    assert(heart_session_trend_at(&session, 0) == 70);
    assert(heart_session_trend_at(&session, HEART_TREND_LEN - 1) == 79);
}

static void test_rmssd(void)
{
    heart_session_t session;
    heart_session_reset(&session, 30);

    const uint16_t intervals[] = {1000, 960, 1000};
    for (uint32_t i = 0; i < 3; i++) {
        heart_packet_t p = packet(70, intervals[i]);
        assert(heart_session_record(&session, &p, 1000 + i * 1000));
    }
    assert(session.rr_count == 3);
    assert(heart_session_rmssd(&session) == 40);

    for (uint32_t i = 0; i < 20; i++) {
        heart_packet_t p = packet(70, 1000);
        assert(heart_session_record(&session, &p, 10000 + i * 1000));
    }
    assert(session.rr_count == HEART_RR_WINDOW);
    assert(heart_session_rmssd(&session) == 0);

    heart_session_t empty;
    heart_session_reset(&empty, 30);
    assert(heart_session_rmssd(&empty) == -1);
}

static void test_zone_percent(void)
{
    heart_session_t session;
    heart_session_reset(&session, 30);

    heart_packet_t rest = packet(70, 800);
    heart_packet_t peak = packet(170, 350);
    assert(heart_session_record(&session, &rest, 100));
    heart_session_tick(&session, 0);
    heart_session_tick(&session, 1000);
    heart_session_tick(&session, 2000);
    assert(heart_session_zone_percent(&session, HEART_ZONE_REST) == 100);

    assert(heart_session_record(&session, &peak, 2100));
    heart_session_tick(&session, 3000);
    heart_session_tick(&session, 4000);
    assert(heart_session_zone_percent(&session, HEART_ZONE_REST) == 50);
    assert(heart_session_zone_percent(&session, HEART_ZONE_PEAK) == 50);

    heart_session_reset(&session, 30);
    assert(heart_session_zone_percent(&session, HEART_ZONE_REST) == 0);
}

static void test_parse_compat_raw_bpm(void)
{
    heart_packet_t packet;
    const uint8_t le16[] = {72, 0};
    const uint8_t be16[] = {0, 72};
    const uint8_t one_byte[] = {72};
    const uint8_t malformed_flags[] = {0x01, 72};
    const uint8_t malformed_energy[] = {0x08, 72};
    const uint8_t zero[] = {0, 0};
    const uint8_t low[] = {24};
    const uint8_t high[] = {0, 251};
    const uint8_t noise[] = {0xFF, 0x00};

    assert(heart_parse_measurement_compat(le16, sizeof(le16), &packet));
    assert(packet.bpm == 72);
    assert(packet.rr_ms == 833);

    assert(heart_parse_measurement_compat(be16, sizeof(be16), &packet));
    assert(packet.bpm == 72);

    assert(heart_parse_measurement_compat(one_byte, sizeof(one_byte), &packet));
    assert(packet.bpm == 72);

    assert(heart_parse_measurement_compat(malformed_flags, sizeof(malformed_flags), &packet));
    assert(packet.bpm == 72);

    assert(heart_parse_measurement_compat(malformed_energy, sizeof(malformed_energy), &packet));
    assert(packet.bpm == 72);

    assert(!heart_parse_measurement_compat(zero, sizeof(zero), &packet));
    assert(!heart_parse_measurement_compat(low, sizeof(low), &packet));
    assert(!heart_parse_measurement_compat(high, sizeof(high), &packet));
    assert(!heart_parse_measurement_compat(noise, sizeof(noise), &packet));
    assert(!heart_parse_measurement_compat(NULL, 2, &packet));
}

int main(void)
{
    test_parse_8bit();
    test_parse_contact();
    test_parse_16bit_energy_rr();
    test_parse_rejects_bad_frames();
    test_parse_multiple_rr();
    test_parse_compat_raw_bpm();
    test_zones();
    test_session_statistics();
    test_pause_and_tick();
    test_trend_wraps();
    test_rmssd();
    test_zone_percent();
    return 0;
}
