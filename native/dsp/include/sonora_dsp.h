#ifndef SONORA_DSP_H
#define SONORA_DSP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SONORA_EQ_BANDS
#define SONORA_EQ_BANDS 10
#endif

typedef enum SonoraPosition {
  SONORA_POSITION_CENTER = 0,
  SONORA_POSITION_FRONT = 1,
  SONORA_POSITION_BACK = 2,
  SONORA_POSITION_RIGHT = 3,
  SONORA_POSITION_LEFT = 4
} SonoraPosition;

typedef struct SonoraParameters {
  uint32_t generation;
  uint32_t enabled;
  uint32_t position;
  float preamp_db;
  float eq_db[SONORA_EQ_BANDS];
} SonoraParameters;

typedef struct SonoraDsp SonoraDsp;

SonoraDsp *sonora_dsp_create(uint32_t sample_rate);
void sonora_dsp_destroy(SonoraDsp *dsp);
void sonora_dsp_reset(SonoraDsp *dsp);

/* Processes interleaved float PCM. Supports one or two channels; unsupported layouts return -1. */
int32_t sonora_dsp_process(
  SonoraDsp *dsp,
  const float *input,
  float *output,
  size_t frames,
  uint32_t channels,
  const SonoraParameters *parameters);

#ifdef __cplusplus
}
#endif

#endif
