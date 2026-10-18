#include "sonora_audio_effect_abi.h"
#include "sonora_dsp.h"
#include "sonora_shared_state.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr effect_uuid_t kSonoraTypeUuid = {0x238b4a9f, 0x6e5c, 0x704b, 0x219f, {0x53, 0x4f, 0x4e, 0x4f, 0x52, 0x41}};
constexpr effect_uuid_t kSonoraImplementationUuid = {0x238b4a9f, 0x6e5c, 0x704b, 0x219f, {0x53, 0x4f, 0x4e, 0x4f, 0x52, 0x42}};
constexpr uint32_t kMaxFramesPerChunk = 128;

struct SonoraEffectContext {
  const effect_interface_t *interface;
  effect_config_t config;
  SonoraDsp *dsp;
  int stateFd;
  const SonoraSharedState *shared;
  uint32_t sampleRate;
  uint32_t inputChannels;
  uint32_t outputChannels;
  uint8_t inputFormat;
  uint8_t outputFormat;
  bool frameworkEnabled;
  SonoraParameters parameters;
};

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

uint32_t channelCount(uint32_t mask) {
  return static_cast<uint32_t>(__builtin_popcount(mask));
}

int32_t copyThrough(const audio_buffer_t *input, audio_buffer_t *output, uint32_t channels, uint8_t format) {
  if (!input || !output || !input->raw || !output->raw || channels == 0) return -EINVAL;
  if (input->raw == output->raw) return 0;
  const size_t bytesPerSample = format == AUDIO_FORMAT_PCM_FLOAT ? sizeof(float)
      : format == AUDIO_FORMAT_PCM_16_BIT ? sizeof(int16_t) : 0;
  if (bytesPerSample == 0) return -ENOSYS;
  const size_t frames = std::min(input->frameCount, output->frameCount);
  std::memmove(output->raw, input->raw, frames * channels * bytesPerSample);
  return 0;
}

bool readSharedState(SonoraEffectContext *context, SonoraParameters *out) {
  if (!context || !context->shared || !out) return false;
  const SonoraSharedState *state = context->shared;
  for (int attempt = 0; attempt < 3; ++attempt) {
    const uint32_t before = __atomic_load_n(&state->sequence, __ATOMIC_ACQUIRE);
    if (before & 1u) continue;
    const uint32_t magic = state->magic;
    const uint32_t version = state->version;
    SonoraParameters candidate{};
    candidate.generation = before;
    candidate.enabled = state->enabled;
    candidate.position = state->position;
    candidate.preamp_db = state->preamp_db;
    for (size_t i = 0; i < SONORA_EQ_BANDS; ++i) candidate.eq_db[i] = state->eq_db[i];
    const uint32_t after = __atomic_load_n(&state->sequence, __ATOMIC_ACQUIRE);
    if (before != after || (after & 1u)) continue;
    if (magic != SONORA_STATE_MAGIC || version != SONORA_STATE_VERSION || candidate.enabled > 1 ||
        candidate.position > SONORA_POSITION_LEFT || !std::isfinite(candidate.preamp_db)) return false;
    for (float gain : candidate.eq_db) if (!std::isfinite(gain)) return false;
    *out = candidate;
    return true;
  }
  return false;
}

void refreshParameters(SonoraEffectContext *context) {
  SonoraParameters next{};
  if (readSharedState(context, &next) && next.generation != context->parameters.generation) {
    context->parameters = next;
  }
  if (!context->frameworkEnabled) {
    context->parameters.enabled = 0;
    context->parameters.generation |= 0x80000000u;
  }
}

int32_t processFloat(SonoraEffectContext *context, const audio_buffer_t *input, audio_buffer_t *output,
                     size_t frames, uint32_t channels) {
  if (!context->dsp) return -ENODEV;
  refreshParameters(context);
  for (size_t offset = 0; offset < frames;) {
    const size_t count = std::min<size_t>(kMaxFramesPerChunk, frames - offset);
    const int32_t result = sonora_dsp_process(context->dsp, input->f32 + offset * channels,
        output->f32 + offset * channels, count, channels, &context->parameters);
    if (result != 0) return -EINVAL;
    offset += count;
  }
  return 0;
}

int32_t processPcm16(SonoraEffectContext *context, const audio_buffer_t *input, audio_buffer_t *output,
                     size_t frames, uint32_t channels) {
  if (!context->dsp) return -ENODEV;
  refreshParameters(context);
  float inputBlock[kMaxFramesPerChunk * 2];
  float outputBlock[kMaxFramesPerChunk * 2];
  for (size_t offset = 0; offset < frames;) {
    const size_t count = std::min<size_t>(kMaxFramesPerChunk, frames - offset);
    const size_t samples = count * channels;
    for (size_t i = 0; i < samples; ++i) inputBlock[i] = static_cast<float>(input->s16[offset * channels + i]) / 32768.0f;
    const int32_t result = sonora_dsp_process(context->dsp, inputBlock, outputBlock, count, channels, &context->parameters);
    if (result != 0) return -EINVAL;
    for (size_t i = 0; i < samples; ++i) {
      const float bounded = std::clamp(outputBlock[i], -1.0f, 0.9999695f);
      output->s16[offset * channels + i] = static_cast<int16_t>(std::lrintf(bounded * 32768.0f));
    }
    offset += count;
  }
  return 0;
}

int32_t effectProcess(effect_handle_t self, audio_buffer_t *inBuffer, audio_buffer_t *outBuffer) {
  SonoraEffectContext *context = contextFrom(self);
  if (!context) return -EINVAL;
  audio_buffer_t *input = inBuffer ? inBuffer : &context->config.inputCfg.buffer;
  audio_buffer_t *output = outBuffer ? outBuffer : &context->config.outputCfg.buffer;
  if (!input->raw || !output->raw) return -EINVAL;
  const size_t frames = std::min(input->frameCount, output->frameCount);
  if (frames == 0) return 0;

  const uint32_t channels = context->inputChannels;
  if (channels == 0 || channels != context->outputChannels || context->inputFormat != context->outputFormat) return -EINVAL;
  if (channels > 2 || (context->inputFormat != AUDIO_FORMAT_PCM_FLOAT && context->inputFormat != AUDIO_FORMAT_PCM_16_BIT)) {
    return copyThrough(input, output, channels, context->inputFormat);
  }
  if (context->inputFormat == AUDIO_FORMAT_PCM_FLOAT) return processFloat(context, input, output, frames, channels);
  return processPcm16(context, input, output, frames, channels);
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
      const uint32_t rate = requested.inputCfg.samplingRate;
      const uint32_t inputCount = channelCount(requested.inputCfg.channels);
      const uint32_t outputCount = channelCount(requested.outputCfg.channels);
      if (rate < 8000 || rate > 192000 || inputCount == 0 || inputCount != outputCount ||
          requested.outputCfg.samplingRate != rate || requested.inputCfg.format != requested.outputCfg.format) {
        return replyStatus(replySize, replyData, -EINVAL);
      }
      SonoraDsp *replacement = context->dsp;
      if (rate != context->sampleRate) {
        replacement = sonora_dsp_create(rate);
        if (!replacement) return replyStatus(replySize, replyData, -ENOMEM);
      }
      if (replacement != context->dsp) {
        sonora_dsp_destroy(context->dsp);
        context->dsp = replacement;
      }
      context->config = requested;
      context->sampleRate = rate;
      context->inputChannels = inputCount;
      context->outputChannels = outputCount;
      context->inputFormat = requested.inputCfg.format;
      context->outputFormat = requested.outputCfg.format;
      return replyStatus(replySize, replyData, status);
    }
    case EFFECT_CMD_RESET:
      sonora_dsp_reset(context->dsp);
      if (replySize) *replySize = 0;
      return 0;
    case EFFECT_CMD_ENABLE:
      context->frameworkEnabled = true;
      return replyStatus(replySize, replyData, status);
    case EFFECT_CMD_DISABLE:
      context->frameworkEnabled = false;
      return replyStatus(replySize, replyData, status);
    case EFFECT_CMD_GET_CONFIG:
      if (!replySize || !replyData || *replySize < sizeof(effect_config_t)) return -EINVAL;
      std::memcpy(replyData, &context->config, sizeof(context->config));
      *replySize = sizeof(context->config);
      return 0;
    case EFFECT_CMD_SET_CONFIG_REVERSE:
      return replyStatus(replySize, replyData, status);
    default:
      return -EINVAL;
  }
}

const effect_interface_t kInterface = {effectProcess, effectCommand, effectGetDescriptor, nullptr};

int32_t openState(SonoraEffectContext *context) {
  context->stateFd = open(SONORA_CONTROL_FILE, O_RDONLY | O_CLOEXEC);
  if (context->stateFd < 0) return 0; // No state file means safe bypass.
  struct stat info{};
  if (fstat(context->stateFd, &info) != 0 || info.st_size < static_cast<off_t>(sizeof(SonoraSharedState))) {
    close(context->stateFd);
    context->stateFd = -1;
    return 0;
  }
  void *mapping = mmap(nullptr, sizeof(SonoraSharedState), PROT_READ, MAP_SHARED, context->stateFd, 0);
  if (mapping == MAP_FAILED) {
    close(context->stateFd);
    context->stateFd = -1;
    return 0;
  }
  context->shared = static_cast<const SonoraSharedState *>(mapping);
  return 0;
}

int32_t createEffect(const effect_uuid_t *uuid, int32_t, int32_t, effect_handle_t *handle) {
  if (!uuid || !handle) return -EINVAL;
  if (std::memcmp(uuid, &kSonoraImplementationUuid, sizeof(effect_uuid_t)) != 0) return -ENOENT;
  SonoraEffectContext *context = new (std::nothrow) SonoraEffectContext{};
  if (!context) return -ENOMEM;
  context->interface = &kInterface;
  context->stateFd = -1;
  context->sampleRate = 48000;
  context->inputChannels = 2;
  context->outputChannels = 2;
  context->inputFormat = AUDIO_FORMAT_PCM_FLOAT;
  context->outputFormat = AUDIO_FORMAT_PCM_FLOAT;
  context->frameworkEnabled = false;
  context->parameters.generation = 0;
  context->parameters.position = SONORA_POSITION_CENTER;
  context->dsp = sonora_dsp_create(context->sampleRate);
  if (!context->dsp) {
    delete context;
    return -ENOMEM;
  }
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
  if (context->shared) munmap(const_cast<SonoraSharedState *>(context->shared), sizeof(SonoraSharedState));
  if (context->stateFd >= 0) close(context->stateFd);
  sonora_dsp_destroy(context->dsp);
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
