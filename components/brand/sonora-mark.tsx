import { useState } from "react";
import { Image, Platform, StyleSheet, View } from "react-native";
import { Ionicons } from "@expo/vector-icons";
import { GENERATED_ICON_CONCEPTS } from "@/assets/brand-image-refs";

const GENERATED_PREVIEW_ICON = GENERATED_ICON_CONCEPTS[0];
const ACCENT = "#58E6D0";

export function SonoraMark() {
  const [imageFailed, setImageFailed] = useState(false);

  if (Platform.OS === "web" && !imageFailed) {
    return (
      <Image
        source={{ uri: GENERATED_PREVIEW_ICON }}
        resizeMode="cover"
        onError={() => setImageFailed(true)}
        accessibilityLabel="Símbolo Sonora"
        style={styles.image}
      />
    );
  }

  return (
    <View style={styles.fallback}>
      <Ionicons name="radio-outline" size={20} color={ACCENT} />
    </View>
  );
}

const styles = StyleSheet.create({
  image: { width: 38, height: 38, borderRadius: 12, backgroundColor: "#112522" },
  fallback: { width: 38, height: 38, alignItems: "center", justifyContent: "center", backgroundColor: "#112522", borderColor: "#28544D", borderWidth: 1, borderRadius: 12 },
});
