#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// 🔹 Identifiants WiFi
const char* ssid = "Math";
const char* password = "SamsungA52S";

// 🔹 Serveur MQTT
const char* mqtt_server = "test.mosquitto.org";

const char* ledStateTopic = "led1/state";

// 🔹 Broches
const int ledPin = 12;     // ✅ D6 (GPIO12) pour la LED
#define ONE_WIRE_BUS 2     // ✅ D4 (GPIO2) pour le DS18B20
const int buttonPin = 13;  // ✅ D5 (GPIO14) pour le bouton physique

// 🔹 Objets
WiFiClient espClient;
PubSubClient client(espClient);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7200); // UTC+2

// 🔹 Variables globales
unsigned long lastTempSend = 0;  // Dernier envoi de température
unsigned long lastCheck = 0;     // Dernière vérification de plage horaire
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200;  // anti-rebond pour le bouton
bool lastButtonState = HIGH;
bool buttonState = HIGH;

String lastPlagesJson = "";
int ledState = LOW;

// ===================================================
// 🔹 Connexion WiFi
// ===================================================
void setup_wifi() {
  Serial.begin(115200);
  delay(10);
  Serial.println();
  Serial.print("Connexion au WiFi : ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n✅ WiFi connecté !");
  Serial.print("Adresse IP : ");
  Serial.println(WiFi.localIP());
}

void publishLedState() {
  client.publish(ledStateTopic, ledState ? "ON" : "OFF", true);
}
// ===================================================
// 🔹 Conversion heure → secondes
// ===================================================
int timeToSeconds(String t) {
  int h = t.substring(0, 2).toInt();
  int m = t.substring(3, 5).toInt();
  int s = t.substring(6, 8).toInt();
  return h * 3600 + m * 60 + s;
}

// ===================================================
// 🔹 Callback MQTT
// ===================================================
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  message.trim();

  Serial.printf("📩 Message reçu (%d octets) sur [%s] : %s\n", message.length(), topic, message.c_str());

  if (String(topic) == "plages/horaires") {
    lastPlagesJson = message;
    Serial.println("🗓 Nouvelles plages reçues !");
  }

  // Commande directe LED depuis le site
  if (String(topic) == "led1/control") {
    if (message.equalsIgnoreCase("ON")) {
      digitalWrite(ledPin, HIGH);
      ledState = HIGH;
      publishLedState(); // 🔹 publication de l'état
      Serial.println("💡 LED1 allumée (commande MQTT)");
    } 
    else if (message.equalsIgnoreCase("OFF")) {
      digitalWrite(ledPin, LOW);
      ledState = LOW;
      publishLedState(); // 🔹 publication de l'état
      Serial.println("💡 LED1 éteinte (commande MQTT)");
    }
  }
}

// ===================================================
// 🔹 Reconnexion au broker MQTT
// ===================================================
void reconnect() {
  while (!client.connected()) {
    Serial.print("Connexion au broker MQTT...");
    if (client.connect("ESP8266Client_LED1")) {
      Serial.println("✅ Connecté !");
      client.subscribe("led1/control");
      client.subscribe("plages/horaires");
    } else {
      Serial.print("❌ Échec, code = ");
      Serial.println(client.state());
      delay(5000);
    }
  }
}

// ===================================================
// 🔹 Vérifie le bouton physique
// ===================================================
void checkButton() {
  int reading = digitalRead(buttonPin);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;

      // Si bouton appuyé (état LOW car INPUT_PULLUP)
      if (buttonState == LOW) {
        ledState = !ledState; // on inverse l’état
        digitalWrite(ledPin, ledState);
        Serial.println(ledState ? "💡 LED1 allumée via bouton physique" : "💡 LED1 éteinte via bouton physique");
        publishLedState(); // 🔹 publication de l'état au site web
      }
    }
  }

  lastButtonState = reading;
}

// ===================================================
// 🔹 SETUP
// ===================================================
void setup() {
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  pinMode(buttonPin, INPUT_PULLUP); // ✅ Bouton physique

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  sensors.begin();
  timeClient.begin();

  Serial.println("🔍 Initialisation terminée !");
}

// ===================================================
// 🔹 LOOP principale
// ===================================================
void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  checkButton(); // 🔹 Vérifie le bouton à chaque tour

  unsigned long currentMillis = millis();

  // --- Envoi température toutes les 2 secondes ---
  if (currentMillis - lastTempSend > 2000) {
    lastTempSend = currentMillis;

    sensors.requestTemperatures();
    float tempC = sensors.getTempCByIndex(0);

    if (tempC == DEVICE_DISCONNECTED_C) {
      Serial.println("⚠️ Capteur DS18B20 non détecté !");
    } else {
      Serial.printf("🌡️ Température lue = %.2f °C\n", tempC);
      client.publish("capteur/temperature", String(tempC).c_str(), true);
      Serial.println("✅ Température envoyée au broker !");
    }
  }

  // --- Vérifie les plages horaires toutes les 10 secondes ---
  if (currentMillis - lastCheck > 10000) {
    lastCheck = currentMillis;
    timeClient.update();
    String currentTime = timeClient.getFormattedTime();

    Serial.println("\n==============================");
    Serial.print("🕒 Vérification à ");
    Serial.println(currentTime);

    if (lastPlagesJson.length() > 0) {
      StaticJsonDocument<1024> doc;
      DeserializationError error = deserializeJson(doc, lastPlagesJson);

      if (!error) {
        int nowSec = timeToSeconds(currentTime);
        bool ledShouldBeOn = false;

        for (JsonObject plage : doc.as<JsonArray>()) {
          String debut = plage["debut"];
          String fin = plage["fin"];
          String etat = plage["etat"];

          int debutSec = timeToSeconds(debut);
          int finSec = timeToSeconds(fin);

          Serial.printf("⏰ %s → %s (%s)\n", debut.c_str(), fin.c_str(), etat.c_str());

          if (nowSec >= debutSec && nowSec <= finSec && etat.equalsIgnoreCase("ON")) {
            ledShouldBeOn = true;
          }
        }

        if (ledShouldBeOn && ledState == LOW) {
          digitalWrite(ledPin, HIGH);
          ledState = HIGH;
          publishLedState(); // 🔹 publication de l'état
          Serial.println("💡 LED1 ALLUMÉE (plage active)");
        } 
        else if (!ledShouldBeOn && ledState == HIGH) {
          digitalWrite(ledPin, LOW);
          ledState = LOW;
          publishLedState(); // 🔹 publication de l'état
          Serial.println("💡 LED1 ÉTEINTE (hors plage)");
        } 
        else {
          Serial.println("⏳ Aucun changement d'état de la LED1.");
        }

      } else {
        Serial.println("⚠️ Erreur de parsing JSON !");
      }
    } else {
      Serial.println("⚠️ Aucune plage horaire reçue !");
    }
  }
}
