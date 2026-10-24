
import random
import sys
import time
from datetime import datetime, timezone

import requests

URL = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:5000/leituras"
QTD = int(sys.argv[2]) if len(sys.argv) > 2 else 10

for i in range(QTD):
    temp = round(random.uniform(10, 35), 1)
    umid = round(random.uniform(30, 80), 1)
    luz = random.choice([0, 1])
    pres = random.choice([0, 1])

    prob = 0
    if 15 <= temp <= 30: prob += 25
    if 40 <= umid <= 70: prob += 25
    if luz: prob += 20
    if pres: prob += 30

    payload = {
        "timestamp": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "temperatura_c": temp,
        "umidade_pct": umid,
        "luminosidade": luz,
        "presenca": pres,
        "probabilidade_vida": float(prob),
    }
    try:
        r = requests.post(URL, json=payload, timeout=5)
        print(f"[{i+1}/{QTD}] {r.status_code} {r.json()}")
    except requests.RequestException as e:
        print(f"[{i+1}/{QTD}] falhou: {e}")
    time.sleep(2)
