#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <NoDelay.h>
#include <Timezone.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include "arduino_secrets.h"

// ST7735 TFT module connections
#define TFT_RST   D3
#define TFT_CS    D8
#define TFT_DC    D4
// initialize ST7735 TFT library with hardware SPI module
// SCK (CLK) ---> NodeMCU pin D5 (GPIO14)
// MOSI(DIN) ---> NodeMCU pin D7 (GPIO13)
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
#define ST7735_GRAY 0x528a
#define ST7735_LIGHTBLUE 0x55bf

#define ROTARY_CLK D0
#define ROTARY_DT D1
#define ROTARY_SW D2


// Interval between WiFi network connection checks
noDelay networkingTime(5000);
// Interval between time display updates
noDelay ntpTime(1000);
// Interval between checking LMS playing song
noDelay lmsTime(10000);
// Interval between checking weather
noDelay weatherTime(60000);
// Interval between checking weather
noDelay dayWeatherTime(60000);
// Interval between checking weather
noDelay aquaTempTime(60000);
// Interval between state switch
noDelay stateTime(10000);
// Inteval between display updates
noDelay displayRefreshTime(200);

bool displayTime();
bool displayWeather();
bool displayDayWeather();
bool displayAquaTemp();
bool displayRoomTemp();
bool displayPlaying();

typedef bool (* StateFunction)();
StateFunction states[6] = {
  &displayTime,
  &displayRoomTemp,
  &displayWeather,
  &displayDayWeather,
  &displayAquaTemp,
  &displayPlaying
};
int currentState = 0;

DynamicJsonDocument lastDay0Weather(256);
DynamicJsonDocument lastDay1Weather(256);
DynamicJsonDocument lastDay2Weather(256);
DynamicJsonDocument lastDay3Weather(256);
DynamicJsonDocument lastDay4Weather(256);
DynamicJsonDocument lastDay5Weather(256);
bool gotLastDayWeather = false;
DynamicJsonDocument lastWeatherNow(300);
DynamicJsonDocument lastWeatherNext24Hours(256);
bool gotLastWeather = false;
DynamicJsonDocument lastAquaTemp(64);
bool gotLastAquaTemp = false;
DynamicJsonDocument lastPlaying(4096);
bool gotLastPlaying = false;
time_t lastTime = 0;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ntp.se", 0, 60000);
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     // Central European Summer Time
TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       // Central European Standard Time
Timezone CE(CEST, CET);

void setup() {
  
  pinMode(ROTARY_CLK, INPUT_PULLUP);
  pinMode(ROTARY_DT, INPUT_PULLUP);

  tft.initR(INITR_BLACKTAB);     // initialize a ST7735S chip, black tab
  tft.fillScreen(ST7735_BLACK);  // fill screen with black color
  tft.setRotation(1);

  Serial.begin(9600);
  while(!Serial && (millis() < 30000));
  
  // Establish WiFi connection
  ensureNetworkConnection();

  // Initialize time client
  timeClient.begin();
}

int rotary_value = 0;
void loop() {
  // Update time
  timeClient.update();

  int rotary = read_rotary();
  if(rotary != 0) {
    switchState(rotary);
  }
  
  if (stateTime.update()) {
    switchState(1);
  }
  if (displayRefreshTime.update()) {
    bool use = states[currentState]();
    while(!use) {
      switchState(1);
      use = states[currentState]();
    }
  }

  // Check WiFi connection and re-establish connection if not connected
  if (networkingTime.update()) {
    ensureNetworkConnection();
  }
}

void switchState(int direction) {
  currentState = currentState + direction;
  if(currentState>=sizeof(states)/sizeof(states[0])) {
    currentState = 0;
  }
  Serial.print("Switching to state: ");
  Serial.println(currentState);
  stateTime.start();
  tft.fillScreen(ST7735_BLACK);  // fill screen with black color
}

bool displayTime() {
  if (ntpTime.update() || lastTime == 0) {
    unsigned long unix_epoch = timeClient.getEpochTime();
    TimeChangeRule *tcr;
    lastTime = CE.toLocal(unix_epoch, &tcr);
  }
  if (lastTime != 0) {
    tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
    tft.setTextSize(5);
    tft.setCursor(7, 20);
    tft.printf( "%02u:%02u", hour(lastTime), minute(lastTime) );
  
    tft.setTextSize(2); 
    tft.setCursor(45, 80);
    tft.printf( "%s", weekdayStr(weekday(lastTime)));
    tft.setCursor(45, 100);
    tft.printf( "%02u %s", day(lastTime), monthStr(month(lastTime)));
  }
  return true;
}

bool displayWeather() {
  if (weatherTime.update() || !gotLastWeather ) {
    retrieveWeather(lastWeatherNow, 1);
    retrieveWeather(lastWeatherNext24Hours, 24);
  }
  if(gotLastWeather) {
    JsonObject jsonNow = lastWeatherNow.as<JsonObject>();

    tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
    if (jsonNow["tempAvg"].as<float>()<-20.0 || jsonNow["tempAvg"].as<float>()>25) {
      tft.setTextColor(ST7735_YELLOW, ST7735_BLACK);
    }else if (jsonNow["tempAvg"].as<float>()>-2.0 && jsonNow["tempAvg"].as<float>()<2) {
      tft.setTextColor(ST7735_YELLOW, ST7735_BLACK);
    }
    tft.setTextSize(4);
    tft.setCursor(20, 20);
    tft.printf( "%+0.1f", jsonNow["tempAvg"].as<float>());

    tft.setTextColor(ST7735_GRAY, ST7735_BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, 65);
    tft.printf( "Just nu");

    tft.setTextSize(2);
    tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
    if (jsonNow["windAvg"].as<float>()<8.0) {
      tft.setTextColor(ST7735_GRAY, ST7735_BLACK);
    }else if(jsonNow["windAvg"].as<float>()>14.0) {
      tft.setTextColor(ST7735_YELLOW, ST7735_BLACK);
    }
    tft.setCursor(10, 80);
    tft.printf( "%0.0f", jsonNow["windAvg"].as<float>());
    tft.setTextSize(1);
    tft.setCursor(35, 87);
    tft.printf( " m/s");

    tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
    if (jsonNow["precipitationTotal"].as<float>()<0.01) {
      tft.setTextColor(ST7735_GRAY, ST7735_BLACK);
    }else if (jsonNow["precipitationTotal"].as<float>()>4.0) {
      tft.setTextColor(ST7735_YELLOW, ST7735_BLACK);
    }
    
    tft.setTextSize(2);
    tft.setCursor(10, 100);
    tft.printf( "%0.0f", jsonNow["precipitationTotal"].as<float>());
    tft.setTextSize(1);
    tft.setCursor(35, 107);
    tft.printf( " mm");

    JsonObject json24Hours = lastWeatherNext24Hours.as<JsonObject>();

    tft.setTextColor(ST7735_GRAY, ST7735_BLACK);
    tft.setTextSize(1);
    tft.setCursor(80, 65);
    tft.printf( "24 tim");
    
    tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
    if (json24Hours["windAvg"].as<float>()<8.0) {
      tft.setTextColor(ST7735_GRAY, ST7735_BLACK);
    }else if(json24Hours["windAvg"].as<float>()>14.0) {
      tft.setTextColor(ST7735_YELLOW, ST7735_BLACK);
    }
    tft.setTextSize(2);
    tft.setCursor(80, 80);
    tft.printf( "%0.0f", json24Hours["windAvg"].as<float>());
    tft.setTextSize(1);
    tft.setCursor(105, 87);
    tft.printf( " m/s");

    tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
    if (json24Hours["precipitationTotal"].as<float>()<0.01) {
      tft.setTextColor(ST7735_GRAY, ST7735_BLACK);
    }else if (json24Hours["precipitationTotal"].as<float>()>15.0) {
      tft.setTextColor(ST7735_YELLOW, ST7735_BLACK);
    }
    
    tft.setTextSize(2);
    tft.setCursor(80, 100);
    tft.printf( "%0.0f", json24Hours["precipitationTotal"].as<float>());
    tft.setTextSize(1);
    tft.setCursor(105, 107);
    tft.printf( " mm");
}
  return true;
}

bool displayAquaTemp() {
  if (aquaTempTime.update() || !gotLastAquaTemp ) {
    retrieveAquaTemp(lastAquaTemp);
  }
  if(gotLastAquaTemp) {
    JsonObject json = lastAquaTemp.as<JsonObject>();

    tft.setTextColor(ST7735_BLUE, ST7735_BLACK);
    
    tft.setTextSize(4);
    tft.setCursor(20, 20);
    tft.printf( "%0.1f", json["tempOutside"].as<float>());
    tft.setTextSize(3);
    tft.setCursor(10, 70);
    tft.printf( "Akvarium");
  }
  return true;
}

bool displayRoomTemp() {
  if (aquaTempTime.update() || !gotLastAquaTemp ) {
    retrieveAquaTemp(lastAquaTemp);
  }
  if(gotLastAquaTemp) {
    JsonObject json = lastAquaTemp.as<JsonObject>();

    tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
    
    tft.setTextSize(4);
    tft.setCursor(20, 20);
    tft.printf( "%0.1f", json["tempInside"].as<float>());
    tft.setTextSize(3);
    tft.setCursor(10, 70);
    tft.printf( "Inomhus");
  }
  return true;
}

bool displayDayWeather() {
  if (dayWeatherTime.update() || !gotLastDayWeather ) {
    retrieveDayWeather(lastDay1Weather, 1);
    retrieveDayWeather(lastDay2Weather, 2);
    retrieveDayWeather(lastDay3Weather, 3);
    retrieveDayWeather(lastDay4Weather, 4);
  }
  if(gotLastDayWeather) {
    displayDayWeatherArea(lastDay0Weather, "Om 1 dag", 5,10);
    displayDayWeatherArea(lastDay1Weather, "Om 2 dagar", 85,10);
    displayDayWeatherArea(lastDay2Weather, "Om 3 dagar", 5,80);
    displayDayWeatherArea(lastDay3Weather, "Om 4 dagar", 85,80);
  }
  return true;
}

void displayDayWeatherArea(DynamicJsonDocument& jsonDocument, char* title, int x, int y) {
    JsonObject json = jsonDocument.as<JsonObject>();

    tft.setTextColor(ST7735_GRAY, ST7735_BLACK);
    tft.setTextSize(1);
    tft.setCursor(x, y);
    tft.printf( "%s", title);

    tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
    if (json["tempMax"].as<float>()<-20.0 || json["tempMax"].as<float>()>25) {
      tft.setTextColor(ST7735_YELLOW, ST7735_BLACK);
    }else if (json["tempMax"].as<float>()>-2.0 && json["tempMax"].as<float>()<2) {
      tft.setTextColor(ST7735_YELLOW, ST7735_BLACK);
    }
    tft.setTextSize(2);
    tft.setCursor(x, y+25);
    tft.printf( "%+0.0f", json["tempMax"].as<float>());
    

    tft.setTextSize(1);

    tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
    if (json["tempMin"].as<float>()<-20.0 || json["tempMin"].as<float>()>25) {
      tft.setTextColor(ST7735_YELLOW, ST7735_BLACK);
    }else if (json["tempMin"].as<float>()>-2.0 && json["tempMin"].as<float>()<2) {
      tft.setTextColor(ST7735_YELLOW, ST7735_BLACK);
    }
    tft.setCursor(x+45, y+15);
    tft.printf( "%+0.0f\xF7", json["tempMin"].as<float>());

    tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
    if (json["windAvg"].as<float>()<8.0) {
      tft.setTextColor(ST7735_GRAY, ST7735_BLACK);
    }else if(json["windAvg"].as<float>()>14.0) {
      tft.setTextColor(ST7735_YELLOW, ST7735_BLACK);
    }
    tft.setCursor(x+45, y+25);
    tft.printf( "%0.0f m/s", json["windAvg"].as<float>());
    

    tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
    if (json["precipitationTotal"].as<float>()<0.01) {
      tft.setTextColor(ST7735_GRAY, ST7735_BLACK);
    }else if (json["precipitationTotal"].as<float>()>4.0) {
      tft.setTextColor(ST7735_YELLOW, ST7735_BLACK);
    }
    
    tft.setTextSize(1);
    tft.setCursor(x+45, y+35);
    tft.printf( "%0.0f mm", json["precipitationTotal"].as<float>());
    
}

void retrieveWeather(DynamicJsonDocument& weatherJson, int hours) {
  WiFiClient client;
  HTTPClient http;

  char url[40];
  sprintf(url, "http://%s/next/%d", SECRET_WEATHER_SERVER, hours);
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.GET();
  if (httpResponseCode>0) {
    String payload = http.getString();
  
   // Parse JSON object
    DeserializationError error = deserializeJson(weatherJson, payload);
    if (!error) {
      gotLastWeather = true;
    }else {
      Serial.print("Error(");
      Serial.print(url);
      Serial.print("): ");
      Serial.println(error.c_str());
    }
  }
  else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

void retrieveAquaTemp(DynamicJsonDocument& jsonResponse) {
  WiFiClient client;
  HTTPClient http;

  char url[40];
  sprintf(url, "http://%s/", SECRET_AQUATEMP_IP);
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.GET();
  if (httpResponseCode>0) {
    String payload = http.getString();
  
   // Parse JSON object
    DeserializationError error = deserializeJson(jsonResponse, payload);
    if (!error) {
      gotLastAquaTemp = true;
    }else {
      Serial.print("Error: ");
      Serial.println(error.c_str());
    }
  }
  else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

void retrieveDayWeather(DynamicJsonDocument& weatherJson, int dayNo) {
  WiFiClient client;
  HTTPClient http;

  char url[40];
  sprintf(url, "http://%s/days/%d", SECRET_WEATHER_SERVER, dayNo);
  
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.GET();
  if (httpResponseCode>0) {
    String payload = http.getString();
    
   // Parse JSON object
    DeserializationError error = deserializeJson(weatherJson, payload);
    if (!error) {
      gotLastDayWeather = true;
    }else {
      Serial.print("Error: ");
      Serial.println(error.c_str());
    }
  }
  else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

bool displayPlaying() {
  if (lmsTime.update() || !gotLastPlaying) {
    WiFiClient client;
    HTTPClient http;

    char url[40];
    sprintf(url, "http://%s:9000/jsonrpc.js", SECRET_LMS_IP);
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    // Data to send with HTTP POST
    char httpRequestData[128];
    snprintf(httpRequestData, sizeof(httpRequestData), "{\"method\": \"slim.request\", \"params\": [\"%s\", [\"status\", \"-\", 1, \"tags:al\"]]}", SECRET_PLAYER_ID);
    // Send HTTP POST request
    int httpResponseCode = http.POST(httpRequestData);
    if (httpResponseCode>0) {
      String payload = http.getString();

     // Parse JSON object
      DeserializationError error = deserializeJson(lastPlaying, payload);
      if (!error) {
        gotLastPlaying = true;
      }else {
        Serial.print("Error: ");
        Serial.println(error.c_str());
      }
    }
    else {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  }
  if (gotLastPlaying) {
    JsonObject json = lastPlaying.as<JsonObject>();
    if (strcmp(json["result"]["mode"].as<char*>(),"play")!=0) {
      return false;
    }
    if (json["result"]["playlist_loop"].size()>0) {

      tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
      tft.setTextSize(1);
      tft.setCursor(10, 20);
      tft.printf( "%s", json["result"]["playlist_loop"][0]["title"].as<char*>());
    
      tft.setCursor(10, 40);
      tft.printf( "%s", json["result"]["playlist_loop"][0]["album"].as<char*>());
  
      tft.setCursor(10, 60);
      tft.printf( "%s", json["result"]["playlist_loop"][0]["artist"].as<char*>());
    }

  }
  return true;
}
// Ensure network is connected and connect if it isn't
int wifiStatus = WL_IDLE_STATUS;
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
void ensureNetworkConnection() {
  wifiStatus = WiFi.status();
  if( wifiStatus != WL_CONNECTED ) {
    while (wifiStatus != WL_CONNECTED) {
      Serial.print("Attempting to connect to network:");
      Serial.println(ssid);
      wifiStatus = WiFi.begin(ssid, pass);
  
      int i=0;
      while (wifiStatus != WL_CONNECTED && i<10) {
        delay(1000);
        wifiStatus = WiFi.status();
        i++;
      }
    }
    Serial.print("You're connected to the network: ");
    Serial.print(WiFi.SSID());
    Serial.print(" with IP: ");
    Serial.println(WiFi.localIP());
  }
}

static uint8_t prevNextCode = 0;
static uint16_t store=0;
// A vald CW or  CCW move returns 1, invalid returns 0.
int8_t read_rotary() {
  static int8_t rot_enc_table[] = {0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0};

  prevNextCode <<= 2;
  if (digitalRead(ROTARY_DT)) prevNextCode |= 0x02;
  if (digitalRead(ROTARY_CLK)) prevNextCode |= 0x01;
  prevNextCode &= 0x0f;

   // If valid then store as 16 bit data.
   if  (rot_enc_table[prevNextCode] ) {
      store <<= 4;
      store |= prevNextCode;
      //if (store==0xd42b) return 1;
      //if (store==0xe817) return -1;
      if ((store&0xff)==0x2b) return -1;
      if ((store&0xff)==0x17) return 1;
   }
   return 0;
}

char* monthStr(int month) {
  switch(month) {
    case 1:
      return "Jan";
    case 2:
      return "Feb";
    case 3: 
      return "Mar";
    case 4:
      return "Apr";
    case 5:
      return "Maj";
    case 6:
      return "Jun";
    case 7:
      return "Jul";
    case 8:
      return "Aug";
    case 9:
      return "Sep";
    case 10:
      return "Okt";
    case 11:
      return "Nov";
    case 12:
      return "Dec";
    default:
      return "";
  }
}

char* weekdayStr(int weekday) {
  switch(weekday) {
    case 1:
      return "S\x94ndag";
    case 2:
      return "M\x86ndag";
    case 3:
      return "Tisdag";
    case 4:
      return "Onsdag";
    case 5:
      return "Torsdag";
    case 6:
      return "Fredag";
    case 7:
      return "L\x94rdag";
    default:
      return "";
  }
}
