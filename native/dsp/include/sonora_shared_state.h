#ifndef SONORA_SHARED_STATE_H
#define SONORA_SHARED_STATE_H

#include <stdint.h>

/*
 * Fixed-layout shared control block between the root helper and the audio
 * effect. The audio callback only reads this mmap; it never performs file I/O.
 * The target-specific file path is deliberately mandatory: confirm the
 * audioserver SELinux access policy before packaging for a device.
 */
#ifndef SONORA_CONTROL_FILE
#error "Define SONORA_CONTROL_FILE after validating the target device's audio SELinux policy."
#endif

#define SONORA_STATE_MAGIC 0x534F4E41u /* SONA */
#define SONORA_STATE_VERSION 1u
#define SONORA_STATE_BYTES 64u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SonoraSharedState {
  uint32_t magic;
  uint32_t version;
  volatile uint32_t sequence; /* odd while a writer updates; even when stable */
  uint32_t enabled;
  uint32_t position; /* 0 center, 1 front, 2 back, 3 right, 4 left */
  float preamp_db;
  float eq_db[10];
} SonoraSharedState;

#ifdef __cplusplus
}
static_assert(sizeof(SonoraSharedState) == SONORA_STATE_BYTES, "Sonora shared-state ABI must remain 64 bytes");
#endif

#endif
