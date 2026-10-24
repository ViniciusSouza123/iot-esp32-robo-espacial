#include <ESP32Servo.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <UrlEncode.h>
#include <esp_now.h>
#include "DHTesp.h"
#include "secrets.h"   // WIFI_SSID, WIFI_PASS, PHONE_NUM e API_KEY (veja secrets.example.h)

// ================== CONFIGURAÇÕES ==================
const char* ssid     = WIFI_SSID;   // mesma rede do controle
const char* password = WIFI_PASS;

String phoneNumber = PHONE_NUM;  // 55 + DDD + número, sem o primeiro 9
String apiKey      = API_KEY;    // APIKEY recebida do CallMeBot

// Módulo LDR com saída digital (DO): o nível do pino quando há luz depende do módulo.
// Se o Monitor Serial mostrar "ESCURO" com luz acesa, troque LOW por HIGH.
// O limite de sensibilidade se ajusta no potenciômetro (parafuso) do próprio módulo.
#define NIVEL_CLARO LOW

// ================== PINOS ==================
#define PIN_DHT          21
#define PIN_LDR          22   // módulo LDR, saída digital (DO)
#define PIN_PIR          19   // HC-SR501 alimentado em VIN (5V); saída é 3,3V
#define PIN_LED_VERDE    23
#define PIN_LED_VERMELHO 5
// Servo (fio de sinal)
#define PIN_SERVO 15

// Ângulos do servo (0 a 90 graus), um ângulo para cada comando
#define ANG_PARADO   45
#define ANG_FRENTE   90
#define ANG_TRAS     0
#define ANG_ESQUERDA 25
#define ANG_DIREITA  65

// ================== PROTOCOLO (igual no controle) ==================
enum Cmd : uint8_t { CMD_PARAR = 0, CMD_FRENTE, CMD_TRAS, CMD_ESQUERDA, CMD_DIREITA, CMD_DESLIGAR, CMD_LIGAR };
enum EstadoRobo : uint8_t { ROBO_LIGADO = 0, ROBO_DESLIGADO, ROBO_ALERTA };

typedef struct { uint8_t cmd; } PacoteComando;
typedef struct { uint8_t estado; uint8_t prob; } PacoteStatus;

// ================== VARIÁVEIS ==================
DHTesp sensor;
Servo servo;
int angAtual = -1;

volatile uint8_t cmdRecebido = CMD_PARAR;
volatile unsigned long ultimoCmdMs = 0;
volatile bool comandoNovo = false;

uint8_t macControle[6];
volatile bool temControle = false;
volatile bool precisaAddPeer = false;
bool peerAdicionado = false;

bool ligado = true;
uint8_t estado = ROBO_LIGADO;
uint8_t probabilidade = 0;
bool presencaLatch = false;

unsigned long ultimoCicloMs = 0, ultimoStatusMs = 0, ultimoAlertaMs = 0;
bool alertaEnviadoUmaVez = false;

// ================== SERVO ==================
// Só escreve no servo quando o ângulo muda (evita tremor)
void irPara(int ang) {
  if (ang != angAtual) { servo.write(ang); angAtual = ang; }
}

void parar() { irPara(ANG_PARADO); }

void mover(uint8_t cmd) {
  switch (cmd) {
    case CMD_FRENTE:   irPara(ANG_FRENTE);   break; // 90
    case CMD_TRAS:     irPara(ANG_TRAS);     break; // 0
    case CMD_ESQUERDA: irPara(ANG_ESQUERDA); break; // 25
    case CMD_DIREITA:  irPara(ANG_DIREITA);  break; // 65
    default:           parar();              break; // 45
  }
}

// ================== ESP-NOW ==================
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
  const uint8_t *mac = info->src_addr;
#else
void onRecv(const uint8_t *mac, const uint8_t *data, int len)
{
#endif
  if (len != sizeof(PacoteComando)) return;
  PacoteComando p;
  memcpy(&p, data, sizeof(p));
  cmdRecebido = p.cmd;
  ultimoCmdMs = millis();
  comandoNovo = true;
  if (!temControle) {
    memcpy(macControle, mac, 6);
    temControle = true;
    precisaAddPeer = true;
  }
}

void enviarStatus() {
  if (!peerAdicionado) return;
  PacoteStatus s = { estado, probabilidade };
  esp_now_send(macControle, (uint8_t*)&s, sizeof(s));
}

// ================== CALLMEBOT (HTTP) ==================
void sendMessage(String message) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Sem WiFi: mensagem nao enviada.");
    return;
  }
  String url = "https://api.callmebot.com/whatsapp.php?phone=" + phoneNumber +
               "&apikey=" + apiKey + "&text=" + urlEncode(message);

  // Versão com a chave escondida, só para mostrar no Serial
  String urlSegura = "https://api.callmebot.com/whatsapp.php?phone=" + phoneNumber +
                     "&apikey=XXXXXXX&text=" + urlEncode(message);
  Serial.println("URL enviada: " + urlSegura);

  HTTPClient http;
  http.begin(url);
  int httpResponseCode = http.GET();
  String resposta = http.getString();

  Serial.print("HTTP response code: ");
  Serial.println(httpResponseCode);
  Serial.println("Resposta do CallMeBot:");
  Serial.println(resposta);

  if (httpResponseCode == 200) {
    Serial.println("Mensagem enviada com sucesso!");
  } else {
    Serial.println("Erro na mensagem!");
  }
  http.end();
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);

  servo.setPeriodHertz(50);
  servo.attach(PIN_SERVO, 500, 2400);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_VERMELHO, OUTPUT);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_LDR, INPUT);
  parar();

  sensor.setup(PIN_DHT, DHTesp::DHT11);

  WiFi.mode(WIFI_STA);
  Serial.print("MAC deste robo: ");
  Serial.println(WiFi.macAddress());

  WiFi.begin(ssid, password);
  Serial.print("Conectando");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Conectado na rede WiFi com IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("Canal WiFi: ");
  Serial.println(WiFi.channel());

  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao iniciar ESP-NOW");
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(onRecv);
  Serial.println("Robo pronto. Aguardando comandos do controle...");
}

// ================== LOOP ==================
void loop() {
  // Registra o controle como peer (para responder o status)
  if (precisaAddPeer && !peerAdicionado) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, macControle, 6);
    peer.channel = 0;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) == ESP_OK) {
      peerAdicionado = true;
      Serial.println("Controle registrado.");
    }
    precisaAddPeer = false;
  }

  // Processa comando recebido
  if (comandoNovo) {
    comandoNovo = false;
    uint8_t c = cmdRecebido;
    if (c == CMD_DESLIGAR) {
      if (ligado) Serial.println("Comando recebido: DESLIGAR");
      ligado = false;
    } else if (c == CMD_LIGAR) {
      if (!ligado) Serial.println("Comando recebido: LIGAR");
      ligado = true;
    }
  }

  // Failsafe: sem comando por 1 s => servo volta para o ângulo de parado
  bool semSinal = (millis() - ultimoCmdMs) > 1000;
  if (!ligado || semSinal) {
    parar();
  } else {
    mover(cmdRecebido);
  }

  // PIR: guarda se houve movimento desde o último ciclo
  if (digitalRead(PIN_PIR)) presencaLatch = true;

  // Ciclo de sensores a cada 2 s
  if (millis() - ultimoCicloMs >= 2000) {
    ultimoCicloMs = millis();

    TempAndHumidity d = sensor.getTempAndHumidity();
    float temp = d.temperature;
    float umid = d.humidity;
    bool luzOk = (digitalRead(PIN_LDR) == NIVEL_CLARO);
    bool presenca = presencaLatch;
    presencaLatch = false;
    bool dhtOk = (sensor.getStatus() == 0) && !isnan(temp) && !isnan(umid);

    // ---- "Aprendizado de máquina" simples: modelo de regras ponderadas ----
    int prob = 0;
    if (dhtOk && temp >= 15 && temp <= 30) prob += 25;
    if (dhtOk && umid >= 40 && umid <= 70) prob += 25;
    if (luzOk)                              prob += 20;
    if (presenca)                           prob += 30;
    probabilidade = prob;

    // ---- Estado ----
    if (!ligado)          estado = ROBO_DESLIGADO;
    else if (prob > 75)   estado = ROBO_ALERTA;
    else                  estado = ROBO_LIGADO;

    // ---- LEDs ----
    bool vermelho = (estado == ROBO_DESLIGADO || estado == ROBO_ALERTA);
    digitalWrite(PIN_LED_VERMELHO, vermelho);
    digitalWrite(PIN_LED_VERDE, !vermelho);

    // ---- Monitor Serial ----
    Serial.println("------------------------------");
    if (dhtOk) {
      Serial.print("Temperatura: "); Serial.print(temp, 1); Serial.println(" C");
      Serial.print("Umidade: ");     Serial.print(umid, 1); Serial.println(" %");
    } else {
      Serial.println("Erro na leitura do DHT11");
    }
    Serial.print("Luz: "); Serial.println(luzOk ? "CLARO (adequada)" : "ESCURO");
    Serial.println(presenca ? "Presenca detectada" : "Sem presenca");
    Serial.print("Estado do robo: ");
    Serial.println(estado == ROBO_DESLIGADO ? "DESLIGADO" : (estado == ROBO_ALERTA ? "ALERTA" : "LIGADO"));
    Serial.print("Probabilidade de vida: "); Serial.print(prob); Serial.println("%");

    if (estado == ROBO_ALERTA) {
      Serial.println("ALERTA! Alta probabilidade de vida detectada!");
      // Envia ao entrar em alerta e no máximo 1x a cada 60 s
      if (!alertaEnviadoUmaVez || millis() - ultimoAlertaMs > 60000) {
        parar();
        sendMessage("Alerta! Alta probabilidade de vida detectada no planeta.");
        alertaEnviadoUmaVez = true;
        ultimoAlertaMs = millis();
      }
    } else if (estado == ROBO_LIGADO) {
      Serial.println("Exploracao normal. Nenhum indicio relevante detectado.");
      alertaEnviadoUmaVez = false;
    }
  }

  // Status para o controle a cada 500 ms (também é o heartbeat)
  if (millis() - ultimoStatusMs >= 500) {
    ultimoStatusMs = millis();
    enviarStatus();
  }
}
