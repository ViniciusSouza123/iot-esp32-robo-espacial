# iot-esp32-robo-espacial

Robô explorador espacial que busca indícios de vida extraterrestre em um planeta simulado.

## Objetivo

- O **robô** (ESP32) lê sensores, calcula a probabilidade de vida e envia um alerta por WhatsApp (CallMeBot) quando ela passa de 75%.
- O **controle remoto** (ESP32 + joystick) envia comandos de movimento e o comando de desligar/ligar.
- Um **backend em Python (Flask + SQLite)** recebe e armazena as leituras dos sensores com timestamp.

## Estrutura do repositório

```
.
├── robo/
│   └── robo.ino              # (sensores, servo, alerta, ESP-NOW)
├── controle/
│   └── controle.ino          # (joystick, botão, LEDs)
├── app.py                    # backend Flask (POST e GET /leituras)
├── schema.sql                # tabela leituras (SQLite)
├── simular_leituras.py       # simula o robô enviando leituras ao backend
├── requirements.txt          # dependências Python
└── secrets.example.h         # modelo das credenciais 
```

## Componentes

**Robô**

| Componente | Pino | Função |
|---|---|---|
| ESP32 | - | Microcontrolador |
| DHT11 | 21 | Temperatura e umidade |
| Módulo LDR (saída digital) | 22 | Luminosidade (claro/escuro) |
| Sensor PIR HC-SR501 | 19 | Presença |
| Servo | 15 | Movimento (um ângulo por comando) |
| LED verde | 23 | Robô ligado e normal |
| LED vermelho | 5 | Robô desligado ou em alerta |

**Controle remoto**

| Componente | Pino | Função |
|---|---|---|
| ESP32 | - | Microcontrolador |
| Joystick analógico | 32 (X) e 33 (Y) | Direção do robô |
| Botão | 18 | Liga/desliga o robô remotamente |
| LED verde | 23 | Controle conectado ao robô |
| LED vermelho | 22 | Robô desligado ou desconectado |


## Como montar o robô

1. Ligue os componentes ao ESP32 do robô conforme a tabela de pinos acima.
2. Copie `secrets.example.h` para `robo/secrets.h` e preencha o Wi-Fi, o número de telefone (55 + DDD + número, sem o primeiro 9) e a APIKEY do CallMeBot.
3. No Arduino IDE, instale as bibliotecas **ESP32Servo**, **DHT sensor library for ESPx (DHTesp)** e **UrlEncode**.
4. Abra `robo/robo.ino`, selecione a placa ESP32 e grave.
5. Repita para o controle: ligue os componentes, copie `secrets.example.h` para `controle/secrets.h` (só Wi-Fi e senha são usados) e grave `controle/controle.ino`. O MAC do robô em `controle.ino` (`macRobo`) deve ser o do ESP32.

### Regras do robô

- Probabilidade de vida (soma): temperatura entre 15 e 30 ºC (+25%), umidade entre 40 e 70% (+25%), luz adequada (+20%), presença detectada (+30%).
- Probabilidade até 75%: LED verde e mensagem "Exploração normal".
- Probabilidade acima de 75%: LED vermelho, alerta no Monitor Serial e mensagem no WhatsApp.
- O botão do controle liga e desliga o robô. Sem comando por 1 s, o robô para (failsafe).

## Como rodar o backend Python

Requisitos: Python 3.

```
pip install -r requirements.txt
python app.py
```

O servidor sobe em `http://127.0.0.1:5000`. O banco `leituras.db` é criado automaticamente a partir do `schema.sql`.

Se o comando `pip` não for reconhecido no Windows, use `python -m pip install -r requirements.txt`.

### Enviar leituras de teste

Em outro terminal, o simulador envia uma leitura a cada 2 segundos (o último número é a quantidade):

```
python simular_leituras.py http://127.0.0.1:5000/leituras 10
```

Cada envio deve responder `201`.



## Como consultar os dados salvos

Abra no navegador:

```
http://127.0.0.1:5000/leituras
```

A rota `GET /leituras` retorna as últimas 100 leituras, da mais recente para a mais antiga.

Formato de uma leitura (também é o JSON esperado no `POST /leituras`):

```
{
  "timestamp": "2025-09-02T14:35:00Z",
  "temperatura_c": 24.3,
  "umidade_pct": 55,
  "luminosidade": 1,
  "presenca": 1,
  "probabilidade_vida": 78.0
}
```

- `timestamp` é opcional: se não for enviado, o servidor preenche com a hora atual.
- Os demais campos são obrigatórios. Se algum faltar, a resposta é `400` com a lista dos campos que faltaram.
- `luminosidade` é `0` ou `1`, porque o módulo LDR usado tem saída digital.

## Credenciais

O arquivo `secrets.h` (Wi-Fi, telefone e APIKEY) **não é versionado**: está no `.gitignore`. Use o `secrets.example.h` como modelo.

## Autor

Vinícius Souza
