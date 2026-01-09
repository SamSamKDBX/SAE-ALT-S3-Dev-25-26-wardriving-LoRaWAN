#include <M5Unified.h>      // Bibliothèque pour le M5Stack
#include <TinyGPSPlus.h>    // Bibliothèque pour lire les données GPS


TinyGPSPlus gps;


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
}

void loop() {
  // Tant qu'il y a des données qui arrivent du GPS
  while (Serial2.available() > 0) {
    // On lit un caractère et on le donne à décoder au GPS
    gps.encode(Serial2.read());
  }

  // Si on a reçu une nouvelle position complète
  if (gps.location.isUpdated()) {
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

  // Si aucune donnée GPS n'est reçue pendant 5 secondes
  if (millis() > 5000 && gps.charsProcessed() < 10) {
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.setTextColor(RED);
    M5.Display.println("ERREUR:");
    M5.Display.println("Pas de signal GPS");
    M5.Display.println();
    M5.Display.setTextColor(WHITE);
    M5.Display.println("Verifiez:");
    M5.Display.println("- Antenne branchee");
    M5.Display.println("- Etre dehors");
    delay(1000);
  }
}