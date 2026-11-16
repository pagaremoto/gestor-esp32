
/*
  ==========================================================
  CONTROL GENERAL 4 RELES
  ESP32 + Arduino IDE + LittleFS + AsyncWebServer
  ==========================================================

  RELES:
  GPIO 23 = RELE 1
  GPIO 22 = RELE 2
  GPIO 21 = RELE 3
  GPIO 19 = RELE 4

  HIGH = ENCENDIDO
  LOW  = APAGADO

  UN SOLO CONTROL PARA LOS CUATRO RELES
  ==========================================================
*/

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "LittleFS.h"

// ----------------------------------------------------------
// SERVIDOR WEB
// ----------------------------------------------------------

AsyncWebServer server(80);

// ----------------------------------------------------------
// CONFIGURACION WIFI
// ----------------------------------------------------------

const char* PARAM_INPUT_1 = "ssid";
const char* PARAM_INPUT_2 = "pass";
const char* PARAM_INPUT_3 = "ip";
const char* PARAM_INPUT_4 = "gateway";

const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
const char* ipPath = "/ip.txt";
const char* gatewayPath = "/gateway.txt";

String ssid;
String pass;
String ip;
String gateway;

IPAddress localIP;
IPAddress localGateway;
IPAddress subnet(255, 255, 255, 0);

const char* AP_SSID = "ESP-WIFI-MANAGER";
const char* AP_PASSWORD = "12345678";

const unsigned long WIFI_TIMEOUT = 15000;

// ----------------------------------------------------------
// CONFIGURACION DE LOS CUATRO RELES
// ----------------------------------------------------------

const uint8_t relayPins[] = {23, 22, 21, 19};
const uint8_t numRelays = 4;

// Estado general de los cuatro relés
bool relaysOn = false;

// ----------------------------------------------------------
// REINICIO DESPUES DE GUARDAR WIFI
// ----------------------------------------------------------

bool restartPending = false;
unsigned long restartAt = 0;

// ----------------------------------------------------------
// LITTLEFS
// ----------------------------------------------------------

void initLittleFS() {

  if (!LittleFS.begin(true)) {
    Serial.println("ERROR: LittleFS no disponible");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("LittleFS iniciado correctamente");
}

// Leer archivo
String readFile(fs::FS &fs, const char* path) {

  File file = fs.open(path, FILE_READ);

  if (!file || file.isDirectory()) {
    return "";
  }

  String content = file.readString();
  content.trim();

  file.close();

  return content;
}

// Escribir archivo
bool writeFile(fs::FS &fs, const char* path,
               const char* message) {

  File file = fs.open(path, FILE_WRITE);

  if (!file) {
    Serial.printf("Error escribiendo: %s\n", path);
    return false;
  }

  bool result = file.print(message);

  file.close();

  return result;
}

// ----------------------------------------------------------
// CONTROL GENERAL DE LOS CUATRO RELES
// ----------------------------------------------------------

void setAllRelays(bool state) {

  relaysOn = state;

  for (uint8_t i = 0; i < numRelays; i++) {

    digitalWrite(relayPins[i],
                 state ? HIGH : LOW);
  }

  if (relaysOn) {
    Serial.println("LOS CUATRO RELES ENCENDIDOS");
  } else {
    Serial.println("LOS CUATRO RELES APAGADOS");
  }
}

// ----------------------------------------------------------
// PROCESADOR HTML
// ----------------------------------------------------------


// =====Con emoji=====

String processor(const String& var) {

  if (var == "STATE") {

    if (relaysOn) {
      return "🟢 💡 ENCENDIDOS";
    } else {
      return "🔴 🌙 APAGADOS";
    }
  }

  return String();
}

// ----------------------------------------------------------
// CONEXION WIFI
// ----------------------------------------------------------

bool initWiFi() {

  if (ssid.isEmpty() || ip.isEmpty() ||
      gateway.isEmpty()) {

    Serial.println("Configuracion WiFi incompleta");
    return false;
  }

  if (!localIP.fromString(ip) ||
      !localGateway.fromString(gateway)) {

    Serial.println("Direccion IP o Gateway incorrectos");
    return false;
  }

  WiFi.mode(WIFI_STA);

  if (!WiFi.config(localIP, localGateway,
                   subnet, localGateway)) {

    Serial.println("Error configurando IP");
    return false;
  }

  WiFi.begin(ssid.c_str(), pass.c_str());

  Serial.print("Conectando a WiFi");

  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED) {

    if (millis() - startTime >= WIFI_TIMEOUT) {

      Serial.println("\nTiempo de conexion agotado");
      return false;
    }

    Serial.print(".");
    delay(250);
  }

  Serial.println();
  Serial.println("WiFi conectado correctamente");

  Serial.print("Direccion IP: ");
  Serial.println(WiFi.localIP());

  return true;
}

// ----------------------------------------------------------
// PAGINA PRINCIPAL
// ----------------------------------------------------------

void configurarServidorNormal() {

  // Pagina principal
  server.on("/", HTTP_GET,
    [](AsyncWebServerRequest *request) {

      request->send(LittleFS,
                    "/index.html",
                    "text/html",
                    false,
                    processor);
    }
  );

  // ENCENDER LOS CUATRO RELES
  server.on("/on", HTTP_GET,
    [](AsyncWebServerRequest *request) {

      setAllRelays(true);

      request->redirect("/");
    }
  );

  // APAGAR LOS CUATRO RELES
  server.on("/off", HTTP_GET,
    [](AsyncWebServerRequest *request) {

      setAllRelays(false);

      request->redirect("/");
    }
  );

  // Archivos CSS y otros archivos estáticos
  server.serveStatic("/", LittleFS, "/");

  server.begin();

  Serial.println("Servidor web iniciado");
  Serial.println("Control de cuatro reles disponible");
}

// ----------------------------------------------------------
// WIFI MANAGER - PUNTO DE ACCESO
// ----------------------------------------------------------

void configurarServidorAP() {

  Serial.println("Iniciando punto de acceso WiFi");

  WiFi.mode(WIFI_AP);

  WiFi.softAP(AP_SSID, AP_PASSWORD);

  Serial.print("Red WiFi: ");
  Serial.println(AP_SSID);

  Serial.print("IP del punto de acceso: ");
  Serial.println(WiFi.softAPIP());

  // Mostrar formulario WiFi
  server.on("/", HTTP_GET,
    [](AsyncWebServerRequest *request) {

      request->send(LittleFS,
                    "/wifimanager.html",
                    "text/html");
    }
  );

  // Recibir configuracion WiFi
  server.on("/", HTTP_POST,
    [](AsyncWebServerRequest *request) {

      String newSSID;
      String newPass;
      String newIP;
      String newGateway;

      bool hasSSID = false;
      bool hasIP = false;
      bool hasGateway = false;

      int params = request->params();

      for (int i = 0; i < params; i++) {

        const AsyncWebParameter* p =
          request->getParam(i);

        if (p->isPost()) {

          if (p->name() == PARAM_INPUT_1) {
            newSSID = p->value();
            hasSSID = true;
          }

          else if (p->name() == PARAM_INPUT_2) {
            newPass = p->value();
          }

          else if (p->name() == PARAM_INPUT_3) {
            newIP = p->value();
            hasIP = true;
          }

          else if (p->name() == PARAM_INPUT_4) {
            newGateway = p->value();
            hasGateway = true;
          }
        }
      }

      IPAddress testIP;
      IPAddress testGateway;

      if (!hasSSID || newSSID.isEmpty() ||
          !hasIP || !hasGateway ||
          !testIP.fromString(newIP) ||
          !testGateway.fromString(newGateway)) {

        request->send(400, "text/plain",
                      "Error: revise SSID, IP y Gateway");

        return;
      }

      bool saved = true;

      saved &= writeFile(LittleFS, ssidPath,
                         newSSID.c_str());

      saved &= writeFile(LittleFS, passPath,
                         newPass.c_str());

      saved &= writeFile(LittleFS, ipPath,
                         newIP.c_str());

      saved &= writeFile(LittleFS, gatewayPath,
                         newGateway.c_str());

      if (!saved) {

        request->send(500, "text/plain",
                      "Error guardando la configuracion");

        return;
      }

      Serial.println("Configuracion WiFi guardada");

      request->send(200, "text/plain",
                    "Configuracion guardada. "
                    "El ESP32 se reiniciara.");

      restartPending = true;
      restartAt = millis() + 1500;
    }
  );

  // Archivos estaticos
  server.serveStatic("/", LittleFS, "/");

  server.begin();

  Serial.println("WiFi Manager iniciado");
}

// ----------------------------------------------------------
// SETUP
// ----------------------------------------------------------

void setup() {

  Serial.begin(115200);

  delay(500);

  Serial.println();
  Serial.println("================================");
  Serial.println(" CONTROL GENERAL 4 RELES");
  Serial.println("================================");

  // Inicializar salidas
  for (uint8_t i = 0; i < numRelays; i++) {

    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
  }

  relaysOn = false;

  // Iniciar LittleFS
  initLittleFS();

  // Leer configuracion guardada
  ssid = readFile(LittleFS, ssidPath);
  pass = readFile(LittleFS, passPath);
  ip = readFile(LittleFS, ipPath);
  gateway = readFile(LittleFS, gatewayPath);

  Serial.println("Configuracion WiFi cargada");

  Serial.print("SSID: ");
  Serial.println(ssid);

  Serial.print("IP: ");
  Serial.println(ip);

  Serial.print("Gateway: ");
  Serial.println(gateway);

  // Intentar conexion WiFi
  if (initWiFi()) {

    configurarServidorNormal();

  } else {

    // Si no conecta, activar WiFi Manager
    configurarServidorAP();
  }
}

// ----------------------------------------------------------
// LOOP
// ----------------------------------------------------------

void loop() {

  if (restartPending &&
      (long)(millis() - restartAt) >= 0) {

    ESP.restart();
  }
}
