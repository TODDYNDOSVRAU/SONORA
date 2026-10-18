export const POSITION_IDS = ["center", "front", "back", "right", "left"] as const;
export type SpatialPosition = (typeof POSITION_IDS)[number];

export const POSITION_LABELS: Record<SpatialPosition, string> = {
  center: "Centro",
  front: "Frente",
  back: "Atrás",
  right: "Direita",
  left: "Esquerda",
};

export const POSITION_ANGLES_DEG: Record<SpatialPosition, number> = {
  center: 0,
  front: 0,
  back: 180,
  right: 90,
  left: -90,
};

export const EQ_FREQUENCIES_HZ = [31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000] as const;
export const EQ_LABELS = ["31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"] as const;
export const EQ_MIN_DB = -12;
export const EQ_MAX_DB = 12;

export interface AudioSettings {
  enabled: boolean;
  position: SpatialPosition;
  preampDb: number;
  eqGainsDb: number[];
}

export interface EngineStatus {
  platform: "android" | "web" | "ios" | "unknown";
  rootAvailable: boolean;
  kernelModuleInstalled: boolean;
  effectListed: boolean;
  outputRoute: string | null;
  detail: string;
  checkedAt: number;
}

export const DEFAULT_AUDIO_SETTINGS: AudioSettings = {
  enabled: false,
  position: "center",
  preampDb: 0,
  eqGainsDb: Array(EQ_FREQUENCIES_HZ.length).fill(0),
};

export function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}

export function normalizeSettings(input: Partial<AudioSettings> | null | undefined): AudioSettings {
  const gains = Array.isArray(input?.eqGainsDb) ? input.eqGainsDb : [];
  return {
    enabled: Boolean(input?.enabled),
    position: POSITION_IDS.includes(input?.position as SpatialPosition)
      ? (input?.position as SpatialPosition)
      : "center",
    preampDb: clamp(Number.isFinite(input?.preampDb) ? Number(input?.preampDb) : 0, -12, 6),
    eqGainsDb: EQ_FREQUENCIES_HZ.map((_, index) =>
      clamp(Number.isFinite(gains[index]) ? Number(gains[index]) : 0, EQ_MIN_DB, EQ_MAX_DB),
    ),
  };
}
