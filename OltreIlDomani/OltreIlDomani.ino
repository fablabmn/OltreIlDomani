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

// Intervallo pubblicazione MQTT (ms)
#define INTERVALLO_PUBBLICAZIONE_MS 60000
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

void mostra_letture();
void mostra_intro();


/* ============================================================================
 *  Setup
 * ========================================================================== */

void setup() {
  // Alimentazione "di fortuna" del display OLED tramite GPIO (pin 12)
  pinMode(12, OUTPUT);
  digitalWrite(12, HIGH);

  // Inizializzazione della porta seriale
  Serial.begin(9600);

  // Inizializzazione display oled
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_INDIRIZZO)) {
    Serial.println("[OLED] Allocazione SSD1306 fallita");
  } else {
    display.clearDisplay();
    display.display();

    delay(100);

    mostra_intro();
  }

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

    delay(5000);
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

  // Inizializzazione I2C e sensore SEN5x su Wire1
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
  delay(1000);

  // Lettura valori misurati dal sensore
  errore = sen5x.readMeasuredValues(
    pm1, pm2_5, pm4, pm10,
    umidita_relativa, temperatura_ambiente,
    indice_voc, indice_nox
  );

  // Log seriale valori letti
  if (errore) {
    Serial.print("[SEN54] Errore durante la lettura delle misurazioni: ");
    errorToString(errore, messaggio_errore, 256);
    Serial.println(messaggio_errore);
  } else {
    Serial.print("[SEN54] PM1: ");
    Serial.print(pm1);
    Serial.print("\t");

    Serial.print("PM2.5: ");
    Serial.print(pm2_5);
    Serial.print("\t");

    Serial.print("PM4: ");
    Serial.print(pm4);
    Serial.print("\t");

    Serial.print("PM10: ");
    Serial.print(pm10);
    Serial.print("\t");

    Serial.print("Umidità: ");
    if (isnan(umidita_relativa)) {
      Serial.print("N/A");
    } else {
      Serial.print(umidita_relativa);
    }
    Serial.print("\t");

    Serial.print("Temperatura: ");
    if (isnan(temperatura_ambiente)) {
      Serial.print("N/A");
    } else {
      Serial.print(temperatura_ambiente);
    }
    Serial.println();
  }

  // Aggiornamento display
  mostra_letture();

  // Gestione MQTT
  client_mqtt.poll();

  unsigned long millis_correnti = millis();
  if (millis_correnti - millis_precedenti >= INTERVALLO_PUBBLICAZIONE_MS) {
    millis_precedenti = millis_correnti;

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

    Serial.println();
  }
}


/* ============================================================================
 *  Funzioni: UI (OLED)
 * ========================================================================== */

void mostra_letture() {
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
  delay(3000);
}
