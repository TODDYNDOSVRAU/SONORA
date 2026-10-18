import { Pressable, StyleSheet, Text, View, type GestureResponderEvent } from "react-native";

import { EQ_MAX_DB, EQ_MIN_DB } from "@/lib/audio/model";

const C = { accent: "#58E6D0", track: "#29383C", text: "#D8E5E3", muted: "#819399" };
const TRACK_HEIGHT = 100;
const ZERO_Y = TRACK_HEIGHT / 2;

interface BandSliderProps {
  label: string;
  valueDb: number;
  disabled?: boolean;
  onChange: (value: number) => void;
}

export function BandSlider({ label, valueDb, disabled = false, onChange }: BandSliderProps) {
  const bounded = Math.max(EQ_MIN_DB, Math.min(EQ_MAX_DB, valueDb));
  const magnitudeHeight = (Math.abs(bounded) / EQ_MAX_DB) * ZERO_Y;
  const fillBottom = bounded >= 0 ? ZERO_Y : ZERO_Y - magnitudeHeight;
  const thumbBottom = Math.max(0, Math.min(TRACK_HEIGHT - 12, ZERO_Y + (bounded / EQ_MAX_DB) * ZERO_Y - 6));

  const setFromTouch = (event: GestureResponderEvent) => {
    const y = Math.max(0, Math.min(TRACK_HEIGHT, event.nativeEvent.locationY));
    const value = EQ_MAX_DB - (y / TRACK_HEIGHT) * (EQ_MAX_DB - EQ_MIN_DB);
    onChange(Math.round(value * 2) / 2);
  };

  const adjust = (delta: number) => onChange(Math.max(EQ_MIN_DB, Math.min(EQ_MAX_DB, Math.round((bounded + delta) * 2) / 2)));

  return (
    <View style={styles.band}>
      <Text style={styles.value}>{bounded > 0 ? "+" : ""}{bounded.toFixed(1)}</Text>
      <Pressable
        accessibilityRole="adjustable"
        accessibilityLabel={`${label} hertz`}
        accessibilityValue={{ min: EQ_MIN_DB, max: EQ_MAX_DB, now: bounded, text: `${bounded.toFixed(1)} dB` }}
        accessibilityActions={[{ name: "increment", label: "Aumentar 0,5 dB" }, { name: "decrement", label: "Reduzir 0,5 dB" }]}
        onAccessibilityAction={(event) => adjust(event.nativeEvent.actionName === "increment" ? 0.5 : -0.5)}
        disabled={disabled}
        onPress={setFromTouch}
        style={[styles.track, disabled && styles.disabled]}
      >
        <View style={[styles.fill, { height: magnitudeHeight, bottom: fillBottom, backgroundColor: C.accent }]} />
        <View style={styles.zeroLine} />
        <View style={[styles.thumb, { bottom: thumbBottom }]} />
      </Pressable>
      <View style={styles.stepRow}>
        <Pressable accessibilityLabel={`Reduzir ${label} hertz`} hitSlop={5} onPress={() => adjust(-0.5)} disabled={disabled} style={styles.stepButton}>
          <Text style={styles.stepText}>−</Text>
        </Pressable>
        <Pressable accessibilityLabel={`Aumentar ${label} hertz`} hitSlop={5} onPress={() => adjust(0.5)} disabled={disabled} style={styles.stepButton}>
          <Text style={styles.stepText}>+</Text>
        </Pressable>
      </View>
      <Text style={styles.label}>{label}</Text>
    </View>
  );
}

const styles = StyleSheet.create({
  band: { width: 42, alignItems: "center", gap: 5 },
  value: { minHeight: 15, color: C.text, fontSize: 9, fontVariant: ["tabular-nums"] },
  track: { width: 22, height: TRACK_HEIGHT, borderRadius: 11, backgroundColor: C.track, overflow: "hidden" },
  fill: { position: "absolute", left: 0, right: 0, opacity: 0.36 },
  zeroLine: { position: "absolute", top: ZERO_Y, left: 0, right: 0, height: 1, backgroundColor: "#9AAEB2", zIndex: 2 },
  thumb: { position: "absolute", left: -1, width: 24, height: 12, borderRadius: 6, backgroundColor: C.accent, borderWidth: 2, borderColor: "#0D1718", zIndex: 3 },
  stepRow: { flexDirection: "row", alignItems: "center", justifyContent: "center", gap: 1 },
  stepButton: { width: 19, height: 19, borderRadius: 10, alignItems: "center", justifyContent: "center", backgroundColor: "#1B282B" },
  stepText: { color: C.text, fontSize: 14, lineHeight: 16, fontWeight: "600" },
  label: { color: C.muted, fontSize: 9, fontVariant: ["tabular-nums"] },
  disabled: { opacity: 0.55 },
});
