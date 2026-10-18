#ifndef SONORA_SHARED_STATE_H
#define SONORA_SHARED_STATE_H

#include <stddef.h>
#include <stdint.h>

/*
 * Fixed-layout mmap control block shared by the root helper and the audio
 * effect. Every word is accessed with lock-free 32-bit atomic builtins; float
 * values are transported as IEEE-754 bit patterns. The callback performs no
 * file I/O, allocation, or mutex operations.
 *
 * SONORA_CONTROL_FILE is target-specific and must be chosen only after
 * reviewing the device's filesystem and audio SELinux policy.
 */
#ifndef SONORA_CONTROL_FILE
#error "Define SONORA_CONTROL_FILE after validating the target device's audio SELinux policy."
#endif

#define SONORA_STATE_MAGIC 0x534F4E41u /* SONA */
#define SONORA_STATE_VERSION 2u
#define SONORA_STATE_BYTES 64u
#ifndef SONORA_EQ_BANDS
#define SONORA_EQ_BANDS 10u
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SonoraSharedState {
  uint32_t magic;
  uint32_t version;
  uint32_t sequence; /* odd while a writer updates; even when stable */
  uint32_t enabled;
  uint32_t position; /* 0 center, 1 front, 2 back, 3 right, 4 left */
  uint32_t preamp_db_bits;
  uint32_t eq_db_bits[SONORA_EQ_BANDS];
} SonoraSharedState;

#ifdef __cplusplus
}
static_assert(sizeof(SonoraSharedState) == SONORA_STATE_BYTES, "Sonora shared-state ABI must remain 64 bytes");
static_assert(alignof(SonoraSharedState) >= alignof(uint32_t), "Shared-state words must be naturally aligned");
static_assert(__atomic_always_lock_free(sizeof(uint32_t), nullptr), "32-bit shared-state atomics must be lock-free");
static_assert(offsetof(SonoraSharedState, eq_db_bits) == 24, "Sonora shared-state field offsets changed");
#endif

#endif
