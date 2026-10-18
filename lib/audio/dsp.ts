import {
  EQ_FREQUENCIES_HZ,
  EQ_MAX_DB,
  EQ_MIN_DB,
  POSITION_ANGLES_DEG,
  clamp,
  type AudioSettings,
  type SpatialPosition,
} from "./model";

const SPEED_OF_SOUND_M_S = 343;
const HEAD_RADIUS_M = 0.0875;

export interface SpatialCoefficients {
  azimuthDeg: number;
  panScalar: number;
  leftGain: number;
  rightGain: number;
  farEarDelayMs: number;
  farEarDelaySamples: number;
  rearCue: boolean;
  frontCue: boolean;
  spatialBypass: boolean;
  farEarAttenuationDb: number;
  rearHighShelfDb: number;
  rearNotchHz: number;
  rearNotchDb: number;
}

/**
 * Generic horizontal cues for one virtual source over an already mixed stereo signal.
 * The equal-power pair follows the guide's panner equation; the simple head-shadow and
 * Woodworth-style ITD are approximations, not a measured or personalized HRTF/HRIR.
 * This does not recover objects from a mix that has already been rendered.
 */
export function spatialCoefficients(position: SpatialPosition, sampleRate = 48_000): SpatialCoefficients {
  const azimuthDeg = POSITION_ANGLES_DEG[position];
  const radians = (azimuthDeg * Math.PI) / 180;
  const panScalar = clamp(Math.sin(radians), -1, 1);

  // Equation 5 in the supplied guide: theta=(p+1)π/4; gL=cos(theta), gR=sin(theta).
  const pannerAngle = ((panScalar + 1) * Math.PI) / 4;
  const leftGain = Math.cos(pannerAngle);
  const rightGain = Math.sin(pannerAngle);

  // Simple horizontal-head approximation. Front/back ambiguity remains without a
  // subject-specific HRTF; rear mode therefore uses only a conservative spectral cue.
  const headAngle = Math.asin(panScalar);
  const itdSeconds = (HEAD_RADIUS_M / SPEED_OF_SOUND_M_S) * (headAngle + Math.sin(headAngle));
  const farEarDelayMs = Math.min(Math.abs(itdSeconds) * 1000, 0.7);
  const safeSampleRate = clamp(sampleRate, 8_000, 192_000);
  const farEarDelaySamples = Math.round((farEarDelayMs * safeSampleRate) / 1000);
  const farEarAttenuationDb = 6 * Math.abs(panScalar);

  return {
    azimuthDeg,
    panScalar,
    leftGain,
    rightGain,
    farEarDelayMs,
    farEarDelaySamples,
    rearCue: position === "back",
    frontCue: position === "front",
    spatialBypass: position === "center",
    farEarAttenuationDb,
    rearHighShelfDb: position === "back" ? -2.5 : 0,
    rearNotchHz: 6_000,
    rearNotchDb: position === "back" ? -3 : 0,
  };
}

export interface BiquadCoefficients {
  b0: number;
  b1: number;
  b2: number;
  a1: number;
  a2: number;
}

/** RBJ peaking-EQ coefficients, normalized for a direct-form biquad. */
export function peakingBiquad(
  sampleRate: number,
  frequencyHz: number,
  gainDb: number,
  q = 1,
): BiquadCoefficients {
  const safeSampleRate = Math.max(8_000, sampleRate);
  const nyquistLimit = safeSampleRate * 0.49;
  const frequency = clamp(frequencyHz, 10, nyquistLimit);
  const gain = clamp(gainDb, EQ_MIN_DB, EQ_MAX_DB);
  const safeQ = clamp(q, 0.1, 10);
  const a = 10 ** (gain / 40);
  const omega = (2 * Math.PI * frequency) / safeSampleRate;
  const cosine = Math.cos(omega);
  const alpha = Math.sin(omega) / (2 * safeQ);
  const a0 = 1 + alpha / a;

  return {
    b0: (1 + alpha * a) / a0,
    b1: (-2 * cosine) / a0,
    b2: (1 - alpha * a) / a0,
    a1: (-2 * cosine) / a0,
    a2: (1 - alpha / a) / a0,
  };
}

export interface EngineParameters {
  enabled: boolean;
  position: SpatialPosition;
  preampLinear: number;
  eq: Array<{ frequencyHz: number; gainDb: number; q: number }>;
  spatial: SpatialCoefficients;
}

export function toEngineParameters(settings: AudioSettings, sampleRate = 48_000): EngineParameters {
  const safePreampDb = clamp(settings.preampDb, -12, 6);
  return {
    enabled: settings.enabled,
    position: settings.position,
    preampLinear: 10 ** (safePreampDb / 20),
    eq: EQ_FREQUENCIES_HZ.map((frequencyHz, index) => ({
      frequencyHz,
      gainDb: clamp(settings.eqGainsDb[index] ?? 0, EQ_MIN_DB, EQ_MAX_DB),
      q: 1,
    })),
    spatial: spatialCoefficients(settings.position, sampleRate),
  };
}
