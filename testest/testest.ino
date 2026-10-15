void setup() {
    Serial.begin(115200);
    Serial.println("ESP32-C3 is running!");
}
int count = 1;
void loop() {
    Serial.print("Hello ");
    Serial.print(count);
    count += 1;
    Serial.print("\n");
    delay(1000);
}