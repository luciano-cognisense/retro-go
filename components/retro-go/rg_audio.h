#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct // __attribute__((packed))
{
    int16_t left;
    int16_t right;
} rg_audio_frame_t;

typedef rg_audio_frame_t rg_audio_sample_t;

typedef struct
{
    const char *name;                                             // Required
    bool (*init)(int device, int sample_rate);                    // Required
    bool (*deinit)(void);                                         // Required
    bool (*submit)(const rg_audio_frame_t *frames, size_t count); // Required
    bool (*set_mute)(bool mute);                                  // Optional
    bool (*set_volume)(int percent);                              // Optional
    bool (*set_sample_rate)(int sample_rate);                     // Optional
    const char *(*get_error)(void);                               // Optional
} rg_audio_driver_t;

typedef struct
{
    const rg_audio_driver_t *driver;
    int device;
    const char *name;
} rg_audio_sink_t;

typedef struct
{
    int64_t totalSamples;
    int64_t busyTime;
} rg_audio_counters_t;

typedef struct
{
    uint64_t submittedFrames;
    uint64_t writeTimeUs;
    uint32_t txDone;
    uint32_t underruns;
    uint32_t writeCalls;
    uint32_t slowWrites;
    uint32_t writeMaxUs;
    uint32_t eventBacklogMax;
    uint32_t rateEpoch;
    uint16_t dmaBufferFrames;
    uint16_t dmaBufferCount;
} rg_audio_i2s_telemetry_t;

void rg_audio_init(int sample_rate);
void rg_audio_deinit(void);
void rg_audio_submit(const rg_audio_frame_t *frames, size_t count);
rg_audio_counters_t rg_audio_get_counters(void);

// const char **rg_audio_get_drivers(void);
const char *rg_audio_get_driver(void);

const rg_audio_sink_t *rg_audio_get_sinks(size_t *count);
const rg_audio_sink_t *rg_audio_get_sink(void);
void rg_audio_set_sink(const char *driver_name, int device);

int rg_audio_get_volume(void);
void rg_audio_set_volume(int percent);
bool rg_audio_get_mute(void);
void rg_audio_set_mute(bool mute);
int rg_audio_get_sample_rate(void);
void rg_audio_set_sample_rate(int sample_rate);

// Diagnostic-only. Available when CONFIG_RG_AUDIO_I2S_TELEMETRY is enabled.
// Counters are monotonic within an I2S driver lifetime. rateEpoch increments
// after every hardware sample-rate reprogramming, whose DMA reset makes an
// occupancy estimate from the previous epoch invalid.
bool rg_audio_i2s_get_telemetry(rg_audio_i2s_telemetry_t *out);
