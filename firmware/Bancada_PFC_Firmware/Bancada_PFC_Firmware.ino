/*
  Bancada PFC - Prova de conceito (motor KTC-42BLS04 + driver KTC-BLD120-J)
  Placa: ESP32 DevKit comum (ESP32-WROOM-32).

  Funcoes:
   1) Diagnostico estatico: resistencia dos 3 pares (U-V, V-W, W-U) por 4 fios
      (metodo Kelvin), com calculo ratiometrico, desequilibrio entre pares e decisao
      APROVADO / REPROVADO.
      - Corrente de medicao: 3V3 -> R_lim -> reles -> par de fases -> reles -> MOSFET
        -> R_ref -> GND. O par e selecionado por um modulo de 4 reles (5 V, optoacoplado,
        ativo em nivel BAIXO, contatos NA): U-V = IN1+IN2 | V-W = IN2+IN3 | W-U = IN1+IN4.
        Em repouso, todos abertos.
      - Tensao (fios finos direto nos terminais do motor, sem passar pelos reles):
        AIN0 = U, AIN1 = V, AIN3 = W  ->  U-V = AIN0-AIN1 | V-W = AIN1-AIN3 | W-U = AIN0-AIN3.
      - R_ref abaixo do MOSFET, lido em AIN2 em relacao ao GND do ADS1115 (GND do ADS
        ligado direto na ponta de baixo do R_ref).
   2) Ensaio sob carga leve (somente se APROVADO): corrente e tensao do barramento
      (INA226), linha de base, alerta de aumento de corrente, teto de ensaio e
      deteccao de provavel atuacao da protecao do driver -> TRAVADO.
   3) TRAVADO so e liberado pelo BOTAO FISICO (pressionar 2 s). Depois disso o
      sistema volta para AGUARDANDO e exige novo diagnostico estatico.
   4) Interface: OLED na bancada + pagina web servida pelo proprio ESP32
      (Wi-Fi "Bancada-PFC", endereco http://192.168.4.1), com grafico ao vivo,
      configuracoes editaveis e download dos dados em CSV.

  Bibliotecas (Gerenciador de Bibliotecas da Arduino IDE):
   - Adafruit ADS1X15
   - INA226 (Rob Tillaart)
   - Adafruit SH110X  (+ Adafruit GFX Library)
*/

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Adafruit_ADS1X15.h>
#include <INA226.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// ----------------------------- Pinos ---------------------------------------
// ESP32 DevKit comum (ESP32-WROOM-32)
const int PIN_SDA   = 21;
const int PIN_SCL   = 22;
const int PIN_GATE  = 4;   // MOSFET da corrente de medicao
const int PIN_LED   = 26;   // LED de estado
const int PIN_BOTAO = 27;   // botao de liberacao da trava (para GND)

// Modulo de 4 reles (IN1..IN4), ativo em nivel BAIXO: LOW = rele ligado.
//   K1 (IN1): U ao lado +      K2 (IN2): V ao lado -
//   K3 (IN3): W ao lado +      K4 (IN4): W ao lado -
// Leitura Kelvin: fios finos dos terminais U, V e W direto em AIN0, AIN1 e AIN3.
const int PIN_K[4] = {16, 17, 18, 19};          // IN1, IN2, IN3, IN4
const int RELE_LIGA = LOW, RELE_DESLIGA = HIGH;
const uint32_t RELE_ASSENTAMENTO_MS = 50;       // comutacao + repique dos contatos

// Declarado aqui no topo porque a Arduino IDE gera os prototipos das funcoes
// antes do primeiro codigo; o tipo precisa existir antes de nomeEstado(Estado).
enum Estado { AGUARDANDO, APROVADO, REPROVADO, CARGA, TRAVADO };

// ----------------------------- Wi-Fi ---------------------------------------
const char* AP_SSID = "Bancada-PFC";
const char* AP_SENHA = "TROQUE_A_SENHA";  // defina uma senha propria (minimo 8 caracteres)

// ----------------------------- Objetos -------------------------------------
Adafruit_ADS1115 ads;
INA226 ina(0x40);
Adafruit_SH1106G oled(128, 64, &Wire, -1);
WebServer server(80);
Preferences prefs;

bool temADS = false, temINA = false, temOLED = false;
const bool temDS = false;   // sem sensor de temperatura nesta fase

// ----------------------------- Configuracao --------------------------------
struct Config {
  float rRef      = 1.000;   // ohm  - resistor de referencia (medido/calibrado)
  float rNom      = 0.000;   // ohm  - resistencia nominal entre fases a 20 C (0 = nao verificar)
  float tolNom    = 20.0;    // %    - tolerancia em relacao ao nominal
  float deseqMax  = 5.0;     // %    - desequilibrio maximo entre os 3 pares (provisorio)
  float iTeto     = 4.0;     // A    - teto de ensaio no barramento
  float fatorAlerta = 2.0;   // x    - alerta quando I > fator x linha de base
  float shunt     = 0.0091;  // ohm  - shunt do INA226: 2 x 0,02 ohm em paralelo (0,01) // R100 do modulo (calibrar)
  int   nMedias   = 32;      // amostras por leitura no ADS1115
} cfg;

void carregarConfig() {
  prefs.begin("bancada", true);
  cfg.rRef        = prefs.getFloat("rRef", cfg.rRef);
  cfg.rNom        = prefs.getFloat("rNom", cfg.rNom);
  cfg.tolNom      = prefs.getFloat("tolNom", cfg.tolNom);
  cfg.deseqMax    = prefs.getFloat("deseqMax", cfg.deseqMax);
  cfg.iTeto       = prefs.getFloat("iTeto", cfg.iTeto);
  cfg.fatorAlerta = prefs.getFloat("fator", cfg.fatorAlerta);
  cfg.shunt       = prefs.getFloat("shunt", cfg.shunt);
  cfg.nMedias     = prefs.getInt("nMedias", cfg.nMedias);
  prefs.end();
}

void salvarConfig() {
  prefs.begin("bancada", false);
  prefs.putFloat("rRef", cfg.rRef);
  prefs.putFloat("rNom", cfg.rNom);
  prefs.putFloat("tolNom", cfg.tolNom);
  prefs.putFloat("deseqMax", cfg.deseqMax);
  prefs.putFloat("iTeto", cfg.iTeto);
  prefs.putFloat("fator", cfg.fatorAlerta);
  prefs.putFloat("shunt", cfg.shunt);
  prefs.putInt("nMedias", cfg.nMedias);
  prefs.end();
}

// ----------------------------- Estado --------------------------------------
Estado estado = AGUARDANDO;
const char* nomeEstado(Estado e) {
  switch (e) {
    case AGUARDANDO: return "AGUARDANDO";
    case APROVADO:   return "APROVADO";
    case REPROVADO:  return "REPROVADO";
    case CARGA:      return "CARGA";
    case TRAVADO:    return "TRAVADO";
  }
  return "?";
}
String motivo = "Aguardando diagnostico estatico";

// Diagnostico estatico
const char* NOMES_PAR[3] = {"U-V", "V-W", "W-U"};
float rPar[3]    = {NAN, NAN, NAN};   // resistencia medida
float r20Par[3]  = {NAN, NAN, NAN};   // corrigida para 20 C
float tempMed[3] = {NAN, NAN, NAN};
bool  medido[3]  = {false, false, false};
float rMedia20 = NAN, deseqPct = NAN;

// Ensaio sob carga
float iAtual = 0, vAtual = 0, iBase = NAN, iPico = 0;
bool  alerta = false, tetoAtingido = false;
uint32_t tInicioCarga = 0, tUltimoAlerta = 0;
double somaBase = 0; uint32_t nBase = 0;
const uint32_t JANELA_BASE_MS = 3000;
const uint32_t PERIODO_CARGA_MS = 100;   // 10 Hz
uint32_t tUltimaAmostra = 0;

struct Amostra { uint32_t t; float i; float v; uint8_t flags; };
Amostra* buf = nullptr;
uint32_t bufCap = 0, bufN = 0;

// ----------------------------- Utilidades ----------------------------------
float lerTemperatura() {
  return NAN;                       // sem sensor: resistencia fica sem correcao
}

// Corrige para 20 C (cobre: 0,00393 / C). Sem temperatura, mantem o valor medido.
float corrigir20(float r, float t) {
  if (isnan(t)) return r;
  return r / (1.0f + 0.00393f * (t - 20.0f));
}

// ----------------------------- Reles ---------------------------------------
// So estas 3 combinacoes sao usadas (bit0 = IN1 ... bit3 = IN4):
//   par 0  U-V : IN1 + IN2      par 1  V-W : IN2 + IN3      par 2  W-U : IN1 + IN4
// Nunca dois reles do mesmo lado (IN1+IN3 ou IN2+IN4): ligaria duas fases entre si.
// Os reles so comutam com o MOSFET desligado (sem corrente nos contatos).
const uint8_t COMBO_PAR[3] = {0b0011, 0b0110, 0b1001};

void reles(uint8_t mascara) {
  for (int k = 0; k < 4; k++)
    digitalWrite(PIN_K[k], ((mascara >> k) & 1) ? RELE_LIGA : RELE_DESLIGA);
}

void relesRepouso() {                 // todas as fases soltas
  digitalWrite(PIN_GATE, LOW);
  delay(10);
  reles(0);
}

void selecionarPar(int idx) {
  relesRepouso();                     // abre tudo antes de fechar o novo par
  delay(RELE_ASSENTAMENTO_MS);
  reles(COMBO_PAR[idx]);
  delay(RELE_ASSENTAMENTO_MS);
}

// ----------------------------- INA226 ---------------------------------------
// O INA226 mede ate 81,92 mV entre IN+ e IN-. A corrente maxima configurada e
// limitada para que I_max x R_shunt fique abaixo desse valor (com folga):
//   R_shunt = 0,0091 ohm  ->  I_max = 0,0815 / 0,0091 = 8,96 A
// (Com o valor antigo de 16 A fixo, qualquer shunt acima de ~5 mohm era recusado.)
void configurarINA() {
  if (!temINA) return;
  float iMax = 0.0815f / cfg.shunt;
  if (iMax > 20.0f) iMax = 20.0f;
  int err = ina.setMaxCurrentShunt(iMax, cfg.shunt);
  Serial.printf("INA226: shunt %.5f ohm, faixa ate %.2f A (cod. %d)\n", cfg.shunt, iMax, err);
}

// ----------------------------- ADS1115 (Kelvin) -----------------------------
// Tensao no par selecionado, lida nos terminais do motor.
int16_t lerParBruto(int idx) {
  if (idx == 0) return ads.readADC_Differential_0_1();   // U-V = AIN0 - AIN1
  if (idx == 1) return ads.readADC_Differential_1_3();   // V-W = AIN1 - AIN3
  return ads.readADC_Differential_0_3();                 // W-U = AIN0 - AIN3
}
// Tensao no R_ref (AIN2 em relacao ao GND do ADS, ligado na ponta de baixo do R_ref)
int16_t lerRefBruto() { return ads.readADC_SingleEnded(2); }

// Retorna 0 = ok, 1 = circuito aberto, 2 = fora de faixa, 3 = ADS ausente
int medirPar(int idx) {
  if (!temADS) return 3;
  const int n = cfg.nMedias;
  double offC = 0, offR = 0, sC = 0, sR = 0;

  selecionarPar(idx);

  // 1) offset com corrente desligada (remove tensoes termoeletricas e offset do ADC)
  digitalWrite(PIN_GATE, LOW);
  delay(20);
  for (int k = 0; k < n; k++) {
    offC += ads.computeVolts(lerParBruto(idx));
    offR += ads.computeVolts(lerRefBruto());
  }
  // 2) com corrente
  digitalWrite(PIN_GATE, HIGH);
  delay(60);                       // estabilizacao (constante L/R do enrolamento)
  bool saturou = false;
  for (int k = 0; k < n; k++) {
    int16_t rc = lerParBruto(idx);
    int16_t rr = lerRefBruto();
    if (rc >= 32700 || rc <= -32700) saturou = true;
    sC += ads.computeVolts(rc);
    sR += ads.computeVolts(rr);
  }
  relesRepouso();                  // desliga a corrente e abre os reles

  float vCoil = fabsf((sC - offC) / n);
  float vRef  = (sR - offR) / n;
  float t = lerTemperatura();

  if (vRef < 0.010f) {             // < 10 mA no resistor de 1 ohm: nao circula corrente
    rPar[idx] = NAN; medido[idx] = true; tempMed[idx] = t;
    return 1;
  }
  if (saturou) {                   // tensao no par acima da escala (+/-0,256 V): fio fino solto ou trocado
    rPar[idx] = NAN; medido[idx] = true; tempMed[idx] = t;
    return 2;
  }
  rPar[idx] = cfg.rRef * vCoil / vRef; // calculo ratiometrico
  r20Par[idx] = corrigir20(rPar[idx], t);
  tempMed[idx] = t;
  medido[idx] = true;
  return 0;
}

void avaliarDiagnostico() {
  for (int k = 0; k < 3; k++) if (!medido[k]) return;   // aguarda os 3 pares

  for (int k = 0; k < 3; k++) {
    if (isnan(rPar[k])) {
      estado = REPROVADO;
      motivo = String("Par ") + NOMES_PAR[k] + ": circuito aberto ou fora de faixa";
      return;
    }
  }
  float mn = r20Par[0], mx = r20Par[0], soma = 0;
  for (int k = 0; k < 3; k++) {
    mn = min(mn, r20Par[k]); mx = max(mx, r20Par[k]); soma += r20Par[k];
  }
  rMedia20 = soma / 3.0f;
  deseqPct = (mx - mn) / rMedia20 * 100.0f;

  if (deseqPct > cfg.deseqMax) {
    estado = REPROVADO;
    motivo = "Desequilibrio " + String(deseqPct, 2) + " % > limite " + String(cfg.deseqMax, 1) + " %";
    return;
  }
  if (cfg.rNom > 0) {
    float desvio = fabs(rMedia20 - cfg.rNom) / cfg.rNom * 100.0f;
    if (desvio > cfg.tolNom) {
      estado = REPROVADO;
      motivo = "Media " + String(rMedia20, 4) + " ohm fora da tolerancia do nominal";
      return;
    }
  }
  estado = APROVADO;
  motivo = "Diagnostico estatico aprovado - acionamento sob carga liberado";
}

void limparDiagnostico() {
  for (int k = 0; k < 3; k++) { rPar[k] = r20Par[k] = tempMed[k] = NAN; medido[k] = false; }
  rMedia20 = deseqPct = NAN;
}

void iniciarCarga() {
  estado = CARGA;
  motivo = "Ensaio sob carga: medindo linha de base (motor em vazio)";
  iBase = NAN; iPico = 0; somaBase = 0; nBase = 0;
  alerta = false; tetoAtingido = false; tUltimoAlerta = 0;
  tInicioCarga = millis(); tUltimaAmostra = 0;
  bufN = 0;
}

void tratarCarga() {
  if (estado != CARGA || !temINA) return;
  uint32_t agora = millis();
  if (agora - tUltimaAmostra < PERIODO_CARGA_MS) return;
  tUltimaAmostra = agora;

  iAtual = ina.getCurrent();
  vAtual = ina.getBusVoltage();
  uint32_t t = agora - tInicioCarga;
  uint8_t flags = 0;

  if (t < JANELA_BASE_MS) {                      // linha de base
    somaBase += iAtual; nBase++;
  } else {
    if (isnan(iBase) && nBase > 0) {
      iBase = somaBase / nBase;
      motivo = "Linha de base " + String(iBase, 3) + " A - aplique a carga gradualmente";
    }
    if (iAtual > iPico) iPico = iAtual;
    alerta = (!isnan(iBase) && iAtual > cfg.fatorAlerta * iBase);
    if (alerta) { tUltimoAlerta = agora; flags |= 0x01; }
    
    // Aviso de teto acompanha a corrente atual
    if (iAtual >= cfg.iTeto) {
      flags |= 0x02;
      if (!tetoAtingido) motivo = "TETO DE ENSAIO ATINGIDO - alivie a carga";
      tetoAtingido = true;
    } else if (tetoAtingido && iAtual < 0.95f * cfg.iTeto) {
      tetoAtingido = false;
      motivo = "Carga aliviada - corrente abaixo do teto (pico " + String(iPico, 2) + " A)";
    }
    // Provavel atuacao da protecao do driver: corrente desaba logo apos alerta
    bool alertaRecente = (tUltimoAlerta > 0) && (agora - tUltimoAlerta < 2000);
    float limiarQueda = max(0.3f * iBase, 0.05f);
    if (alertaRecente && iAtual < limiarQueda) {
      flags |= 0x04;
      estado = TRAVADO;
      motivo = "Queda brusca de corrente apos sobrecarga (pico " + String(iPico, 2) +
               " A): provavel atuacao da protecao do driver. Liberar pelo botao fisico.";
    }
  }
  if (buf && bufN < bufCap) buf[bufN++] = {t, iAtual, vAtual, flags};
}

void encerrarCarga() {
  if (estado == CARGA) { estado = APROVADO; motivo = "Ensaio sob carga encerrado pelo operador"; }
}

// ----------------------------- Botao / LED / OLED --------------------------
uint32_t tBotaoPress = 0;
void tratarBotao() {
  bool press = (digitalRead(PIN_BOTAO) == LOW);
  if (press) {
    if (tBotaoPress == 0) tBotaoPress = millis();
    if (estado == TRAVADO && (millis() - tBotaoPress) > 2000) {
      limparDiagnostico();
      estado = AGUARDANDO;
      motivo = "Trava liberada no botao fisico - refaca o diagnostico estatico";
      tBotaoPress = 0;
      while (digitalRead(PIN_BOTAO) == LOW) delay(10);
    }
  } else {
    tBotaoPress = 0;
  }
}

void tratarLED() {
  uint32_t m = millis();
  bool on = false;
  switch (estado) {
    case AGUARDANDO: on = false; break;
    case APROVADO:   on = true; break;
    case REPROVADO:  on = (m / 500) % 2; break;
    case CARGA:      on = alerta ? (m / 120) % 2 : true; break;
    case TRAVADO:    on = (m / 120) % 2; break;
  }
  digitalWrite(PIN_LED, on ? HIGH : LOW);
}

uint32_t tOled = 0;
void atualizarOLED() {
  if (!temOLED || (millis() - tOled) < 250) return;
  tOled = millis();
  oled.clearDisplay();
  oled.setTextColor(SH110X_WHITE);
  oled.setTextSize(2);
  oled.setCursor(0, 0);
  oled.print(nomeEstado(estado));
  oled.setTextSize(1);
  if (estado == CARGA || estado == TRAVADO) {
    oled.setCursor(0, 22); oled.printf("I: %.3f A", iAtual);
    oled.setCursor(0, 32); oled.printf("V: %.2f V", vAtual);
    oled.setCursor(0, 42);
    if (isnan(iBase)) oled.print("Base: medindo...");
    else oled.printf("Base %.2f Pico %.2f", iBase, iPico);
    oled.setCursor(0, 54);
    oled.print(tetoAtingido ? "TETO! alivie carga" : (alerta ? "ALERTA corrente" : ""));
  } else {
    for (int k = 0; k < 3; k++) {
      oled.setCursor(0, 22 + (k * 10));
      if (!medido[k]) oled.printf("%s: --", NOMES_PAR[k]);
      else if (isnan(rPar[k])) oled.printf("%s: ABERTO/FAIXA", NOMES_PAR[k]);
      else oled.printf("%s: %.4f ohm", NOMES_PAR[k], rPar[k]);
    }
    oled.setCursor(0, 54);
    if (!isnan(deseqPct)) oled.printf("Deseq: %.2f %%", deseqPct);
    else oled.print("192.168.4.1");
  }
  oled.display();
}

// ----------------------------- Web: pagina ---------------------------------
const char PAGINA[] PROGMEM = R"HTML(
<!doctype html><html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bancada PFC</title>
<style>
:root{--bg:#f4f2fb;--card:#fff;--tx:#1b1446;--mut:#6b6690;--pri:#3b2a94;--ok:#1b8a4b;--bad:#c62828;--warn:#d97706;}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,sans-serif;background:var(--bg);color:var(--tx)}
header{background:var(--pri);color:#fff;padding:14px 16px}header h1{margin:0;font-size:18px}
main{max-width:900px;margin:0 auto;padding:12px 16px;display:grid;gap:12px}
.card{background:var(--card);border-radius:12px;padding:14px;box-shadow:0 1px 3px #0002}
h2{font-size:15px;margin:0 0 10px}
.badge{display:inline-block;padding:6px 12px;border-radius:20px;font-weight:700;color:#fff;background:var(--mut)}
.APROVADO,.CARGA{background:var(--ok)}.REPROVADO,.TRAVADO{background:var(--bad)}
table{width:100%;border-collapse:collapse;font-size:14px}td,th{padding:6px;border-bottom:1px solid #eee;text-align:left}
button{background:var(--pri);color:#fff;border:0;border-radius:8px;padding:9px 14px;font-size:14px;margin:3px 4px 3px 0;cursor:pointer}
button.sec{background:#e7e4f7;color:var(--pri)}button:disabled{opacity:.4;cursor:default}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:8px;align-items:end}
.kpi{background:#f7f6fd;border-radius:8px;padding:8px}.kpi b{display:block;font-size:20px}.kpi span{font-size:12px;color:var(--mut)}
canvas{width:100%;height:220px;background:#fbfaff;border-radius:8px}
label{display:block;font-size:13px;color:var(--mut);margin-top:6px}input{width:100%;padding:7px;border:1px solid #ccc;border-radius:6px}
#motivo{margin-top:8px;font-size:14px}.warn{color:var(--warn);font-weight:700}
</style></head><body>
<header><h1>Bancada PFC – Prova de conceito (KTC-42BLS04)</h1></header>
<main>
<div class="card"><h2>Estado</h2><span id="estado" class="badge">...</span><div id="motivo"></div></div>

<div class="card"><h2>1. Diagnóstico estático (motor desconectado do driver)</h2>
<button onclick="todos()">Medir os 3 pares</button><button class="sec" onclick="medir(0)">Remedir U–V</button><button class="sec" onclick="medir(1)">Remedir V–W</button><button class="sec" onclick="medir(2)">Remedir W–U</button>
<button class="sec" onclick="novo()">Novo ensaio</button>
<table><thead id="cab"></thead><tbody id="tab"></tbody></table>
<p><span id="lmedia">Média</span>: <b id="media">–</b> · Desequilíbrio: <b id="deseq">–</b></p>
<p id="nota" style="font-size:13px;color:#6b6690"></p></div>

<div class="card"><h2>2. Ensaio sob carga leve</h2>
<button id="bIni" onclick="carga(1)">Iniciar ensaio sob carga</button><button class="sec" onclick="carga(0)">Encerrar</button>
<div class="grid" style="margin-top:8px">
<div class="kpi"><span>Corrente</span><b id="i">–</b></div><div class="kpi"><span>Tensão</span><b id="v">–</b></div>
<div class="kpi"><span>Linha de base</span><b id="base">–</b></div><div class="kpi"><span>Pico</span><b id="pico">–</b></div></div>
<p id="alerta"></p><canvas id="cv"></canvas>
<p><a href="/api/csv">Baixar dados do ensaio (CSV)</a></p></div>

<div class="card"><h2>Configurações</h2><form id="fcfg" onsubmit="return salvar(event)">
<div class="grid">
<div><label>R_ref (Ω)</label><input name="rRef" step="any" type="number"></div>
<div><label>R nominal entre fases (Ω, 0 = não usar)</label><input name="rNom" step="any" type="number"></div>
<div><label>Tolerância do nominal (%)</label><input name="tolNom" step="any" type="number"></div>
<div><label>Desequilíbrio máximo (%)</label><input name="deseqMax" step="any" type="number"></div>
<div><label>Teto de ensaio (A)</label><input name="iTeto" step="any" type="number"></div>
<div><label>Fator de alerta (× base)</label><input name="fator" step="any" type="number"></div>
<div><label>Shunt INA226 (Ω)</label><input name="shunt" step="any" type="number"></div>
<div><label>Amostras por leitura</label><input name="nMedias" type="number"></div>
</div><button type="submit">Salvar</button></form></div>
</main>
<script>
const $=id=>document.getElementById(id);const f=(x,d)=>x===null?'–':Number(x).toFixed(d);
let serie=[],ult=null,cfgOk=false;
async function post(u){const r=await fetch(u,{method:'POST'});return r.text();}
const CONF_A='Confirme: motor DESCONECTADO do driver e chicote do módulo de relés ligado às fases U, V e W.';
async function todos(){if(!confirm(CONF_A))return;const t=await post('/api/medir_todos');if(t!=='ok')alert(t);}
async function medir(k){if(!confirm(CONF_A))return;
 const t=await post('/api/medir?par='+k);if(t!=='ok')alert(t);}
async function novo(){const t=await post('/api/novo');if(t!=='ok')alert(t);serie=[];}
async function carga(on){if(on&&!confirm('Confirme: chicote do módulo de relés desligado do motor, motor ligado ao driver e carga zerada.'))return;
 const t=await post(on?'/api/carga/iniciar':'/api/carga/encerrar');if(t!=='ok')alert(t);if(on)serie=[];}
async function salvar(e){e.preventDefault();const p=new URLSearchParams(new FormData($('fcfg')));
 const r=await fetch('/api/config',{method:'POST',body:p});alert(await r.text());return false;}
async function cfg(){const c=await (await fetch('/api/config')).json();for(const k in c){const el=document.querySelector('[name='+k+']');if(el)el.value=c[k];}cfgOk=true;}
function desenha(s){const c=$('cv'),g=c.getContext('2d'),dpr=window.devicePixelRatio||1,W=c.clientWidth,H=220;
 if(c.width!==Math.round(W*dpr)||c.height!==Math.round(H*dpr)){c.width=Math.round(W*dpr);c.height=Math.round(H*dpr);}
 g.setTransform(dpr,0,0,dpr,0,0);g.clearRect(0,0,W,H);
 if(!serie.length)return;const mx=Math.max(s.iTeto*1.1,...serie.map(p=>p.i),0.1);
 const y=v=>H-20-(v/mx)*(H-30),x=k=>40+k*(W-50)/Math.max(serie.length-1,1);
 g.fillStyle='#6b6690';g.font='12px sans-serif';g.fillText(mx.toFixed(1)+' A',2,14);g.fillText('0',2,H-20);
 const linha=(v,cor,txt)=>{if(v===null)return;g.strokeStyle=cor;g.setLineDash([6,4]);g.beginPath();g.moveTo(40,y(v));g.lineTo(W-10,y(v));g.stroke();g.setLineDash([]);g.fillStyle=cor;g.fillText(txt,W-50,y(v)-4);};
 linha(s.iTeto,'#c62828','teto');if(s.base!==null){linha(s.base,'#1b8a4b','base');linha(s.base*s.fator,'#d97706','alerta');}
 g.strokeStyle='#3b2a94';g.lineWidth=2;g.beginPath();serie.forEach((p,k)=>{k?g.lineTo(x(k),y(p.i)):g.moveTo(x(k),y(p.i));});g.stroke();g.lineWidth=1;}
async function loop(){try{const s=await (await fetch('/api/status')).json();
 $('estado').textContent=s.estado;$('estado').className='badge '+s.estado;$('motivo').textContent=s.motivo;
 const ct=s.comTemp;$('cab').innerHTML='<tr><th>Par</th><th>R medida (Ω)</th>'+(ct?'<th>R a 20 °C (Ω)</th><th>Temp. (°C)</th>':'')+'</tr>';
 $('tab').innerHTML=s.pares.map(p=>`<tr><td>${p.par}</td><td>${p.medido?(p.r===null?'ABERTO/FAIXA':f(p.r,5)):'–'}</td>`+(ct?`<td>${f(p.r20,5)}</td><td>${f(p.t,1)}</td>`:'')+'</tr>').join('');
 $('lmedia').textContent=ct?'Média a 20 °C':'Média';
 $('nota').textContent=ct?'':'Sem sensor de temperatura: valores sem correção. O desequilíbrio entre pares não é afetado, pois os três pares são medidos na mesma temperatura.';
 $('media').textContent=s.media===null?'–':f(s.media,5)+' Ω';$('deseq').textContent=s.deseq===null?'–':f(s.deseq,2)+' %';
 $('i').textContent=f(s.i,3)+' A';$('v').textContent=f(s.v,2)+' V';$('base').textContent=s.base===null?'–':f(s.base,3)+' A';$('pico').textContent=f(s.pico,3)+' A';
 $('alerta').innerHTML=s.teto?'<span class="warn">TETO DE ENSAIO ATINGIDO – alivie a carga</span>':(s.alerta?'<span class="warn">Alerta: corrente acima de '+s.fator+'× a linha de base</span>':'');
 $('bIni').disabled=(s.estado!=='APROVADO');
 if(s.estado==='CARGA'||s.estado==='TRAVADO'){if(s.t!==ult){serie.push({i:s.i});ult=s.t;if(serie.length>300)serie.shift();}}
 desenha(s);}catch(e){$('motivo').textContent='Sem conexão com a bancada...';}
 setTimeout(loop,300);}
cfg();loop();
</script></body></html>
)HTML";

// ----------------------------- Web: API ------------------------------------
String num(float v, int d) { return isnan(v) ? String("null") : String(v, d); }

String statusJSON() {
  String j = "{";
  j += "\"estado\":\"" + String(nomeEstado(estado)) + "\",";
  j += "\"motivo\":\"" + motivo + "\",";
  j += "\"pares\":[";
  for (int k = 0; k < 3; k++) {
    if (k) j += ",";
    j += "{\"par\":\"" + String(NOMES_PAR[k]) + "\",\"medido\":" + (medido[k] ? "true" : "false") +
         ",\"r\":" + num(rPar[k], 5) + ",\"r20\":" + num(r20Par[k], 5) + ",\"t\":" + num(tempMed[k], 2) + "}";
  }
  j += "],";
  j += "\"media\":" + num(rMedia20, 5) + ",\"deseq\":" + num(deseqPct, 3) + ",";
  j += "\"i\":" + num(iAtual, 4) + ",\"v\":" + num(vAtual, 3) + ",\"base\":" + num(iBase, 4) +
       ",\"pico\":" + num(iPico, 4) + ",";
  j += "\"alerta\":" + String(alerta ? "true" : "false") + ",\"teto\":" + String(tetoAtingido ? "true" : "false") + ",";
  j += "\"iTeto\":" + String(cfg.iTeto, 3) + ",\"fator\":" + String(cfg.fatorAlerta, 2) + ",";
  j += "\"t\":" + String(bufN) + ",";
  j += "\"comTemp\":" + String(temDS ? "true" : "false");
  j += "}";
  return j;
}

void apiStatus() { server.send(200, "application/json", statusJSON()); }

String cmdMedir(int k) {
  if (estado == CARGA || estado == TRAVADO) return "Indisponivel no estado atual";
  if (k < 0 || k > 2) return "Par invalido";
  if (estado == APROVADO || estado == REPROVADO) {
    estado = AGUARDANDO; rMedia20 = deseqPct = NAN;
  }
  int r = medirPar(k);
  if (r == 3) return "ADS1115 nao encontrado";
  motivo = String("Par ") + NOMES_PAR[k] + (r == 0 ? " medido" : (r == 1 ? ": circuito aberto" : ": fora de faixa"));
  avaliarDiagnostico();
  return "ok";
}

// Mede os 3 pares em sequencia (mesma temperatura), selecionando-os pelos reles.
String cmdMedirTodos() {
  if (estado == CARGA || estado == TRAVADO) return "Indisponivel no estado atual";
  if (!temADS) return "ADS1115 nao encontrado";
  limparDiagnostico(); estado = AGUARDANDO;
  for (int k = 0; k < 3; k++) medirPar(k);
  motivo = "Tres pares medidos";
  avaliarDiagnostico();
  return "ok";
}

String cmdNovo() {
  if (estado == CARGA || estado == TRAVADO) return "Indisponivel no estado atual";
  limparDiagnostico(); estado = AGUARDANDO; motivo = "Novo ensaio: meca os tres pares";
  return "ok";
}

String cmdCargaIniciar() {
  if (estado != APROVADO) return "Bloqueado: o diagnostico estatico precisa estar APROVADO";
  if (!temINA) return "INA226 nao encontrado";
  iniciarCarga();
  return "ok";
}

void apiMedir()         { server.send(200, "text/plain", cmdMedir(server.arg("par").toInt())); }
void apiMedirTodos()    { server.send(200, "text/plain", cmdMedirTodos()); }
void apiNovo()          { server.send(200, "text/plain", cmdNovo()); }
void apiCargaIniciar()  { server.send(200, "text/plain", cmdCargaIniciar()); }
void apiCargaEncerrar() { encerrarCarga(); server.send(200, "text/plain", "ok"); }

// ----------------------------- Monitor Serial ------------------------------
void imprimirStatus() {
  Serial.printf("[%s] %s\n", nomeEstado(estado), motivo.c_str());
  for (int k = 0; k < 3; k++) {
    if (!medido[k]) Serial.printf("  %s: --\n", NOMES_PAR[k]);
    else if (isnan(rPar[k])) Serial.printf("  %s: ABERTO/FORA DE FAIXA\n", NOMES_PAR[k]);
    else if (temDS) Serial.printf("  %s: %.5f ohm (20 C: %.5f ohm, %.1f C)\n", NOMES_PAR[k], rPar[k], r20Par[k], tempMed[k]);
    else Serial.printf("  %s: %.5f ohm\n", NOMES_PAR[k], rPar[k]);
  }
  if (!isnan(deseqPct)) Serial.printf("  Media%s: %.5f ohm | Desequilibrio: %.2f %%\n", temDS ? " 20 C" : "", rMedia20, deseqPct);
}

void ajudaSerial() {
  Serial.println("Comandos: m = medir os 3 pares | 1/2/3 = remedir U-V/V-W/W-U | n = novo ensaio | c = iniciar carga | e = encerrar carga | d = leitura bruta do ADS (par U-V) | s = status | ? = ajuda");
}

void diagnosticoADS();   // definida mais abaixo

void tratarSerial() {
  if (!Serial.available()) return;
  char c = Serial.read();
  String r = "";
  switch (c) {
    case 'm': case 'M': r = cmdMedirTodos(); break;
    case '1': case '2': case '3': r = cmdMedir(c - '1'); break;
    case 'n': case 'N': r = cmdNovo(); break;
    case 'c': case 'C': r = cmdCargaIniciar(); break;
    case 'e': case 'E': encerrarCarga(); r = "ok"; break;
    case 'd': case 'D': diagnosticoADS(); return;
    case 's': case 'S': imprimirStatus(); return;
    case '?': ajudaSerial(); return;
    default: return;
  }
  if (r != "ok") Serial.println("-> " + r);
  imprimirStatus();
}

uint32_t tLogCarga = 0;
void logCargaSerial() {
  if ((estado != CARGA && estado != TRAVADO) || (millis() - tLogCarga) < 1000) return;
  tLogCarga = millis();
  Serial.printf("I=%.3f A  V=%.2f V  base=%s  pico=%.3f A %s%s\n", iAtual, vAtual,
                isnan(iBase) ? "medindo" : String(iBase, 3).c_str(), iPico,
                alerta ? " [ALERTA]" : "", tetoAtingido ? " [TETO]" : "");
}

void diagnosticoADS() {
  if (!temADS) {
    Serial.println("ADS1115 nao detectado!");
    return;
  }
  
  if (estado == CARGA || estado == TRAVADO) {
    Serial.println("Indisponivel no estado atual (chicote dos reles deve estar desligado do motor)");
    return;
  }
  Serial.println("\n--- DIAGNOSTICO BRUTO DO ADS1115 (par U-V pelos reles) ---");
  selecionarPar(0);
  
  // 1. Testa com o MOSFET desligado (Offset)
  digitalWrite(PIN_GATE, LOW);
  delay(20);
  int16_t rawC_off = lerParBruto(0);
  int16_t rawR_off = lerRefBruto();
  float vC_off = ads.computeVolts(rawC_off);
  float vR_off = ads.computeVolts(rawR_off);
  
  // 2. Testa com o MOSFET ligado
  digitalWrite(PIN_GATE, HIGH);
  delay(60);
  int16_t rawC_on = lerParBruto(0);
  int16_t rawR_on = lerRefBruto();
  float vC_on = ads.computeVolts(rawC_on);
  float vR_on = ads.computeVolts(rawR_on);
  relesRepouso();

  Serial.printf("AIN0-AIN1 (par U-V) -> Desligado: %d (%.5f V) | Ligado: %d (%.5f V)\n", rawC_off, vC_off, rawC_on, vC_on);
  Serial.printf("AIN2 (R_ref)       -> Desligado: %d (%.5f V) | Ligado: %d (%.5f V)\n", rawR_off, vR_off, rawR_on, vR_on);
  Serial.println("----------------------------------------\n");
}

String configJSON() {
  return "{\"rRef\":" + String(cfg.rRef, 5) + ",\"rNom\":" + String(cfg.rNom, 5) +
         ",\"tolNom\":" + String(cfg.tolNom, 2) + ",\"deseqMax\":" + String(cfg.deseqMax, 2) +
         ",\"iTeto\":" + String(cfg.iTeto, 3) + ",\"fator\":" + String(cfg.fatorAlerta, 2) +
         ",\"shunt\":" + String(cfg.shunt, 5) + ",\"nMedias\":" + String(cfg.nMedias) + "}";
}

bool valorDe(const String& q, const char* nome, String& saida) {
  String alvo = String("&") + nome + "=";
  String qq = "&" + q;
  int p = qq.indexOf(alvo);
  if (p < 0) return false;
  p += alvo.length();
  int e = qq.indexOf('&', p);
  saida = (e < 0) ? qq.substring(p) : qq.substring(p, e);
  return saida.length() > 0;
}

String cmdConfig(const String& q) {
  if (estado == CARGA) return "Encerre o ensaio sob carga antes de alterar";
  String v;
  float rRef = valorDe(q, "rRef", v) ? v.toFloat() : cfg.rRef;
  float shunt = valorDe(q, "shunt", v) ? v.toFloat() : cfg.shunt;
  int nMed = valorDe(q, "nMedias", v) ? v.toInt() : cfg.nMedias;
  if (rRef <= 0 || shunt <= 0 || nMed < 1 || nMed > 256) return "Valores invalidos";
  cfg.rRef = rRef; cfg.shunt = shunt; cfg.nMedias = nMed;
  if (valorDe(q, "rNom", v))     cfg.rNom = v.toFloat();
  if (valorDe(q, "tolNom", v))   cfg.tolNom = v.toFloat();
  if (valorDe(q, "deseqMax", v)) cfg.deseqMax = v.toFloat();
  if (valorDe(q, "iTeto", v))    cfg.iTeto = v.toFloat();
  if (valorDe(q, "fator", v))    cfg.fatorAlerta = v.toFloat();
  salvarConfig();
  configurarINA();
  return "Configuracoes salvas";
}

void apiConfigGet() { server.send(200, "application/json", configJSON()); }

void apiConfigPost() {
  const char* nomes[] = {"rRef", "rNom", "tolNom", "deseqMax", "iTeto", "fator", "shunt", "nMedias"};
  String q = "";
  for (const char* n : nomes)
    if (server.hasArg(n)) q += String(q.length() ? "&" : "") + n + "=" + server.arg(n);
  server.send(200, "text/plain", cmdConfig(q));
}

void apiCSV() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Content-Disposition", "attachment; filename=ensaio_bancada.csv");
  server.send(200, "text/csv", "");
  String h = "# Diagnostico estatico\n# par;R_medida_ohm;R20_ohm;temp_C\n";
  for (int k = 0; k < 3; k++)
    h += "# " + String(NOMES_PAR[k]) + ";" + num(rPar[k], 5) + ";" + num(r20Par[k], 5) + ";" + num(tempMed[k], 2) + "\n";
  h += "# media20_ohm;" + num(rMedia20, 5) + ";desequilibrio_pct;" + num(deseqPct, 3) + "\n";
  h += "# linha_base_A;" + num(iBase, 4) + ";pico_A;" + num(iPico, 4) + "\n";
  h += "t_ms;corrente_A;tensao_V;alerta;teto;corte\n";
  server.sendContent(h);
  String bloco;
  for (uint32_t k = 0; k < bufN; k++) {
    const Amostra& a = buf[k];
    bloco += String(a.t) + ";" + String(a.i, 4) + ";" + String(a.v, 3) + ";" +
             String((a.flags & 1) ? 1 : 0) + ";" + String((a.flags & 2) ? 1 : 0) + ";" + String((a.flags & 4) ? 1 : 0) + "\n";
    if (bloco.length() > 1400) { server.sendContent(bloco); bloco = ""; }
  }
  if (bloco.length()) server.sendContent(bloco);
  server.sendContent("");
}

// ----------------------------- Setup / Loop --------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_GATE, OUTPUT); digitalWrite(PIN_GATE, LOW);
  for (int k = 0; k < 4; k++) {               // nivel ALTO antes de virar saida: rele nao pulsa no boot
    digitalWrite(PIN_K[k], RELE_DESLIGA);
    pinMode(PIN_K[k], OUTPUT);
  }
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BOTAO, INPUT_PULLUP);

  carregarConfig();
  Wire.begin(PIN_SDA, PIN_SCL);

  temADS = ads.begin(0x48, &Wire);
  if (temADS) {
    ads.setGain(GAIN_SIXTEEN);                  // +/-0,256 V
    ads.setDataRate(RATE_ADS1115_128SPS);
  }
  temINA = ina.begin();
  if (temINA) {
    configurarINA();
    ina.setAverage(INA226_16_SAMPLES);
  }
  
  // Inicialização correta do SH1106 para substituir o SSD1306
  temOLED = oled.begin(0x3C, true); 

  if (psramFound()) { bufCap = 36000; buf = (Amostra*)ps_malloc(bufCap * sizeof(Amostra)); }
  if (!buf) { bufCap = 6000; buf = (Amostra*)malloc(bufCap * sizeof(Amostra)); }
  if (!buf) bufCap = 0;

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_SENHA);

  server.on("/", []() { server.send_P(200, "text/html", PAGINA); });
  server.on("/api/status", HTTP_GET, apiStatus);
  server.on("/api/medir", HTTP_POST, apiMedir);
  server.on("/api/medir_todos", HTTP_POST, apiMedirTodos);
  server.on("/api/novo", HTTP_POST, apiNovo);
  server.on("/api/carga/iniciar", HTTP_POST, apiCargaIniciar);
  server.on("/api/carga/encerrar", HTTP_POST, apiCargaEncerrar);
  server.on("/api/config", HTTP_GET, apiConfigGet);
  server.on("/api/config", HTTP_POST, apiConfigPost);
  server.on("/api/csv", HTTP_GET, apiCSV);
  server.begin();

  Serial.println("\nBancada PFC - prova de conceito");
  Serial.printf("ADS1115:%d INA226:%d OLED:%d\n", temADS, temINA, temOLED);
  Serial.printf("Wi-Fi: %s  ->  http://%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
  ajudaSerial();
}

void loop() {
  server.handleClient();
  tratarSerial();
  tratarCarga();
  logCargaSerial();
  tratarBotao();
  tratarLED();
  atualizarOLED();
}