#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include "MPU6050.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

// ======================================================
// CONFIGURACIÓN
// ======================================================
uint8_t receptorMAC[] = {0xA4, 0xF0, 0x0F, 0x8F, 0xE7, 0x68};
const int LED_PIN = 4;
uint8_t canal = 1;   // ✅ mismo canal que el receptor

// MPU6050
const float G_LSB = 16384.0;
const float TH_IMPACTO      = 2.6;
const float TH_CANCEL_MOV   = 2.0;
const float TH_INMOV_DELTA  = 0.25;
const unsigned long TIEMPO_POST = 2200;

// ======================================================
// MENSAJES
// ======================================================
typedef struct {
  bool ledState;
} Message;

Message data;

// Evento caída
typedef struct {
  uint8_t  tipo;
  uint32_t id;
} EventoMsg;

EventoMsg evento;

// ======================================================
MPU6050 mpu;
int16_t ax, ay, az;

bool lastState = false;
bool trigger = false;

uint32_t contadorCaidas = 0;
volatile bool caidaConfirmada = false;

// ======================================================
enum Estado {
  NORMAL,
  IMPACTO,
  POST_IMPACTO
};

volatile Estado estado = NORMAL;
volatile unsigned long tiempoImpacto = 0;

// ======================================================
// RECEPCIÓN (para vibración/LED remoto)
// ======================================================
void onReceive(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {

  if (len == sizeof(Message)) {
    memcpy(&data, incomingData, sizeof(data));

    if (data.ledState == true && lastState == false) {
      trigger = true;
    }

    lastState = data.ledState;
  }
}

// ======================================================
// CALLBACK ENVÍO
// ======================================================
void OnDataSent(const esp_now_send_info_t *info, esp_now_send_status_t status) {
  Serial.print("Estado envio: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "ERROR");
}

// ======================================================
// TASK SENSOR (detección de caída)
// ======================================================
void TaskSensor(void *pvParameters) {
  while (true) {

    mpu.getAcceleration(&ax, &ay, &az);

    float axg = ax / G_LSB;
    float ayg = ay / G_LSB;
    float azg = az / G_LSB;

    float mag = sqrt(axg*axg + ayg*ayg + azg*azg);
    float deltaG = fabs(mag - 1.0);

    switch (estado) {

      case NORMAL:
        if (mag > TH_IMPACTO) {
          estado = IMPACTO;
          tiempoImpacto = millis();
          Serial.println("IMPACTO FUERTE");
        }
        break;

      case IMPACTO:
        estado = POST_IMPACTO;
        break;

      case POST_IMPACTO:

        if (mag > TH_CANCEL_MOV) {
          estado = NORMAL;
          Serial.println("Movimiento descartado");
          break;
        }

        if ((millis() - tiempoImpacto) >= TIEMPO_POST &&
            deltaG < TH_INMOV_DELTA) {

          caidaConfirmada = true;
          estado = NORMAL;
        }
        break;
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ======================================================
// TASK COMUNICACIÓN (envío ESP-NOW)
// ======================================================
void TaskComms(void *pvParameters) {
  while (true) {

    if (caidaConfirmada) {
      caidaConfirmada = false;

      contadorCaidas++;

      evento.tipo = 1;
      evento.id   = contadorCaidas;

      Serial.print("CAIDA CONFIRMADA ID ");
      Serial.println(contadorCaidas);

      esp_now_send(receptorMAC,
                   (uint8_t*)&evento,
                   sizeof(evento));
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ======================================================
// TASK VIBRACIÓN / LED
// ======================================================
void TaskVibracion(void *pvParameters) {
  while (true) {

    if (trigger) {
      trigger = false;

      Serial.println("PULSO RECIBIDO");

      digitalWrite(LED_PIN, HIGH);
      vTaskDelay(pdMS_TO_TICKS(500));
      digitalWrite(LED_PIN, LOW);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ======================================================
// SETUP
// ======================================================
void setup() {

  Serial.begin(115200);
  delay(500);

  Serial.println("=== PULSERA PANCHITA RF PRO ===");

  // I2C
  Wire.begin(8, 9);
  mpu.initialize();

  if (!mpu.testConnection()) {
    Serial.println("MPU ERROR");
    while (true);
  }

  // ================= WIFI PRO =================
  WiFi.mode(WIFI_STA);
  delay(200);

  Serial.print("MAC EMISOR: ");
  Serial.println(WiFi.macAddress());

  // ✅ Ajuste clave para ESP32-C3 MINI
  esp_wifi_set_max_tx_power(34);

  WiFi.disconnect();
  delay(300);

  esp_wifi_set_ps(WIFI_PS_NONE);
  delay(200);

  esp_wifi_set_channel(canal, WIFI_SECOND_CHAN_NONE);
  delay(300);

  // ================= ESP-NOW =================
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW FAIL");
    while (true);
  }

  esp_now_register_recv_cb(onReceive);
  esp_now_register_send_cb(OnDataSent);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, receptorMAC, 6);
  peer.channel = canal;
  peer.encrypt = false;
  peer.ifidx = WIFI_IF_STA;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Error al agregar peer");
    while (true);
  }

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // ================= TASKS =================
  xTaskCreatePinnedToCore(TaskSensor, "Sensor", 4096, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(TaskComms, "Comms", 4096, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(TaskVibracion, "Vibra", 2048, NULL, 2, NULL, 0);

  Serial.println("SISTEMA LISTO");
}

// ======================================================
void loop() {}