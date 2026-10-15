#include <HardwareSerial.h>

#define simSerial Serial0

#define MCU_SIM_BAUDRATE 115200
#define MCU_SIM_TX_PIN 21
#define MCU_SIM_RX_PIN 20
#define MCU_SIM_EN_PIN 2

String response;

bool sendAT(const char *cmd, String &output,
            unsigned long timeout = 2000) {
    while (simSerial.available()) {
        simSerial.read();
    }

    simSerial.println(cmd);
    output = "";

    unsigned long start = millis();

    while (millis() - start < timeout) {
        while (simSerial.available()) {
            char c = simSerial.read();
            output += c;
        }

        if (output.indexOf("OK") >= 0 ||
            output.indexOf("ERROR") >= 0) {
            break;
        }

        delay(5);
    }

    return output.indexOf("OK") >= 0;
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

    if (sendAT("AT", response)) {
        Serial.println("# EG800K ready");
    } else {
        Serial.println("# EG800K not responding");
    }

    if (sendAT("AT+QGPS=1", response, 3000)) {
        Serial.println("# GPS started");
    } else {
        Serial.println("# GPS start response:");
        Serial.println(response);
    }
}

void loop() {
    String response;

    if (sendAT("AT+QGPSLOC=0", response, 3000)) {
        int start = response.indexOf("+QGPSLOC:");

        if (start >= 0) {
            int end = response.indexOf('\n', start);

            String loc = response.substring(
                start + 9,
                end >= 0 ? end : response.length()
            );

            loc.trim();

            // Output the raw GNSS location fields.
            // Python will parse latitude and longitude.
            Serial.print("LOC,");
            Serial.println(loc);
        } else {
            Serial.println("# Waiting for GPS fix");
        }
    } else {
        Serial.println("# GPS query failed");
    }

    delay(5000);
}
