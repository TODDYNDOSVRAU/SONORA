#include "sonora_dsp.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
void require(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}

SonoraParameters neutral() {
  SonoraParameters p{};
  p.generation = 1;
  p.position = SONORA_POSITION_CENTER;
  return p;
}
}

int main() {
  constexpr size_t frames = 8192;
  std::vector<float> input(frames * 2, 0.25f);
  std::vector<float> output(frames * 2, 0.0f);
  SonoraDsp *dsp = sonora_dsp_create(48'000);
  require(dsp != nullptr, "create engine at 48 kHz");

  auto p = neutral();
  require(sonora_dsp_process(dsp, input.data(), output.data(), frames, 2, &p) == 0, "neutral bypass processes stereo");
  require(output == input, "neutral bypass is bit-exact");

  sonora_dsp_reset(dsp);
  p.generation = 2;
  p.enabled = 1;
  p.preamp_db = -6.0f;
  require(sonora_dsp_process(dsp, input.data(), output.data(), frames, 2, &p) == 0, "enabled EQ/preamp processes stereo");
  require(std::fabs(output.back() - 0.25f * std::pow(10.0f, -6.0f / 20.0f)) < 0.01f, "preamp reaches smoothed target");

  sonora_dsp_reset(dsp);
  p = neutral();
  p.generation = 3;
  p.enabled = 1;
  p.position = SONORA_POSITION_RIGHT;
  require(sonora_dsp_process(dsp, input.data(), output.data(), frames, 2, &p) == 0, "right position processes stereo");
  require(std::fabs(output[frames * 2 - 2]) < std::fabs(output[frames * 2 - 1]), "right position has stronger right-ear level");

  sonora_dsp_reset(dsp);
  p = neutral();
  p.generation = 4;
  p.enabled = 1;
  p.preamp_db = 6.0f;
  std::vector<float> hot(frames * 2, 1.0f);
  require(sonora_dsp_process(dsp, hot.data(), output.data(), frames, 2, &p) == 0, "limiter processes hot input");
  for (float sample : output) require(std::fabs(sample) <= 1.0f, "soft limiter keeps the output below full scale");
  require(std::fabs(output.back()) < 0.99f, "soft limiter leaves true-peak headroom");

  require(sonora_dsp_process(dsp, input.data(), output.data(), frames, 3, &p) == -1, "unsupported channel layout is rejected for wrapper bypass");
  sonora_dsp_destroy(dsp);
  std::puts("PASS: native DSP core");
  return 0;
}
