#include "sonora_dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kHeadRadiusM = 0.0875f;
constexpr float kSpeedOfSoundMps = 343.0f;
constexpr size_t kDelayCapacity = 256;
constexpr float kSmoothingSeconds = 0.035f;
constexpr float kMaxOutput = 0.98f;
constexpr float kLimiterKnee = 0.85f;
constexpr float kHeadroomNorm = 1.41421356237f;
constexpr float kPanHeadroom = 0.82f;
constexpr float kSideWidth = 0.28f;

struct BiquadCoefficients {
  float b0 = 1.0f;
  float b1 = 0.0f;
  float b2 = 0.0f;
  float a1 = 0.0f;
  float a2 = 0.0f;
};

struct BiquadState {
  BiquadCoefficients c;
  float z1 = 0.0f;
  float z2 = 0.0f;
};

BiquadCoefficients makePeaking(uint32_t sampleRate, float frequency, float gainDb, float q) {
  const double sr = std::max<uint32_t>(sampleRate, 8000);
  const double f = std::clamp<double>(frequency, 10.0, sr * 0.49);
  const double g = std::clamp<double>(gainDb, -12.0, 12.0);
  const double safeQ = std::clamp<double>(q, 0.1, 10.0);
  const double A = std::pow(10.0, g / 40.0);
  const double w0 = 2.0 * kPi * f / sr;
  const double alpha = std::sin(w0) / (2.0 * safeQ);
  const double a0 = 1.0 + alpha / A;
  return {
    static_cast<float>((1.0 + alpha * A) / a0),
    static_cast<float>((-2.0 * std::cos(w0)) / a0),
    static_cast<float>((1.0 - alpha * A) / a0),
    static_cast<float>((-2.0 * std::cos(w0)) / a0),
    static_cast<float>((1.0 - alpha / A) / a0)
  };
}

BiquadCoefficients makeHighShelf(uint32_t sampleRate, float frequency, float gainDb) {
  const double sr = std::max<uint32_t>(sampleRate, 8000);
  const double f = std::clamp<double>(frequency, 10.0, sr * 0.49);
  const double g = std::clamp<double>(gainDb, -12.0, 12.0);
  const double A = std::pow(10.0, g / 40.0);
  const double w0 = 2.0 * kPi * f / sr;
  const double c = std::cos(w0);
  const double s = std::sin(w0);
  constexpr double slope = 1.0;
  const double alpha = (s / 2.0) * std::sqrt((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
  const double beta = 2.0 * std::sqrt(A) * alpha;
  const double a0 = (A + 1.0) - (A - 1.0) * c + beta;
  return {
    static_cast<float>((A * ((A + 1.0) + (A - 1.0) * c + beta)) / a0),
    static_cast<float>((-2.0 * A * ((A - 1.0) + (A + 1.0) * c)) / a0),
    static_cast<float>((A * ((A + 1.0) + (A - 1.0) * c - beta)) / a0),
    static_cast<float>((2.0 * ((A - 1.0) - (A + 1.0) * c)) / a0),
    static_cast<float>(((A + 1.0) - (A - 1.0) * c - beta) / a0)
  };
}

float processBiquad(BiquadState &state, float x) {
  const float y = state.c.b0 * x + state.z1;
  state.z1 = state.c.b1 * x - state.c.a1 * y + state.z2;
  state.z2 = state.c.b2 * x - state.c.a2 * y;
  return y;
}

float softLimit(float x) {
  const float magnitude = std::fabs(x);
  if (magnitude <= kLimiterKnee) return x;
  const float limited = kLimiterKnee + (kMaxOutput - kLimiterKnee) *
    std::tanh((magnitude - kLimiterKnee) / (kMaxOutput - kLimiterKnee));
  return std::copysign(std::min(limited, kMaxOutput), x);
}

float dbToLinear(float db) {
  return std::pow(10.0f, db / 20.0f);
}

float readDelayed(const float *buffer, size_t writeIndex, float delaySamples) {
  const float boundedDelay = std::clamp(delaySamples, 0.0f, static_cast<float>(kDelayCapacity - 2));
  const size_t whole = static_cast<size_t>(boundedDelay);
  const float fraction = boundedDelay - static_cast<float>(whole);
  const size_t newer = (writeIndex + kDelayCapacity - whole) % kDelayCapacity;
  const size_t older = (newer + kDelayCapacity - 1) % kDelayCapacity;
  return buffer[newer] * (1.0f - fraction) + buffer[older] * fraction;
}

constexpr float kEqHz[SONORA_EQ_BANDS] = {31.0f, 62.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f};
}

struct SonoraDsp {
  uint32_t sampleRate;
  uint32_t lastGeneration;
  uint32_t targetPosition;
  bool targetEnabled;
  float targetPreampDb;
  float targetEqDb[SONORA_EQ_BANDS];
  float currentEnabled;
  float currentSpatial;
  float currentFront;
  float currentRear;
  float currentPan;
  float currentPreampDb;
  float currentEqDb[SONORA_EQ_BANDS];
  float currentDelaySamples;
  float currentFarGain;
  BiquadState eq[2][SONORA_EQ_BANDS];
  BiquadState rearShelf[2];
  BiquadState rearNotch[2];
  BiquadState frontShelf[2];
  float delayLeft[kDelayCapacity];
  float delayRight[kDelayCapacity];
  size_t delayWriteIndex;
};

extern "C" SonoraDsp *sonora_dsp_create(uint32_t sample_rate) {
  if (sample_rate < 8000 || sample_rate > 192000) return nullptr;
  auto *dsp = new (std::nothrow) SonoraDsp{};
  if (!dsp) return nullptr;
  dsp->sampleRate = sample_rate;
  dsp->lastGeneration = UINT32_MAX;
  dsp->targetPosition = SONORA_POSITION_CENTER;
  dsp->delayWriteIndex = 0;
  for (uint32_t channel = 0; channel < 2; ++channel) {
    dsp->rearShelf[channel].c = makeHighShelf(sample_rate, 8000.0f, -2.5f);
    dsp->rearNotch[channel].c = makePeaking(sample_rate, 6000.0f, -3.0f, 2.0f);
    dsp->frontShelf[channel].c = makeHighShelf(sample_rate, 4500.0f, 0.5f);
  }
  return dsp;
}

extern "C" void sonora_dsp_destroy(SonoraDsp *dsp) {
  delete dsp;
}

extern "C" void sonora_dsp_reset(SonoraDsp *dsp) {
  if (!dsp) return;
  const uint32_t rate = dsp->sampleRate;
  *dsp = SonoraDsp{};
  dsp->sampleRate = rate;
  dsp->lastGeneration = UINT32_MAX;
  dsp->targetPosition = SONORA_POSITION_CENTER;
  for (uint32_t channel = 0; channel < 2; ++channel) {
    dsp->rearShelf[channel].c = makeHighShelf(rate, 8000.0f, -2.5f);
    dsp->rearNotch[channel].c = makePeaking(rate, 6000.0f, -3.0f, 2.0f);
    dsp->frontShelf[channel].c = makeHighShelf(rate, 4500.0f, 0.5f);
  }
}

extern "C" int32_t sonora_dsp_process(
  SonoraDsp *dsp,
  const float *input,
  float *output,
  size_t frames,
  uint32_t channels,
  const SonoraParameters *parameters) {
  if (!dsp || !input || !output || !parameters || frames == 0 || (channels != 1 && channels != 2)) return -1;

  if (parameters->generation != dsp->lastGeneration) {
    dsp->lastGeneration = parameters->generation;
    dsp->targetEnabled = parameters->enabled != 0;
    dsp->targetPosition = parameters->position <= static_cast<uint32_t>(SONORA_POSITION_LEFT)
      ? parameters->position
      : static_cast<uint32_t>(SONORA_POSITION_CENTER);
    dsp->targetPreampDb = std::clamp(parameters->preamp_db, -12.0f, 6.0f);
    for (size_t band = 0; band < SONORA_EQ_BANDS; ++band) {
      const float requested = std::isfinite(parameters->eq_db[band]) ? parameters->eq_db[band] : 0.0f;
      dsp->targetEqDb[band] = std::clamp(requested, -12.0f, 12.0f);
    }
  }

  if (!dsp->targetEnabled && dsp->currentEnabled < 0.00001f) {
    std::memmove(output, input, frames * channels * sizeof(float));
    return 0;
  }

  const float smoothing = 1.0f - std::exp(-static_cast<float>(frames) / (static_cast<float>(dsp->sampleRate) * kSmoothingSeconds));
  const float targetEnabled = dsp->targetEnabled ? 1.0f : 0.0f;
  const bool targetSpatial = dsp->targetPosition != SONORA_POSITION_CENTER;
  const float targetFront = dsp->targetPosition == SONORA_POSITION_FRONT ? 1.0f : 0.0f;
  const float targetRear = dsp->targetPosition == SONORA_POSITION_BACK ? 1.0f : 0.0f;
  float targetPan = 0.0f;
  if (dsp->targetPosition == SONORA_POSITION_RIGHT) targetPan = 1.0f;
  if (dsp->targetPosition == SONORA_POSITION_LEFT) targetPan = -1.0f;

  dsp->currentEnabled += (targetEnabled - dsp->currentEnabled) * smoothing;
  dsp->currentSpatial += ((targetSpatial ? 1.0f : 0.0f) - dsp->currentSpatial) * smoothing;
  dsp->currentFront += (targetFront - dsp->currentFront) * smoothing;
  dsp->currentRear += (targetRear - dsp->currentRear) * smoothing;
  dsp->currentPan += (targetPan - dsp->currentPan) * smoothing;
  dsp->currentPreampDb += (dsp->targetPreampDb - dsp->currentPreampDb) * smoothing;

  for (size_t band = 0; band < SONORA_EQ_BANDS; ++band) {
    dsp->currentEqDb[band] += (dsp->targetEqDb[band] - dsp->currentEqDb[band]) * smoothing;
    const auto coefficients = makePeaking(dsp->sampleRate, kEqHz[band], dsp->currentEqDb[band], 1.0f);
    dsp->eq[0][band].c = coefficients;
    dsp->eq[1][band].c = coefficients;
  }

  const float headAngle = std::asin(std::clamp(dsp->currentPan, -1.0f, 1.0f));
  const float itdSeconds = (kHeadRadiusM / kSpeedOfSoundMps) * (headAngle + std::sin(headAngle));
  const float maxDelay = static_cast<float>(dsp->sampleRate) * 0.0007f;
  const float targetDelay = std::clamp(std::fabs(itdSeconds) * static_cast<float>(dsp->sampleRate), 0.0f, maxDelay);
  dsp->currentDelaySamples += (targetDelay - dsp->currentDelaySamples) * smoothing;
  const float targetFarDb = 6.0f * std::fabs(dsp->currentPan);
  const float targetFarGain = dbToLinear(-targetFarDb);
  dsp->currentFarGain += (targetFarGain - dsp->currentFarGain) * smoothing;
  const float preamp = dbToLinear(dsp->currentPreampDb);
  const float p = std::clamp(dsp->currentPan * kPanHeadroom, -kPanHeadroom, kPanHeadroom);
  const float panAngle = (p + 1.0f) * static_cast<float>(kPi / 4.0);
  const float gainLeft = std::cos(panAngle);
  const float gainRight = std::sin(panAngle);

  for (size_t frame = 0; frame < frames; ++frame) {
    const float inputLeft = input[frame * channels];
    const float inputRight = channels == 2 ? input[frame * channels + 1] : inputLeft;
    float wetLeft = inputLeft;
    float wetRight = inputRight;

    if (channels == 2) {
      for (size_t band = 0; band < SONORA_EQ_BANDS; ++band) {
        wetLeft = processBiquad(dsp->eq[0][band], wetLeft);
        wetRight = processBiquad(dsp->eq[1][band], wetRight);
      }
      const float rearLeft = processBiquad(dsp->rearNotch[0], processBiquad(dsp->rearShelf[0], wetLeft));
      const float rearRight = processBiquad(dsp->rearNotch[1], processBiquad(dsp->rearShelf[1], wetRight));
      const float frontLeft = processBiquad(dsp->frontShelf[0], wetLeft);
      const float frontRight = processBiquad(dsp->frontShelf[1], wetRight);
      wetLeft = (wetLeft * (1.0f - dsp->currentRear) + rearLeft * dsp->currentRear) * (1.0f - dsp->currentFront) + frontLeft * dsp->currentFront;
      wetRight = (wetRight * (1.0f - dsp->currentRear) + rearRight * dsp->currentRear) * (1.0f - dsp->currentFront) + frontRight * dsp->currentFront;
      wetLeft *= preamp;
      wetRight *= preamp;

      const float mid = (wetLeft + wetRight) * 0.5f;
      const float side = (wetLeft - wetRight) * 0.5f;
      const float sideWidth = kSideWidth * (1.0f - std::fabs(p));
      float positionedLeft = mid * gainLeft * kHeadroomNorm + side * sideWidth;
      float positionedRight = mid * gainRight * kHeadroomNorm - side * sideWidth;
      dsp->delayLeft[dsp->delayWriteIndex] = positionedLeft;
      dsp->delayRight[dsp->delayWriteIndex] = positionedRight;

      if (dsp->currentPan > 0.0001f) {
        positionedLeft = readDelayed(dsp->delayLeft, dsp->delayWriteIndex, dsp->currentDelaySamples) * dsp->currentFarGain;
      } else if (dsp->currentPan < -0.0001f) {
        positionedRight = readDelayed(dsp->delayRight, dsp->delayWriteIndex, dsp->currentDelaySamples) * dsp->currentFarGain;
      }
      dsp->delayWriteIndex = (dsp->delayWriteIndex + 1) % kDelayCapacity;

      const float spatialLeft = positionedLeft * dsp->currentSpatial + wetLeft * (1.0f - dsp->currentSpatial);
      const float spatialRight = positionedRight * dsp->currentSpatial + wetRight * (1.0f - dsp->currentSpatial);
      wetLeft = softLimit(spatialLeft);
      wetRight = softLimit(spatialRight);
    } else {
      for (size_t band = 0; band < SONORA_EQ_BANDS; ++band) wetLeft = processBiquad(dsp->eq[0][band], wetLeft);
      wetLeft = processBiquad(dsp->rearNotch[0], processBiquad(dsp->rearShelf[0], wetLeft)) * dsp->currentRear + wetLeft * (1.0f - dsp->currentRear);
      wetLeft = processBiquad(dsp->frontShelf[0], wetLeft) * dsp->currentFront + wetLeft * (1.0f - dsp->currentFront);
      wetLeft = softLimit(wetLeft * preamp);
      wetRight = wetLeft;
    }

    output[frame * channels] = inputLeft * (1.0f - dsp->currentEnabled) + wetLeft * dsp->currentEnabled;
    if (channels == 2) output[frame * channels + 1] = inputRight * (1.0f - dsp->currentEnabled) + wetRight * dsp->currentEnabled;
  }
  return 0;
}
