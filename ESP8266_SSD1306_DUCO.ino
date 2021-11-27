/*
  ,------.          ,--.                       ,-----.       ,--.
  |  .-.  \ ,--.,--.`--',--,--,  ,---. ,-----.'  .--./ ,---. `--',--,--,
  |  |  \  :|  ||  |,--.|      \| .-. |'-----'|  |    | .-. |,--.|      \
  |  '--'  /'  ''  '|  ||  ||  |' '-' '       '  '--'\' '-' '|  ||  ||  |
  `-------'  `----' `--'`--''--' `---'         `-----' `---' `--'`--''--'
  Official code for ESP8266 boards                          version 2.7.3

  Duino-Coin Team & Community 2019-2021 © MIT Licensed
  https://duinocoin.com
  https://github.com/revoxhere/duino-coin

  If you don't know where to start, visit official website and navigate to
  the Getting Started page. Have fun mining!
*/

/* If during compilation the line below causes a
  "fatal error: arduinoJson.h: No such file or directory"
  message to occur; it means that you do NOT have the
  ArduinoJSON library installed. To install it,
  go to the below link and follow the instructions:
  https://github.com/revoxhere/duino-coin/issues/832 */
#include <ArduinoJson.h>

  /* If during compilation the line below causes a
    "fatal error: Crypto.h: No such file or directory"
    message to occur; it means that you do NOT have the
    latest version of the ESP8266/Arduino Core library.
    To install/upgrade it, go to the below link and
    follow the instructions of the readme file:
    https://github.com/esp8266/Arduino */
#include <Crypto.h>  // experimental SHA1 crypto library
using namespace experimental::crypto;

#include <ESP8266WiFi.h> // Include WiFi library
#include <ESP8266mDNS.h> // OTA libraries
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Ticker.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/Picopixel.h>
#include <Fonts/TomThumb.h>

#define SCREEN_WIDTH    128 // OLED display width, in pixels
#define SCREEN_HEIGHT    64 // OLED display height, in pixels
#define OLED_RESET       -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C // <- See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

// -------------------------------------------------------------------
// | Vflip && Hflip Edit file :
// |    ~/Arduino/libraries/Adafruit_SSD1306/Adafruit_SSD1306.cpp
// -------------------------------------------------------------------
// static const uint8_t PROGMEM init3[] = {
//   SSD1306_MEMORYMODE,       // 0x20
//   0x00,                     // 0x0 act like ks0108
//   SSD1306_SEGREMAP | 0x1,   // Original Use for normal display
//   SSD1306_COMSCANDEC        // Original Use for normal display
// };
// -------------------------------------------------------------------
// static const uint8_t PROGMEM init3[] = {
//   SSD1306_MEMORYMODE,       // 0x20
//   0x00,                     // 0x0 act like ks0108
//   SSD1306_SEGREMAP,         // Modify Use for Vflip display
//   SSD1306_COMSCANINC        // Modify Use for Hflip display
// };
// -------------------------------------------------------------------

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

namespace {
  const char* SSID           = "SSID";       // Change this to your WiFi name
  const char* PASSWORD       = "PASSWORD";   // Change this to your WiFi password
  const char* USERNAME       = "USERNAME";   // Change this to your Duino-Coin username
  const char* RIG_IDENTIFIER = "0) ESP8266"; // Change this if you want a custom miner name
  const char* RIG_HOSTNAME   = "0_ESP8266";
  const bool USE_HIGHER_DIFF = true;         // Change to true if using 160 MHz to not get the first share rejected

  const char* urlPool = "http://51.15.127.80:4242/getPool";
  unsigned int share_count = 0; // Share variable
  String host = "";
  int port = 0;

  WiFiClient client;
  String client_buffer = "";
  String chipID        = "";
  String START_DIFF    = "";

  // Loop WDT... please don't feed me...
  // See lwdtcb() and lwdtFeed() below
  Ticker lwdTimer;
  #define LWD_TIMEOUT   60000

  unsigned long lwdCurrentMillis = 0;
  unsigned long lwdTimeOutMillis = LWD_TIMEOUT;

  #define END_TOKEN  '\n'
  #define SEP_TOKEN  ','

  #define LED_BUILTIN 2

  #define BLINK_SHARE_FOUND    1
  #define BLINK_SETUP_COMPLETE 2
  #define BLINK_CLIENT_CONNECT 3
  #define BLINK_RESET_DEVICE   5

  void UpdateHostPort(String input) {
    // Thanks @ricaun for the code
    DynamicJsonDocument doc(256);
    deserializeJson(doc, input);
    const char* name = doc["name"];
    host = String((const char*)doc["ip"]);
    port = int(doc["port"]);
    // Serial.println("Fetched pool: " + String(name) + " " + String(host) + " " + String(port));
  }

  String httpGetString(String URL) {
    String payload = "";
    WiFiClient client;
    HTTPClient http;
    if (http.begin(client, URL)) {
      int httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        payload = http.getString();
      }
      // else {
      //   Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
      // }
      http.end();
    }
    return payload;
  }

  void UpdatePool() {
    String input = httpGetString(urlPool);
    if (input == "") return;
    UpdateHostPort(input);
  }


  void SetupWifi() {
    // Serial.println("Connecting to: " + String(SSID));
    WiFi.mode(WIFI_STA); // Setup ESP in client mode
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.begin(SSID, PASSWORD);

    int wait_passes = 0;
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
      if (++wait_passes >= 10) {
        WiFi.begin(SSID, PASSWORD);
        wait_passes = 0;
      }
    }
    // Serial.println("\nConnected to WiFi!");
    // Serial.println("Local IP address: " + WiFi.localIP().toString());
    UpdatePool();
  }

  void SetupOTA() {
    // Prepare OTA handler
    ArduinoOTA.onStart([]() {
      // Serial.println("Start");
    });
    ArduinoOTA.onEnd([]() {
      // Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      // Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.setHostname(RIG_HOSTNAME); // Give port a name not just address
    ArduinoOTA.begin();
  }

  void blink(uint8_t count, uint8_t pin = LED_BUILTIN) {
    uint8_t state = HIGH;
    for (int x = 0; x < (count << 1); ++x) {
      digitalWrite(pin, state ^= HIGH);
      delay(20);
    }
  }

  void RestartESP(String msg) {
    // Serial.println(msg);
    // Serial.println("Resetting ESP...");
    // blink(BLINK_RESET_DEVICE);
    ESP.reset();
  }

  // Our new WDT to help prevent freezes
  // code concept taken from https://sigmdel.ca/michel/program/esp8266/arduino/watchdogs2_en.html
  void ICACHE_RAM_ATTR lwdtcb(void) {
    if ((millis() - lwdCurrentMillis > LWD_TIMEOUT) || (lwdTimeOutMillis - lwdCurrentMillis != LWD_TIMEOUT))
      RestartESP("Loop WDT Failed!");
  }

  void lwdtFeed(void) {
    lwdCurrentMillis = millis();
    lwdTimeOutMillis = lwdCurrentMillis + LWD_TIMEOUT;
  }

  void VerifyWifi() {
    while (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0))
      WiFi.reconnect();
  }

  void handleSystemEvents(void) {
    VerifyWifi();
    ArduinoOTA.handle();
    yield();
  }

  // https://stackoverflow.com/questions/9072320/split-string-into-string-array
  String getValue(String data, char separator, int index) {
    int found      = 0;
    int strIndex[] = { 0, -1 };
    int max_index  = data.length() - 1;

    for (int i = 0; i <= max_index && found <= index; i++) {
      if (data.charAt(i) == separator || i == max_index) {
        found++;
        strIndex[0] = strIndex[1] + 1;
        strIndex[1] = (i == max_index) ? i + 1 : i;
      }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
  }

  void waitForClientData(void) {
    client_buffer = "";
    while (client.connected()) {
      if (client.available()) {
        client_buffer = client.readStringUntil(END_TOKEN);
        if (client_buffer.length() == 1 && client_buffer[0] == END_TOKEN)
          client_buffer = "???\n"; // NOTE: Should never happen
        break;
      }
      handleSystemEvents();
    }
  }

  void ConnectToServer() {
    if (client.connected()) return;
    // Serial.println("\nConnecting to Duino-Coin server...");
    while (!client.connect(host, port));
    waitForClientData();
    // Serial.println("Connected to the server. Server version: " + client_buffer );
    // blink(BLINK_CLIENT_CONNECT); // Sucessfull connection with the server
  }

  bool max_micros_elapsed(unsigned long current, unsigned long max_elapsed) {
    static unsigned long _start = 0;
    if ((current - _start) > max_elapsed) {
      _start = current;
      return true;
    }
    return false;
  }
} // namespace

void setTextStyles(int style = 0) {
  switch (style) {
  case 0:
    display.setFont();                   // Reset Font
    display.setTextSize(1);              // Normal 1:1 pixel scale
    display.setTextColor(SSD1306_WHITE); // Draw white text
    break;
  case 1:
    display.setFont(&Picopixel);         // Use Font Picopixel
    display.setTextSize(2);              // Draw 2X-scale text
    display.setTextColor(SSD1306_WHITE); // Draw white text
    break;
  case 2:
    display.setFont(&TomThumb);          // Use Font TomThumb
    display.setTextSize(2);              // Draw 2X-scale text
    display.setTextColor(SSD1306_WHITE); // Draw white text
    break;
  case 3:
    display.setFont(&Picopixel);         // Use Font Picopixel
    display.setTextSize(3);              // Draw 3X-scale text
    display.setTextColor(SSD1306_WHITE); // Draw white text
    break;
  case 4:
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); // Draw 'inverse' text
    break;
  default:
    break;
  }
}

String UPUSERNAME;
String STATE;
String NONCE;
String HASHRATE;
String HASHTIME;

void displayWellcome(void) {
  display.clearDisplay();
  display.setFont(&Picopixel);
  display.drawRoundRect(0, 0, display.width() - 1, display.height() - 1, 8, SSD1306_WHITE);
  setTextStyles(3);
  display.setCursor(11, 22);
  display.println(F("DUINO COIN"));
  setTextStyles(2);
  display.setCursor(14, 40);
  display.println(F("ESP8266 MINER"));
  display.setCursor(22, 54);
  display.println(F("VERSION 2.7X"));
  display.display();
}

void displayInfo(void) {
  if (STATE != "") {
    display.clearDisplay();
    display.fillRoundRect(0, 0, display.width(), 14, 4, SSD1306_INVERSE);
    setTextStyles(2);
    display.setCursor(7, 11);
    display.setTextColor(SSD1306_INVERSE);
    display.println(UPUSERNAME + " : " + NONCE);
    display.setTextColor(SSD1306_WHITE);
    setTextStyles();
    display.setCursor(0, 20);
    display.print(STATE);
    display.print(" : ");
    setTextStyles(1);
    display.print("# ");
    display.println(String(share_count));

    setTextStyles();
    display.setCursor(0, 32);
    display.print(F("RATE : "));
    setTextStyles(1);
    display.print(HASHRATE);
    display.println(F(" kH/s"));
    setTextStyles();
    display.setCursor(0, 44);
    display.print(F("TIME : "));
    setTextStyles(1);
    display.print(HASHTIME);
    display.println(F(" Sec"));

    setTextStyles();
    display.fillRoundRect(0, 54, display.width() - 1, 10, 4, SSD1306_INVERSE);
    display.setTextColor(SSD1306_INVERSE);
    display.setCursor(4, 55);
    display.println(F("ASKING FOR A NEW JOB"));
    display.display();
  } else {
    display.clearDisplay();
    display.drawRoundRect(0, 0, display.width() - 1, display.height() - 1, 8, SSD1306_WHITE);
    setTextStyles(3);
    display.setCursor(20, 28);
    display.println(UPUSERNAME);
    setTextStyles();
    display.setCursor(4, 40);
    display.println(F("ASKING FOR A NEW JOB"));
    display.display();
  }
}

void displayJob(String last, String exp, String diff) {
  display.clearDisplay();
  display.drawRoundRect(0, 0, display.width() - 1, display.height() - 1, 8, SSD1306_WHITE);
  setTextStyles();
  display.setCursor(6, 6);
  display.println(F("--[ JOB RECIVED ]--"));
  display.setCursor(6, 22);
  display.print(F("DIFFICULTY > "));
  setTextStyles(1);
  display.println(diff);
  setTextStyles();
  display.setCursor(6, 36);
  display.print(F("LAST > "));
  setTextStyles(1);
  display.println(last);
  setTextStyles();
  display.setCursor(6, 50);
  display.print(F("EXP  > "));
  setTextStyles(1);
  display.println(exp);
  display.display();
}

void setup() {
  // Serial.begin(500000);
  // Serial.println("\nDuino-Coin ESP8266 Miner v2.7");

  pinMode(LED_BUILTIN, OUTPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    // Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ; // Don't proceed, loop forever
  }

  displayWellcome();
  SetupWifi();
  SetupOTA();
  lwdtFeed();
  lwdTimer.attach_ms(LWD_TIMEOUT, lwdtcb);
  if (USE_HIGHER_DIFF) START_DIFF = "ESP8266H";
  else START_DIFF = "ESP8266";
  chipID = String(ESP.getChipId(), HEX);
  UPUSERNAME = String(USERNAME);
  UPUSERNAME.toUpperCase();
  blink(BLINK_SETUP_COMPLETE);
}

void loop() {
  // 1 minute watchdog
  lwdtFeed();

  // OTA handlers
  VerifyWifi();
  ArduinoOTA.handle();

  ConnectToServer();
  displayInfo();
  // Serial.println("Asking for a new job for user: " + String(USERNAME));
  client.print("JOB," + String(USERNAME) + "," + String(START_DIFF));

  waitForClientData();
  String last_block_hash  = getValue(client_buffer, SEP_TOKEN, 0);
  String expected_hash    = getValue(client_buffer, SEP_TOKEN, 1);
  unsigned int difficulty = getValue(client_buffer, SEP_TOKEN, 2).toInt() * 100 + 1;

  // Serial.println("Job received: "
  //   + last_block_hash
  //   + " "
  //   + expected_hash
  //   + " "
  //   + String(difficulty)
  // );
  String last_block_hash1 = last_block_hash;
  last_block_hash1.toUpperCase();
  expected_hash.toUpperCase();

  char last_block_hash2[10];
  char expected_hash1[10];

  last_block_hash1.toCharArray(last_block_hash2, 10);
  expected_hash.toCharArray(expected_hash1, 10);

  displayJob(String(last_block_hash2), String(expected_hash1), String(difficulty));

  float start_time = micros();
  max_micros_elapsed(start_time, 0);

  for (unsigned int duco_numeric_result = 0; duco_numeric_result < difficulty; duco_numeric_result++) {
    // Difficulty loop
    String result = SHA1::hash(last_block_hash + String(duco_numeric_result));

    if (result == expected_hash) {
      // If result is found
      unsigned long elapsed_time = micros() - start_time;
      float elapsed_time_s = elapsed_time * .000001f;
      float hashrate = duco_numeric_result / elapsed_time_s;
      share_count++;

      client.print(String(duco_numeric_result)
        + ","
        + String(hashrate)
        + ",Official ESP8266 Miner 2.73"
        + ","
        + String(RIG_IDENTIFIER)
        + ",DUCOID"
        + String(chipID)
      );

      waitForClientData();
      // Serial.println(client_buffer
      //   + " share #"
      //   + String(share_count)
      //   + " (" + String(duco_numeric_result) + ")"
      //   + " hashrate: "
      //   + String(hashrate / 1000, 2)
      //   + " kH/s ("
      //   + String(elapsed_time_s)
      //   + "s"
      // );

      STATE = client_buffer;
      if (STATE == "BAD") STATE += " ";
      NONCE = String(duco_numeric_result);
      HASHRATE = String(hashrate / 1000, 2);
      HASHTIME = String(elapsed_time_s);

      blink(BLINK_SHARE_FOUND);
      break;
    }
    if (max_micros_elapsed(micros(), 250000))
      handleSystemEvents();
  }
}
