#include "sonora_audio_effect_abi.h"
#include "sonora_dsp.h"
#include "sonora_shared_state.h"

#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <thread>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static_assert(offsetof(buffer_config_t, format) == 24, "AOSP ARM64 format offset");
static_assert(offsetof(buffer_config_t, accessMode) == 25, "AOSP ARM64 access offset");
static_assert(offsetof(buffer_config_t, mask) == 26, "AOSP ARM64 mask offset");
static_assert(offsetof(buffer_config_t, bufferProvider) == 32, "AOSP ARM64 provider offset");
static_assert(sizeof(buffer_config_t) == 56, "AOSP ARM64 buffer_config_t size");
static_assert(sizeof(effect_config_t) == 112, "AOSP ARM64 effect_config_t size");

extern "C" audio_effect_library_t AELI;

namespace {
const effect_uuid_t kSonoraImplementationUuid = {0x238b4a9f, 0x6e5c, 0x704b, 0x219f, {0x53, 0x4f, 0x4e, 0x4f, 0x52, 0x42}};
constexpr int kAtomicOrder = __ATOMIC_SEQ_CST;

uint32_t floatToBits(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

void storeWord(uint32_t *word, uint32_t value) {
  __atomic_store_n(word, value, kAtomicOrder);
}

bool writeControl(uint32_t enabled, uint32_t position) {
  static uint32_t nextSequence = 0;
  const int fd = open(SONORA_CONTROL_FILE, O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
  if (fd < 0) return false;
  if (ftruncate(fd, sizeof(SonoraSharedState)) != 0) { close(fd); return false; }
  auto *state = static_cast<SonoraSharedState *>(mmap(nullptr, sizeof(SonoraSharedState), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  if (state == MAP_FAILED) { close(fd); return false; }
  nextSequence += 2;
  storeWord(&state->sequence, nextSequence - 1u);
  storeWord(&state->magic, SONORA_STATE_MAGIC);
  storeWord(&state->version, SONORA_STATE_VERSION);
  storeWord(&state->enabled, enabled);
  storeWord(&state->position, position);
  storeWord(&state->preamp_db_bits, floatToBits(0.0f));
  for (size_t i = 0; i < SONORA_EQ_BANDS; ++i) storeWord(&state->eq_db_bits[i], floatToBits(0.0f));
  storeWord(&state->sequence, nextSequence);
  const bool synced = msync(state, sizeof(*state), MS_SYNC) == 0;
  munmap(state, sizeof(*state));
  close(fd);
  return synced;
}

effect_config_t stereoConfig(uint32_t sampleRate) {
  effect_config_t config{};
  config.inputCfg.samplingRate = sampleRate;
  config.outputCfg.samplingRate = sampleRate;
  config.inputCfg.channels = AUDIO_CHANNEL_OUT_STEREO;
  config.outputCfg.channels = AUDIO_CHANNEL_OUT_STEREO;
  config.inputCfg.format = AUDIO_FORMAT_PCM_FLOAT;
  config.outputCfg.format = AUDIO_FORMAT_PCM_FLOAT;
  config.inputCfg.accessMode = EFFECT_BUFFER_ACCESS_READ;
  config.outputCfg.accessMode = EFFECT_BUFFER_ACCESS_WRITE;
  return config;
}

bool sendCommand(effect_handle_t handle, uint32_t command, uint32_t commandSize, void *commandData,
                 int32_t expectedStatus = 0) {
  int32_t reply = -1;
  uint32_t replySize = sizeof(reply);
  const int32_t commandResult = (*handle)->command(handle, command, commandSize, commandData, &replySize, &reply);
  return commandResult == 0 && replySize == sizeof(reply) && reply == expectedStatus;
}

bool configure(effect_handle_t handle, uint32_t sampleRate) {
  effect_config_t config = stereoConfig(sampleRate);
  return sendCommand(handle, EFFECT_CMD_SET_CONFIG, sizeof(config), &config) &&
      sendCommand(handle, EFFECT_CMD_ENABLE, 0, nullptr);
}

bool rejectMono(effect_handle_t handle) {
  effect_config_t config = stereoConfig(48000);
  config.inputCfg.channels = AUDIO_CHANNEL_OUT_MONO;
  return sendCommand(handle, EFFECT_CMD_SET_CONFIG, sizeof(config), &config, -ENOSYS);
}

bool reset(effect_handle_t handle) {
  uint32_t replySize = 0;
  return (*handle)->command(handle, EFFECT_CMD_RESET, 0, nullptr, &replySize, nullptr) == 0;
}

void fillTone(std::vector<float> &input, uint32_t sampleRate) {
  for (size_t frame = 0; frame < input.size() / 2; ++frame) {
    const float sample = 0.4f * std::sin(2.0f * 3.14159265358979323846f * 440.0f * static_cast<float>(frame) / static_cast<float>(sampleRate));
    input[frame * 2] = sample;
    input[frame * 2 + 1] = sample;
  }
}

bool reconfigureConcurrently(effect_handle_t handle) {
  std::vector<float> input(2048 * 2, 0.0f);
  std::vector<float> output(input.size(), 0.0f);
  fillTone(input, 48000);
  audio_buffer_t inBuffer{};
  inBuffer.frameCount = input.size() / 2;
  inBuffer.f32 = input.data();
  audio_buffer_t outBuffer{};
  outBuffer.frameCount = output.size() / 2;
  outBuffer.f32 = output.data();
  std::atomic<bool> stop{false};
  std::atomic<bool> failed{false};
  std::thread audioThread([&]() {
    while (!stop.load(std::memory_order_acquire)) {
      if ((*handle)->process(handle, &inBuffer, &outBuffer) != 0) failed.store(true, std::memory_order_release);
    }
  });
  bool commandsOk = true;
  for (uint32_t i = 0; i < 80; ++i) {
    const uint32_t rate = (i & 1u) ? 44100u : 48000u;
    effect_config_t config = stereoConfig(rate);
    if (!sendCommand(handle, EFFECT_CMD_SET_CONFIG, sizeof(config), &config) || !reset(handle)) {
      commandsOk = false;
      break;
    }
  }
  stop.store(true, std::memory_order_release);
  audioThread.join();
  return commandsOk && !failed.load(std::memory_order_acquire);
}
} // namespace

int main() {
  std::remove(SONORA_CONTROL_FILE);
  effect_descriptor_t descriptor{};
  if (AELI.get_descriptor(&kSonoraImplementationUuid, &descriptor) != 0) return 2;
  if (std::strcmp(descriptor.name, "Sonora Spatial") != 0) return 3;
  if ((descriptor.flags & EFFECT_FLAG_TYPE_MASK) != EFFECT_FLAG_TYPE_INSERT ||
      (descriptor.flags & EFFECT_FLAG_INSERT_LAST) == 0 ||
      (descriptor.flags & EFFECT_FLAG_INSERT_FIRST) != 0) return 11;

  effect_handle_t handle = nullptr;
  if (AELI.create_effect(&kSonoraImplementationUuid, 0, 0, &handle) != 0 || !handle) return 4;
  if (!rejectMono(handle)) return 12;
  if (!configure(handle, 48000)) return 5;
  /* The effect instance exists before the helper creates the mmap file. */
  if (!writeControl(0, SONORA_POSITION_CENTER)) return 13;
  if (!sendCommand(handle, EFFECT_CMD_ENABLE, 0, nullptr)) return 14; /* retries map */

  std::vector<float> input(48000 * 2);
  std::vector<float> output(input.size(), 0.0f);
  fillTone(input, 48000);
  audio_buffer_t inBuffer{};
  inBuffer.frameCount = input.size() / 2;
  inBuffer.f32 = input.data();
  audio_buffer_t outBuffer{};
  outBuffer.frameCount = output.size() / 2;
  outBuffer.f32 = output.data();
  if ((*handle)->process(handle, &inBuffer, &outBuffer) != 0) return 6;
  if (std::memcmp(input.data(), output.data(), input.size() * sizeof(float)) != 0) return 7;

  if (!writeControl(1, SONORA_POSITION_RIGHT)) return 8;
  if ((*handle)->process(handle, &inBuffer, &outBuffer) != 0) return 9;
  double leftEnergy = 0.0, rightEnergy = 0.0;
  for (size_t frame = input.size() / 2 - 4096; frame < input.size() / 2; ++frame) {
    leftEnergy += output[frame * 2] * output[frame * 2];
    rightEnergy += output[frame * 2 + 1] * output[frame * 2 + 1];
  }
  const double ratio = rightEnergy / (leftEnergy + 1e-12);
  if (ratio < 3.0) {
    std::fprintf(stderr, "right-position energy ratio too small: %.3f\n", ratio);
    return 10;
  }
  if (!reconfigureConcurrently(handle)) return 15;
  if (!configure(handle, 48000)) return 16;

  AELI.release_effect(handle);
  std::remove(SONORA_CONTROL_FILE);
  std::puts("PASS: AOSP insert-last ABI, stereo-only config, late mmap retry, shared state, bypass and concurrent reconfiguration");
  return 0;
}
