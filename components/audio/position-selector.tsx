import Svg, { Circle, Ellipse, Line } from "react-native-svg";
import { Pressable, StyleSheet, Text, View } from "react-native";

import { POSITION_LABELS, type SpatialPosition } from "@/lib/audio/model";

const C = {
  accent: "#58E6D0",
  muted: "#597077",
  text: "#EAF4F2",
  panel: "#121B1E",
};

const POINTS: { id: SpatialPosition; x: number; y: number }[] = [
  { id: "front", x: 150, y: 20 },
  { id: "left", x: 28, y: 88 },
  { id: "center", x: 150, y: 88 },
  { id: "right", x: 272, y: 88 },
  { id: "back", x: 150, y: 156 },
];

interface PositionSelectorProps {
  value: SpatialPosition;
  disabled?: boolean;
  onChange: (position: SpatialPosition) => void;
}

export function PositionSelector({ value, disabled = false, onChange }: PositionSelectorProps) {
  return (
    <View style={styles.wrap}>
      <Svg width={300} height={176} viewBox="0 0 300 176" style={StyleSheet.absoluteFill}>
        <Ellipse cx={150} cy={88} rx={122} ry={68} fill="none" stroke="#25363A" strokeWidth={1.5} strokeDasharray="4 6" />
        <Ellipse cx={150} cy={88} rx={72} ry={40} fill="none" stroke="#1E2B2F" strokeWidth={1} />
        <Line x1={150} y1={22} x2={150} y2={154} stroke="#223237" strokeWidth={1} />
        <Line x1={28} y1={88} x2={272} y2={88} stroke="#223237" strokeWidth={1} />
        <Circle cx={150} cy={88} r={3} fill="#52676D" />
      </Svg>
      {POINTS.map(({ id, x, y }) => {
        const selected = value === id;
        return (
          <Pressable
            key={id}
            accessibilityRole="button"
            accessibilityLabel={`Posição ${POSITION_LABELS[id]}`}
            accessibilityState={{ selected, disabled }}
            disabled={disabled}
            onPress={() => onChange(id)}
            style={({ pressed }) => [
              styles.point,
              { left: x - 36, top: y - 17, borderColor: selected ? C.accent : "#314449", backgroundColor: selected ? "#183632" : C.panel },
              pressed && !disabled ? styles.pressed : null,
              disabled ? styles.disabled : null,
            ]}
          >
            <View style={[styles.dot, { backgroundColor: selected ? C.accent : C.muted }]} />
            <Text style={[styles.pointText, selected && styles.selectedText]}>{POSITION_LABELS[id]}</Text>
          </Pressable>
        );
      })}
    </View>
  );
}

const styles = StyleSheet.create({
  wrap: { width: 300, height: 176, alignSelf: "center", marginTop: 4, marginBottom: 2 },
  point: { position: "absolute", minWidth: 72, height: 34, borderRadius: 18, borderWidth: 1, paddingHorizontal: 9, gap: 5, flexDirection: "row", alignItems: "center", justifyContent: "center" },
  dot: { width: 6, height: 6, borderRadius: 3 },
  pointText: { color: C.text, fontSize: 11, fontWeight: "600" },
  selectedText: { color: C.accent },
  pressed: { opacity: 0.78, transform: [{ scale: 0.97 }] },
  disabled: { opacity: 0.6 },
});
