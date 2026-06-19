/*******************************************************************
  Web radio simple à base d'ESP32 et VS1053
  Basé sur un sketch de Vince Gellár (github.com/vincegellar)
  Bibliotheque VS1053 de baldram (https://github.com/baldram/ESP_VS1053_Library)
  Plus d'infos:  https://electroniqueamateur.blogspot.com/2021/03/esp32-et-vs1053-ecouter-la-radio-sur.html
*********************************************************************/

#include <VS1053.h>
#include <WiFi.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <ESP32_VS1053_Stream.h>

// broches utilisées
#define VS1053_CS     32
#define VS1053_DCS    33
#define VS1053_DREQ   15

// nom et mot de passe de votre réseau:
const char *ssid = "AndroidAP";
const char *password = "Mathias le Goat";

#define BUFFSIZE 64  //32, 64 ou 128
uint8_t mp3buff[BUFFSIZE];

int volume = 85;  // volume sonore 0 à 100

// Variables globales pour les réglages de tonalité
uint8_t trebleAmp = 8;   // de 0 à 15 (0 = off)
const uint8_t trebleFreq = 10; // de 0 à 15 (x1000 Hz)
uint8_t bassAmp = 8;     // de 0 à 15 (0 = off)
const uint8_t bassFreq = 10;   // de 0 à 15 (x10 Hz)

// Liste des stations radio (URLs complètes)
const char* stations[] = {
  "http://live.radioking.fr/azur-fm-68",
  "http://ice4.somafm.com/seventies-128-mp3",
  "https://florfm.ice.infomaniak.ch/webmulhouse.mp3",
  "http://lbs-th2-2.nrjaudio.fm/fr/30601/mp3_128.mp3",
  "http://radios.rtbf.be/wr-c21-metal-128.mp3",
  "http://ecoutez.chyz.ca:8000/mp3",
  "http://lyon1ere.ice.infomaniak.ch/lyon1ere-high.mp3"
};

#define NOMBRECHAINES (sizeof(stations)/sizeof(stations[0]))
int chaine = 0; //station actuellement sélectionnée

// Création de l'objet player
VS1053 player(VS1053_CS, VS1053_DCS, VS1053_DREQ);
ESP32_VS1053_Stream stream;
WiFiClient client;

//Spacialisation
#define SCI_MODE 0x00

//Variable pour le réglage de la spatialisation
uint16_t val = player.getModeRegister(); //lit le registre SCI_MODE
int mode = 0;

void applyTone() {
  uint8_t rtone[4] = {trebleAmp, trebleFreq, bassAmp, bassFreq};
  player.setTone(rtone);
}

void spatialisation () {
  uint16_t val = player.getModeRegister();
  val &= ~(0b0000000000010000 | 0b0000000010000000);
  if (mode == 0) {
    Serial.println("Off");
  }
  if (mode == 1) {
    val |= 0b0000000000010000;
    Serial.println("Low");
  }
  if (mode == 2) {
    val |= 0b0000000010000000;
    Serial.println("Medium");
  }
  if (mode == 3) {
    val |= 0b0000000000010000 | 0b0000000010000000;
    Serial.println("High");
  }; // Appel direct de la méthode ajoutée
  player.writeRegister(SCI_MODE, val);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nRadio WiFi");
  Serial.println("Controles: ");
  Serial.println("\t n: synthoniser une autre chaine");
  Serial.println("\t y / b: controle du volume");
  Serial.println("\t g / f: controle des basses");
  Serial.println("\t j / h: controle des aigus");
  Serial.println("\t d: tonalité par défaut");
  Serial.println("\t s: changer de mode de spatialisation");

  Serial.print("Connexion au reseau ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("WiFi connecte");
  Serial.println("Adresse IP: ");
  Serial.println(WiFi.localIP());

  SPI.begin();
  player.begin();
  player.switchToMp3Mode();
  player.setVolume(volume);
  Serial.print("Connexion à la station : ");
  Serial.println(stations[chaine]);
  stream.connecttohost(stations[chaine]);
  WiFiManager wm;
//  wm.resetSettings();
  bool res = wm.autoConnect("AndroidESP32");
    if(!res) {
      Serial.println("Failed to connect");
    } 
    else {
      Serial.println("connected...yeey :)");
    }
  player.switchToMp3Mode();
  player.setVolume(volume);
  Serial.print("Connexion à la station : ");
  Serial.println(stations[chaine]);
  player.stopSong();
  stream.connecttohost(stations[chaine]);
    // it is a good practice to make sure your code sets wifi mode how you want it.
  applyTone();  // Applique les réglages initiaux

  Serial.println("Commandes:");
  Serial.println("y = +volume | b = -volume");
  Serial.println("g = +graves | h = -graves");
  Serial.println("f = +aigus  | j = -aigus");
  Serial.println("d = normal");
  Serial.println("s = spatialisation");
  
}

void loop() {
  stream.loop();
  if (Serial.available()) {
    char c = Serial.read();

    // n: prochaine chaine
    if (c == 'n') {
      Serial.println("On change de chaine");
      chaine = (chaine+1)%NOMBRECHAINES;
      Serial.println(stations[chaine]);
      player.stopSong();
      stream.connecttohost(stations[chaine]);
    }
        // v: chaine précedente
    if (c == 'v') {
      Serial.println("On retourne à la chaine précedente");
      chaine = (chaine-1)%NOMBRECHAINES;
      Serial.println(stations[chaine]);
      player.stopSong();
      stream.connecttohost(stations[chaine]);
    }
    // y: augmenter le volume
    if (c == 'y') {
      if (volume < 100) {
        Serial.println("Plus fort");
        volume++;
        player.setVolume(volume);
      }
    }

    // b: diminuer le volume
    if (c == 'b') {
      if (volume > 0) {
        Serial.println("Moins fort");
        volume--;
        player.setVolume(volume);
      }
    }
    // g: augmenter les basses
    if (c == 'g') {
      if (bassAmp < 15) {
        bassAmp++;
        applyTone();  // Met à jour les réglages du VS1053
        Serial.print("Basses + : ");
        Serial.println(bassAmp);
      }
    }
    // f: Diminuer les basses
    if (c == 'f') {
      if (bassAmp > 0) {
        bassAmp--;
        applyTone();  // Met à jour les réglages du VS1053
        Serial.print("Basses - : ");
        Serial.println(bassAmp);
      }
    }
    // j : augmenter aigus
    if (c == 'j') {
      if (trebleAmp < 15) {
        trebleAmp++;
        applyTone();  // Met à jour les réglages du VS1053
        Serial.print("Aigus + : ");
        Serial.println(trebleAmp);
      }
    }
    // h : diminuer aigus
    if (c == 'h') {
      if (trebleAmp > 0) {
        trebleAmp--;
        applyTone();  // Met à jour les réglages du VS1053
        Serial.print("Aigus - : ");
        Serial.println(trebleAmp);
      }
    }
    // s: changer de spatialisation
    if (c == 's') {
      mode = (mode +1) % 4;
      Serial.println("Changement de spatialisation");
      spatialisation();
    }
        // d : mode par défaut
    if (c == 'd') {
      bassAmp = 8;
      trebleAmp = 8;
      applyTone();
      Serial.println("Tonalité par defaut");
      volume = 85;
      player.setVolume(volume);
      Serial.println("Volume par defaut");
    }
  }
}