import paho.mqtt.client as mqtt
import json
import ssl
import base64
import os
import re
from datetime import datetime

# Chemin du fichier de donnees (configurable via variable d'environnement)
DATA_FILE = os.environ.get("DATA_FILE", "data.json")

# Lire la configuration depuis config.h
# Chercher d'abord dans /app (Docker), sinon chemin relatif (local)
config_h_paths = [
    "/app/config.h",
    os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "config.h")
]

config_h_path = None
for path in config_h_paths:
    if os.path.exists(path):
        config_h_path = path
        break

if not config_h_path:
    raise FileNotFoundError("config.h non trouve")

with open(config_h_path, "r") as f:
    content = f.read()
    # Extraire la valeur de M5STACK_ID depuis config.h
    match = re.search(r'#define\s+M5STACK_ID\s+0x([0-9A-Fa-f]+)', content)
    if match:
        M5STACK_ID = match.group(1).lower()
    else:
        raise ValueError("M5STACK_ID non trouve dans config.h")

# config
BROKER = "chirpstack.iut-blagnac.fr"
DEVEUI = "0c7e4501020301" + M5STACK_ID
PORT = 8883
TOPIC = "application/6/device/" + DEVEUI + "/event/up"

print("Starting...")

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
            "timestamp": datetime.utcnow().isoformat() + "Z",
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

# On veut TLS
client.tls_set(tls_version=ssl.PROTOCOL_TLS_CLIENT)

# Si besoin d'ids
# client.username_pw_set("utilisateur", "motdepasse")

client.on_connect = on_connect
client.on_message = on_message

client.connect(BROKER, PORT, keepalive=60)
client.loop_forever()