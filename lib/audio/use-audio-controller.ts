import { useCallback, useEffect, useRef, useState } from "react";
import { AppState } from "react-native";

import { readEngineStatus, requestRootPermission, sendSettings } from "./bridge";
import { DEFAULT_AUDIO_SETTINGS, normalizeSettings, type AudioSettings, type EngineStatus } from "./model";
import { loadAudioSettings, saveAudioSettings } from "./storage";

const SAVE_DEBOUNCE_MS = 180;
const ROOT_WRITE_DEBOUNCE_MS = 80;

type Timer = ReturnType<typeof setTimeout>;

function canSendSettings(status: EngineStatus | null | undefined): boolean {
  return Boolean(status?.rootAvailable && status.kernelModuleInstalled && status.controlReady);
}

export function useAudioController() {
  const [settings, setSettings] = useState<AudioSettings>(() => normalizeSettings(DEFAULT_AUDIO_SETTINGS));
  const [status, setStatus] = useState<EngineStatus | null>(null);
  const [loading, setLoading] = useState(true);
  const [notice, setNotice] = useState<string | null>(null);
  const settingsRef = useRef(settings);
  const statusRef = useRef<EngineStatus | null>(null);
  const revisionRef = useRef(0);
  const saveTimerRef = useRef<Timer | null>(null);
  const writeTimerRef = useRef<Timer | null>(null);
  const writeQueueRef = useRef<Promise<unknown>>(Promise.resolve());
  const mountedRef = useRef(true);

  const enqueueSettings = useCallback((revision: number): Promise<boolean> => {
    const operation = writeQueueRef.current.catch(() => undefined).then(async () => {
      if (!mountedRef.current || revision !== revisionRef.current) return false;
      const result = await sendSettings(settingsRef.current);
      if (mountedRef.current && revision === revisionRef.current) setNotice(result.detail);
      return result.ok;
    });
    writeQueueRef.current = operation;
    return operation;
  }, []);

  const refreshStatus = useCallback(async () => {
    const nextStatus = await readEngineStatus();
    statusRef.current = nextStatus;
    setStatus(nextStatus);
    return nextStatus;
  }, []);

  const maybeApplyLatest = useCallback(async (nextStatus: EngineStatus) => {
    if (canSendSettings(nextStatus)) {
      await enqueueSettings(revisionRef.current);
    }
  }, [enqueueSettings]);

  useEffect(() => {
    mountedRef.current = true;
    let active = true;
    void (async () => {
      const [stored, currentStatus] = await Promise.all([loadAudioSettings(), readEngineStatus()]);
      if (!active || !mountedRef.current) return;
      if (revisionRef.current === 0) {
        settingsRef.current = stored;
        setSettings(stored);
      }
      statusRef.current = currentStatus;
      setStatus(currentStatus);
      setLoading(false);
      await maybeApplyLatest(currentStatus);
    })();

    const subscription = AppState.addEventListener("change", (nextState) => {
      if (nextState === "active") {
        void refreshStatus().then(maybeApplyLatest);
      }
    });

    return () => {
      active = false;
      mountedRef.current = false;
      subscription.remove();
      if (saveTimerRef.current) clearTimeout(saveTimerRef.current);
      if (writeTimerRef.current) clearTimeout(writeTimerRef.current);
      void saveAudioSettings(settingsRef.current);
    };
  }, [maybeApplyLatest, refreshStatus]);

  const requestRoot = useCallback(async () => {
    const result = await requestRootPermission();
    setNotice(result.detail);
    const nextStatus = await refreshStatus();
    if (result.ok) await maybeApplyLatest(nextStatus);
    return result.ok;
  }, [maybeApplyLatest, refreshStatus]);

  const updateSettings = useCallback((patch: Partial<AudioSettings>) => {
    const next = normalizeSettings({ ...settingsRef.current, ...patch });
    settingsRef.current = next;
    setSettings(next);
    const revision = ++revisionRef.current;

    if (saveTimerRef.current) clearTimeout(saveTimerRef.current);
    saveTimerRef.current = setTimeout(() => {
      const snapshot = settingsRef.current;
      void saveAudioSettings(snapshot).then(() => {
        if (mountedRef.current && revision === revisionRef.current &&
            !canSendSettings(statusRef.current)) {
          setNotice("Preferências guardadas localmente; não foram aplicadas ao mix de mídia.");
        }
      }).catch(() => {
        if (mountedRef.current && revision === revisionRef.current) {
          setNotice("Não foi possível persistir a configuração; ela permanece somente nesta sessão.");
        }
      });
      saveTimerRef.current = null;
    }, SAVE_DEBOUNCE_MS);

    const currentStatus = statusRef.current;
    if (canSendSettings(currentStatus)) {
      if (writeTimerRef.current) clearTimeout(writeTimerRef.current);
      setNotice("Enviando a revisão atual ao helper do stream de mídia…");
      writeTimerRef.current = setTimeout(() => {
        writeTimerRef.current = null;
        void enqueueSettings(revision);
      }, ROOT_WRITE_DEBOUNCE_MS);
      return true;
    }

    setNotice("Preferências guardadas localmente; conecte root e módulo compatível para enviá-las ao mix de mídia.");
    return false;
  }, [enqueueSettings]);

  const applySavedSettings = useCallback(async () => {
    const currentStatus = statusRef.current ?? await refreshStatus();
    if (!canSendSettings(currentStatus)) {
      setNotice("O helper do módulo ainda não está pronto; as preferências permanecem locais.");
      return false;
    }
    return enqueueSettings(revisionRef.current).catch(() => false);
  }, [enqueueSettings, refreshStatus]);

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
