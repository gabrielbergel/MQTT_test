#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <NewPing.h>
#include <ArduinoJson.h>

// --- CONFIGURAÇÕES DE REDE E MQTT ---
const char* SSID = "AMF";
const char* PASSWORD = "amf@2025";
const char* MQTT_BROKER = "test.mosquitto.org";
const char* MQTT_TOPIC = "garagem/vaga01/status";

// --- MAPEAMENTO DOS PINOS ---
// Sensores
const int TRIGGER_PIN = 15;
const int ECHO_PIN = 4;
const int SOUND_AO_PIN = 34; // Usaremos o analógico para detectar o ruído do motor

// Atuadores (LEDs)
const int LED_VERDE_PIN = 25;
const int LED_AMARELO_PIN = 33;
const int LED_VERMELHO_PIN = 32;

// --- PARÂMETROS DE LÓGICA ---
const int DISTANCIA_VAGA_OCUPADA = 100; // Distância em cm para considerar a vaga ocupada (ajuste conforme sua garagem)
const int LIMIAR_RUIDO_MOTOR = 2500;   // Nível de ruído analógico para detectar um motor (0-4095, ajuste com testes)
const int MINIMA_VARIACAO_DISTANCIA = 5; // Mínima variação em cm para considerar o carro em movimento

// --- CLIENTES E OBJETOS ---
WiFiClient espClient;
PubSubClient client(espClient);
NewPing sonar(TRIGGER_PIN, ECHO_PIN, 200);
StaticJsonDocument<256> jsonDoc;

// Variáveis globais para a lógica
int distanciaAnterior = 0;
String statusVaga = "Iniciando";

void setup() {
  Serial.begin(115200);

  // Configura os pinos dos LEDs como saída
  pinMode(LED_VERDE_PIN, OUTPUT);
  pinMode(LED_AMARELO_PIN, OUTPUT);
  pinMode(LED_VERMELHO_PIN, OUTPUT);

  // Conexão Wi-Fi
  Serial.print("Conectando à rede Wi-Fi: ");
  Serial.println(SSID);
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWi-Fi conectado!");
  Serial.print("Endereço IP: "); Serial.println(WiFi.localIP());

  client.setServer(MQTT_BROKER, 1883);
}

void loop() {
  if (!client.connected()) {
    // A função reconnect foi removida para simplificar e focar na lógica principal
    // Em um projeto final, ela deveria ser adicionada de volta
  }
  client.loop();

  // --- LÓGICA PRINCIPAL DO MONITOR DE VAGA ---
  int distanciaAtual = sonar.ping_cm();
  if (distanciaAtual == 0) distanciaAtual = 200; // Se a leitura falhar, considera como livre
  
  int nivelDeRuido = analogRead(SOUND_AO_PIN);
  int variacaoDistancia = abs(distanciaAtual - distanciaAnterior);

  // 1. LÓGICA DE ESTADO: VAGA LIVRE
  if (distanciaAtual > DISTANCIA_VAGA_OCUPADA) {
    statusVaga = "LIVRE";
    digitalWrite(LED_VERDE_PIN, HIGH);
    digitalWrite(LED_AMARELO_PIN, LOW);
    digitalWrite(LED_VERMELHO_PIN, LOW);
  }
  // 2. LÓGICA DE ESTADO: VAGA COM CARRO (OCUPADA OU LIBERANDO)
  else {
    // Condição para "LIBERANDO": carro está presente, há ruído de motor E o carro está se movendo
    if (nivelDeRuido > LIMIAR_RUIDO_MOTOR && variacaoDistancia > MINIMA_VARIACAO_DISTANCIA) {
      statusVaga = "LIBERANDO";
      digitalWrite(LED_VERDE_PIN, LOW);
      digitalWrite(LED_AMARELO_PIN, HIGH); // LED Amarelo aceso
      digitalWrite(LED_VERMELHO_PIN, LOW);
    }
    // Condição para "OCUPADA": carro está presente, mas parado e/ou em silêncio
    else {
      statusVaga = "OCUPADA";
      digitalWrite(LED_VERDE_PIN, LOW);
      digitalWrite(LED_AMARELO_PIN, LOW);
      digitalWrite(LED_VERMELHO_PIN, HIGH);
    }
  }

  // Atualiza a distância anterior para a próxima leitura
  distanciaAnterior = distanciaAtual;

  // --- PUBLICAÇÃO MQTT (a cada 5 segundos) ---
  static unsigned long lastPublishTime = 0;
  if (millis() - lastPublishTime > 5000) {
    lastPublishTime = millis();

    jsonDoc.clear();
    jsonDoc["vagaId"] = "Vaga01";
    jsonDoc["status"] = statusVaga;
    jsonDoc["distancia_cm"] = distanciaAtual;
    jsonDoc["nivel_ruido_raw"] = nivelDeRuido;

    char jsonBuffer[256];
    serializeJson(jsonDoc, jsonBuffer);
    client.publish(MQTT_TOPIC, jsonBuffer);

    Serial.printf("Status: %s | Dist: %d cm | Ruído: %d | JSON: %s\n", statusVaga.c_str(), distanciaAtual, nivelDeRuido, jsonBuffer);
  }

  delay(200); // Pequeno delay para estabilizar as leituras
}