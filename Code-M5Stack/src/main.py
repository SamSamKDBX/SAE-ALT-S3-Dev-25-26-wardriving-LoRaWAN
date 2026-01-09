import paho.mqtt.client as mqtt
import json
import ssl

# config
BROKER = "chirpstack.iut-blagnac.fr"
DEVEUI = "0c7e450102030106"
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
    
    # Pour chaque passerelle
    for passerelle in jsonMsg['rxInfo']:
        # Récupération de la qualité du réseau
        print("\n-------------------")
        print("Passerelle :", passerelle['name'])
        print("SNR :", passerelle['loRaSNR'])
        print("RSSI :", passerelle['rssi'])

client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)

# On veut TLS
client.tls_set(tls_version=ssl.PROTOCOL_TLS_CLIENT)

# Si besoin d'ids
# client.username_pw_set("utilisateur", "motdepasse")

client.on_connect = on_connect
client.on_message = on_message

client.connect(BROKER, PORT, keepalive=60)
client.loop_forever()