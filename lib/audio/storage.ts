import AsyncStorage from "@react-native-async-storage/async-storage";

import { DEFAULT_AUDIO_SETTINGS, normalizeSettings, type AudioSettings } from "./model";

const SETTINGS_KEY = "sonora.audio.settings.v1";

export async function loadAudioSettings(): Promise<AudioSettings> {
  try {
    const raw = await AsyncStorage.getItem(SETTINGS_KEY);
    return normalizeSettings(raw ? JSON.parse(raw) : DEFAULT_AUDIO_SETTINGS);
  } catch {
    return { ...DEFAULT_AUDIO_SETTINGS, eqGainsDb: [...DEFAULT_AUDIO_SETTINGS.eqGainsDb] };
  }
}

export async function saveAudioSettings(settings: AudioSettings): Promise<void> {
  await AsyncStorage.setItem(SETTINGS_KEY, JSON.stringify(normalizeSettings(settings)));
}
