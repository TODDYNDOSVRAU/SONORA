#include "sonora_audio_effect_abi.h"
#include "sonora_dsp.h"
#include "sonora_shared_state.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <grp.h>
#include <mutex>
#include <new>
#include <pwd.h>
#include <thread>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr effect_uuid_t kSonoraTypeUuid = {0x238b4a9f, 0x6e5c, 0x704b, 0x219f, {0x53, 0x4f, 0x4e, 0x4f, 0x52, 0x41}};
constexpr effect_uuid_t kSonoraImplementationUuid = {0x238b4a9f, 0x6e5c, 0x704b, 0x219f, {0x53, 0x4f, 0x4e, 0x4f, 0x52, 0x42}};
constexpr uint32_t kMaxFramesPerChunk = 128;
constexpr uint32_t kForcedBypassBit = 0x80000000u;
constexpr int kAtomicOrder = __ATOMIC_SEQ_CST;

struct RuntimeSlot {
  SonoraDsp *dsp = nullptr;
  effect_config_t config{};
  uint32_t sampleRate = 48000;
  uint32_t channels = 2;
  uint8_t format = AUDIO_FORMAT_PCM_FLOAT;
  SonoraParameters parameters{};
  std::atomic<uint32_t> readers{0};
  std::atomic<bool> resetRequested{false};
};

struct SonoraEffectContext {
  const effect_interface_t *interface = nullptr;
  RuntimeSlot slots[2];
  std::atomic<uint32_t> activeSlot{0};
  std::mutex commandMutex;
  effect_config_t config{};
  bool configSet = false; /* guarded by commandMutex */
  int stateFd = -1;       /* guarded by stateMutex */
  std::mutex stateMutex;
  std::atomic<const SonoraSharedState *> shared{nullptr};
  std::atomic<bool> frameworkEnabled{false};
};

static_assert(std::atomic<uint32_t>::is_always_lock_free, "Audio slot counters and index must be lock-free");
static_assert(std::atomic<bool>::is_always_lock_free, "Audio enable flags must be lock-free");

const effect_descriptor_t kDescriptor = {
  kSonoraTypeUuid,
  kSonoraImplementationUuid,
  EFFECT_CONTROL_API_VERSION,
  EFFECT_FLAG_TYPE_INSERT | EFFECT_FLAG_INSERT_LAST,
  120,
  0,
  "Sonora Spatial",
  "Sonora open DSP"
};

SonoraEffectContext *contextFrom(effect_handle_t handle) {
  return handle ? reinterpret_cast<SonoraEffectContext *>(handle) : nullptr;
}

float bitsToFloat(uint32_t bits) {
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

uint32_t loadWord(const uint32_t *word) {
  return __atomic_load_n(word, kAtomicOrder);
}

bool readSharedState(SonoraEffectContext *context, SonoraParameters *out) {
  if (!context || !out) return false;
  const SonoraSharedState *state = context->shared.load(std::memory_order_acquire);
  if (!state) return false;

  for (int attempt = 0; attempt < 4; ++attempt) {
    const uint32_t before = loadWord(&state->sequence);
    if (before & 1u) continue;
    const uint32_t magic = loadWord(&state->magic);
    const uint32_t version = loadWord(&state->version);
    SonoraParameters candidate{};
    candidate.generation = before;
    candidate.enabled = loadWord(&state->enabled);
    candidate.position = loadWord(&state->position);
    candidate.preamp_db = bitsToFloat(loadWord(&state->preamp_db_bits));
    bool valuesValid = std::isfinite(candidate.preamp_db) && candidate.preamp_db >= -12.0f && candidate.preamp_db <= 6.0f;
    for (size_t i = 0; i < SONORA_EQ_BANDS; ++i) {
      candidate.eq_db[i] = bitsToFloat(loadWord(&state->eq_db_bits[i]));
      if (!std::isfinite(candidate.eq_db[i]) || candidate.eq_db[i] < -12.0f || candidate.eq_db[i] > 12.0f) valuesValid = false;
    }
    const uint32_t after = loadWord(&state->sequence);
    if (before != after || (after & 1u)) continue;
    if (magic != SONORA_STATE_MAGIC || version != SONORA_STATE_VERSION || candidate.enabled > 1 ||
        candidate.position > SONORA_POSITION_LEFT || !valuesValid) return false;
    *out = candidate;
    return true;
  }
  return false;
}

void forceBypass(SonoraParameters *parameters) {
  if (!parameters || parameters->enabled == 0) return;
  parameters->enabled = 0;
  parameters->generation = (parameters->generation & ~kForcedBypassBit) | kForcedBypassBit;
}

void refreshParameters(SonoraEffectContext *context, RuntimeSlot *slot) {
  SonoraParameters next{};
  if (readSharedState(context, &next)) {
    if (next.generation != slot->parameters.generation) slot->parameters = next;
  } else {
    forceBypass(&slot->parameters);
  }
  if (!context->frameworkEnabled.load(std::memory_order_acquire)) forceBypass(&slot->parameters);
}

int32_t copyThrough(const audio_buffer_t *input, audio_buffer_t *output, uint8_t format) {
  if (!input || !output || !input->raw || !output->raw) return -EINVAL;
  const size_t bytesPerSample = format == AUDIO_FORMAT_PCM_FLOAT ? sizeof(float)
      : format == AUDIO_FORMAT_PCM_16_BIT ? sizeof(int16_t) : 0;
  if (bytesPerSample == 0) return -ENOSYS;
  if (input->frameCount != output->frameCount) return -EINVAL;
  if (input->raw != output->raw) {
    std::memmove(output->raw, input->raw, input->frameCount * 2 * bytesPerSample);
  }
  return 0;
}

class SlotLease {
 public:
  explicit SlotLease(SonoraEffectContext *context) : context_(context), index_(0) {
    for (;;) {
      index_ = context_->activeSlot.load(std::memory_order_acquire) & 1u;
      context_->slots[index_].readers.fetch_add(1, std::memory_order_acq_rel);
      if ((context_->activeSlot.load(std::memory_order_acquire) & 1u) == index_) break;
      context_->slots[index_].readers.fetch_sub(1, std::memory_order_release);
    }
  }
  ~SlotLease() { context_->slots[index_].readers.fetch_sub(1, std::memory_order_release); }
  RuntimeSlot &slot() { return context_->slots[index_]; }
  SlotLease(const SlotLease &) = delete;
  SlotLease &operator=(const SlotLease &) = delete;
 private:
  SonoraEffectContext *context_;
  uint32_t index_;
};

int32_t processFloat(SonoraEffectContext *context, RuntimeSlot *slot, const audio_buffer_t *input,
                     audio_buffer_t *output, size_t frames) {
  refreshParameters(context, slot);
  for (size_t offset = 0; offset < frames;) {
    const size_t count = std::min<size_t>(kMaxFramesPerChunk, frames - offset);
    const int32_t result = sonora_dsp_process(slot->dsp, input->f32 + offset * 2,
        output->f32 + offset * 2, count, 2, &slot->parameters);
    if (result != 0) return -EINVAL;
    offset += count;
  }
  return 0;
}

int32_t processPcm16(SonoraEffectContext *context, RuntimeSlot *slot, const audio_buffer_t *input,
                     audio_buffer_t *output, size_t frames) {
  refreshParameters(context, slot);
  float inputBlock[kMaxFramesPerChunk * 2];
  float outputBlock[kMaxFramesPerChunk * 2];
  for (size_t offset = 0; offset < frames;) {
    const size_t count = std::min<size_t>(kMaxFramesPerChunk, frames - offset);
    const size_t samples = count * 2;
    for (size_t i = 0; i < samples; ++i) inputBlock[i] = static_cast<float>(input->s16[offset * 2 + i]) / 32768.0f;
    const int32_t result = sonora_dsp_process(slot->dsp, inputBlock, outputBlock, count, 2, &slot->parameters);
    if (result != 0) return -EINVAL;
    for (size_t i = 0; i < samples; ++i) {
      const float bounded = std::clamp(outputBlock[i], -1.0f, 0.9999695f);
      output->s16[offset * 2 + i] = static_cast<int16_t>(std::lrintf(bounded * 32768.0f));
    }
    offset += count;
  }
  return 0;
}

int32_t effectProcess(effect_handle_t self, audio_buffer_t *inBuffer, audio_buffer_t *outBuffer) {
  SonoraEffectContext *context = contextFrom(self);
  if (!context) return -EINVAL;
  SlotLease lease(context);
  RuntimeSlot &slot = lease.slot();
  if (!slot.dsp) return -ENODEV;
  if (slot.resetRequested.exchange(false, std::memory_order_acq_rel)) sonora_dsp_reset(slot.dsp);

  audio_buffer_t *input = inBuffer ? inBuffer : &slot.config.inputCfg.buffer;
  audio_buffer_t *output = outBuffer ? outBuffer : &slot.config.outputCfg.buffer;
  if (!input->raw || !output->raw || input->frameCount != output->frameCount) return -EINVAL;
  const size_t frames = input->frameCount;
  if (frames == 0) return 0;
  if (!context->frameworkEnabled.load(std::memory_order_acquire)) return copyThrough(input, output, slot.format);
  if (slot.channels != 2) return -EINVAL; /* all other layouts are rejected by SET_CONFIG */
  if (slot.format == AUDIO_FORMAT_PCM_FLOAT) return processFloat(context, &slot, input, output, frames);
  if (slot.format == AUDIO_FORMAT_PCM_16_BIT) return processPcm16(context, &slot, input, output, frames);
  return -ENOSYS;
}

int32_t effectGetDescriptor(effect_handle_t self, effect_descriptor_t *descriptor) {
  if (!contextFrom(self) || !descriptor) return -EINVAL;
  *descriptor = kDescriptor;
  return 0;
}

int32_t replyStatus(uint32_t *replySize, void *replyData, int32_t status) {
  if (!replySize) return -EINVAL;
  if (*replySize < sizeof(status) || !replyData) {
    *replySize = 0;
    return -EINVAL;
  }
  std::memcpy(replyData, &status, sizeof(status));
  *replySize = sizeof(status);
  return 0;
}

bool configSupported(const effect_config_t &config) {
  const uint32_t rate = config.inputCfg.samplingRate;
  return rate >= 8000 && rate <= 192000 && config.outputCfg.samplingRate == rate &&
      config.inputCfg.channels == AUDIO_CHANNEL_OUT_STEREO &&
      config.outputCfg.channels == AUDIO_CHANNEL_OUT_STEREO &&
      config.inputCfg.format == config.outputCfg.format &&
      (config.inputCfg.format == AUDIO_FORMAT_PCM_FLOAT || config.inputCfg.format == AUDIO_FORMAT_PCM_16_BIT) &&
      config.inputCfg.accessMode == EFFECT_BUFFER_ACCESS_READ &&
      config.outputCfg.accessMode == EFFECT_BUFFER_ACCESS_WRITE;
}

int32_t openState(SonoraEffectContext *context);

int32_t setConfig(SonoraEffectContext *context, const effect_config_t &requested) {
  if (!configSupported(requested)) return -ENOSYS;
  std::lock_guard<std::mutex> commandLock(context->commandMutex);
  const uint32_t active = context->activeSlot.load(std::memory_order_acquire) & 1u;
  const uint32_t inactive = active ^ 1u;
  RuntimeSlot &slot = context->slots[inactive];
  while (slot.readers.load(std::memory_order_acquire) != 0) std::this_thread::yield();

  SonoraDsp *replacement = sonora_dsp_create(requested.inputCfg.samplingRate);
  if (!replacement) return -ENOMEM;
  SonoraDsp *retired = slot.dsp;
  slot.dsp = replacement;
  slot.config = requested;
  slot.sampleRate = requested.inputCfg.samplingRate;
  slot.channels = 2;
  slot.format = requested.inputCfg.format;
  slot.parameters = SonoraParameters{};
  slot.parameters.position = SONORA_POSITION_CENTER;
  slot.resetRequested.store(false, std::memory_order_relaxed);
  context->config = requested;
  context->configSet = true;
  if (retired) sonora_dsp_destroy(retired);
  context->activeSlot.store(inactive, std::memory_order_release);
  return 0;
}

int32_t effectCommand(effect_handle_t self, uint32_t command, uint32_t commandSize, void *commandData,
                      uint32_t *replySize, void *replyData) {
  SonoraEffectContext *context = contextFrom(self);
  if (!context) return -EINVAL;
  int32_t status = 0;
  switch (command) {
    case EFFECT_CMD_INIT:
      return replyStatus(replySize, replyData, status);
    case EFFECT_CMD_SET_CONFIG: {
      if (!commandData || commandSize < sizeof(effect_config_t)) return replyStatus(replySize, replyData, -EINVAL);
      effect_config_t requested{};
      std::memcpy(&requested, commandData, sizeof(requested));
      status = setConfig(context, requested);
      if (status == 0 && !context->shared.load(std::memory_order_acquire)) openState(context);
      return replyStatus(replySize, replyData, status);
    }
    case EFFECT_CMD_RESET: {
      std::lock_guard<std::mutex> commandLock(context->commandMutex);
      const uint32_t active = context->activeSlot.load(std::memory_order_acquire) & 1u;
      context->slots[active].resetRequested.store(true, std::memory_order_release);
      if (replySize) *replySize = 0;
      return 0;
    }
    case EFFECT_CMD_ENABLE:
      if (!context->shared.load(std::memory_order_acquire)) openState(context);
      context->frameworkEnabled.store(true, std::memory_order_release);
      return replyStatus(replySize, replyData, status);
    case EFFECT_CMD_DISABLE:
      context->frameworkEnabled.store(false, std::memory_order_release);
      return replyStatus(replySize, replyData, status);
    case EFFECT_CMD_GET_CONFIG: {
      std::lock_guard<std::mutex> commandLock(context->commandMutex);
      if (!context->configSet || !replySize || !replyData || *replySize < sizeof(effect_config_t)) return -EINVAL;
      std::memcpy(replyData, &context->config, sizeof(context->config));
      *replySize = sizeof(context->config);
      return 0;
    }
    case EFFECT_CMD_SET_CONFIG_REVERSE:
      return replyStatus(replySize, replyData, status);
    default:
      return -EINVAL;
  }
}

const effect_interface_t kInterface = {effectProcess, effectCommand, effectGetDescriptor, nullptr};

int32_t openState(SonoraEffectContext *context) {
  if (!context) return -EINVAL;
  std::lock_guard<std::mutex> lock(context->stateMutex);
  if (context->shared.load(std::memory_order_acquire)) return 0;

  int fd = -1;
#ifdef __ANDROID__
  if (std::strcmp(SONORA_CONTROL_FILE, "/data/vendor/audio/sonora_state.bin") != 0) return 0;
  const int dataFd = open("/data", O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
  if (dataFd < 0) return 0;
  const int vendorFd = openat(dataFd, "vendor", O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
  close(dataFd);
  if (vendorFd < 0) return 0;
  const int audioFd = openat(vendorFd, "audio", O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
  close(vendorFd);
  if (audioFd < 0) return 0;
  const struct passwd *audioUser = getpwnam("audio");
  const struct group *audioGroup = getgrnam("audio");
  struct stat directoryInfo{};
  const bool trustedDirectory = audioUser && audioGroup && fstat(audioFd, &directoryInfo) == 0 &&
      S_ISDIR(directoryInfo.st_mode) && directoryInfo.st_uid == audioUser->pw_uid &&
      directoryInfo.st_gid == audioGroup->gr_gid && (directoryInfo.st_mode & 0777) == 0770;
  if (!trustedDirectory) { close(audioFd); return 0; }
  fd = openat(audioFd, "sonora_state.bin", O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  close(audioFd);
#else
  fd = open(SONORA_CONTROL_FILE, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
#endif
  if (fd < 0) return 0; /* absent/unsafe state means bypass; control commands may retry */

  struct stat info{};
  bool validFile = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 &&
      info.st_size == static_cast<off_t>(SONORA_STATE_BYTES);
#ifdef __ANDROID__
  validFile = validFile && audioGroup && info.st_uid == 0 && info.st_gid == audioGroup->gr_gid &&
      (info.st_mode & 0777) == 0640;
#endif
  if (!validFile) { close(fd); return 0; }
  void *mapping = mmap(nullptr, SONORA_STATE_BYTES, PROT_READ, MAP_SHARED, fd, 0);
  if (mapping == MAP_FAILED) { close(fd); return 0; }
  context->stateFd = fd;
  context->shared.store(static_cast<const SonoraSharedState *>(mapping), std::memory_order_release);
  return 0;
}

void initializeDefaultConfig(effect_config_t *config) {
  *config = effect_config_t{};
  config->inputCfg.samplingRate = 48000;
  config->outputCfg.samplingRate = 48000;
  config->inputCfg.channels = AUDIO_CHANNEL_OUT_STEREO;
  config->outputCfg.channels = AUDIO_CHANNEL_OUT_STEREO;
  config->inputCfg.format = AUDIO_FORMAT_PCM_FLOAT;
  config->outputCfg.format = AUDIO_FORMAT_PCM_FLOAT;
  config->inputCfg.accessMode = EFFECT_BUFFER_ACCESS_READ;
  config->outputCfg.accessMode = EFFECT_BUFFER_ACCESS_WRITE;
}

int32_t createEffect(const effect_uuid_t *uuid, int32_t, int32_t, effect_handle_t *handle) {
  if (!uuid || !handle) return -EINVAL;
  if (std::memcmp(uuid, &kSonoraImplementationUuid, sizeof(effect_uuid_t)) != 0) return -ENOENT;
  SonoraEffectContext *context = new (std::nothrow) SonoraEffectContext{};
  if (!context) return -ENOMEM;
  context->interface = &kInterface;
  initializeDefaultConfig(&context->config);
  context->configSet = true;
  RuntimeSlot &initial = context->slots[0];
  initial.config = context->config;
  initial.sampleRate = 48000;
  initial.channels = 2;
  initial.format = AUDIO_FORMAT_PCM_FLOAT;
  initial.parameters.position = SONORA_POSITION_CENTER;
  initial.dsp = sonora_dsp_create(initial.sampleRate);
  if (!initial.dsp) { delete context; return -ENOMEM; }
  openState(context);
  *handle = reinterpret_cast<effect_handle_t>(context);
  return 0;
}

int32_t createEffect31(const effect_uuid_t *uuid, int32_t sessionId, int32_t ioId, int32_t, effect_handle_t *handle) {
  return createEffect(uuid, sessionId, ioId, handle);
}

int32_t releaseEffect(effect_handle_t handle) {
  SonoraEffectContext *context = contextFrom(handle);
  if (!context) return -EINVAL;
  context->frameworkEnabled.store(false, std::memory_order_release);
  for (RuntimeSlot &slot : context->slots) {
    while (slot.readers.load(std::memory_order_acquire) != 0) std::this_thread::yield();
  }
  {
    std::lock_guard<std::mutex> lock(context->stateMutex);
    const SonoraSharedState *mapping = context->shared.exchange(nullptr, std::memory_order_acq_rel);
    if (mapping) munmap(const_cast<SonoraSharedState *>(mapping), SONORA_STATE_BYTES);
    if (context->stateFd >= 0) close(context->stateFd);
  }
  for (RuntimeSlot &slot : context->slots) {
    if (slot.dsp) sonora_dsp_destroy(slot.dsp);
    slot.dsp = nullptr;
  }
  delete context;
  return 0;
}

int32_t libraryGetDescriptor(const effect_uuid_t *uuid, effect_descriptor_t *descriptor) {
  if (!uuid || !descriptor) return -EINVAL;
  if (std::memcmp(uuid, &kSonoraImplementationUuid, sizeof(effect_uuid_t)) != 0) return -ENOENT;
  *descriptor = kDescriptor;
  return 0;
}
} // namespace

extern "C" {
__attribute__((visibility("default"))) audio_effect_library_t AELI = {
  AUDIO_EFFECT_LIBRARY_TAG,
  EFFECT_LIBRARY_API_VERSION,
  "Sonora Spatial",
  "Sonora open DSP",
  createEffect,
  releaseEffect,
  libraryGetDescriptor,
  createEffect31
};
}
