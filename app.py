import os
import sqlite3
from datetime import datetime, timezone

from flask import Flask, g, jsonify, request

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DB_PATH = os.path.join(BASE_DIR, "leituras.db")
SCHEMA_PATH = os.path.join(BASE_DIR, "schema.sql")

CAMPOS = ["temperatura_c", "umidade_pct", "luminosidade", "presenca", "probabilidade_vida"]

app = Flask(__name__)


def get_db():
    if "db" not in g:
        g.db = sqlite3.connect(DB_PATH)
        g.db.row_factory = sqlite3.Row
    return g.db


@app.teardown_appcontext
def close_db(_exc):
    db = g.pop("db", None)
    if db is not None:
        db.close()


def init_db():
    with sqlite3.connect(DB_PATH) as conn, open(SCHEMA_PATH, encoding="utf-8") as f:
        conn.executescript(f.read())


@app.route("/leituras", methods=["POST"])
def criar_leitura():
    dados = request.get_json(silent=True)
    if not isinstance(dados, dict):
        return jsonify(erro="JSON inválido"), 400

    faltando = [c for c in CAMPOS if c not in dados]
    if faltando:
        return jsonify(erro="Campos faltando", campos=faltando), 400

    # O ESP32 não tem relógio: se não mandar timestamp, o servidor preenche.
    ts = dados.get("timestamp") or datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    db = get_db()
    cur = db.execute(
        """INSERT INTO leituras
           (timestamp, temperatura_c, umidade_pct, luminosidade, presenca, probabilidade_vida)
           VALUES (?, ?, ?, ?, ?, ?)""",
        (ts, dados["temperatura_c"], dados["umidade_pct"], dados["luminosidade"],
         dados["presenca"], dados["probabilidade_vida"]),
    )
    db.commit()
    return jsonify(status="ok", id=cur.lastrowid, timestamp=ts), 201


@app.route("/leituras", methods=["GET"])
def listar_leituras():
    linhas = get_db().execute(
        "SELECT * FROM leituras ORDER BY id DESC LIMIT 100"
    ).fetchall()
    return jsonify([dict(l) for l in linhas])


if __name__ == "__main__":
    init_db()
    # host 0.0.0.0 permite o ESP32 (mesma rede) acessar o PC
    app.run(host="0.0.0.0", port=5000, debug=False)
