#include <M5Unified.h>

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <TinyGPSPlus.h>
#include "../config.h"

// #-----------------------------------------------------------------#
// # Sprite pour affichage sans scintillement                        #
// #-----------------------------------------------------------------#
static M5Canvas canvas(&M5.Display);

// #-----------------------------------------------------------------#
// # Définition des données utiles à la récuparation des données GPS #
// #-----------------------------------------------------------------#
TinyGPSPlus gps;
unsigned long lastGPSUpdate = 0; // dernière position GPS
#define GPS_TIMEOUT 10000        // Délai maximum sans signal GPS

#define GPS_RX 16 // Le fil qui REÇOIT les données du GPS
#define GPS_TX 17 // Le fil qui ENVOIE des données au GPS (pas utilisé ici)

static uint32_t lastGpsLostUiMs = 0;

static void pollGPS()
{
  while (Serial2.available() > 0)
  {
    gps.encode(Serial2.read());
  }
}

// #-----------------------------------------------------------------#
// # Définition des données utiles à la récupération des données SNR #
// #-----------------------------------------------------------------#

// This EUI must be in little-endian format, so least-significant-byte
// first. When copying an EUI from ttnctl output, this means to reverse
// the bytes. For TTN issued EUIs the last bytes should be 0xD5, 0xB3,
// 0x70.
static const u1_t PROGMEM APPEUI[8] = {0};
void os_getArtEui(u1_t *buf) { memcpy_P(buf, APPEUI, 8); }

// This should also be in little endian format, see above.
static const u1_t PROGMEM DEVEUI[8] = LORAWAN_DEVEUI;
void os_getDevEui(u1_t *buf) { memcpy_P(buf, DEVEUI, 8); }

// This key should be in big endian format (or, since it is not really a
// number but a block of memory, endianness does not really apply). In
// practice, a key taken from ttnctl can be copied as-is.
static const u1_t PROGMEM APPKEY[16] = LORAWAN_APPKEY;
void os_getDevKey(u1_t *buf) { memcpy_P(buf, APPKEY, 16); }

char mydata[100];
static osjob_t sendjob;

// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
const unsigned TX_INTERVAL = 10;

// Pin mapping
// https://docs.m5stack.com/en/module/lora868
const lmic_pinmap lmic_pins = {
    .nss = 5,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 26,
    .dio = {36, 35, LMIC_UNUSED_PIN},
};

void printHex2(unsigned v)
{
  v &= 0xff;
  if (v < 16)
    Serial.print('0');
  Serial.print(v, HEX);
}

static bool gpsIsFresh()
{
  return gps.location.isValid() && gps.location.age() < GPS_TIMEOUT;
}

void do_send(osjob_t *j)
{
  if (LMIC.opmode & OP_TXRXPEND)
  {
    Serial.println(F("OP_TXRXPEND, not sending"));
    return;
  }

  M5.update();
  pollGPS();


  if (gpsIsFresh())
  {
    // Exemple:
    // {"type":"gps","lat":48.856613,"lon":2.352222,"sat":7,"age":120}
    snprintf(
        mydata,
        sizeof(mydata),
        "{\"type\":\"gps\",\"lat\":%.6f,\"lon\":%.6f,\"sat\":%lu,\"age\":%lu}",
        gps.location.lat(),
        gps.location.lng(),
        (unsigned long)gps.satellites.value(),
        (unsigned long)gps.location.age());
  }
  else
  {
    // Pas de fix / donnée trop vieille
    snprintf(mydata, sizeof(mydata), "{\"type\":\"gps\",\"fix\":false}");
  }

  LMIC_setTxData2(1, (unsigned char *)mydata, strlen(mydata), 1);

  Serial.print("Envoi => ");
  Serial.println(mydata);
  Serial.println(F("Packet queued"));
}

void onEvent(ev_t ev)
{
  Serial.print(os_getTime());
  Serial.print(": ");
  switch (ev)
  {
  case EV_SCAN_TIMEOUT:
    Serial.println(F("EV_SCAN_TIMEOUT"));
    break;
  case EV_BEACON_FOUND:
    Serial.println(F("EV_BEACON_FOUND"));
    break;
  case EV_BEACON_MISSED:
    Serial.println(F("EV_BEACON_MISSED"));
    break;
  case EV_BEACON_TRACKED:
    Serial.println(F("EV_BEACON_TRACKED"));
    break;
  case EV_JOINING:
    Serial.println(F("EV_JOINING"));
    break;
  case EV_JOINED:
    Serial.println(F("EV_JOINED"));
    {
      u4_t netid = 0;
      devaddr_t devaddr = 0;
      u1_t nwkKey[16];
      u1_t artKey[16];
      LMIC_getSessionKeys(&netid, &devaddr, nwkKey, artKey);
      Serial.print("netid: ");
      Serial.println(netid, DEC);
      Serial.print("devaddr: ");
      Serial.println(devaddr, HEX);
      Serial.print("AppSKey: ");
      for (size_t i = 0; i < sizeof(artKey); ++i)
      {
        if (i != 0)
          Serial.print("-");
        printHex2(artKey[i]);
      }
      Serial.println("");
      Serial.print("NwkSKey: ");
      for (size_t i = 0; i < sizeof(nwkKey); ++i)
      {
        if (i != 0)
          Serial.print("-");
        printHex2(nwkKey[i]);
      }
      Serial.println();
    }
    // Disable link check validation (automatically enabled
    // during join, but because slow data rates change max TX
    // size, we don't use it in this example.
    LMIC_setLinkCheckMode(0);
    break;
  /*
  || This event is defined but not used in the code. No
  || point in wasting codespace on it.
  ||
  || case EV_RFU1:
  ||     Serial.println(F("EV_RFU1"));
  ||     break;
  */
  case EV_JOIN_FAILED:
    Serial.println(F("EV_JOIN_FAILED"));
    break;
  case EV_REJOIN_FAILED:
    Serial.println(F("EV_REJOIN_FAILED"));
    break;
  case EV_TXCOMPLETE:
    Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
    if (LMIC.txrxFlags & TXRX_ACK)
      Serial.println(F("Received ack"));
    if (LMIC.dataLen)
    {
      Serial.print(F("Received "));
      Serial.print(LMIC.dataLen);
      Serial.println(F(" bytes of payload"));
    }
    // Schedule next transmission
    os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL), do_send);
    break;
  case EV_LOST_TSYNC:
    Serial.println(F("EV_LOST_TSYNC"));
    break;
  case EV_RESET:
    Serial.println(F("EV_RESET"));
    break;
  case EV_RXCOMPLETE:
    Serial.println(F("EV_RXCOMPLETE"));
    break;
  case EV_LINK_DEAD:
    Serial.println(F("EV_LINK_DEAD"));
    break;
  case EV_LINK_ALIVE:
    Serial.println(F("EV_LINK_ALIVE"));
    break;
  case EV_TXSTART:
    Serial.println(F("EV_TXSTART"));
    break;
  case EV_TXCANCELED:
    Serial.println(F("EV_TXCANCELED"));
    break;
  case EV_RXSTART:
    break;
  case EV_JOIN_TXCOMPLETE:
    Serial.println(F("EV_JOIN_TXCOMPLETE: no JoinAccept"));
    break;

  default:
    Serial.print(F("Unknown event: "));
    Serial.println((unsigned)ev);
    break;
  }
}

void setup()
{
  // Initialiser la M5 stack
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Power.begin();
  Serial2.setRxBufferSize(1024);
  Serial2.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.begin(115200);
  while (!Serial)
  {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  // Creer le sprite avec une profondeur de couleur reduite (8 bits) pour economiser la memoire
  canvas.setColorDepth(8);
  canvas.createSprite(M5.Display.width(), M5.Display.height());
  canvas.setTextSize(2);

  // Affichage initial via sprite
  canvas.fillSprite(TFT_BLACK);
  canvas.setCursor(0, 0);
  canvas.setTextColor(TFT_GREEN);
  canvas.println("Starting...\nRecherche GPS...");
  canvas.println();
  canvas.setTextColor(TFT_WHITE);
  canvas.println("Verifiez:\n- Antenne branchee\n- Etre dehors\n- Attendre 1-2 min");
  canvas.pushSprite(0, 0);

  // Initialisation LMIC
  os_init();
  // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();

  // Start job (sending automatically starts OTAA too)
  do_send(&sendjob);
}

void loop()
{
  // Poll GPS le plus souvent possible
  pollGPS();

  // Partie GPS
  if (gps.location.isUpdated())
  {
    lastGPSUpdate = millis();

    canvas.fillSprite(TFT_BLACK);
    canvas.setCursor(0, 0);
    canvas.setTextColor(TFT_YELLOW);
    canvas.println("=== MA POSITION ===");
    canvas.println();
    canvas.setTextColor(TFT_GREEN);
    canvas.print("Latitude: ");
    canvas.setTextColor(TFT_WHITE);
    canvas.println(gps.location.lat(), 6);
    canvas.setTextColor(TFT_GREEN);
    canvas.print("Longitude: ");
    canvas.setTextColor(TFT_WHITE);
    canvas.println(gps.location.lng(), 6);
    canvas.setTextColor(TFT_GREEN);
    canvas.print("Satellites: ");
    canvas.setTextColor(TFT_WHITE);
    canvas.println(gps.satellites.value());
    canvas.pushSprite(0, 0);

    Serial.print("Position: ");
    Serial.print(gps.location.lat(), 6);
    Serial.print(", ");
    Serial.println(gps.location.lng(), 6);
  }

  // Détection de perte de signal GPS (sans delay bloquant)
  if (lastGPSUpdate > 0 && (millis() - lastGPSUpdate) > GPS_TIMEOUT)
  {
    if (millis() - lastGpsLostUiMs >= 1000)
    {
      lastGpsLostUiMs = millis();

      canvas.fillSprite(TFT_BLACK);
      canvas.setCursor(0, 0);
      canvas.setTextColor(TFT_RED);
      canvas.println("SIGNAL PERDU !");
      canvas.println();
      canvas.setTextColor(TFT_ORANGE);
      canvas.print("Depuis :");
      canvas.setTextColor(TFT_WHITE);
      canvas.print((millis() - lastGPSUpdate) / 1000);
      canvas.println(" sec");
      canvas.println();
      canvas.setTextColor(TFT_WHITE);
      canvas.println("Verifiez:");
      canvas.println("- Antenne GPS");
      canvas.println("- Etre dehors");
      canvas.println("- Vue du ciel");
      canvas.pushSprite(0, 0);
    }
  }

  // Partie SNR (LMIC)
  os_runloop_once();

  // Repoll GPS après LMIC (utile si LMIC prend du temps)
  pollGPS();

  //Code
}