/*
 * Minimal legacy Android Audio Effect C ABI declarations used by Sonora.
 * Field layouts and callback signatures follow the AOSP headers:
 * hardware/libhardware/include_all/hardware/audio_effect.h and
 * system/media/audio/include/system/audio_effect.h (Apache-2.0).
 * This is not a Dolby SDK and does not advertise Dolby certification.
 */
#ifndef SONORA_AUDIO_EFFECT_ABI_H
#define SONORA_AUDIO_EFFECT_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct audio_uuid_s {
  uint32_t timeLow;
  uint16_t timeMid;
  uint16_t timeHiAndVersion;
  uint16_t clockSeq;
  uint8_t node[6];
} audio_uuid_t;

typedef audio_uuid_t effect_uuid_t;

typedef struct effect_descriptor_s {
  effect_uuid_t type;
  effect_uuid_t uuid;
  uint32_t apiVersion;
  uint32_t flags;
  uint16_t cpuLoad;
  uint16_t memoryUsage;
  char name[64];
  char implementor[64];
} effect_descriptor_t;

typedef struct audio_buffer_s {
  size_t frameCount;
  union {
    void *raw;
    float *f32;
    int32_t *s32;
    int16_t *s16;
    uint8_t *u8;
  };
} audio_buffer_t;

typedef int32_t (*buffer_function_t)(void *cookie, audio_buffer_t *buffer);
typedef struct buffer_provider_s {
  buffer_function_t getBuffer;
  buffer_function_t releaseBuffer;
  void *cookie;
} buffer_provider_t;

typedef struct buffer_config_s {
  audio_buffer_t buffer;
  uint32_t samplingRate;
  uint32_t channels;
  uint8_t format;
  uint8_t accessMode;
  uint16_t mask;
  buffer_provider_t bufferProvider;
} buffer_config_t;

typedef struct effect_config_s {
  buffer_config_t inputCfg;
  buffer_config_t outputCfg;
} effect_config_t;

struct effect_interface_s;
typedef struct effect_interface_s **effect_handle_t;

typedef struct effect_interface_s {
  int32_t (*process)(effect_handle_t self, audio_buffer_t *inBuffer, audio_buffer_t *outBuffer);
  int32_t (*command)(effect_handle_t self, uint32_t cmdCode, uint32_t cmdSize, void *pCmdData,
                     uint32_t *replySize, void *pReplyData);
  int32_t (*get_descriptor)(effect_handle_t self, effect_descriptor_t *pDescriptor);
  int32_t (*process_reverse)(effect_handle_t self, audio_buffer_t *inBuffer, audio_buffer_t *outBuffer);
} effect_interface_t;

typedef struct audio_effect_library_s {
  uint32_t tag;
  uint32_t version;
  const char *name;
  const char *implementor;
  int32_t (*create_effect)(const effect_uuid_t *uuid, int32_t sessionId, int32_t ioId,
                           effect_handle_t *pHandle);
  int32_t (*release_effect)(effect_handle_t handle);
  int32_t (*get_descriptor)(const effect_uuid_t *uuid, effect_descriptor_t *pDescriptor);
  int32_t (*create_effect_3_1)(const effect_uuid_t *uuid, int32_t sessionId, int32_t ioId,
                               int32_t deviceId, effect_handle_t *pHandle);
} audio_effect_library_t;

enum {
  EFFECT_CMD_INIT = 0,
  EFFECT_CMD_SET_CONFIG = 1,
  EFFECT_CMD_RESET = 2,
  EFFECT_CMD_ENABLE = 3,
  EFFECT_CMD_DISABLE = 4,
  EFFECT_CMD_SET_CONFIG_REVERSE = 12,
  EFFECT_CMD_GET_CONFIG = 14
};

#define EFFECT_MAKE_API_VERSION(M, m) (((M) << 16) | ((m) & 0xFFFF))
#define EFFECT_CONTROL_API_VERSION EFFECT_MAKE_API_VERSION(2, 0)
#define EFFECT_LIBRARY_API_VERSION EFFECT_MAKE_API_VERSION(3, 0)
#define AUDIO_EFFECT_LIBRARY_TAG (((uint32_t)'A' << 24) | ((uint32_t)'E' << 16) | ((uint32_t)'L' << 8) | (uint32_t)'T')
#define AUDIO_EFFECT_LIBRARY_INFO_SYM AELI
#define AUDIO_FORMAT_PCM_16_BIT 0x1u
#define AUDIO_FORMAT_PCM_FLOAT 0x5u
#define EFFECT_FLAG_TYPE_MASK 0x00000007u
#define EFFECT_FLAG_TYPE_INSERT 0u
#define EFFECT_FLAG_INSERT_FIRST (1u << 3)
#define EFFECT_FLAG_INSERT_LAST (2u << 3)
#define AUDIO_CHANNEL_OUT_MONO 0x1u
#define AUDIO_CHANNEL_OUT_STEREO 0x3u
#define EFFECT_BUFFER_ACCESS_WRITE 0u
#define EFFECT_BUFFER_ACCESS_READ 1u
#define EFFECT_BUFFER_ACCESS_ACCUMULATE 2u

#ifdef __cplusplus
}
static_assert(sizeof(audio_uuid_t) == 16, "AOSP audio UUID layout changed");
static_assert(EFFECT_FLAG_TYPE_MASK == 7u && EFFECT_FLAG_TYPE_INSERT == 0u, "AOSP effect type flags changed");
static_assert(EFFECT_FLAG_INSERT_FIRST == 8u && EFFECT_FLAG_INSERT_LAST == 16u, "AOSP insert ordering flags changed");
static_assert(EFFECT_BUFFER_ACCESS_WRITE == 0u && EFFECT_BUFFER_ACCESS_READ == 1u, "AOSP effect buffer access values changed");
#if INTPTR_MAX == INT64_MAX
static_assert(offsetof(buffer_config_t, format) == 24, "AOSP ARM64 buffer format offset changed");
static_assert(offsetof(buffer_config_t, accessMode) == 25, "AOSP ARM64 buffer access offset changed");
static_assert(offsetof(buffer_config_t, mask) == 26, "AOSP ARM64 buffer mask offset changed");
static_assert(offsetof(buffer_config_t, bufferProvider) == 32, "AOSP ARM64 provider offset changed");
static_assert(sizeof(buffer_config_t) == 56, "AOSP ARM64 buffer config size changed");
static_assert(sizeof(effect_config_t) == 112, "AOSP ARM64 effect config size changed");
#endif
#endif

#endif
