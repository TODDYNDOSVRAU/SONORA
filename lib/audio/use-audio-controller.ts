import { useCallback, useEffect, useRef, useState } from "react";

import { readEngineStatus, requestRootPermission, sendSettings } from "./bridge";
import { DEFAULT_AUDIO_SETTINGS, normalizeSettings, type AudioSettings, type EngineStatus } from "./model";
import { loadAudioSettings, saveAudioSettings } from "./storage";

const SAVE_DEBOUNCE_MS = 180;
const ROOT_WRITE_DEBOUNCE_MS = 80;

type Timer = ReturnType<typeof setTimeout>;

export function useAudioController() {
  const [settings, setSettings] = useState<AudioSettings>(() => normalizeSettings(DEFAULT_AUDIO_SETTINGS));
  const [status, setStatus] = useState<EngineStatus | null>(null);
  const [loading, setLoading] = useState(true);
  const [notice, setNotice] = useState<string | null>(null);
  const settingsRef = useRef(settings);
  const statusRef = useRef<EngineStatus | null>(null);
  const saveTimerRef = useRef<Timer | null>(null);
  const writeTimerRef = useRef<Timer | null>(null);

  useEffect(() => {
    statusRef.current = status;
  }, [status]);

  const refreshStatus = useCallback(async () => {
    const nextStatus = await readEngineStatus();
    statusRef.current = nextStatus;
    setStatus(nextStatus);
  }, []);

  useEffect(() => {
    let mounted = true;
    void (async () => {
      const [stored, currentStatus] = await Promise.all([loadAudioSettings(), readEngineStatus()]);
      if (!mounted) return;
      settingsRef.current = stored;
      statusRef.current = currentStatus;
      setSettings(stored);
      setStatus(currentStatus);
      setLoading(false);
    })();
    return () => {
      mounted = false;
      if (saveTimerRef.current) clearTimeout(saveTimerRef.current);
      if (writeTimerRef.current) clearTimeout(writeTimerRef.current);
      void saveAudioSettings(settingsRef.current);
    };
  }, []);

  const requestRoot = useCallback(async () => {
    const result = await requestRootPermission();
    setNotice(result.detail);
    await refreshStatus();
    const currentStatus = statusRef.current;
    if (result.ok && currentStatus?.rootAvailable && currentStatus.kernelModuleInstalled) {
      const applied = await sendSettings(settingsRef.current);
      setNotice(applied.detail);
    }
    return result.ok;
  }, [refreshStatus]);

  const updateSettings = useCallback((patch: Partial<AudioSettings>) => {
    const next = normalizeSettings({ ...settingsRef.current, ...patch });
    settingsRef.current = next;
    setSettings(next);

    if (saveTimerRef.current) clearTimeout(saveTimerRef.current);
    saveTimerRef.current = setTimeout(() => {
      void saveAudioSettings(settingsRef.current).catch(() => {
        setNotice("Não foi possível persistir a configuração; ela permanece somente nesta sessão.");
      });
      saveTimerRef.current = null;
    }, SAVE_DEBOUNCE_MS);

    const currentStatus = statusRef.current;
    if (currentStatus?.rootAvailable && currentStatus.kernelModuleInstalled) {
      if (writeTimerRef.current) clearTimeout(writeTimerRef.current);
      setNotice("Enviando os parâmetros ao módulo Sonora…");
      writeTimerRef.current = setTimeout(() => {
        void sendSettings(settingsRef.current).then((result) => setNotice(result.detail));
        writeTimerRef.current = null;
      }, ROOT_WRITE_DEBOUNCE_MS);
      return true;
    }

    setNotice("Configuração salva no aparelho; o efeito global não está carregado e nada foi aplicado ao áudio.");
    return false;
  }, []);

  const applySavedSettings = useCallback(async () => {
    const result = await sendSettings(settingsRef.current);
    setNotice(result.detail);
    return result.ok;
  }, []);

  return {
    settings,
    status,
    loading,
    notice,
    setNotice,
    updateSettings,
    refreshStatus,
    requestRoot,
    applySavedSettings,
  };
}
