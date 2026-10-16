
#include <HardwareSerial.h>
#include <time.h>
#include <math.h>

#define simSerial Serial0

#define MCU_SIM_BAUDRATE 115200
#define MCU_SIM_TX_PIN   21
#define MCU_SIM_RX_PIN   20
#define MCU_SIM_EN_PIN   2

// ===== CONFIGURATION: CHANGE THESE =====
const char APN[] = "v-internet";
const char SERVER_URL[] =
    "https://translator-smell-modelling-acquired.trycloudflare.com/api/records"; // changes every time cloudflared is launched
const char API_KEY[] = "forest";

// Send one record every 1 sec
const unsigned long UPLOAD_INTERVAL = 1000;

// ===== STATE =====
String modemBuffer;
double latitude = NAN;
double longitude = NAN;
bool gpsFix = false;
bool modemReady = false;
bool dataReady = false;
unsigned long lastUpload = 0;

// Read modem output until a token arrives or timeout expires.
String readUntil(const char *token, unsigned long timeout) {
    String result;
    unsigned long start = millis();

    while (millis() - start < timeout) {
        while (simSerial.available()) {
            char c = simSerial.read();
            result += c;
            if (result.indexOf(token) >= 0) {
                return result;
            }
        }
        delay(2);
    }
    return result;
}

bool sendAT(const String &cmd, String &output,
            unsigned long timeout = 3000) {
    while (simSerial.available()) simSerial.read();

    simSerial.println(cmd);
    output = readUntil("OK", timeout);

    if (output.indexOf("ERROR") >= 0 ||
        output.indexOf("+CME ERROR") >= 0) {
        return false;
    }

    return output.indexOf("OK") >= 0;
}

// Send a command that should return CONNECT.
bool sendATPrompt(const String &cmd, String &output,
                  unsigned long timeout = 5000) {
    while (simSerial.available()) simSerial.read();

    simSerial.println(cmd);
    output = readUntil("CONNECT", timeout);

    return output.indexOf("CONNECT") >= 0;
}

// Send exactly 'length' bytes after a CONNECT prompt.
bool sendPayload(const String &payload, String &output,
                 unsigned long timeout = 15000) {
    simSerial.print(payload);
    output = readUntil("OK", timeout);

    return output.indexOf("OK") >= 0 &&
           output.indexOf("ERROR") < 0;
}

bool parseCoordinate(String value, char direction,
                     double &result) {
    value.trim();
    if (value.length() < 4) return false;

    // Quectel format: ddmm.mmmmN or dddmm.mmmmE
    int dot = value.indexOf('.');
    if (dot < 0) return false;

    int degreeDigits = dot - 2;
    if (degreeDigits < 1) return false;

    double degrees = value.substring(0, degreeDigits).toDouble();
    double minutes = value.substring(degreeDigits, value.length() - 1)
                         .toDouble();

    result = degrees + minutes / 60.0;

    if (direction == 'S' || direction == 'W') {
        result = -result;
    }

    return true;
}

bool getGPS() {
    String response;

    // Query current GNSS location.
    if (!sendAT("AT+QGPSLOC=0", response, 5000)) {
        Serial.println("# GPS query failed");
        gpsFix = false;
        return false;
    }

    int start = response.indexOf("+QGPSLOC:");
    if (start < 0) {
        Serial.println("# Waiting for GPS fix");
        gpsFix = false;
        return false;
    }

    int end = response.indexOf('\n', start);
    String loc = response.substring(
        start + 9,
        end >= 0 ? end : response.length()
    );
    loc.trim();

    // Fields:
    // time,latitude,longitude,hdop,altitude,...
    int comma1 = loc.indexOf(',');
    int comma2 = loc.indexOf(',', comma1 + 1);
    int comma3 = loc.indexOf(',', comma2 + 1);

    if (comma1 < 0 || comma2 < 0 || comma3 < 0) {
        Serial.println("# Invalid GPS response");
        gpsFix = false;
        return false;
    }

    String latText = loc.substring(comma1 + 1, comma2);
    String lonText = loc.substring(comma2 + 1, comma3);

    if (latText.length() < 2 || lonText.length() < 2) {
        gpsFix = false;
        return false;
    }

    char latDir = latText.charAt(latText.length() - 1);
    char lonDir = lonText.charAt(lonText.length() - 1);

    if (!parseCoordinate(latText, latDir, latitude) ||
        !parseCoordinate(lonText, lonDir, longitude)) {
        Serial.println("# Coordinate parsing failed");
        gpsFix = false;
        return false;
    }

    gpsFix = true;

    Serial.print("# GPS: ");
    Serial.print(latitude, 6);
    Serial.print(", ");
    Serial.println(longitude, 6);

    return true;
}

bool setupCellular() {
    String response;

    // Check SIM and registration.
    sendAT("AT+CPIN?", response, 3000);
    Serial.println(response);

    sendAT("AT+CSQ", response, 3000);
    Serial.println(response);

    sendAT("AT+CEREG?", response, 3000);
    Serial.println(response);

    // Configure PDP context using your carrier's APN.
    if (!sendAT(
        String("AT+QICSGP=1,1,\"") + APN + "\",\"\",\"\",0",
        response, 5000)) {
        Serial.println("# APN configuration failed");
        Serial.println(response);
        return false;
    }

    if (!sendAT("AT+QIACT=1", response, 30000)) {
        // It may already be active; check status.
        sendAT("AT+QIACT?", response, 5000);
        Serial.println(response);

        if (response.indexOf("+QIACT: 1,") < 0) {
            Serial.println("# Cellular data activation failed");
            return false;
        }
    }

    if (!sendAT("AT+QHTTPCFG=\"contextid\",1",
                response, 5000)) {
        Serial.println("# HTTP(S) commands may be unsupported");
        Serial.println(response);
        return false;
    }

    // JSON body and a custom API-key header.
    if (!sendAT("AT+QHTTPCFG=\"contenttype\",4",
                response, 5000)) {
        Serial.println("# JSON content type configuration failed");
        Serial.println(response);
        return false;
    }

    if (!sendAT(
        String("AT+QHTTPCFG=\"reqheader/add\",\"X-API-Key\",\"") +
        API_KEY + "\"",
        response, 5000)) {
        Serial.println("# Custom API header setup failed");
        Serial.println(response);
        return false;
    }

    // Configure SSL context for HTTPS.
    if (!sendAT("AT+QHTTPCFG=\"sslctxid\",1",
                response, 5000)) {
        Serial.println("# SSL context setup failed");
        Serial.println(response);
        return false;
    }

    // Do not disable TLS certificate verification here.
    // The SSL context must trust the server certificate chain.

    Serial.println("# Cellular data and HTTP(S) configured");
    return true;
}

bool uploadRecord() {
    if (!gpsFix) return false;

    // UTC timestamp from modem/network time if available.
    // If no clock is set, leave recorded_at null.
    String timestamp = "";

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
        char buffer[25];
        strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ",
                 &timeinfo);
        timestamp = buffer;
    }

    String json = "{";
    if (timestamp.length()) {
        json += "\"recorded_at\":\"" + timestamp + "\",";
    } else {
        json += "\"recorded_at\":null,";
    }

    json += "\"latitude\":" + String(latitude, 6) + ",";
    json += "\"longitude\":" + String(longitude, 6);
    json += "}";

    String response;

    // Set destination URL.
    if (!sendATPrompt(
        String("AT+QHTTPURL=") + String(SERVER_URL).length() + ",80",
        response, 10000)) {
        Serial.println("# URL prompt failed");
        Serial.println(response);
        return false;
    }

    if (!sendPayload(String(SERVER_URL), response, 10000)) {
        Serial.println("# URL upload failed");
        Serial.println(response);
        return false;
    }

    // Ask modem to accept a POST body of exactly this length.
    String postCmd = String("AT+QHTTPPOST=") +
                     String(json.length()) + ",80,80";

    if (!sendATPrompt(postCmd, response, 10000)) {
        Serial.println("# POST prompt failed");
        Serial.println(response);
        return false;
    }

    if (!sendPayload(json, response, 90000)) {
        Serial.println("# POST body transfer failed");
        Serial.println(response);
        return false;
    }

    // The modem reports HTTP status asynchronously.
    response += readUntil("+QHTTPPOST:", 90000);
    Serial.println(response);

    if (response.indexOf("+QHTTPPOST: 0,201") >= 0) {
        Serial.println("# Record stored");
        return true;
    }

    if (response.indexOf("+QHTTPPOST: 0,200") >= 0) {
        Serial.println("# HTTP request succeeded");
        return true;
    }

    Serial.println("# Upload not confirmed; inspect modem response");
    return false;
}

void setup() {
    Serial.begin(115200);

    pinMode(MCU_SIM_EN_PIN, OUTPUT);
    digitalWrite(MCU_SIM_EN_PIN, HIGH);
    delay(8000);

    simSerial.begin(
        MCU_SIM_BAUDRATE,
        SERIAL_8N1,
        MCU_SIM_RX_PIN,
        MCU_SIM_TX_PIN
    );

    String response;

    if (!sendAT("AT", response, 5000)) {
        Serial.println("# EG800K not responding");
        return;
    }

    Serial.println("# EG800K ready");

    if (sendAT("AT+QGPS=1", response, 5000)) {
        Serial.println("# GPS started");
    } else {
        Serial.println("# GPS start response:");
        Serial.println(response);
    }

    modemReady = true;
    dataReady = setupCellular();
}

void loop() {
    if (!modemReady) {
        delay(5000);
        return;
    }

    getGPS();

    if (gpsFix && dataReady &&
        (lastUpload == 0 ||
         millis() - lastUpload >= UPLOAD_INTERVAL)) {
        lastUpload = millis();
        uploadRecord();
    }

    delay(5000);
}
