#include <M5Unified.h>

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <TinyGPSPlus.h>
#include "../config.h"

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

// --- Ajout: parsing hex depuis macros injectées par PlatformIO ---
static bool g_keysInit = false;
static u1_t g_devEui[8];
static u1_t g_appKey[16];

static int _hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

// Parse une string hex (accepte ":" "-" espaces) vers bytes.
// Si reverse=true, inverse l'ordre des octets (utile DevEUI LMIC).
static bool parseHexBytes(const char* s, uint8_t* out, size_t outLen, bool reverse) {
  if (!s) return false;

  // Collecte des nibbles valides
  uint8_t tmp[32]; // suffisant pour 16 bytes
  size_t tmpLen = 0;

  int hi = -1;
  for (const char* p = s; *p; ++p) {
    int v = _hexval(*p);
    if (v < 0) continue; // ignore séparateurs
    if (hi < 0) {
      hi = v;
    } else {
      if (tmpLen >= sizeof(tmp)) return false;
      tmp[tmpLen++] = (uint8_t)((hi << 4) | v);
      hi = -1;
    }
  }

  if (hi >= 0) return false; // nombre impair de nibbles
  if (tmpLen != outLen) return false;

  if (reverse) {
    for (size_t i = 0; i < outLen; ++i) out[i] = tmp[outLen - 1 - i];
  } else {
    for (size_t i = 0; i < outLen; ++i) out[i] = tmp[i];
  }
  return true;
}

static void initKeysOnce() {
  if (g_keysInit) return;
  g_keysInit = true;

#if defined(LORAWAN_DEVEUI_STR) && defined(LORAWAN_APPKEY_STR)
  // Si les strings sont vides ou invalides, on retombe sur l'ancien mode.
  bool okDev = parseHexBytes(LORAWAN_DEVEUI_STR, g_devEui, sizeof(g_devEui), /*reverse=*/true);
  bool okKey = parseHexBytes(LORAWAN_APPKEY_STR, g_appKey, sizeof(g_appKey), /*reverse=*/false);

  if (okDev && okKey) {
    return;
  }
#endif

  // Fallback: comportement actuel basé sur M5STACK_ID / config.h
  const u1_t fallbackDevEui[8] = {M5STACK_ID, 0x01, 0x03, 0x02, 0x01, 0x45, 0x7E, 0x0C};
  const u1_t fallbackAppKey[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                  0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x01, M5STACK_ID};

  memcpy(g_devEui, fallbackDevEui, sizeof(g_devEui));
  memcpy(g_appKey, fallbackAppKey, sizeof(g_appKey));
}

void os_getDevEui(u1_t *buf) {
  initKeysOnce();
  memcpy(buf, g_devEui, 8);
}

void os_getDevKey(u1_t *buf) {
  initKeysOnce();
  memcpy(buf, g_appKey, 16);
}

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
  
  // Initialiser l'ecran LCD
  M5.Display.setTextColor(GREEN);
  M5.Display.setTextSize(2);
  M5.Display.fillScreen(BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.println("Starting...\nRecherche GPS...");
  M5.Display.println();
  M5.Display.setTextColor(WHITE);
  M5.Display.println("Verifiez:\n- Antenne branchee\n- Etre dehors\n- Attendre 1-2 min");

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

    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.setTextColor(YELLOW);
    M5.Display.println("=== MA POSITION ===");
    M5.Display.println();
    M5.Display.setTextColor(GREEN);
    M5.Display.print("Latitude: ");
    M5.Display.setTextColor(WHITE);
    M5.Display.println(gps.location.lat(), 6);
    M5.Display.setTextColor(GREEN);
    M5.Display.print("Longitude: ");
    M5.Display.setTextColor(WHITE);
    M5.Display.println(gps.location.lng(), 6);
    M5.Display.setTextColor(GREEN);
    M5.Display.print("Satellites: ");
    M5.Display.setTextColor(WHITE);
    M5.Display.println(gps.satellites.value());

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
    }
  }

  // Partie SNR (LMIC)
  os_runloop_once();

  // Repoll GPS après LMIC (utile si LMIC prend du temps)
  pollGPS();
}