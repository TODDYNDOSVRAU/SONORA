import { requireNativeModule } from "expo";
import { Platform } from "react-native";

import type { AudioSettings, EngineStatus } from "./model";

interface NativeAudioControl {
  getStatus(): Promise<Partial<EngineStatus> & { detail?: string }>;
  requestRoot(): Promise<{ ok: boolean; detail?: string }>;
  applySettings(settings: AudioSettings): Promise<{ ok: boolean; detail?: string }>;
}

let nativeAudio: NativeAudioControl | null = null;
if (Platform.OS === "android") {
  try {
    nativeAudio = requireNativeModule<NativeAudioControl>("SonoraAudioControl");
  } catch {
    nativeAudio = null;
  }
}

export function platformName(): EngineStatus["platform"] {
  if (Platform.OS === "android") return "android";
  if (Platform.OS === "web") return "web";
  if (Platform.OS === "ios") return "ios";
  return "unknown";
}

export async function readEngineStatus(): Promise<EngineStatus> {
  const platform = platformName();
  if (platform !== "android") {
    return {
      platform,
      rootAvailable: false,
      kernelModuleInstalled: false,
      effectListed: false,
      outputRoute: null,
      detail: platform === "web"
        ? "A prévia web não controla o áudio global do Android. Abra o APK no dispositivo."
        : "O Sonora está planejado para Android; esta plataforma não tem o módulo de sistema.",
      checkedAt: Date.now(),
    };
  }

  if (!nativeAudio) {
    return {
      platform,
      rootAvailable: false,
      kernelModuleInstalled: false,
      effectListed: false,
      outputRoute: null,
      detail: "Módulo nativo ausente neste build; nenhum áudio foi alterado.",
      checkedAt: Date.now(),
    };
  }

  try {
    const native = await nativeAudio.getStatus();
    return {
      platform,
      rootAvailable: Boolean(native.rootAvailable),
      kernelModuleInstalled: Boolean(native.kernelModuleInstalled),
      effectListed: Boolean(native.effectListed),
      outputRoute: native.outputRoute ?? null,
      detail: native.detail ?? "Estado do mecanismo atualizado.",
      checkedAt: Date.now(),
    };
  } catch (error) {
    return {
      platform,
      rootAvailable: false,
      kernelModuleInstalled: false,
      effectListed: false,
      outputRoute: null,
      detail: error instanceof Error ? error.message : "Não foi possível consultar o módulo.",
      checkedAt: Date.now(),
    };
  }
}

export async function requestRootPermission(): Promise<{ ok: boolean; detail: string }> {
  if (platformName() !== "android") {
    return { ok: false, detail: "A solicitação root existe somente no APK Android." };
  }
  if (!nativeAudio) {
    return { ok: false, detail: "Módulo nativo ausente neste build; a solicitação root não foi iniciada." };
  }
  try {
    const result = await nativeAudio.requestRoot();
    return { ok: result.ok, detail: result.detail ?? (result.ok ? "Acesso root concedido." : "Acesso root não concedido.") };
  } catch (error) {
    return { ok: false, detail: error instanceof Error ? error.message : "Falha na solicitação root." };
  }
}

export async function sendSettings(settings: AudioSettings): Promise<{ ok: boolean; detail: string }> {
  if (platformName() !== "android") {
    return { ok: false, detail: "O processamento global só existe no APK Android com o módulo KernelSU." };
  }
  if (!nativeAudio) {
    return { ok: false, detail: "Ponte nativa ausente neste build; o áudio não foi alterado." };
  }
  try {
    const result = await nativeAudio.applySettings(settings);
    return { ok: result.ok, detail: result.detail ?? (result.ok ? "Parâmetros enviados." : "O módulo recusou os parâmetros.") };
  } catch (error) {
    return { ok: false, detail: error instanceof Error ? error.message : "Falha ao enviar parâmetros ao módulo." };
  }
}
