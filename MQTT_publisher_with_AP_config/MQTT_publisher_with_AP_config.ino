#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#define BOOTLOADER_BUTTON 0

WebServer server(80);

// Update these with values suitable for your network.

const char* ssid_ap = "ESP32-Config";
const char* password_ap = "12345678";

// MQTT - HiveMQ Cloud
const char* mqtt_broker   = "92c9c7c9edff45999b7f0dcca22eefb1.s1.eu.hivemq.cloud";
const int   mqtt_port     = 8883;
const char* mqtt_username = "AllSpark";
const char* mqtt_password = "AllSpark2000";
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

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "esp32-" + WiFi.macAddress().substring(12);
    if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("MQTT: conectado");
      Serial.println("MQTT: inscrito em " + String(topic));
    } else {
      Serial.print("MQTT fail, rc=");
      Serial.println(client.state());
    }
  }
}

void setup() {
  pinMode(BOOTLOADER_BUTTON, INPUT);
  Serial.begin(115200);
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
    reconnect();//connectMQTT();
  }
}

unsigned long lastMqttAttempt = 0;

bool lastButtonState = LOW;
bool currentState = LOW;
bool valueToSend = false;

void loop() {
  server.handleClient();

  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected() && millis() - lastMqttAttempt > 5000) {
      lastMqttAttempt = millis();
      reconnect();
    }
    client.loop();
  }

  currentState = digitalRead(BOOTLOADER_BUTTON);
  
  // Detecta transição de LOW para HIGH (borda de subida)
  if (currentState == HIGH && lastButtonState == LOW) {
    valueToSend = !valueToSend;
    bool pub = client.publish(topic, valueToSend ? "1" : "0");
    Serial.println("Publish: " + String(pub));
    Serial.println("Botão pressionado, valor enviado: " + String(valueToSend));
    delay(200); // debounce simples
  }

  lastButtonState = currentState;
}
