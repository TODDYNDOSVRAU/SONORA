import { Pressable, StyleSheet, Text, View } from "react-native";

import type { EngineStatus } from "@/lib/audio/model";

const C = { accent: "#58E6D0", danger: "#FFB46E", text: "#EAF4F2", muted: "#9CAEB1", panel: "#11191C", border: "#29393D" };

interface EngineStatusProps {
  status: EngineStatus | null;
  onRefresh: () => void;
  onConnectRoot: () => void;
}

export function EngineStatusCard({ status, onRefresh, onConnectRoot }: EngineStatusProps) {
  const ready = Boolean(status?.rootAvailable && status?.kernelModuleInstalled && status?.effectListed);
  const needsRoot = status?.platform === "android" && !status.rootAvailable;
  const title = ready ? "EFEITO LISTADO" : status?.platform === "web" ? "PRÉVIA WEB" : needsRoot ? "ROOT NÃO CONECTADO" : "AGUARDANDO MÓDULO";
  const rows = [
    { label: "ACESSO SU", ok: Boolean(status?.rootAvailable) },
    { label: "MÓDULO DETECTADO", ok: Boolean(status?.kernelModuleInstalled) },
    { label: "NOME DO EFEITO LISTADO", ok: Boolean(status?.effectListed) },
  ];

  return (
    <View style={styles.card}>
      <View style={styles.header}>
        <View style={[styles.liveDot, { backgroundColor: ready ? C.accent : C.danger }]} />
        <View style={styles.headerText}>
          <Text style={[styles.title, { color: ready ? C.accent : C.danger }]}>{title}</Text>
          <Text style={styles.subtitle}>{status?.outputRoute ? `SAÍDAS DETECTADAS · ${status.outputRoute}` : "SAÍDAS DETECTADAS · aguardando Android"}</Text>
        </View>
        <Pressable
          onPress={needsRoot ? onConnectRoot : onRefresh}
          accessibilityRole="button"
          accessibilityLabel={needsRoot ? "Conectar acesso root pelo gerenciador instalado" : "Atualizar estado do motor"}
          style={styles.refreshButton}
        >
          <Text style={styles.refreshText}>{needsRoot ? "CONECTAR ROOT" : "ATUALIZAR"}</Text>
        </Pressable>
      </View>
      <View style={styles.checks}>
        {rows.map((row) => (
          <View key={row.label} style={styles.check}>
            <View style={[styles.checkDot, { backgroundColor: row.ok ? C.accent : "#48595D" }]} />
            <Text style={[styles.checkText, row.ok && styles.checkTextOn]}>{row.label}</Text>
          </View>
        ))}
      </View>
      <Text style={styles.detail}>{status?.detail ?? "Consultando o estado do motor…"}</Text>
      <Text style={styles.disclaimer}>A lista mostra saídas detectadas, não a rota ativa. O nome aparecer no AudioFlinger não prova que buffers estejam sendo processados; confirme com reprodução real. A prévia web não altera o áudio do telefone.</Text>
    </View>
  );
}

const styles = StyleSheet.create({
  card: { backgroundColor: C.panel, borderColor: C.border, borderWidth: 1, borderRadius: 16, padding: 14, gap: 12 },
  header: { flexDirection: "row", alignItems: "center", gap: 9 },
  liveDot: { width: 8, height: 8, borderRadius: 4 },
  headerText: { flex: 1, gap: 2 },
  title: { fontSize: 11, fontWeight: "800", letterSpacing: 1.3 },
  subtitle: { color: C.muted, fontSize: 10, letterSpacing: 0.3 },
  refreshButton: { borderRadius: 9, paddingVertical: 8, paddingHorizontal: 9, borderWidth: 1, borderColor: C.border },
  refreshText: { color: C.text, fontSize: 9, fontWeight: "700", letterSpacing: 0.7 },
  checks: { flexDirection: "row", flexWrap: "wrap", gap: 10 },
  check: { flexDirection: "row", alignItems: "center", gap: 5 },
  checkDot: { width: 5, height: 5, borderRadius: 3 },
  checkText: { fontSize: 8, letterSpacing: 0.6, color: "#788A8E", fontWeight: "700" },
  checkTextOn: { color: C.accent },
  detail: { color: C.text, fontSize: 12, lineHeight: 17 },
  disclaimer: { color: C.muted, fontSize: 10, lineHeight: 15 },
});
