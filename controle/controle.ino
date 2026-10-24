#include <WiFi.h>
#include <esp_now.h>
#include "secrets.h"   // WIFI_SSID e WIFI_PASS (veja secrets.example.h)

// ================== CONFIGURAÇÕES ==================
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASS;

// MAC do ESP32 do ROBÔ
uint8_t macRobo[6] = {0x00, 0x70, 0x07, 0xE6, 0xFF, 0x84};

// ================== PINOS ==================
#define PIN_JOY_X        32   // VRx (ADC1, funciona com WiFi)
#define PIN_JOY_Y        33   // VRy (ADC1, funciona com WiFi)
#define PIN_BTN          18   // botão -> GND (INPUT_PULLUP)
#define PIN_LED_VERDE    23   // LED + resistor 220R
#define PIN_LED_VERMELHO 22

// ================== PROTOCOLO (igual no robô) ==================
enum Cmd : uint8_t { CMD_PARAR = 0, CMD_FRENTE, CMD_TRAS, CMD_ESQUERDA, CMD_DIREITA, CMD_DESLIGAR, CMD_LIGAR };
enum EstadoRobo : uint8_t { ROBO_LIGADO = 0, ROBO_DESLIGADO, ROBO_ALERTA };

typedef struct { uint8_t cmd; } PacoteComando;
typedef struct { uint8_t estado; uint8_t prob; } PacoteStatus;

// ================== AJUSTES DO JOYSTICK ==================
const int CENTRO   = 2048;
const int ZONA_MORTA = 700;   // ajuste conforme seu joystick

// ================== VARIÁVEIS ==================
volatile unsigned long ultimoStatusMs = 0;
volatile uint8_t estadoRobo = ROBO_DESLIGADO;
volatile uint8_t probRobo = 0;

bool desligarAtivo = false;
int  repetirLigar = 0;
uint8_t ultimoCmdEnviado = 255;
unsigned long ultimoEnvioMs = 0, ultimoPrintMs = 0, ultimoBtnMs = 0;
bool btnAnterior = HIGH;

const char* nomeCmd(uint8_t c) {
  switch (c) {
    case CMD_FRENTE:   return "Frente";
    case CMD_TRAS:     return "Tras";
    case CMD_ESQUERDA: return "Esquerda";
    case CMD_DIREITA:  return "Direita";
    case CMD_DESLIGAR: return "DESLIGAR";
    case CMD_LIGAR:    return "LIGAR";
    default:           return "Parado";
  }
}

// Callback de recepção (assinatura muda entre core ESP32 2.x e 3.x)
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
#else
void onRecv(const uint8_t *mac, const uint8_t *data, int len)
#endif
{
  if (len == sizeof(PacoteStatus)) {
    PacoteStatus s;
    memcpy(&s, data, sizeof(s));
    estadoRobo = s.estado;
    probRobo = s.prob;
    ultimoStatusMs = millis();
  }
}

bool roboConectado() {
  return ultimoStatusMs != 0 && (millis() - ultimoStatusMs) < 3000;
}

uint8_t lerJoystick() {
  int x = analogRead(PIN_JOY_X) - CENTRO;
  int y = analogRead(PIN_JOY_Y) - CENTRO;
  if (abs(x) < ZONA_MORTA && abs(y) < ZONA_MORTA) return CMD_PARAR;
  if (abs(y) >= abs(x)) {
    // Frente/trás já corrigidos para o seu joystick
    return (y > 0) ? CMD_TRAS : CMD_FRENTE;
  } else {
    return (x > 0) ? CMD_DIREITA : CMD_ESQUERDA;
  }
}

void enviar(uint8_t cmd) {
  PacoteComando p = { cmd };
  esp_now_send(macRobo, (uint8_t*)&p, sizeof(p));
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_VERMELHO, OUTPUT);
  digitalWrite(PIN_LED_VERMELHO, HIGH);

  WiFi.mode(WIFI_STA);
  Serial.print("MAC deste controle: ");
  Serial.println(WiFi.macAddress());

  // Conecta no WiFi só para alinhar o canal com o robô
  WiFi.begin(ssid, password);
  Serial.print("Conectando no WiFi");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi OK, canal ");
    Serial.println(WiFi.channel());
  } else {
    Serial.println("AVISO: sem WiFi. O ESP-NOW pode falhar se o robo estiver em outro canal.");
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao iniciar ESP-NOW");
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(onRecv);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, macRobo, 6);
  peer.channel = 0;      // 0 = canal atual
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Erro ao adicionar peer (robo)");
  }
  Serial.println("Controle pronto.");
}

void loop() {
  // ---- Botão (liga/desliga) com debounce simples ----
  bool btn = digitalRead(PIN_BTN);
  if (btnAnterior == HIGH && btn == LOW && millis() - ultimoBtnMs > 250) {
    ultimoBtnMs = millis();
    desligarAtivo = !desligarAtivo;
    if (desligarAtivo) {
      Serial.println("Comando enviado: DESLIGAR");
    } else {
      Serial.println("Comando enviado: LIGAR");
      repetirLigar = 5;  // repete para garantir a entrega
    }
  }
  btnAnterior = btn;

  // ---- Envio de comando a cada 100 ms ----
  if (millis() - ultimoEnvioMs >= 100) {
    ultimoEnvioMs = millis();
    uint8_t cmd;
    if (desligarAtivo) {
      cmd = CMD_DESLIGAR;
    } else if (repetirLigar > 0) {
      cmd = CMD_LIGAR;
      repetirLigar--;
    } else {
      cmd = lerJoystick();
    }
    enviar(cmd);

    if (cmd != ultimoCmdEnviado) {
      ultimoCmdEnviado = cmd;
      Serial.print("Comando: ");
      Serial.println(nomeCmd(cmd));
    }
  }

  // ---- LEDs de status ----
  bool conectado = roboConectado();
  bool verde = conectado && estadoRobo != ROBO_DESLIGADO;
  digitalWrite(PIN_LED_VERDE, verde ? HIGH : LOW);
  digitalWrite(PIN_LED_VERMELHO, verde ? LOW : HIGH);

  // ---- Monitor serial a cada 1 s ----
  if (millis() - ultimoPrintMs >= 1000) {
    ultimoPrintMs = millis();
    Serial.print("Ultimo comando: ");
    Serial.print(nomeCmd(ultimoCmdEnviado));
    Serial.print(" | Robo: ");
    if (!conectado) {
      Serial.println("DESCONECTADO (LED vermelho)");
    } else if (estadoRobo == ROBO_DESLIGADO) {
      Serial.println("DESLIGADO (LED vermelho)");
    } else if (estadoRobo == ROBO_ALERTA) {
      Serial.print("LIGADO em ALERTA (LED verde) | Prob. vida: ");
      Serial.print(probRobo);
      Serial.println("%");
    } else {
      Serial.print("LIGADO (LED verde) | Prob. vida: ");
      Serial.print(probRobo);
      Serial.println("%");
    }
  }
}
