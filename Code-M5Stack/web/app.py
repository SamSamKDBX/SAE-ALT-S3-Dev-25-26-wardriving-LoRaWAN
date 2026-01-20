from flask import Flask, render_template, jsonify
import json
import os

app = Flask(__name__)
DATA_FILE = os.environ.get("DATA_FILE", "/data/data.json")


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/data")
def get_data():
    """Lire le fichier NDJSON et retourner en JSON array."""
    entries = []
    if os.path.exists(DATA_FILE):
        with open(DATA_FILE, "r") as f:
            for line in f:
                if line.strip():
                    try:
                        entries.append(json.loads(line))
                    except json.JSONDecodeError:
                        continue
    return jsonify(entries)


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000)
