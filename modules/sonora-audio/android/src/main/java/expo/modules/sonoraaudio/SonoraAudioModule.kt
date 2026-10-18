package expo.modules.sonoraaudio

import android.content.Context
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Build
import expo.modules.kotlin.modules.Module
import expo.modules.kotlin.modules.ModuleDefinition
import java.util.Locale
import java.util.concurrent.TimeUnit

private const val MODULE_DIR = "/data/adb/modules/sonora_spatial_audio"
private const val CONTROL_BIN = "$MODULE_DIR/bin/sonora-ctl"
private const val PREFS_NAME = "sonora.root.v1"
private const val ROOT_APPROVED_KEY = "root_approved"
private const val EQ_BANDS = 10

class SonoraAudioModule : Module() {
  override fun definition() = ModuleDefinition {
    Name("SonoraAudioControl")

    AsyncFunction("getStatus") {
      getStatus()
    }

    AsyncFunction("requestRoot") {
      requestRoot()
    }

    AsyncFunction("applySettings") { input: Map<String, Any?> ->
      applySettings(input)
    }
  }

  private val context: Context
    get() = requireNotNull(appContext.reactContext)

  private fun preferences() = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

  private data class CommandResult(val exitCode: Int, val output: String)

  private fun runRoot(command: String, timeoutMillis: Long = 10_000): CommandResult {
    return try {
      val process = ProcessBuilder("su", "-c", command)
        .redirectErrorStream(true)
        .start()
      val completed = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
        process.waitFor(timeoutMillis, TimeUnit.MILLISECONDS)
      } else {
        process.waitFor()
        true
      }
      if (!completed) {
        process.destroyForcibly()
        return CommandResult(-1, "Tempo limite ao aguardar resposta do su.")
      }
      val output = process.inputStream.bufferedReader().use { it.readText() }
      CommandResult(process.exitValue(), output.trim())
    } catch (error: Exception) {
      CommandResult(-1, error.message ?: "Falha ao executar su")
    }
  }

  private fun requestRoot(): Map<String, Any> {
    val result = runRoot("id -u", 60_000)
    val granted = result.exitCode == 0 && result.output.lineSequence().any { it.trim() == "0" }
    preferences().edit().putBoolean(ROOT_APPROVED_KEY, granted).apply()
    return mapOf(
      "ok" to granted,
      "detail" to if (granted) "O comando su retornou UID 0; confirme o provedor root no gerenciador instalado." else "Root não concedido. Aprove o acesso no KernelSU Manager e tente novamente."
    )
  }

  private fun getStatus(): Map<String, Any?> {
    val route = getOutputRoute()
    if (!preferences().getBoolean(ROOT_APPROVED_KEY, false)) {
      return statusMap(false, false, false, route, "Toque em CONECTAR ROOT para solicitar acesso pelo KernelSU Manager.")
    }

    val command = """
      if [ "$(id -u)" != "0" ]; then echo ROOT=0; exit 1; fi
      echo ROOT=1
      if [ -f '$MODULE_DIR/module.prop' ] && grep -q '^id=sonora_spatial_audio$' '$MODULE_DIR/module.prop' && [ ! -f '$MODULE_DIR/disable' ]; then echo MODULE=1; else echo MODULE=0; fi
      if [ -x '$CONTROL_BIN' ] && '$CONTROL_BIN' status 2>/dev/null | grep -q '^CONTROL=1$'; then echo CONTROL=1; else echo CONTROL=0; fi
      if dumpsys media.audio_flinger 2>/dev/null | grep -qi 'Sonora Spatial'; then echo EFFECT=1; else echo EFFECT=0; fi
    """.trimIndent()

    val result = runRoot(command)
    val rootOk = result.exitCode == 0 && result.output.contains("ROOT=1")
    if (!rootOk) {
      preferences().edit().putBoolean(ROOT_APPROVED_KEY, false).apply()
      return statusMap(false, false, false, route, "O acesso root não está disponível. Refaça a autorização no KernelSU Manager.")
    }

    val moduleInstalled = result.output.contains("MODULE=1")
    val controlReady = result.output.contains("CONTROL=1")
    val effectListed = moduleInstalled && controlReady && result.output.contains("EFFECT=1")
    val detail = when {
      !moduleInstalled -> "Root ativo; o módulo Sonora não foi detectado como ativo no diretório KernelSU."
      !controlReady -> "Módulo detectado, mas helper ou estado compartilhado não está pronto. Nenhum áudio foi alterado."
      !effectListed -> "Módulo e helper detectados; o nome do efeito ainda não aparece no AudioFlinger. Inicie uma reprodução e atualize o estado."
      else -> "O nome Sonora aparece no AudioFlinger; isso não confirma que o callback processa áudio ou que a mudança esteja audível."
    }
    return statusMap(true, moduleInstalled, effectListed, route, detail)
  }

  private fun applySettings(input: Map<String, Any?>): Map<String, Any> {
    if (!preferences().getBoolean(ROOT_APPROVED_KEY, false)) {
      return mapOf("ok" to false, "detail" to "Conecte o acesso root pelo botão antes de enviar parâmetros.")
    }

    val position = input["position"] as? String
    val allowedPositions = setOf("center", "front", "back", "right", "left")
    if (position !in allowedPositions) {
      return mapOf("ok" to false, "detail" to "Posição inválida.")
    }
    val enabled = input["enabled"] as? Boolean ?: false
    val preampDb = (input["preampDb"] as? Number)?.toDouble() ?: return mapOf("ok" to false, "detail" to "Pré-amplificação inválida.")
    if (!preampDb.isFinite() || preampDb !in -12.0..6.0) {
      return mapOf("ok" to false, "detail" to "Pré-amplificação fora do limite de −12 a +6 dB.")
    }
    val gains = (input["eqGainsDb"] as? List<*>)
      ?.map { (it as? Number)?.toDouble() ?: Double.NaN }
      ?: return mapOf("ok" to false, "detail" to "Equalização inválida.")
    if (gains.size != EQ_BANDS || gains.any { !it.isFinite() || it !in -12.0..12.0 }) {
      return mapOf("ok" to false, "detail" to "A EQ deve conter 10 bandas entre −12 e +12 dB.")
    }

    val enabledArg = if (enabled) "1" else "0"
    val preampArg = String.format(Locale.US, "%.1f", preampDb)
    val eqArg = gains.joinToString(",") { String.format(Locale.US, "%.1f", it) }
    val command = "if [ -x '$CONTROL_BIN' ]; then '$CONTROL_BIN' apply $enabledArg $position $preampArg '$eqArg'; else echo CONTROL_HELPER_MISSING; exit 127; fi"
    val result = runRoot(command)
    val success = result.exitCode == 0 && !result.output.contains("CONTROL_HELPER_MISSING")
    return mapOf(
      "ok" to success,
      "detail" to if (success) "Parâmetros encaminhados ao módulo Sonora." else "Falha ao encaminhar parâmetros: ${result.output.ifBlank { "helper ausente" }}"
    )
  }

  private fun statusMap(
    rootAvailable: Boolean,
    kernelModuleInstalled: Boolean,
    effectListed: Boolean,
    outputRoute: String?,
    detail: String
  ): Map<String, Any?> = mapOf(
    "platform" to "android",
    "rootAvailable" to rootAvailable,
    "kernelModuleInstalled" to kernelModuleInstalled,
    "effectListed" to effectListed,
    "outputRoute" to outputRoute,
    "detail" to detail,
    "checkedAt" to System.currentTimeMillis()
  )

  private fun getOutputRoute(): String? {
    return try {
      val manager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
      val outputs = manager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
      outputs.map { deviceLabel(it) }.distinct().take(3).joinToString(" · ").takeIf { it.isNotBlank() }
    } catch (_: Exception) {
      null
    }
  }

  private fun deviceLabel(device: AudioDeviceInfo): String = when (device.type) {
    AudioDeviceInfo.TYPE_BUILTIN_SPEAKER -> "Alto-falante do aparelho"
    AudioDeviceInfo.TYPE_WIRED_HEADPHONES, AudioDeviceInfo.TYPE_WIRED_HEADSET -> "Fone com fio"
    AudioDeviceInfo.TYPE_BLUETOOTH_A2DP -> "Bluetooth A2DP"
    AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> "Bluetooth SCO"
    AudioDeviceInfo.TYPE_USB_HEADSET -> "Fone USB"
    else -> if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
      when (device.type) {
        AudioDeviceInfo.TYPE_BLE_HEADSET -> "Fone Bluetooth LE"
        AudioDeviceInfo.TYPE_BLE_SPEAKER -> "Alto-falante Bluetooth LE"
        else -> "Saída Android (${device.type})"
      }
    } else {
      "Saída Android (${device.type})"
    }
  }
}
