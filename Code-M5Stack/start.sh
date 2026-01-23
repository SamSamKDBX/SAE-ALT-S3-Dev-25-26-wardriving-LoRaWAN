#!/bin/bash

# Creer le dossier de donnees s'il n'existe pas
mkdir -p /data

# Reinitialiser le fichier de donnees au demarrage
echo "Reinitialisation des donnees..."
> /data/data.json

# Lancer le collecteur MQTT en arriere-plan
python /app/collector.py &

# Lancer le serveur Flask
cd /app/web && python app.py
