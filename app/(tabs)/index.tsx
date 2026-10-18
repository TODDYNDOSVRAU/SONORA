import { useEffect, useState } from "react";
import {
  AppState,
  Pressable,
  ScrollView,
  StatusBar,
  StyleSheet,
  Switch,
  Text,
  View,
} from "react-native";
import { SafeAreaView } from "react-native-safe-area-context";
import { Ionicons } from "@expo/vector-icons";

import { BandSlider } from "@/components/audio/band-slider";
import { EngineStatusCard } from "@/components/audio/engine-status";
import { PositionSelector } from "@/components/audio/position-selector";
import { SonoraMark } from "@/components/brand/sonora-mark";
import { EQ_LABELS, type SpatialPosition } from "@/lib/audio/model";
import { useAudioController } from "@/lib/audio/use-audio-controller";

const C = {
  bg: "#080D0F",
  surface: "#11191C",
  surfaceRaised: "#152024",
  border: "#26363A",
  accent: "#58E6D0",
  text: "#EAF4F2",
  muted: "#91A3A7",
  danger: "#FFB46E",
};

export default function HomeScreen() {
  const { settings, status, loading, notice, updateSettings, refreshStatus, requestRoot } = useAudioController();
  const [appState, setAppState] = useState(AppState.currentState);
  const engineListed = Boolean(status?.rootAvailable && status?.kernelModuleInstalled && status?.effectListed);
  const processingSubtitle = engineListed
    ? settings.enabled ? "Ativação solicitada · áudio não confirmado" : "Efeito listado · bypass solicitado"
    : status?.kernelModuleInstalled ? "Módulo detectado · aguardando efeito listado" : "Preferências salvas · motor indisponível";

  useEffect(() => {
    const sub = AppState.addEventListener("change", (next) => {
      setAppState(next);
      if (next === "active") void refreshStatus();
    });
    return () => sub.remove();
  }, [refreshStatus]);

  const updatePosition = (position: SpatialPosition) => { void updateSettings({ position }); };
  const updateBand = (index: number, gain: number) => {
    const next = [...settings.eqGainsDb];
    next[index] = gain;
    void updateSettings({ eqGainsDb: next });
  };
  const adjustPreamp = (delta: number) => {
    const next = Math.max(-12, Math.min(6, Math.round((settings.preampDb + delta) * 2) / 2));
    void updateSettings({ preampDb: next });
  };
  const reset = () => {
    void updateSettings({ enabled: false, position: "center", preampDb: 0, eqGainsDb: Array(10).fill(0) });
  };

  return (
    <View style={styles.root}>
      <StatusBar barStyle="light-content" backgroundColor={C.bg} />
      <SafeAreaView style={styles.safeArea} edges={["top", "left", "right"]}>
        <ScrollView contentContainerStyle={styles.content} showsVerticalScrollIndicator={false}>
          <View style={styles.topBar}>
            <View style={styles.brandLockup}>
              <SonoraMark />
              <View>
                <Text style={styles.brand}>SONORA</Text>
                <Text style={styles.brandMeta}>SISTEMA DE ÁUDIO · ANDROID</Text>
              </View>
            </View>
            <View style={styles.versionPill}><Text style={styles.versionText}>DSP / 01</Text></View>
          </View>

          <EngineStatusCard status={status} onRefresh={() => { void refreshStatus(); }} onConnectRoot={() => { void requestRoot(); }} />

          <View style={styles.sectionHeader}>
            <View>
              <Text style={styles.eyebrow}>CAMPO SONORO</Text>
              <Text style={styles.sectionTitle}>Posição virtual</Text>
            </View>
            <Text style={styles.angleReadout}>{positionReadout(settings.position)}</Text>
          </View>
          <View style={styles.positionCard}>
            <PositionSelector value={settings.position} onChange={updatePosition} />
            <View style={styles.positionFooter}>
              <Ionicons name="move-outline" size={15} color={C.accent} />
              <Text style={styles.positionHint}>Toque num ponto para mover a fonte virtual</Text>
            </View>
          </View>

          <View style={styles.engineToggle}>
            <View style={styles.toggleIcon}>
              <Ionicons name="power-outline" size={18} color={engineListed && settings.enabled ? C.accent : C.muted} />
            </View>
            <View style={styles.toggleCopy}>
              <Text style={styles.toggleTitle}>Processamento global</Text>
              <Text style={styles.toggleSubtitle}>{processingSubtitle}</Text>
            </View>
            <Switch
              value={settings.enabled}
              onValueChange={(enabled) => { void updateSettings({ enabled }); }}
              trackColor={{ false: "#2A393D", true: "#23766C" }}
              thumbColor={settings.enabled ? C.accent : "#A8B6B8"}
              accessibilityLabel="Ativar processamento global Sonora"
            />
          </View>

          <View style={styles.sectionHeader}>
            <View>
              <Text style={styles.eyebrow}>EQUALIZAÇÃO</Text>
              <Text style={styles.sectionTitle}>10 bandas</Text>
            </View>
            <Pressable onPress={reset} style={styles.resetButton} accessibilityRole="button">
              <Ionicons name="refresh-outline" size={13} color={C.accent} />
              <Text style={styles.resetText}>NEUTRO</Text>
            </Pressable>
          </View>
          <View style={styles.eqCard}>
            <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={styles.eqScroll}>
              {EQ_LABELS.map((label, index) => (
                <BandSlider key={label} label={label} valueDb={settings.eqGainsDb[index] ?? 0} onChange={(gain) => updateBand(index, gain)} />
              ))}
            </ScrollView>
            <View style={styles.eqDivider} />
            <View style={styles.preampRow}>
              <View style={styles.preampCopy}>
                <Text style={styles.preampLabel}>PRÉ-AMPLIFICAÇÃO</Text>
                <Text style={styles.preampNote}>Reduza se as bandas estiverem elevadas</Text>
              </View>
              <View style={styles.preampControl}>
                <Pressable onPress={() => adjustPreamp(-0.5)} style={styles.preampButton} accessibilityLabel="Diminuir pré-amplificação">
                  <Text style={styles.preampButtonText}>−</Text>
                </Pressable>
                <Text style={styles.preampValue}>{settings.preampDb > 0 ? "+" : ""}{settings.preampDb.toFixed(1)}<Text style={styles.dbUnit}> dB</Text></Text>
                <Pressable onPress={() => adjustPreamp(0.5)} style={styles.preampButton} accessibilityLabel="Aumentar pré-amplificação">
                  <Text style={styles.preampButtonText}>+</Text>
                </Pressable>
              </View>
            </View>
          </View>

          {notice ? (
            <View style={styles.notice}>
              <Ionicons name={engineListed ? "checkmark-circle-outline" : "information-circle-outline"} size={16} color={engineListed ? C.accent : C.danger} />
              <Text style={styles.noticeText}>{notice}</Text>
            </View>
          ) : null}

          <View style={styles.infoBlock}>
            <Text style={styles.infoTitle}>SOBRE A POSIÇÃO</Text>
            <Text style={styles.infoText}>A posição usa pistas estéreo aproximadas de panorâmica, diferença de tempo e nível entre os canais e filtros espectrais, seguindo conceitos do guia técnico enviado. O processamento atua sobre o mix inteiro como uma única fonte virtual; não separa instrumentos nem objetos já combinados por outros apps. É um DSP próprio, não um renderer Dolby Atmos nem uma certificação Dolby.</Text>
          </View>

          <View style={styles.footer}>
            <View style={styles.footerLine} />
            <Text style={styles.footerText}>{loading ? "INICIALIZANDO" : engineListed && settings.enabled ? "SONORA · ATIVAÇÃO SOLICITADA" : engineListed ? "SONORA · BYPASS SOLICITADO" : "AGUARDANDO ROOT + EFEITO COMPATÍVEL"}</Text>
            <Text style={styles.footerSub}>{appState === "active" ? "Preferências guardadas neste aparelho" : ""}</Text>
          </View>
        </ScrollView>
      </SafeAreaView>
    </View>
  );
}

function positionReadout(position: SpatialPosition): string {
  const readouts: Record<SpatialPosition, string> = {
    center: "0° · NEUTRO",
    front: "0° · FRENTE",
    back: "180° · ATRÁS",
    right: "+90° · DIREITA",
    left: "−90° · ESQUERDA",
  };
  return readouts[position];
}

const styles = StyleSheet.create({
  root: { flex: 1, backgroundColor: C.bg },
  safeArea: { flex: 1 },
  content: { paddingHorizontal: 18, paddingTop: 12, paddingBottom: 32, gap: 18 },
  topBar: { minHeight: 54, flexDirection: "row", alignItems: "center", justifyContent: "space-between", marginBottom: 1 },
  brandLockup: { flexDirection: "row", alignItems: "center", gap: 11 },
  brand: { color: C.text, fontSize: 15, letterSpacing: 2.3, fontWeight: "800" },
  brandMeta: { color: C.muted, fontSize: 8, letterSpacing: 1, marginTop: 2 },
  versionPill: { borderWidth: 1, borderColor: C.border, backgroundColor: C.surface, borderRadius: 8, paddingHorizontal: 9, paddingVertical: 6 },
  versionText: { color: C.accent, fontSize: 9, fontWeight: "700", letterSpacing: 0.8 },
  sectionHeader: { flexDirection: "row", justifyContent: "space-between", alignItems: "flex-end", marginTop: 2, marginBottom: -9 },
  eyebrow: { color: C.accent, fontSize: 9, fontWeight: "700", letterSpacing: 1.7, marginBottom: 4 },
  sectionTitle: { color: C.text, fontSize: 19, fontWeight: "700", letterSpacing: -0.3 },
  angleReadout: { color: C.muted, fontSize: 9, fontVariant: ["tabular-nums"], letterSpacing: 0.8, paddingBottom: 3 },
  positionCard: { backgroundColor: C.surface, borderColor: C.border, borderWidth: 1, borderRadius: 18, paddingTop: 13, paddingBottom: 12, overflow: "hidden" },
  positionFooter: { flexDirection: "row", alignItems: "center", justifyContent: "center", gap: 6, marginTop: 1 },
  positionHint: { color: C.muted, fontSize: 10 },
  engineToggle: { minHeight: 66, borderWidth: 1, borderColor: C.border, backgroundColor: C.surface, borderRadius: 15, paddingHorizontal: 12, flexDirection: "row", alignItems: "center", gap: 10 },
  toggleIcon: { width: 34, height: 34, borderRadius: 11, backgroundColor: C.surfaceRaised, alignItems: "center", justifyContent: "center" },
  toggleCopy: { flex: 1, gap: 3 },
  toggleTitle: { color: C.text, fontSize: 13, fontWeight: "700" },
  toggleSubtitle: { color: C.muted, fontSize: 10 },
  resetButton: { flexDirection: "row", alignItems: "center", gap: 4, paddingHorizontal: 9, paddingVertical: 7, borderWidth: 1, borderColor: C.border, borderRadius: 8 },
  resetText: { color: C.accent, fontSize: 9, fontWeight: "700", letterSpacing: 0.7 },
  eqCard: { backgroundColor: C.surface, borderColor: C.border, borderWidth: 1, borderRadius: 18, paddingTop: 14, paddingBottom: 12 },
  eqScroll: { paddingHorizontal: 12, gap: 3 },
  eqDivider: { height: 1, backgroundColor: C.border, marginHorizontal: 13, marginTop: 13, marginBottom: 12 },
  preampRow: { paddingHorizontal: 14, flexDirection: "row", alignItems: "center", justifyContent: "space-between", gap: 8 },
  preampCopy: { flex: 1, gap: 3 },
  preampLabel: { color: C.text, fontSize: 9, fontWeight: "700", letterSpacing: 0.9 },
  preampNote: { color: C.muted, fontSize: 9, lineHeight: 13 },
  preampControl: { flexDirection: "row", alignItems: "center", gap: 8 },
  preampButton: { width: 28, height: 28, borderRadius: 9, backgroundColor: C.surfaceRaised, borderWidth: 1, borderColor: C.border, alignItems: "center", justifyContent: "center" },
  preampButtonText: { color: C.text, fontSize: 17, lineHeight: 19 },
  preampValue: { color: C.accent, fontSize: 12, fontVariant: ["tabular-nums"], minWidth: 51, textAlign: "center", fontWeight: "700" },
  dbUnit: { color: C.muted, fontSize: 9, fontWeight: "500" },
  notice: { flexDirection: "row", alignItems: "flex-start", gap: 8, borderWidth: 1, borderColor: C.border, borderRadius: 12, backgroundColor: C.surfaceRaised, padding: 11 },
  noticeText: { flex: 1, color: C.text, fontSize: 11, lineHeight: 16 },
  infoBlock: { paddingHorizontal: 2, gap: 6 },
  infoTitle: { color: C.muted, fontSize: 9, fontWeight: "700", letterSpacing: 1.2 },
  infoText: { color: C.muted, fontSize: 10, lineHeight: 15 },
  footer: { alignItems: "center", gap: 5, paddingTop: 3 },
  footerLine: { width: 34, height: 2, borderRadius: 1, backgroundColor: C.accent, opacity: 0.65, marginBottom: 2 },
  footerText: { color: C.muted, fontSize: 8, letterSpacing: 1, textAlign: "center" },
  footerSub: { color: "#607176", fontSize: 9 },
});
