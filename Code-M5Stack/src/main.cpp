#include <M5Unified.h>      // Bibliothèque pour le M5Stack
#include <TinyGPSPlus.h>    // Bibliothèque pour lire les données GPS


TinyGPSPlus gps;
unsigned long lastGPSUpdate = 0; // dernière position GPS
#define GPS_TIMEOUT 10000  // Délai maximum sans signal GPS 

#define GPS_RX 16  // Le fil qui REÇOIT les données du GPS
#define GPS_TX 17  // Le fil qui ENVOIE des données au GPS (pas utilisé ici)

void setup(){
  auto cfg = M5.config();
  M5.begin(cfg); 
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);

  M5.Display.setTextColor(GREEN);
  M5.Display.setTextSize(2);
  M5.Display.fillScreen(BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.println("Recherche GPS...");
  M5.Display.println();
  M5.Display.setTextColor(WHITE);
  M5.Display.println("Verifiez:");
  M5.Display.println("- Antenne branchee");
  M5.Display.println("- Etre dehors");
  M5.Display.println("- Attendre 1-2 min");
}

void loop() {
  // Tant qu'il y a des données qui arrivent du GPS
  while (Serial2.available() > 0) {
    gps.encode(Serial2.read());
  }

  // Si on a reçu une nouvelle position complète
  if (gps.location.isUpdated()) {
    // On mémorise l'heure de cette position
    lastGPSUpdate = millis();

    // Affichage sur l'écran
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.setTextColor(YELLOW);
    M5.Display.println("=== MA POSITION ===");
    M5.Display.println();
    M5.Display.setTextColor(GREEN);
    M5.Display.print("Latitude: ");
    M5.Display.setTextColor(WHITE);
    M5.Display.println(gps.location.lat(), 6); // 6 chiffres après la virgule
    M5.Display.setTextColor(GREEN);
    M5.Display.print("Longitude: ");
    M5.Display.setTextColor(WHITE);
    M5.Display.println(gps.location.lng(), 6);
    M5.Display.setTextColor(GREEN);
    M5.Display.print("Satellites: ");
    M5.Display.setTextColor(WHITE);
    M5.Display.println(gps.satellites.value());

    // Affichage dans le moniteur série
    Serial.print("Position: ");
    Serial.print(gps.location.lat(), 6);
    Serial.print(", ");
    Serial.println(gps.location.lng(), 6);
  }

  // Détection de perte de signal GPS (seulement si on en avait un avant)
  if (lastGPSUpdate > 0 && (millis() - lastGPSUpdate) > GPS_TIMEOUT) {
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.setTextColor(RED);
    M5.Display.println("SIGNAL PERDU !");
    M5.Display.println();
    M5.Display.setTextColor(ORANGE);
    M5.Display.print("Depuis :");
    M5.Display.setTextColor(WHITE);
    M5.Display.print((millis() - lastGPSUpdate) / 1000);
    M5.Display.println(" sec");
    M5.Display.println();
    M5.Display.setTextColor(WHITE);
    M5.Display.println("Verifiez:");
    M5.Display.println("- Antenne GPS");
    M5.Display.println("- Etre dehors");
    M5.Display.println("- Vue du ciel");
    delay(1000);
  }
}