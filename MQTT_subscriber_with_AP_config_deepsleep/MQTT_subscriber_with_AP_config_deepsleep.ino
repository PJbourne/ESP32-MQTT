#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "driver/rtc_io.h"


#define LED_GPIO 2

WebServer server(80);

const char* ssid_ap = "ESP32-Config";
const char* password_ap = "12345678";

// MQTT - HiveMQ Cloud
const char* mqtt_broker   = "92c9c7c9edff45999b7f0dcca22eefb1.s1.eu.hivemq.cloud";
const int   mqtt_port     = 8883;
const char* mqtt_username = "Allspark";
const char* mqtt_password = "Allspark2000";
const char* topic         = "emqx/esp32";

WiFiClientSecure espClient;
PubSubClient client(espClient);

struct WiFiConfig {
  String ssid;
  String password;
};
WiFiConfig wifiConfig;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Configurar WiFi ESP32</title>
</head>
<body style="font-family: Arial; text-align: center;">
  <h1>Configurar Wi-Fi</h1>
  <form action="/save" method="POST">
    <input type="text" name="ssid" placeholder="SSID" required><br><br>
    <input type="password" name="password" placeholder="Senha"><br><br>
    <button type="submit">Salvar e Conectar</button>
  </form>
  <br>
  <form action="/reset" method="POST">
    <button type="submit">Resetar Configurações</button>
  </form>
</body>
</html>
)rawliteral";

void loadWiFiConfig() {
  if (!SPIFFS.exists("/config2.json")) return;
  File file = SPIFFS.open("/config2.json", "r");
  if (!file) return;
  DynamicJsonDocument doc(256);
  deserializeJson(doc, file);
  wifiConfig.ssid     = doc["ssid"].as<String>();
  wifiConfig.password = doc["password"].as<String>();
  file.close();
}

void saveWiFiConfig(const String& ssid, const String& password) {
  DynamicJsonDocument doc(256);
  doc["ssid"]     = ssid;
  doc["password"] = password;
  File file = SPIFFS.open("/config2.json", "w");
  if (!file) return;
  serializeJson(doc, file);
  file.close();
}

void resetWiFiConfig() {
  SPIFFS.remove("/config2.json");
}

void startAccessPoint() {
  WiFi.softAP(ssid_ap, password_ap);
  Serial.println("Modo AP ativo. Acesse http://192.168.4.1");
}

void connectToWiFi() {
  WiFi.begin(wifiConfig.ssid.c_str(), wifiConfig.password.c_str());
  Serial.print("Conectando ao Wi-Fi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    Serial.print('.');
    delay(500);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi conectado. IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nFalha na conexão. Entrando em AP.");
    startAccessPoint();
  }
}

void setupWebServer() {
  server.on("/", HTTP_GET, [](){
    server.send_P(200, "text/html", index_html);
  });
  server.on("/save", HTTP_POST, [](){
    if (server.hasArg("ssid") && server.hasArg("password")) {
      saveWiFiConfig(server.arg("ssid"), server.arg("password"));
      server.send(200, "text/html", "<h1>Salvo! Reiniciando...</h1>");
      delay(1500);
      ESP.restart();
    } else {
      server.send(400, "text/html", "<h1>Erro: campos inválidos</h1>");
    }
  });
  server.on("/reset", HTTP_POST, [](){
    resetWiFiConfig();
    server.send(200, "text/html", "<h1>Resetado! Reiniciando...</h1>");
    delay(1500);
    ESP.restart();
  });
  server.begin();
  Serial.println("WebServer iniciado em /");
}
/*
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  String msg;
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  Serial.printf("MQTT receb.: %s\n", msg.c_str());
  if (msg == "1") {
    digitalWrite(LED_GPIO, HIGH);
    Serial.println("LED ON");
  } else if (msg == "0") {
    digitalWrite(LED_GPIO, LOW);
    Serial.println("LED OFF");
  }
}
*/

bool status;      // variável que você já usa para decidir entrar em Deep Sleep
int intervalo;    // intervalo em segundos para o Wake-up por timer
// -----------------------------------------------------------------------------

void mqttCallback(char* topic, byte* payload, unsigned int len) {
  // 1) Converte o payload em String
  String msg;
  for (unsigned int i = 0; i < len; i++) {
    msg += (char)payload[i];
  }
  Serial.printf("MQTT receb.: %s\n", msg.c_str());

  // 2) Esperamos a mensagem no formato "X,Y", 
  //    onde X será 0 ou 1 (status) e Y é o intervalo em segundos.
  int sep = msg.indexOf(',');  
  if (sep < 0) {
    Serial.println("Formato inválido. Use: \"<status>,<intervalo>\".");
    //return;
    intervalo = 0;
    status = (msg.toInt() == 1);
  }else{
    // 3) Extrai cada parte em String e converte para inteiro
    String parteStatus    = msg.substring(0, sep);
    String parteIntervalo = msg.substring(sep + 1);

    int novoStatus    = parteStatus.toInt();    // converte "0"/"1" para int
    int novoIntervalo = parteIntervalo.toInt(); // converte "60" para int

    // 4) Atualiza as variáveis globais
    status    = (novoStatus == 1);
    intervalo = novoIntervalo;

    Serial.printf("Ajustado: status = %d, intervalo = %d s\n", status, intervalo);
  }
  rtc_gpio_hold_dis(GPIO_NUM_2);
  // 5) Acende/apaga LED conforme o comando (opcional)
  if (status) {
    digitalWrite(LED_GPIO, HIGH);
    Serial.println("LED ON");
  } else {
    digitalWrite(LED_GPIO, LOW);
    Serial.println("LED OFF");
  }
}

void connectMQTT() {
  if (client.connected()) return;
  String clientId = "esp32-" + WiFi.macAddress().substring(12);
  if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
    Serial.println("MQTT: conectado");
    client.subscribe(topic);
    Serial.println("MQTT: inscrito em " + String(topic));
  } else {
    Serial.print("MQTT fail, rc=");
    Serial.println(client.state());
  }
}

void setup() {
  pinMode(LED_GPIO, OUTPUT);
  Serial.begin(115200);
  
  //rtc_gpio_hold_dis(GPIO_NUM_2);
  printWakeupReason();

  if (!SPIFFS.begin(true)) {
    Serial.println("Erro ao iniciar SPIFFS"); 
    return;
  }

  loadWiFiConfig();
  if (wifiConfig.ssid.length()) {
    connectToWiFi();
  } else {
    startAccessPoint();
  }
  setupWebServer();

  if (WiFi.status() == WL_CONNECTED) {
    espClient.setInsecure();         // permite TLS sem CA
    client.setServer(mqtt_broker, mqtt_port);
    client.setCallback(mqttCallback);
    connectMQTT();
  }
}

unsigned long lastMqttAttempt = 0;

void enterDeepSleep() {
  Serial.println("=========== Entrando em Deep Sleep ===========");
  rtc_gpio_hold_en(GPIO_NUM_2);
  // 1) Wake-up por timer: intervalo (em microssegundos)
  uint64_t uS = (uint64_t)intervalo * 1000000ULL;
  esp_sleep_enable_timer_wakeup(uS);
  Serial.printf("Wake-up configurado para daqui a %d segundos (%llu us)\n", intervalo, uS);

  // 2) Wake-up por botão (GPIO0 nível LOW)
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);

  delay(100);  // garante que as últimas mensagens do Serial sejam impressas
  esp_deep_sleep_start();
  // Depois daqui o ESP “desliga tudo” e só volta quando o timer expira ou o botão for pressionado.
}

void printWakeupReason() {
  //rtc_gpio_hold_dis(GPIO_NUM_2);
  esp_sleep_wakeup_cause_t causa = esp_sleep_get_wakeup_cause();
  switch (causa) {
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("[Wake-up] Timer expirou.");
      break;
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("[Wake-up] Botão (GPIO0) pressionado.");
      break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      Serial.println("[Wake-up] Boot normal (não era Deep Sleep).");
      rtc_gpio_hold_dis(GPIO_NUM_2);
      break;
    default:
      Serial.printf("[Wake-up] Outra causa: %d\n", (int)causa);
      break;
  }
}

void loop() {
  server.handleClient();

  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected() && millis() - lastMqttAttempt > 5000) {
      lastMqttAttempt = millis();
      connectMQTT();
    }
    client.loop();
  }

  if (intervalo > 0) {
    enterDeepSleep();
  }
}
