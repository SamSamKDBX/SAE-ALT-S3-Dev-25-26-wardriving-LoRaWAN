import paho.mqtt.client as mqtt
import json
import ssl
import base64
import os
import re

# --- Config sortie fichier (Docker-friendly) ---
OUTPUT_PATH = os.getenv("OUTPUT_PATH", "data.json")

# --- Config MQTT (surchargable en env) ---
BROKER = os.getenv("BROKER", "chirpstack.iut-blagnac.fr")
PORT = int(os.getenv("PORT", "8883"))
APP_ID = os.getenv("APP_ID", "6")

def _find_config_h() -> str:
    # 1) chemin explicite (Docker/compose)
    env_path = os.getenv("CONFIG_H_PATH")
    if env_path and os.path.exists(env_path):
        return env_path

    # 2) chemins “classiques” (local + docker)
    script_dir = os.path.dirname(os.path.abspath(__file__))

    candidates = [
        os.path.join(script_dir, "config.h"),                 # si main.py et config.h au même niveau
        os.path.join(os.path.dirname(script_dir), "config.h"),# si main.py est dans src/
        "/app/config.h",                                      # cas courant Docker
        "./config.h",
    ]
    for p in candidates:
        if os.path.exists(p):
            return p

    raise FileNotFoundError(
        "config.h introuvable. Fournis CONFIG_H_PATH=/chemin/config.h ou DEVEUI en variable d'environnement."
    )

# DEVEUI: soit en env, soit dérivé de config.h (comportement actuel)
DEVEUI = os.getenv("DEVEUI")
if not DEVEUI:
    config_h_path = _find_config_h()
    with open(config_h_path, "r", encoding="utf-8") as f:
        content = f.read()
        match = re.search(r"#define\s+M5STACK_ID\s+0x([0-9A-Fa-f]+)", content)
        if match:
            m5stack_id = match.group(1).lower()
        else:
            raise ValueError("M5STACK_ID non trouvé dans config.h")

    DEVEUI = "0c7e4501020301" + m5stack_id

TOPIC = os.getenv("TOPIC", f"application/{APP_ID}/device/{DEVEUI}/event/up")

print("Starting...")
print(f"MQTT => {BROKER}:{PORT}")
print(f"TOPIC => {TOPIC}")
print(f"OUTPUT => {OUTPUT_PATH}")

# callbacks
def on_connect(client, userdata, flags, reason_code, properties=None):
    if reason_code == 0:
        print("Connecté au broker MQTT")
        client.subscribe(TOPIC)
    else:
        print(f"Échec de connexion (code={reason_code})")

def on_message(client, userdata, msg):
    jsonMsg = json.loads(msg.payload)

    # Sauvegarde dans un fichier (NDJSON)
    os.makedirs(os.path.dirname(OUTPUT_PATH) or ".", exist_ok=True)
    with open(OUTPUT_PATH, "a", encoding="utf-8") as f:
        f.write(json.dumps(jsonMsg) + "\n")

    # Affichage RF (SNR/RSSI)
    print("\n-----------Nouvelles données------------")
    for passerelle in jsonMsg.get("rxInfo", []):
        print("Passerelle :", passerelle.get("name"))
        print("SNR :", passerelle.get("loRaSNR"))
        print("RSSI :", passerelle.get("rssi"))
        print("\n-------------------\n")

    # Récupérer la payload applicative en texte
    payload_text = None

    obj = jsonMsg.get("object")
    if isinstance(obj, dict) and "mydata" in obj:
        payload_text = obj["mydata"]
    else:
        # fallback: décoder le champ base64 "data"
        b64 = jsonMsg.get("data")
        if b64:
            try:
                payload_text = base64.b64decode(b64).decode("utf-8", errors="replace")
            except Exception:
                payload_text = None

    if not payload_text:
        print("Aucune payload applicative lisible (object.mydata absent et data invalide).")
        return

    # Parser JSON et afficher GPS
    try:
        app = json.loads(payload_text)
    except json.JSONDecodeError:
        print("Payload non-JSON reçue :", payload_text)
        return

    # IMPORTANT: quand le GPS est OK, le M5Stack envoie souvent sans champ "fix"
    gps_fix = app.get("fix", True)

    if app.get("type") == "gps" and gps_fix:
        print(f"GPS => lat={app.get('lat')} lon={app.get('lon')} sat={app.get('sat')} age={app.get('age')}ms")
    else:
        print("GPS => pas de fix")

client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)

# TLS (on garde par défaut)
client.tls_set(tls_version=ssl.PROTOCOL_TLS_CLIENT)

client.on_connect = on_connect
client.on_message = on_message

client.connect(BROKER, PORT, keepalive=60)
client.loop_forever()