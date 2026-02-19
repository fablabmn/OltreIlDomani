/* ============================================================================
 *  Configurazione WiFi + MQTT
 * ========================================================================== */

#include <WiFiS3.h>
#include <ArduinoMqttClient.h>
#include "arduino_secrets.h"

// Client rete + client MQTT
WiFiClient client_wifi;
MqttClient client_mqtt(client_wifi);

// Broker MQTT
#define MQTT_BROKER "37100lab.it"
#define MQTT_PORT   12883

// Topic MQTT
#define TOPIC_PM10 "oltreildomani/centralina1/PM10"
#define TOPIC_UMID "oltreildomani/centralina1/RH"
#define TOPIC_TEMP "oltreildomani/centralina1/T"

// Intervallo pubblicazione MQTT (in secondi)
#define INTERVALLO_PUBBLICAZIONE 60
unsigned long long millis_precedenti = 0;


/* ============================================================================
 *  Display OLED
 * ========================================================================== */

#include <Adafruit_SSD1306.h>

#define OLED_LARGHEZZA  128
#define OLED_ALTEZZA    64
#define OLED_PIN_RESET  -1  // -1 se condiviso col reset di Arduino
#define OLED_INDIRIZZO  0x3C

Adafruit_SSD1306 display(OLED_LARGHEZZA, OLED_ALTEZZA, &Wire, OLED_PIN_RESET);


/* ============================================================================
 *  Sensore (SEN54)
 * ========================================================================== */

#include <SensirionI2CSen5x.h>

SensirionI2CSen5x sen5x;

// Valori letti dal sensore
float pm1, pm2_5, pm4, pm10;
float umidita_relativa, temperatura_ambiente;
float indice_voc, indice_nox;

// Per gli errori del sensore
uint16_t errore;
char messaggio_errore[256];


/* ============================================================================
 *  Prototipi delle funzioni
 * ========================================================================== */

void mostra_intro();
void mostra_letture_su_display();
void mostra_letture_su_seriale();
void invia_dati();


/* ============================================================================
 *  Setup
 * ========================================================================== */

void setup() {
  // Fix di I2C per la libreria core 1.5.2 per Arduino Uno R4 WiFi
  //
  // https://github.com/arduino/ArduinoCore-renesas/issues/520
  // https://github.com/fablabmn/OltreIlDomani/issues/3
  Wire.setTimeout(10000);
  Wire1.setTimeout(10000);

  // Alimentazione "di fortuna" del display OLED tramite GPIO (pin 12)
  pinMode(12, OUTPUT);
  digitalWrite(12, HIGH);

  // Inizializzazione della porta seriale
  Serial.begin(9600);

  // Inizializzazione display OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_INDIRIZZO)) {
    Serial.println("[OLED] Allocazione del display fallita!");
  }
  mostra_intro();

  // Controllo modulo WiFi
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("[WIFI] Comunicazione con il modulo WiFi fallita!");
    while (true);
  }

  // Connessione WiFi
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print("[WIFI] Tentativo di connessione all'SSID: ");
    Serial.println(SECRET_SSID);

    WiFi.begin(SECRET_SSID, SECRET_PASS);

    delay(2500);
  }

  Serial.println("[WIFI] Connesso alla rete!");
  Serial.print("[WIFI] Indirizzo IP: ");
  Serial.println(WiFi.localIP());

  // Connessione MQTT
  Serial.print("[MQTT] Tentativo connessione al broker: ");
  Serial.println(MQTT_BROKER);

  if (!client_mqtt.connect(MQTT_BROKER, MQTT_PORT)) {
    Serial.print("[MQTT] Connessione fallita! Codice errore = ");
    Serial.println(client_mqtt.connectError());
    while (true);
  }

  Serial.println("[MQTT] Connesso al broker!");
  Serial.println();

  // Inizializzazione I2C e sensore SEN5x sulla porta QWIIC
  Wire1.begin();
  sen5x.begin(Wire1);

  // Reset sensore
  errore = sen5x.deviceReset();
  if (errore) {
    Serial.print("[SEN54] Errore durante il reset del dispositivo: ");
    errorToString(errore, messaggio_errore, 256);
    Serial.println(messaggio_errore);
  }

  // Offset temperatura
  float offset_temperatura = 0.0;
  errore = sen5x.setTemperatureOffsetSimple(offset_temperatura);
  if (errore) {
    Serial.print("[SEN54] Errore durante l'impostazione dell'offset della temperatura: ");
    errorToString(errore, messaggio_errore, 256);
    Serial.println(messaggio_errore);
  }

  // Avvio misure
  errore = sen5x.startMeasurement();
  if (errore) {
    Serial.print("[SEN54] Errore in durante l'inizializzazione delle misurazioni: ");
    errorToString(errore, messaggio_errore, 256);
    Serial.println(messaggio_errore);
  }
}


/* ============================================================================
 *  Loop
 * ========================================================================== */

void loop() {
  delay(500);

  // Mantieni viva la comunicazione col server MQTT
  client_mqtt.poll();

  // Leggi i valori misurati dal sensore
  errore = sen5x.readMeasuredValues(
    pm1, pm2_5, pm4, pm10,
    umidita_relativa, temperatura_ambiente,
    indice_voc, indice_nox
  );

  // Log dei valori letti in tempo reale
  if (errore) {
    Serial.print("[SEN54] Errore durante la lettura delle misurazioni: ");
    errorToString(errore, messaggio_errore, 256);
    Serial.println(messaggio_errore);
  } else {
    mostra_letture_su_seriale();
    mostra_letture_su_display();
  }

  // Ogni "INTERVALLO_PUBBLICAZIONE" secondi invia i dati al server MQTT
  unsigned long long millis_correnti = millis();
  if (millis_correnti - millis_precedenti >= (INTERVALLO_PUBBLICAZIONE * 1000)) {
    millis_precedenti = millis_correnti;

    invia_dati();
  }
}


/* ============================================================================
 *  Funzioni
 * ========================================================================== */

void mostra_intro() {
  display.clearDisplay();

  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.println("FabLab");
  display.println("Mantova");

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.println("");
  display.println("");
  display.println("");
  display.println("      presenta...");

  display.display();
  delay(1000);
}

void mostra_letture_su_display() {
  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);

  display.println("Dati in tempo reale:");
  display.println("");

  display.print("PM10: ");
  display.print(pm10);
  display.println(" ug/m3");

  display.print("Temp: ");
  display.print(temperatura_ambiente);
  display.println(" C");

  display.print("Umid: ");
  display.print(umidita_relativa);
  display.print(" %");

  display.display();
}

void mostra_letture_su_seriale() {
  Serial.print("[SEN54] PM1: ");
  Serial.print(pm1);
  Serial.print("\tPM2.5: ");
  Serial.print(pm2_5);
  Serial.print("\tPM4: ");
  Serial.print(pm4);
  Serial.print("\tPM10: ");
  Serial.print(pm10);
  Serial.print("\tUmidità: ");
  Serial.print(umidita_relativa);
  Serial.print("\tTemperatura: ");
  Serial.print(temperatura_ambiente);
  Serial.println();
}

void invia_dati() {
  Serial.print("[MQTT] Invio sul topic: ");
  Serial.print(TOPIC_PM10);
  Serial.print(" -> ");
  Serial.println(pm10);

  client_mqtt.beginMessage(TOPIC_PM10);
  client_mqtt.print(pm10);
  client_mqtt.endMessage();

  Serial.print("[MQTT] Invio sul topic: ");
  Serial.print(TOPIC_UMID);
  Serial.print(" -> ");
  Serial.println(umidita_relativa);

  client_mqtt.beginMessage(TOPIC_UMID);
  client_mqtt.print(umidita_relativa);
  client_mqtt.endMessage();

  Serial.print("[MQTT] Invio sul topic: ");
  Serial.print(TOPIC_TEMP);
  Serial.print(" -> ");
  Serial.println(temperatura_ambiente);

  client_mqtt.beginMessage(TOPIC_TEMP);
  client_mqtt.print(temperatura_ambiente);
  client_mqtt.endMessage();
}
