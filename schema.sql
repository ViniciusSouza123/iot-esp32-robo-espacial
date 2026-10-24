CREATE TABLE IF NOT EXISTS leituras (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp         TEXT    NOT NULL,
    temperatura_c     REAL,
    umidade_pct       REAL,
    luminosidade      INTEGER,
    presenca          INTEGER,
    probabilidade_vida REAL
);
