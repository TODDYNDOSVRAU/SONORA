import { describe, expect, it } from "vitest";

import { peakingBiquad, spatialCoefficients, toEngineParameters } from "../lib/audio/dsp";
import { DEFAULT_AUDIO_SETTINGS, EQ_FREQUENCIES_HZ, type AudioSettings } from "../lib/audio/model";

describe("Sonora DSP coefficients", () => {
  it("uses neutral bypass for center without altering azimuth", () => {
    const center = spatialCoefficients("center");
    expect(center.spatialBypass).toBe(true);
    expect(center.panScalar).toBe(0);
    expect(center.farEarDelaySamples).toBe(0);
  });

  it("uses the guide's equal-power gains for the centered front image", () => {
    const front = spatialCoefficients("front");
    expect(front.spatialBypass).toBe(false);
    expect(front.leftGain).toBeCloseTo(Math.SQRT1_2, 8);
    expect(front.rightGain).toBeCloseTo(Math.SQRT1_2, 8);
    expect(front.leftGain ** 2 + front.rightGain ** 2).toBeCloseTo(1, 8);
  });

  it("maps right and left symmetrically with one-ear lead and an opposite-ear delay", () => {
    const right = spatialCoefficients("right", 48_000);
    const left = spatialCoefficients("left", 48_000);
    expect(right.panScalar).toBeCloseTo(1, 8);
    expect(left.panScalar).toBeCloseTo(-1, 8);
    expect(right.leftGain).toBeCloseTo(left.rightGain, 8);
    expect(right.rightGain).toBeCloseTo(left.leftGain, 8);
    expect(right.farEarDelaySamples).toBeGreaterThan(0);
    expect(right.farEarDelaySamples).toBe(left.farEarDelaySamples);
    expect(right.farEarAttenuationDb).toBeCloseTo(left.farEarAttenuationDb, 8);
  });

  it("uses a spectral-only rear cue for the rear median-plane position", () => {
    const rear = spatialCoefficients("back");
    expect(rear.rearCue).toBe(true);
    expect(rear.frontCue).toBe(false);
    expect(rear.panScalar).toBeCloseTo(0, 8);
    expect(rear.farEarDelaySamples).toBe(0);
    expect(rear.rearNotchHz).toBe(6_000);
    expect(rear.rearNotchDb).toBeLessThan(0);
  });

  it("produces unity coefficients for a 0 dB peaking band", () => {
    const coeffs = peakingBiquad(48_000, 1_000, 0);
    expect(coeffs.b0).toBeCloseTo(1, 12);
    expect(coeffs.b1).toBeCloseTo(coeffs.a1, 12);
    expect(coeffs.b2).toBeCloseTo(coeffs.a2, 12);
  });

  it("creates ten clamped bands and bounded linear preamp values", () => {
    const settings: AudioSettings = {
      ...DEFAULT_AUDIO_SETTINGS,
      position: "right",
      preampDb: 50,
      eqGainsDb: Array(EQ_FREQUENCIES_HZ.length).fill(-50),
    };
    const parameters = toEngineParameters(settings, 44_100);
    expect(parameters.eq).toHaveLength(10);
    expect(parameters.eq[0].gainDb).toBe(-12);
    expect(parameters.preampLinear).toBeCloseTo(10 ** (6 / 20), 8);
    expect(parameters.spatial.farEarDelaySamples).toBeLessThan(40);
  });
});
