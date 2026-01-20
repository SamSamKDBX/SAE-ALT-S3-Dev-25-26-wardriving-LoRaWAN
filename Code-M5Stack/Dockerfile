FROM python:3.12-slim

RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY src/main.py /app/collector.py
COPY web/ /app/web/

ENV PYTHONUNBUFFERED=1
ENV DATA_FILE=/data/data.json

# Script de demarrage qui lance les deux processus
COPY start.sh /app/start.sh
RUN chmod +x /app/start.sh

EXPOSE 5000

CMD ["/app/start.sh"]
