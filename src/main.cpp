#include <Arduino.h>

#include <esp_system.h>
#include <esp_partition.h>

#if __has_include(<esp_chip_info.h>)
#include <esp_chip_info.h>
#endif
#if __has_include(<esp_mac.h>)
#include <esp_mac.h>
#endif
#if __has_include(<esp_flash.h>)
#include <esp_flash.h>
#endif
#include <soc/soc_caps.h>

static void linha() {
  Serial.println("--------------------------------------------------");
}

static void titulo(const char *texto) {
  Serial.println();
  linha();
  Serial.printf("  %s\n", texto);
  linha();
}

// Imprime bytes como KB / MB, que e como datasheet e loja anunciam.
static void tamanho(const char *rotulo, uint32_t bytes) {
  Serial.printf("%-22s %u bytes (%.2f MB / %u KB)\n", rotulo, bytes,
                bytes / (1024.0 * 1024.0), bytes / 1024);
}

static void mac(const char *rotulo, esp_mac_type_t tipo) {
  uint8_t m[8] = {0};
  if (esp_read_mac(m, tipo) != ESP_OK) {
    Serial.printf("%-22s (indisponivel neste chip)\n", rotulo);
    return;
  }
  Serial.printf("%-22s %02X:%02X:%02X:%02X:%02X:%02X\n", rotulo, m[0], m[1],
                m[2], m[3], m[4], m[5]);
}

static const char *motivoReset(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:  return "energia ligada";
    case ESP_RST_EXT:      return "pino de reset externo";
    case ESP_RST_SW:       return "reset por software";
    case ESP_RST_PANIC:    return "panic / excecao";
    case ESP_RST_INT_WDT:  return "watchdog de interrupcao";
    case ESP_RST_TASK_WDT: return "watchdog de tarefa";
    case ESP_RST_WDT:      return "outro watchdog";
    case ESP_RST_DEEPSLEEP: return "acordou de deep sleep";
    case ESP_RST_BROWNOUT: return "brownout (queda de tensao)";
    case ESP_RST_SDIO:     return "reset via SDIO";
    default:               return "desconhecido";
  }
}

static const char *modoFlash(FlashMode_t m) {
  switch (m) {
    case FM_QIO:  return "QIO";
    case FM_QOUT: return "QOUT";
    case FM_DIO:  return "DIO";
    case FM_DOUT: return "DOUT";
    case FM_FAST_READ: return "Fast Read";
    case FM_SLOW_READ: return "Slow Read";
    default: return "desconhecido";
  }
}

static void identidade() {
  titulo("IDENTIDADE DO CHIP");

  esp_chip_info_t info;
  esp_chip_info(&info);

  Serial.printf("%-22s %s\n", "Modelo:", ESP.getChipModel());
  Serial.printf("%-22s %d\n", "Revisao do silicio:", ESP.getChipRevision());
  Serial.printf("%-22s %d\n", "Nucleos (cores):", info.cores);
  Serial.printf("%-22s %s\n", "Alvo de compilacao:", CONFIG_IDF_TARGET);

  uint64_t id = ESP.getEfuseMac();
  Serial.printf("%-22s %04X%08X\n", "ID unico (eFuse MAC):",
                (uint16_t)(id >> 32), (uint32_t)id);
}

static void radios() {
  titulo("RADIOS E RECURSOS");

  esp_chip_info_t info;
  esp_chip_info(&info);

  Serial.printf("%-22s %s\n", "Wi-Fi 2.4 GHz b/g/n:",
                (info.features & CHIP_FEATURE_WIFI_BGN) ? "sim" : "nao");
  Serial.printf("%-22s %s\n", "Bluetooth Classico:",
                (info.features & CHIP_FEATURE_BT) ? "sim" : "nao");
  Serial.printf("%-22s %s\n", "Bluetooth LE:",
                (info.features & CHIP_FEATURE_BLE) ? "sim" : "nao");
#ifdef CHIP_FEATURE_IEEE802154
  Serial.printf("%-22s %s\n", "802.15.4 (Zigbee):",
                (info.features & CHIP_FEATURE_IEEE802154) ? "sim" : "nao");
#endif
  Serial.printf("%-22s %s\n", "Flash embutida no SoC:",
                (info.features & CHIP_FEATURE_EMB_FLASH) ? "sim" : "nao (externa)");
  Serial.printf("%-22s %s\n", "PSRAM embutida no SoC:",
                (info.features & CHIP_FEATURE_EMB_PSRAM) ? "sim" : "nao");
}

static void memoria() {
  titulo("MEMORIA");

  Serial.printf("%-22s %u MHz\n", "Clock da CPU:", ESP.getCpuFreqMHz());
  Serial.println();

  tamanho("Flash total:", ESP.getFlashChipSize());
  Serial.printf("%-22s %u MHz\n", "Velocidade da flash:",
                ESP.getFlashChipSpeed() / 1000000);
  Serial.printf("%-22s %s\n", "Modo da flash:", modoFlash(ESP.getFlashChipMode()));
#if __has_include(<esp_flash.h>)
  uint32_t jedec = 0;
  if (esp_flash_read_id(NULL, &jedec) == ESP_OK) {
    // Byte alto = fabricante (EF=Winbond, C8=GigaDevice, 20=XMC/Micron...).
    Serial.printf("%-22s 0x%06X (fabricante 0x%02X)\n", "JEDEC ID da flash:",
                  jedec, (jedec >> 16) & 0xFF);
  }
#endif

  Serial.println();
  tamanho("Programa ocupa:", ESP.getSketchSize());
  tamanho("Espaco p/ programa:", ESP.getFreeSketchSpace());

  Serial.println();
  tamanho("Heap (RAM) total:", ESP.getHeapSize());
  tamanho("Heap livre agora:", ESP.getFreeHeap());
  tamanho("Menor heap ja visto:", ESP.getMinFreeHeap());
  tamanho("Maior bloco livre:", ESP.getMaxAllocHeap());

  Serial.println();
  if (psramFound() && ESP.getPsramSize() > 0) {
    tamanho("PSRAM total:", ESP.getPsramSize());
    tamanho("PSRAM livre:", ESP.getFreePsram());
  } else {
    Serial.printf("%-22s nenhuma detectada\n", "PSRAM:");
    Serial.println("   (se o modulo tem PSRAM, habilite com");
    Serial.println("    build_flags = -DBOARD_HAS_PSRAM no platformio.ini)");
  }
}

static void perifericos() {
  titulo("PERIFERICOS DISPONIVEIS NO SOC");

  Serial.printf("%-22s %d\n", "Pinos GPIO:", SOC_GPIO_PIN_COUNT);
#ifdef SOC_ADC_PERIPH_NUM
  Serial.printf("%-22s %d\n", "Blocos de ADC:", SOC_ADC_PERIPH_NUM);
#endif
#ifdef SOC_DAC_CHAN_NUM
  Serial.printf("%-22s %d\n", "Canais de DAC:", SOC_DAC_CHAN_NUM);
#endif
#ifdef SOC_UART_NUM
  Serial.printf("%-22s %d\n", "UARTs:", SOC_UART_NUM);
#endif
#ifdef SOC_I2C_NUM
  Serial.printf("%-22s %d\n", "I2C:", SOC_I2C_NUM);
#endif
#ifdef SOC_SPI_PERIPH_NUM
  Serial.printf("%-22s %d\n", "SPI:", SOC_SPI_PERIPH_NUM);
#endif
#ifdef SOC_I2S_NUM
  Serial.printf("%-22s %d\n", "I2S:", SOC_I2S_NUM);
#endif
#ifdef SOC_LEDC_CHANNEL_NUM
  Serial.printf("%-22s %d\n", "Canais PWM (LEDC):", SOC_LEDC_CHANNEL_NUM);
#endif
#ifdef SOC_MCPWM_GROUPS
  Serial.printf("%-22s %d\n", "Grupos MCPWM:", SOC_MCPWM_GROUPS);
#endif
#ifdef SOC_TOUCH_SENSOR_NUM
  Serial.printf("%-22s %d\n", "Sensores touch:", SOC_TOUCH_SENSOR_NUM);
#endif
#ifdef SOC_TWAI_CONTROLLER_NUM
  Serial.printf("%-22s %d\n", "CAN (TWAI):", SOC_TWAI_CONTROLLER_NUM);
#endif
#ifdef SOC_SDMMC_NUM_SLOTS
  Serial.printf("%-22s %d\n", "Slots SD/MMC:", SOC_SDMMC_NUM_SLOTS);
#endif
#ifdef SOC_TEMP_SENSOR_SUPPORTED
  Serial.printf("%-22s %s\n", "Sensor temp. interno:",
                SOC_TEMP_SENSOR_SUPPORTED ? "sim" : "nao");
#endif
#ifdef SOC_USB_OTG_SUPPORTED
  Serial.printf("%-22s %s\n", "USB OTG nativo:",
                SOC_USB_OTG_SUPPORTED ? "sim" : "nao");
#endif
}

static void enderecos() {
  titulo("ENDERECOS MAC");

  mac("Wi-Fi station:", ESP_MAC_WIFI_STA);
  mac("Wi-Fi access point:", ESP_MAC_WIFI_SOFTAP);
  mac("Bluetooth:", ESP_MAC_BT);
  mac("Ethernet:", ESP_MAC_ETH);
}

static void software() {
  titulo("SOFTWARE E PARTICOES");

  Serial.printf("%-22s %s\n", "Versao do ESP-IDF:", ESP.getSdkVersion());
#ifdef ESP_ARDUINO_VERSION_MAJOR
  Serial.printf("%-22s %d.%d.%d\n", "Core Arduino-ESP32:",
                ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR,
                ESP_ARDUINO_VERSION_PATCH);
#endif
  Serial.printf("%-22s %s %s\n", "Compilado em:", __DATE__, __TIME__);
  Serial.printf("%-22s %s\n", "Motivo do boot:", motivoReset(esp_reset_reason()));

  Serial.println();
  Serial.println("Tabela de particoes da flash:");
  Serial.printf("  %-18s %-10s %-12s %-10s\n", "NOME", "TIPO", "ENDERECO", "TAMANHO");

  esp_partition_iterator_t it = esp_partition_find(
      ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
  while (it != NULL) {
    const esp_partition_t *p = esp_partition_get(it);
    Serial.printf("  %-18s %-10s 0x%08X   %u KB\n", p->label,
                  p->type == ESP_PARTITION_TYPE_APP ? "app" : "data",
                  p->address, p->size / 1024);
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);
}

void setup() {
  Serial.begin(115200);
  delay(1500);  // da tempo ao monitor serial de abrir antes do despejo

  Serial.println();
  Serial.println();
  linha();
  Serial.println("  RELATORIO COMPLETO DO ESP32");
  linha();

  identidade();
  radios();
  memoria();
  perifericos();
  enderecos();
  software();

  Serial.println();
  linha();
  Serial.println("  FIM DO RELATORIO");
  linha();
}

void loop() {
  // Nada a fazer: o relatorio e impresso uma vez no boot.
  delay(10000);
}
