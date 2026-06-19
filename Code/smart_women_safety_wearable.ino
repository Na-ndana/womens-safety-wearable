#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <TinyGPSPlus.h>
#include "esp_camera.h"
#include <Wire.h>
#include "RTClib.h"

RTC_DS3231 rtc;

// ----------------- USER CONFIG -----------------

const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

const char* BOT_TOKEN = "YOUR_BOT_TOKEN";
const char* CHAT_ID   = "YOUR_CHAT_ID";

const int PHOTO_COUNT = 5;
const unsigned long PHOTO_INTERVAL_MS = 3000;

const int VIDEO_FRAMES = 10;
const unsigned long VIDEO_FRAME_DELAY_MS = 1000;
// pins
#define GPS_RX_PIN 16
#define MIC_PIN 15
#define VIB_PIN 4
#define SOS_PIN 0

const char* TELEGRAM_HOST = "api.telegram.org";

// ----------------- Camera config (AI-Thinker) -----------------
camera_config_t camera_config = {
  .pin_pwdn = 32,
  .pin_reset = -1,
  .pin_xclk = 0,
  .pin_sccb_sda = 26,
  .pin_sccb_scl = 27,
  .pin_d7 = 35,
  .pin_d6 = 34,
  .pin_d5 = 39,
  .pin_d4 = 36,
  .pin_d3 = 21,
  .pin_d2 = 19,
  .pin_d1 = 18,
  .pin_d0 = 5,
  .pin_vsync = 25,
  .pin_href = 23,
  .pin_pclk = 22,
  .xclk_freq_hz = 20000000,
  .ledc_timer = LEDC_TIMER_0,
  .ledc_channel = LEDC_CHANNEL_0,
  .pixel_format = PIXFORMAT_JPEG,
  .frame_size = FRAMESIZE_VGA,
  .jpeg_quality = 12,
  .fb_count = 1
};

// ----------------- Globals -----------------
HardwareSerial GPSSerial(1);
TinyGPSPlus gps;
WiFiClientSecure tlsClient;

volatile bool sos_flag = false;
volatile bool vib_flag = false;
volatile bool mic_flag = false;

unsigned long lastDebounce = 0;
const unsigned long debounceMs = 1200;

volatile unsigned long lastTamper = 0;
const unsigned long tamperCooldown = 5000;

String lastLat = "", lastLng = "", lastTimeStr = "";

// ----------------- ISRs ----------------
void IRAM_ATTR sosISR() {
  if (millis() - lastDebounce > debounceMs) {
    sos_flag = true;
    lastDebounce = millis();
  }
}

void IRAM_ATTR vibISR() {
  unsigned long now = millis();
  if (now - lastTamper > tamperCooldown) {
    vib_flag = true;
    lastTamper = now;
  }
}

// ----------------- Camera capture ------------
bool captureJPEG(uint8_t **outBuf, size_t *outLen) {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return false;
  }
  *outLen = fb->len;
  *outBuf = (uint8_t*)malloc(*outLen);
  if (!*outBuf) {
    esp_camera_fb_return(fb);
    Serial.println("malloc failed");
    return false;
  }
  memcpy(*outBuf, fb->buf, *outLen);
  esp_camera_fb_return(fb);
  return true;
}

// ----------------- GPS + RTC timestamp ----------------
void updateGpsStrings() {
  if (gps.location.isValid()) {
    lastLat = String(gps.location.lat(), 6);
    lastLng = String(gps.location.lng(), 6);
  } else {
    lastLat = ""; 
    lastLng = "";
  }

  DateTime now = rtc.now();
  char buf[32];
  snprintf(buf, sizeof(buf),
         "%04d-%02d-%02d %02d:%02d:%02d",
         now.year(), now.month(), now.day(),
         now.hour(), now.minute(), now.second());
  lastTimeStr = String(buf);
}

// ----------------- Telegram upload ----------------
bool sendPhotoToTelegram(const uint8_t *buf, size_t len, const char* filename, const char* caption) {
  if (!tlsClient.connect(TELEGRAM_HOST, 443)) {
    Serial.println("TLS connect failed");
    return false;
  }
  tlsClient.setTimeout(30);

  String url = String("/bot") + BOT_TOKEN + "/sendPhoto";
  String boundary = "----ESP32CamBoundary";

  String head = "--" + boundary + "\r\n"
    + "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n"
    + CHAT_ID + "\r\n"
    + "--" + boundary + "\r\n"
    + "Content-Disposition: form-data; name=\"caption\"\r\n\r\n"
    + caption + "\r\n"
    + "--" + boundary + "\r\n"
    + "Content-Disposition: form-data; name=\"photo\"; filename=\"" + String(filename) + "\"\r\n"
    + "Content-Type: image/jpeg\r\n\r\n";

  String tail = "\r\n--" + boundary + "--\r\n";

  unsigned long contentLength = head.length() + len + tail.length();

  String hdr = String("POST ") + url + " HTTP/1.1\r\n"
               + "Host: " + TELEGRAM_HOST + "\r\n"
               + "User-Agent: ESP32-CAM\r\n"
               + "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n"
               + "Content-Length: " + String(contentLength) + "\r\n"
               + "Connection: close\r\n\r\n";

  tlsClient.print(hdr);
  tlsClient.print(head);

  size_t sent = 0;
  const size_t CHUNK = 1024;
  while (sent < len) {
    size_t toSend = min(CHUNK, len - sent);
    tlsClient.write(buf + sent, toSend);
    sent += toSend;
    delay(10);
  }

  tlsClient.print(tail);

  unsigned long tout = millis();
  String resp = "";
  while (millis() - tout < 10000 && tlsClient.connected()) {
    while (tlsClient.available()) {
      resp += (char)tlsClient.read();
    }
  }
  tlsClient.stop();

  return resp.indexOf("\"ok\":true") >= 0;
}

// ----------------- EMERGENCY ACTION ----------------
void doEmergencySequence(const char* reason) {
  updateGpsStrings();

  String maps = lastLat.length() ? 
    String("http://maps.google.com/?q=")+lastLat+","+lastLng : 
    "NoFix";

  String captionBase = String(reason) + 
                       " | " + maps + 
                       " | " + lastTimeStr;

  // Photos
  for (int i=0;i<PHOTO_COUNT;i++) {
    uint8_t *buf = nullptr; size_t len = 0;
    if (captureJPEG(&buf, &len)) {
      String fname = "photo_" + String(millis()) + ".jpg";
      sendPhotoToTelegram(buf, len, fname.c_str(), captionBase.c_str());
      free(buf);
    }
    delay(PHOTO_INTERVAL_MS);
  }

  // Video frames
  for (int f=0; f<VIDEO_FRAMES; f++) {
    uint8_t *buf = nullptr; size_t len = 0;
    if (captureJPEG(&buf, &len)) {
      String fname = "vframe_" + String(millis()) + ".jpg";
      sendPhotoToTelegram(buf, len, fname.c_str(), captionBase.c_str());
      free(buf);
    }
    delay(VIDEO_FRAME_DELAY_MS);
  }
}

// ----------------- TAMPER ----------------
void doTamper() {
  updateGpsStrings();
  String maps = lastLat.length() ? 
    String("http://maps.google.com/?q=")+lastLat+","+lastLng : "NoFix";

  String caption = String("TAMPER ALERT | ") + maps + " | " + lastTimeStr;

  uint8_t *buf = nullptr; size_t len = 0;
  if (captureJPEG(&buf, &len)) {
    sendPhotoToTelegram(buf, len, "tamper.jpg", caption.c_str());
    free(buf);
  } else {
    Serial.println("Tamper: no photo");
  }
}

// ----------------- setup -----------------
void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(14, 15);
  rtc.begin();

  if (esp_camera_init(&camera_config) == ESP_OK)
    Serial.println("Camera OK");
  else
    Serial.println("Camera FAIL");

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  tlsClient.setInsecure();

  GPSSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, -1);

  pinMode(MIC_PIN, INPUT);
  pinMode(VIB_PIN, INPUT);
  pinMode(SOS_PIN, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(SOS_PIN), sosISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(VIB_PIN), vibISR, RISING);

  Serial.println("READY");
}

// ----------------- loop -----------------
void loop() {
  while (GPSSerial.available()) gps.encode(GPSSerial.read());

  if (digitalRead(MIC_PIN) == HIGH) mic_flag = true;

  if (sos_flag) { sos_flag = false; doEmergencySequence("SOS BUTTON PRESSED"); }
  if (mic_flag) { mic_flag = false; doEmergencySequence("LOUD SOUND DETECTED"); }
  if (vib_flag) { vib_flag = false; doTamper(); }

  delay(30);
}
