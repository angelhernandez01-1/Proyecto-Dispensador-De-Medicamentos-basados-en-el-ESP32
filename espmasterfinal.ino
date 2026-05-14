/**************************************************
 * ESP32 MASTER FINAL COMPLETO + RX ESP-NOW
 **************************************************/

#define BLYNK_PRINT Serial
#define BLYNK_TEMPLATE_ID   "TMPL2tXGhUqVD"
#define BLYNK_TEMPLATE_NAME "PANCHITA"
#define BLYNK_AUTH_TOKEN    "VJcpfl9DNsEf7i49F4MX3DQpIDeFSRpF"

// WIFI
char ssid[] = "IEST_2.4G";
char pass[] = "D3lfine$";

// LIBRERÍAS
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_now.h>
#include <BlynkSimpleEsp32.h>
#include <RTClib.h>
#include <Wire.h>
#include <ESP32Servo.h>

// ===== MENSAJES =====
String mensajeSistema = "";
void enviarMensaje(String msg){
  mensajeSistema = msg;
  Serial.println(msg);
  Blynk.virtualWrite(V16, mensajeSistema);
}

// ===== RTC =====
RTC_DS3231 rtc;
bool rtcSincronizado = false;

// ===== ESP-NOW ENVÍO =====
uint8_t receiverMAC[] = {0x08, 0x92, 0x72, 0x25, 0x5A, 0xD4};

typedef struct {
  bool ledState;
} AlarmMessage;

AlarmMessage alarmData;

// ===== ESP-NOW RECEPCIÓN =====
typedef struct {
  uint8_t tipo;  // 1 = CAIDA
  uint32_t id;
} EventoMsg;

EventoMsg recepcion;

// CALLBACK RECEPCIÓN
void OnDataRecv(const esp_now_recv_info_t* info,
                const uint8_t* data,
                int len) {

  if (len != sizeof(recepcion)) return;
  memcpy(&recepcion, data, sizeof(recepcion));

  Serial.println("\n=== EVENTO RECIBIDO ===");

  if (recepcion.tipo == 1) {
    String alerta = "CAIDA DETECTADA ID: " + String(recepcion.id);
    Serial.println(alerta);
enviarMensaje(alerta);
// 🔔 PUSH REAL
Blynk.logEvent("caida", "🚨 CAIDA DETECTADA ID: " + String(recepcion.id));

  }
}

// ===== ALARMAS =====
int a1Hora=-1,a1Min=-1;
int a2Hora=-1,a2Min=-1;
int a3Hora=-1,a3Min=-1;

int b1Hora=-1,b1Min=-1;
int b2Hora=-1,b2Min=-1;
int b3Hora=-1,b3Min=-1;

int hora1=-1,hora2=-1,hora3=-1;
int hora4=-1,hora5=-1,hora6=-1;

int ultimoMinuto=-1;

int lastTrigger_h1=-1,lastTrigger_h2=-1,lastTrigger_h3=-1;
int lastTrigger_h4=-1,lastTrigger_h5=-1,lastTrigger_h6=-1;

// CONTROL
bool alarmaActiva=false;
bool activarManual=false;
bool activarManual2=false;

// SERVOS
Servo servo1;
Servo servo2;
const int PIN_SERVO1=18;
const int PIN_SERVO2=19;

// IR
const int IR1=15;
const int IR2=4;

// ESTADOS
bool dispensando1=false;
bool girando1=false;
unsigned long tiempo1=0;

bool dispensando2=false;
bool girando2=false;
unsigned long tiempo2=0;

const unsigned long DURACION_GIRO=172;
const unsigned long PAUSA=2000;

// IR ESTABLE
unsigned long tiempoDeteccionIR = 0;

// VIBRACIÓN
bool vibrando=false;
bool detectoPastilla=false;
unsigned long ultimoPulso=0;
const unsigned long INTERVALO=800;

// WATCHDOG
unsigned long ultimoBlynkOK=0;
const unsigned long TIMEOUT=10000;

// =================================================
void setupRTC(){
  Wire.begin(21,22);
  if(!rtc.begin()){
    Serial.println("RTC NO DETECTADO");
    while(true);
  }
}

void sincronizarRTC(){
  if(rtcSincronizado) return;
  if(WiFi.status()!=WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  https.begin(client,"https://timeapi.io/api/Time/current/zone?timeZone=America/Mexico_City");

  if(https.GET()==HTTP_CODE_OK){
    StaticJsonDocument<512> doc;
    deserializeJson(doc,https.getString());

    rtc.adjust(DateTime(
      doc["year"],doc["month"],doc["day"],
      doc["hour"],doc["minute"],doc["seconds"]
    ));

    rtcSincronizado=true;
  }
  https.end();
}

// =================================================
void onSent(const wifi_tx_info_t*,esp_now_send_status_t status){
  if(status==ESP_NOW_SEND_SUCCESS) Serial.println("ESP-NOW OK");
}

void enviarPulso(){
  alarmData.ledState=true;
  esp_now_send(receiverMAC,(uint8_t*)&alarmData,sizeof(alarmData));
  delay(50);
  alarmData.ledState=false;
  esp_now_send(receiverMAC,(uint8_t*)&alarmData,sizeof(alarmData));
}

// =================================================
bool IR_detectado(){
  return (digitalRead(IR1)==LOW || digitalRead(IR2)==LOW);
}

// =================================================
void dispensar1(){
  if(IR_detectado()) return;

  enviarMensaje("Dispensando SERVO 1");

  detectoPastilla = false;
  tiempoDeteccionIR = 0;

  dispensando1=true;
  girando1=true;
  servo1.writeMicroseconds(1350);
  tiempo1=millis();
}

void dispensar2(){
  if(IR_detectado()) return;

  enviarMensaje("Dispensando SERVO 2");

  detectoPastilla = false;
  tiempoDeteccionIR = 0;

  dispensando2=true;
  girando2=true;
  servo2.writeMicroseconds(1350);
  tiempo2=millis();
}

// =================================================
void activarAlarma(){
  enviarMensaje("Alarma activada");

  vibrando=true;
  detectoPastilla=false;
  ultimoPulso=0;
  tiempoDeteccionIR=0;

  enviarPulso();
  alarmaActiva=true;
}

void desactivarAlarma(){
  vibrando=false;
  detectoPastilla=false;
  alarmaActiva=false;
}

// =================================================
BLYNK_CONNECTED(){
  ultimoBlynkOK=millis();
  Blynk.syncVirtual(V0,V1,V2,V3,V4,V6,V7,V9,V10,V11,V12,V13,V14);
}

// ===== BLYNK CONFIG =====
BLYNK_WRITE(V1){a1Hora=param.asInt(); lastTrigger_h1=-1;}
BLYNK_WRITE(V4){a1Min=param.asInt(); lastTrigger_h1=-1;}

BLYNK_WRITE(V2){a2Hora=param.asInt(); lastTrigger_h2=-1;}
BLYNK_WRITE(V6){a2Min=param.asInt(); lastTrigger_h2=-1;}

BLYNK_WRITE(V3){a3Hora=param.asInt(); lastTrigger_h3=-1;}
BLYNK_WRITE(V7){a3Min=param.asInt(); lastTrigger_h3=-1;}

BLYNK_WRITE(V9){b1Hora=param.asInt(); lastTrigger_h4=-1;}
BLYNK_WRITE(V12){b1Min=param.asInt(); lastTrigger_h4=-1;}
BLYNK_WRITE(V10){b2Hora=param.asInt(); lastTrigger_h5=-1;}
BLYNK_WRITE(V13){b2Min=param.asInt(); lastTrigger_h5=-1;}
BLYNK_WRITE(V11){b3Hora=param.asInt(); lastTrigger_h6=-1;}
BLYNK_WRITE(V14){b3Min=param.asInt(); lastTrigger_h6=-1;}

BLYNK_WRITE(V0){ if(param.asInt()==1) activarManual=true; }
BLYNK_WRITE(V15){ if(param.asInt()==1) activarManual2=true; }

BLYNK_WRITE(V8){
  if(param.asInt()==1){
    lastTrigger_h1=-1; lastTrigger_h2=-1; lastTrigger_h3=-1;
    lastTrigger_h4=-1; lastTrigger_h5=-1; lastTrigger_h6=-1;
    enviarMensaje("Alarmas reseteadas");
  }
}

// =================================================
void setup(){
  Serial.begin(115200);

  WiFi.begin(ssid,pass);
  while(WiFi.status()!=WL_CONNECTED){ delay(300); }

  setupRTC();
  sincronizarRTC();

  Blynk.begin(BLYNK_AUTH_TOKEN,ssid,pass);
  Blynk.virtualWrite(V16,"Sistema listo");

  // ESP-NOW
  esp_now_init();
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(OnDataRecv);

  esp_now_peer_info_t peer={};
  memcpy(peer.peer_addr,receiverMAC,6);
  esp_now_add_peer(&peer);

  pinMode(IR1,INPUT);
  pinMode(IR2,INPUT);

  servo1.attach(PIN_SERVO1,1000,2000);
  servo2.attach(PIN_SERVO2,1000,2000);

  servo1.writeMicroseconds(1500);
  servo2.writeMicroseconds(1500);

  ultimoBlynkOK=millis();
}

// =================================================
void loop(){

  Blynk.run();

  if(Blynk.connected()) ultimoBlynkOK=millis();

  DateTime now=rtc.now();

  char buffer[9];
  sprintf(buffer,"%02d:%02d:%02d",now.hour(),now.minute(),now.second());
  Blynk.virtualWrite(V5,buffer);

  // ✅ CONVERSIÓN HORAS (CRÍTICO)
  if(a1Hora>=0&&a1Min>=0) hora1=a1Hora*100+a1Min;
  if(a2Hora>=0&&a2Min>=0) hora2=a2Hora*100+a2Min;
  if(a3Hora>=0&&a3Min>=0) hora3=a3Hora*100+a3Min;

  if(b1Hora>=0&&b1Min>=0) hora4=b1Hora*100+b1Min;
  if(b2Hora>=0&&b2Min>=0) hora5=b2Hora*100+b2Min;
  if(b3Hora>=0&&b3Min>=0) hora6=b3Hora*100+b3Min;

  int horaActual=now.hour()*100+now.minute();

  if(now.minute()!=ultimoMinuto){
    ultimoMinuto=now.minute();
    int fecha=now.year()*10000+now.month()*100+now.day();

    if(horaActual==hora1&&lastTrigger_h1!=fecha){activarAlarma();dispensar1();lastTrigger_h1=fecha;}
    if(horaActual==hora2&&lastTrigger_h2!=fecha){activarAlarma();dispensar1();lastTrigger_h2=fecha;}
    if(horaActual==hora3&&lastTrigger_h3!=fecha){activarAlarma();dispensar1();lastTrigger_h3=fecha;}

    if(horaActual==hora4&&lastTrigger_h4!=fecha){activarAlarma();dispensar2();lastTrigger_h4=fecha;}
    if(horaActual==hora5&&lastTrigger_h5!=fecha){activarAlarma();dispensar2();lastTrigger_h5=fecha;}
    if(horaActual==hora6&&lastTrigger_h6!=fecha){activarAlarma();dispensar2();lastTrigger_h6=fecha;}
  }

  // ===== CONTROL SERVOS =====
  if(dispensando1){
    if(IR_detectado()){ servo1.writeMicroseconds(1500); dispensando1=false; girando1=false; }
    else if(girando1&&millis()-tiempo1>=DURACION_GIRO){ servo1.writeMicroseconds(1500); girando1=false; tiempo1=millis(); }
    else if(!girando1&&millis()-tiempo1>=PAUSA){ servo1.writeMicroseconds(1350); girando1=true; tiempo1=millis(); }
  }

  if(dispensando2){
    if(IR_detectado()){ servo2.writeMicroseconds(1500); dispensando2=false; girando2=false; }
    else if(girando2&&millis()-tiempo2>=DURACION_GIRO){ servo2.writeMicroseconds(1500); girando2=false; tiempo2=millis(); }
    else if(!girando2&&millis()-tiempo2>=PAUSA){ servo2.writeMicroseconds(1350); girando2=true; tiempo2=millis(); }
  }

  // ===== FAILSAFE =====
  if(millis() - tiempo1 > 5000){
    servo1.writeMicroseconds(1500);
    dispensando1=false;
    girando1=false;
  }

  if(millis() - tiempo2 > 5000){
    servo2.writeMicroseconds(1500);
    dispensando2=false;
    girando2=false;
  }

  // ===== VIBRACIÓN =====
  if(vibrando && millis()-ultimoPulso>=INTERVALO){
    enviarPulso();
    ultimoPulso=millis();
  }

  // ===== IR ROBUSTO =====
  if(vibrando){
    if(IR_detectado()){
      if(tiempoDeteccionIR==0){
        tiempoDeteccionIR=millis();
      }
      if(millis()-tiempoDeteccionIR>300){
        detectoPastilla=true;
      }
    } else {
      if(detectoPastilla){
        
      enviarMensaje("Medicamento tomado");

// 🔔 PUSH
      Blynk.logEvent("medicamento", "💊 Medicamento tomado correctamente");

        desactivarAlarma();
      }
      tiempoDeteccionIR=0;
    }
  }

  // ===== MANUAL =====
  if(activarManual){
    activarAlarma();
    dispensar1();
    activarManual=false;
  }

  if(activarManual2){
    activarAlarma();
    dispensar2();
    activarManual2=false;
  }

  // ===== WATCHDOG =====
  if(millis()-ultimoBlynkOK>TIMEOUT){
    ESP.restart();
  }

  delay(20);
}