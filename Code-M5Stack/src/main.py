import paho.mqtt.client as mqtt
import json
import ssl
import base64
import os
import re

# Lire la configuration depuis config.h
config_h_path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "config.h")
with open(config_h_path, "r") as f:
    content = f.read()
    # Extraire la valeur de M5STACK_ID depuis config.h
    match = re.search(r'#define\s+M5STACK_ID\s+0x([0-9A-Fa-f]+)', content)
    if match:
        M5STACK_ID = match.group(1).lower()
    else:
        raise ValueError("M5STACK_ID non trouvé dans config.h")

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

    # Sauvegarde dans un fichier
    with open("data.json", "a") as f:
        f.write(json.dumps(jsonMsg) + "\n")

    # Affichage RF (SNR/RSSI)
    for passerelle in jsonMsg.get('rxInfo', []):
        print("\n-------------------")
        print("Passerelle :", passerelle['name'])
        print("SNR :", passerelle.get('loRaSNR'))
        print("RSSI :", passerelle.get('rssi'))

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

    if app.get("type") == "gps" and app.get("fix", True):
        lat = app.get("lat")
        lon = app.get("lon")
        sat = app.get("sat")
        age = app.get("age")
        print(f"GPS => lat={lat} lon={lon} sat={sat} age={age}ms")
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