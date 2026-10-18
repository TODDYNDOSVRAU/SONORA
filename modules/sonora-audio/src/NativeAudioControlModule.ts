import { NativeModule, requireNativeModule } from "expo";

export type NativeEngineStatus = {
  platform: "android";
  rootAvailable: boolean;
  kernelModuleInstalled: boolean;
  effectListed: boolean;
  outputRoute: string | null;
  detail: string;
  checkedAt: number;
};

export type NativeAudioSettings = {
  enabled: boolean;
  position: "center" | "front" | "back" | "right" | "left";
  preampDb: number;
  eqGainsDb: number[];
};

export type NativeOperationResult = { ok: boolean; detail: string };

declare class SonoraAudioControlModule extends NativeModule {
  getStatus(): Promise<NativeEngineStatus>;
  requestRoot(): Promise<NativeOperationResult>;
  applySettings(settings: NativeAudioSettings): Promise<NativeOperationResult>;
}

export default requireNativeModule<SonoraAudioControlModule>("SonoraAudioControl");
