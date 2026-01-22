import paho.mqtt.client as mqtt
import json
import ssl
import base64
import os
from datetime import datetime, timezone

# Configuration via variables d'environnement
DATA_FILE = os.environ.get("DATA_FILE", "data.json")
BROKER = os.environ.get("MQTT_BROKER", "chirpstack.iut-blagnac.fr")
PORT = int(os.environ.get("MQTT_PORT", "8883"))
DEVEUI = os.environ.get("LORAWAN_DEVEUI", "0c7e450102030106").lower()
APP_ID = os.environ.get("APP_ID", "6")
TOPIC = os.environ.get("MQTT_TOPIC", f"application/{APP_ID}/device/{DEVEUI}/event/up").lower()
MQTT_USERNAME = os.environ.get("MQTT_USERNAME", "")
MQTT_PASSWORD = os.environ.get("MQTT_PASSWORD", "")
MQTT_USE_TLS = os.environ.get("MQTT_USE_TLS", "true").lower() == "true"

print("=" * 50)
print("Configuration Wardriving LoRaWAN")
print("=" * 50)
print(f"Broker MQTT  : {BROKER}:{PORT} (TLS: {MQTT_USE_TLS})")
print(f"Auth         : {'Oui' if MQTT_USERNAME else 'Non'}")
print(f"DevEUI       : {DEVEUI}")
print(f"Topic        : {TOPIC}")
print(f"Fichier      : {DATA_FILE}")
print("=" * 50)
print("Demarrage...")

# callbacks
def on_connect(client, userdata, flags, reason_code, properties=None):
    if reason_code == 0:
        print("Connecté au broker MQTT")
        client.subscribe(TOPIC)  # s'abonner après connexion
    else:
        print(f"Échec de connexion (code={reason_code})")

def on_message(client, userdata, msg):
    jsonMsg = json.loads(msg.payload)

    # Extraire SNR/RSSI de la meilleure passerelle
    best_snr = None
    best_rssi = None
    best_gateway = None
    for passerelle in jsonMsg.get('rxInfo', []):
        snr = passerelle.get('loRaSNR', passerelle.get('snr'))
        rssi = passerelle.get('rssi')
        print("\n-------------------")
        print("Passerelle :", passerelle.get('name', 'inconnu'))
        print("SNR :", snr)
        print("RSSI :", rssi)
        if best_snr is None or (snr is not None and snr > best_snr):
            best_snr = snr
            best_rssi = rssi
            best_gateway = passerelle.get('name', 'inconnu')

    # Recuperer la payload applicative en texte
    payload_text = None

    obj = jsonMsg.get("object", {})
    if isinstance(obj, dict) and "mydata" in obj:
        payload_text = obj["mydata"]
    else:
        # fallback: decoder le champ base64 "data"
        b64 = jsonMsg.get("data")
        if b64:
            try:
                payload_text = base64.b64decode(b64).decode("utf-8", errors="replace")
            except Exception:
                payload_text = None

    if not payload_text:
        # Debug: afficher la structure du message pour comprendre le format
        print("Debug - Structure du message recu:")
        print(f"  - Cles disponibles: {list(jsonMsg.keys())}")
        if "object" in jsonMsg:
            print(f"  - object: {jsonMsg['object']}")
        if "data" in jsonMsg:
            print(f"  - data (base64): {jsonMsg.get('data')}")
        print("Aucune payload applicative lisible (object.mydata absent et data invalide).")
        return

    print(f"Payload decodee: {payload_text}")

    # Parser JSON et extraire GPS
    try:
        app = json.loads(payload_text)
    except json.JSONDecodeError:
        print("Payload non-JSON recue :", payload_text)
        return

    # Verifier si c'est une position GPS valide
    # Le M5Stack envoie {"type":"gps","lat":...,"lon":...} quand il y a un fix
    # et {"type":"gps","fix":false} quand il n'y a pas de fix
    if app.get("type") == "gps" and "lat" in app and "lon" in app:
        lat = app["lat"]
        lon = app["lon"]
        sat = app.get("sat")
        age = app.get("age")
        print(f"GPS => lat={lat} lon={lon} sat={sat} age={age}ms")

        # Sauvegarder les donnees traitees pour la carte web
        entry = {
            "timestamp": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
            "latitude": lat,
            "longitude": lon,
            "snr": best_snr,
            "rssi": best_rssi,
            "gateway": best_gateway,
            "satellites": sat,
            "age_ms": age
        }
        with open(DATA_FILE, "a") as f:
            f.write(json.dumps(entry) + "\n")
        print(f"Donnees enregistrees dans {DATA_FILE}")
    else:
        print("GPS => pas de fix")

client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)

# Configuration TLS si active
if MQTT_USE_TLS:
    client.tls_set(tls_version=ssl.PROTOCOL_TLS_CLIENT)

# Authentification si configuree
if MQTT_USERNAME:
    client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)

client.on_connect = on_connect
client.on_message = on_message

client.connect(BROKER, PORT, keepalive=60)
client.loop_forever()