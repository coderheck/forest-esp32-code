#include <HardwareSerial.h>

#define simSerial Serial0
#define MCU_SIM_BAUDRATE 115200
#define MCU_SIM_TX_PIN 21
#define MCU_SIM_RX_PIN 20
#define MCU_SIM_EN_PIN 2

const char APN[] = "v-internet";

String response;

void readResponse(unsigned long timeout = 5000) {
  unsigned long start = millis();

  while (millis() - start < timeout) {
    while (simSerial.available()) {
      char c = simSerial.read();
      Serial.write(c);
      response += c;
    }
    delay(5);
  }

  Serial.println();
}

bool sendAT(const char *cmd, unsigned long timeout = 3000) {
  while (simSerial.available()) simSerial.read();

  response = "";
  Serial.print("\n>> ");
  Serial.println(cmd);

  simSerial.println(cmd);
  readResponse(timeout);

  return response.indexOf("OK") >= 0;
}

bool waitForConnect(unsigned long timeout = 10000) {
  unsigned long start = millis();
  response = "";

  while (millis() - start < timeout) {
    while (simSerial.available()) {
      char c = simSerial.read();
      Serial.write(c);
      response += c;

      if (response.indexOf("CONNECT") >= 0)
        return true;

      if (response.indexOf("ERROR") >= 0)
        return false;
    }
    delay(5);
  }

  Serial.println("\nTimed out waiting for CONNECT.");
  return false;
}

void testDNS() {
  sendAT("AT+QIDNSGIP=1,\"example.com\"", 15000);
}

void testHTTP() {
  // Configure HTTP to use PDP context 1.
  if (!sendAT("AT+QHTTPCFG=\"contextid\",1")) {
    Serial.println("HTTP context configuration failed.");
    return;
  }

  // URL length: strlen("http://example.com") == 18.
  Serial.println("\n>> AT+QHTTPURL=18,80");
  simSerial.println("AT+QHTTPURL=18,80");

  if (!waitForConnect(10000)) {
    Serial.println("Could not enter URL input mode.");
    return;
  }

  // Send URL after CONNECT.
  simSerial.print("http://example.com");

  response = "";
  readResponse(3000);

  Serial.println("\n>> AT+QHTTPGET=80");
  simSerial.println("AT+QHTTPGET=80");

  // HTTP GET can take time; collect asynchronous response.
  response = "";
  readResponse(30000);

  if (response.indexOf("+QHTTPGET: 0,200") >= 0) {
    Serial.println("HTTP TEST PASSED: HTTP 200");
  } else if (response.indexOf("+QHTTPGET:") >= 0) {
    Serial.println("HTTP request returned a result; inspect the status above.");
  } else {
    Serial.println("No final +QHTTPGET result received; inspect modem output.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(MCU_SIM_EN_PIN, OUTPUT);
  digitalWrite(MCU_SIM_EN_PIN, HIGH);

  Serial.println("\n=== TDM2421 EG800K Connectivity Test ===");
  Serial.println("Powering modem...");
  delay(8000);

  simSerial.begin(
    MCU_SIM_BAUDRATE,
    SERIAL_8N1,
    MCU_SIM_RX_PIN,
    MCU_SIM_TX_PIN
  );

  sendAT("AT");
  sendAT("ATI");
  sendAT("AT+CPIN?");
  sendAT("AT+CSQ");
  sendAT("AT+CEREG?");
  sendAT("AT+CGATT?");
  sendAT("AT+QICSGP=1,1,\"v-internet\",\"\",\"\",0");

  // Activation may return ERROR if the context is already active.
  sendAT("AT+QIACT=1", 10000);
  sendAT("AT+QIACT?", 3000);

  Serial.println("\n=== DNS TEST ===");
  testDNS();

  Serial.println("\n=== HTTP TEST ===");
  testHTTP();

  Serial.println("\n=== Tests finished ===");
}

void loop() {
  // Forward any later modem messages to Serial Monitor.
  while (simSerial.available()) {
    Serial.write(simSerial.read());
  }
}