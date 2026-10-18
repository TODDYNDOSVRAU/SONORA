#include "sonora_audio_effect_abi.h"
#include "sonora_shared_state.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern "C" audio_effect_library_t AELI;

namespace {
const effect_uuid_t kSonoraImplementationUuid = {0x238b4a9f, 0x6e5c, 0x704b, 0x219f, {0x53, 0x4f, 0x4e, 0x4f, 0x52, 0x42}};

bool writeControl(uint32_t enabled, uint32_t position) {
  static uint32_t nextSequence = 0;
  const int fd = open(SONORA_CONTROL_FILE, O_RDWR | O_CREAT, 0600);
  if (fd < 0 || ftruncate(fd, sizeof(SonoraSharedState)) != 0) return false;
  auto *state = static_cast<SonoraSharedState *>(mmap(nullptr, sizeof(SonoraSharedState), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  if (state == MAP_FAILED) { close(fd); return false; }
  nextSequence += 2;
  std::memset(state, 0, sizeof(*state));
  state->magic = SONORA_STATE_MAGIC;
  state->version = SONORA_STATE_VERSION;
  state->sequence = nextSequence;
  state->enabled = enabled;
  state->position = position;
  state->preamp_db = 0.0f;
  for (float &gain : state->eq_db) gain = 0.0f;
  msync(state, sizeof(*state), MS_SYNC);
  munmap(state, sizeof(*state));
  close(fd);
  return true;
}

bool configure(effect_handle_t handle) {
  effect_config_t config{};
  config.inputCfg.samplingRate = 48000;
  config.outputCfg.samplingRate = 48000;
  config.inputCfg.channels = 3;
  config.outputCfg.channels = 3;
  config.inputCfg.format = AUDIO_FORMAT_PCM_FLOAT;
  config.outputCfg.format = AUDIO_FORMAT_PCM_FLOAT;
  int32_t reply = -1;
  uint32_t replySize = sizeof(reply);
  if ((*handle)->command(handle, EFFECT_CMD_SET_CONFIG, sizeof(config), &config, &replySize, &reply) != 0 || reply != 0) return false;
  reply = -1;
  replySize = sizeof(reply);
  return (*handle)->command(handle, EFFECT_CMD_ENABLE, 0, nullptr, &replySize, &reply) == 0 && reply == 0;
}
}

int main() {
  std::remove(SONORA_CONTROL_FILE);
  if (!writeControl(0, 0)) return 1;
  effect_descriptor_t descriptor{};
  if (AELI.get_descriptor(&kSonoraImplementationUuid, &descriptor) != 0) return 2;
  if (std::strcmp(descriptor.name, "Sonora Spatial") != 0) return 3;
  effect_handle_t handle = nullptr;
  if (AELI.create_effect(&kSonoraImplementationUuid, 0, 0, &handle) != 0 || !handle) return 4;
  if (!configure(handle)) return 5;

  std::vector<float> input(48000 * 2);
  std::vector<float> output(input.size(), 0.0f);
  for (size_t frame = 0; frame < input.size() / 2; ++frame) {
    const float sample = 0.4f * std::sin(2.0f * 3.14159265358979323846f * 440.0f * static_cast<float>(frame) / 48000.0f);
    input[frame * 2] = sample;
    input[frame * 2 + 1] = sample;
  }

  audio_buffer_t inBuffer{};
  inBuffer.frameCount = input.size() / 2;
  inBuffer.f32 = input.data();
  audio_buffer_t outBuffer{};
  outBuffer.frameCount = output.size() / 2;
  outBuffer.f32 = output.data();
  if ((*handle)->process(handle, &inBuffer, &outBuffer) != 0) return 6;
  if (std::memcmp(input.data(), output.data(), input.size() * sizeof(float)) != 0) return 7;

  if (!writeControl(1, 3)) return 8;
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
  AELI.release_effect(handle);
  std::remove(SONORA_CONTROL_FILE);
  std::puts("PASS: AOSP effect ABI, shared state, bypass, and rightward stereo processing");
  return 0;
}
