#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// 🔹 Identifiants WiFi
const char* ssid = "Math";
const char* password = "SamsungA52S";

// 🔹 Serveur MQTT
const char* mqtt_server = "test.mosquitto.org";

// 🔹 Broches
const int ledPin2 = 12;    // ✅ D6 = GPIO12 pour LED2
const int buttonPin = 13;  // ✅ D7 (GPIO13) pour le bouton

// 🔹 Topics
const char* led2ControlTopic = "led2/control";
const char* led2StateTopic   = "led2/state";
const char* plagesTopic2     = "plages/horaires2";

// 🔹 Objets
WiFiClient espClient;
PubSubClient client(espClient);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7200); // UTC+2

// 🔹 Variables
unsigned long lastCheck = 0;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200; // anti-rebond
bool lastButtonState = HIGH;
bool buttonState = HIGH;

String lastPlagesJson = "";
int led2State = LOW;  // État actuel de la LED2

// ------------------------------------------------------------
// Publie l'état courant de la LED2 sur le broker
// ------------------------------------------------------------
void publishLed2State() {
  if (client.connected()) {
    client.publish(led2StateTopic, led2State ? "ON" : "OFF", true);
  }
}

// ------------------------------------------------------------
// Connexion WiFi
// ------------------------------------------------------------
void setup_wifi() {
  delay(10);
  Serial.begin(115200);
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

int timeToSeconds(String t) {
  int h = t.substring(0, 2).toInt();
  int m = t.substring(3, 5).toInt();
  int s = t.substring(6, 8).toInt();
  return h * 3600 + m * 60 + s;
}

// ------------------------------------------------------------
// Callback MQTT
// ------------------------------------------------------------
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  message.trim();

  Serial.printf("📨 Message reçu (%d octets) sur [%s] : %s\n", message.length(), topic, message.c_str());

  if (String(topic) == plagesTopic2) {
    lastPlagesJson = message;
    Serial.println("📅 Nouvelles plages reçues et enregistrées !");
  }
  else if (String(topic) == led2ControlTopic) {
    if (message.equalsIgnoreCase("ON")) {
      digitalWrite(ledPin2, HIGH);
      led2State = HIGH;
      Serial.println("💡 LED2 allumée via message MQTT !");
      publishLed2State();
    } 
    else if (message.equalsIgnoreCase("OFF")) {
      digitalWrite(ledPin2, LOW);
      led2State = LOW;
      Serial.println("💡 LED2 éteinte via message MQTT !");
      publishLed2State();
    }
  }
}

// ------------------------------------------------------------
// Reconnexion au broker MQTT
// ------------------------------------------------------------
void reconnect() {
  while (!client.connected()) {
    Serial.print("Connexion au broker MQTT...");
    if (client.connect("ESP8266Client_LED2")) {
      Serial.println("✅ Connecté !");
      client.subscribe(led2ControlTopic);
      client.subscribe(plagesTopic2);

      // Publie l'état initial dès la connexion
      publishLed2State();
    } else {
      Serial.print("❌ Échec, code = ");
      Serial.println(client.state());
      delay(5000);
    }
  }
}

// ------------------------------------------------------------
// Vérifie le bouton physique
// ------------------------------------------------------------
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
        led2State = !led2State; // inverse l'état
        digitalWrite(ledPin2, led2State);

        // Publie l'état au broker pour que Django/Frontend s'actualisent
        publishLed2State();

        Serial.println(led2State ? "💡 LED2 allumée via bouton physique" : "💡 LED2 éteinte via bouton physique");
      }
    }
  }

  lastButtonState = reading;
}

// ------------------------------------------------------------
// Setup
// ------------------------------------------------------------
void setup() {
  pinMode(ledPin2, OUTPUT);
  digitalWrite(ledPin2, LOW);

  pinMode(buttonPin, INPUT_PULLUP);  // bouton avec résistance interne

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  timeClient.begin();
  Serial.println("🔍 Initialisation terminée !");
}

// ------------------------------------------------------------
// Loop principal
// ------------------------------------------------------------
void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  // Vérifie le bouton à chaque tour
  checkButton();

  unsigned long currentMillis = millis();

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
        int nowSec = (timeToSeconds(currentTime));
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

        if (ledShouldBeOn && led2State == LOW) {
          digitalWrite(ledPin2, HIGH);
          led2State = HIGH;
          Serial.println("💡 LED2 ALLUMÉE (plage active)");
          publishLed2State();
        } 
        else if (!ledShouldBeOn && led2State == HIGH) {
          digitalWrite(ledPin2, LOW);
          led2State = LOW;
          Serial.println("💡 LED2 ÉTEINTE (hors plage)");
          publishLed2State();
        } 
        else {
          Serial.println("⏳ Aucun changement d'état de la LED2.");
        }

      } else {
        Serial.println("⚠️ Erreur de parsing JSON !");
      }
    } else {
      Serial.println("⚠️ Aucune plage horaire reçue pour le moment.");
    }
  }
}
