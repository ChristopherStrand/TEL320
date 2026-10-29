#include <bluefruit.h>

BLEUart bleuart;

char line[64];
int pos = 0;

void startAdv()
{
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

void setup()
{
  Serial.begin(115200);
  Serial1.begin(115200);

  Bluefruit.begin();
  Bluefruit.setName("A111 Radar");
  Bluefruit.setTxPower(4);

  bleuart.begin();
  startAdv();
}

void loop()
{
  while (Serial1.available()) {
    char c = Serial1.read();

    if (c == '\n') {
      line[pos] = 0;
      Serial.println(line);

      if (Bluefruit.connected()) {
        bleuart.print(line);
        bleuart.print("\n");
      }
      pos = 0;
    } else if (pos < (int)sizeof(line) - 1) {
      line[pos++] = c;
    }
  }
}
