#include "esp_camera.h"
#include <Arduino.h>
#include "FS.h"
#include "SPI.h"
#include "SD_MMC.h"
#include "driver/rtc_io.h"
#include "time.h"
#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <Firebase_ESP_Client.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <ArduinoJson.h>
#include <BLEServer.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <Preferences.h>

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define WIFI_SSID "YOUR_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PW"
#define FIREBASE_HOST "https://YOUR_HOSTNAME.ZONE.firebasedatabase.app"
#define API_KEY "YOUR_API_KEY"
#define FIREBASE_PROJECT_ID "YOUR_PROJECT_ID"
#define FIREBASE_CLIENT_EMAIL "YOUR_FIREBASE_CLIENT_EMAIL"
#define STORAGE_BUCKET_ID "YOUR_STORAGE_BUCKET_ID.appspot.com"
const char PRIVATE_KEY[] PROGMEM = "-----BEGIN PRIVATE KEY-----\nYOUR_PRIVATE_KEY\n-----END PRIVATE KEY-----\n";

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig fbconfig;
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;

#define SERVICE_UUID  "0000aaaa-ead2-11e7-80c1-9a214cf093ae"
#define WIFI_UUID     "00005555-ead2-11e7-80c1-9a214cf093ae"

char apName[] = "BIRBBRO-xxxxxxxxxxxx";
bool hasCredentials = false;
bool BLEActivated = false;

String bleWiFiSSID;
String bleWiFiPassword;

BLECharacteristic *pCharacteristicWiFi;
BLEAdvertising* pAdvertising;
BLEService *pService;
BLEServer *pServer;

StaticJsonBuffer<200> jsonBuffer;

void gcsUploadCallback(UploadStatusInfo info);
void goToSleep();
unsigned long getTime();
void sendFCMMessage(String token);
void createName();

class MyServerCallbacks: public BLEServerCallbacks {
  // TODO this doesn't take into account several clients being connected
  void onConnect(BLEServer* pServer) {
    Serial.println("BLE client connected");
  };

  void onDisconnect(BLEServer* pServer) {
    Serial.println("BLE client disconnected");
    pAdvertising->start();
  }
};

class MyCallbackHandler: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    if (value.length() == 0) {
      return;
    }
    Serial.println("Received over BLE: " + String((char *)&value[0]));

    int keyIndex = 0;
    for (int index = 0; index < value.length(); index ++) {
      value[index] = (char) value[index] ^ (char) apName[keyIndex];
      keyIndex++;
      if (keyIndex >= strlen(apName)) keyIndex = 0;
    }

    JsonObject& jsonIn = jsonBuffer.parseObject((char *)&value[0]);
    if (jsonIn.success()) {
      if (jsonIn.containsKey("ssid") &&
          jsonIn.containsKey("pw") && 
          jsonIn.containsKey("ssidSec")) {
        bleWiFiSSID = jsonIn["ssid"].as<String>();
        bleWiFiPassword = jsonIn["pw"].as<String>();

        Preferences preferences;
        preferences.begin("WiFiCred", false);
        preferences.putString("ssid", bleWiFiSSID);
        preferences.putString("pw", bleWiFiPassword);
        preferences.putBool("valid", true);
        preferences.end();

        Serial.println("Received over bluetooth:");
        Serial.println("SSID: " + bleWiFiSSID + " password: " + bleWiFiPassword);
        hasCredentials = true;
      } 
    } else {
      Serial.println("Received invalid JSON");
    }
    jsonBuffer.clear();
  };

  void onRead(BLECharacteristic *pCharacteristic) {
    Serial.println("BLE onRead request");
    String wifiCredentials;
    JsonObject& jsonOut = jsonBuffer.createObject();
    jsonOut["ssid"] = bleWiFiSSID;
    jsonOut["pw"] = bleWiFiPassword;

    jsonOut.printTo(wifiCredentials);

    int keyIndex = 0;
    Serial.println("Stored settings: " + wifiCredentials);
    for (int index = 0; index < wifiCredentials.length(); index ++) {
      wifiCredentials[index] = (char) wifiCredentials[index] ^ (char) apName[keyIndex];
      keyIndex++;
      if (keyIndex >= strlen(apName)) keyIndex = 0;
    }
    pCharacteristicWiFi->setValue((uint8_t*)&wifiCredentials[0],wifiCredentials.length());
    jsonBuffer.clear();
  }
};

void initBLE() {
  // Initialize BLE and set output power
  BLEDevice::init(apName);
  BLEDevice::setPower(ESP_PWR_LVL_P7);

  // Create BLE Server
  pServer = BLEDevice::createServer();

  // Set server callbacks
  pServer->setCallbacks(new MyServerCallbacks());

  // Create BLE Service
  pService = pServer->createService(BLEUUID(SERVICE_UUID),20);

  // Create BLE Characteristic for WiFi settings
  pCharacteristicWiFi = pService->createCharacteristic(
    BLEUUID(WIFI_UUID),
    // WIFI_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE
  );
  pCharacteristicWiFi->setCallbacks(new MyCallbackHandler());

  // Start the service
  pService->start();

  // Start advertising
  pAdvertising = pServer->getAdvertising();
  pAdvertising->start();
}

void stopBLE() {
  pAdvertising->stop();
  pService->stop();
  BLEDevice::deinit(true);
}

void setup() 
{
  // Disable brownout detector and set Serial output
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();
  Serial.println("BIRDBRO Booting...");

  createName();

  Preferences preferences;
  preferences.begin("WiFiCred", false);
  bool hasPref = preferences.getBool("valid", false);
  if (hasPref) {
    bleWiFiSSID = preferences.getString("ssid","");
    bleWiFiPassword = preferences.getString("pw","");

    if (bleWiFiSSID.equals("") 
        || bleWiFiPassword.equals("")) {
      Serial.println("Found preferences but credentials are invalid");
    } else {
      Serial.println("Read from preferences:");
      Serial.println("SSID: " + bleWiFiSSID + " password: " + bleWiFiPassword);
      hasCredentials = true;
    }
  } else {
    Serial.println("Could not find preferences, need send data over BLE");
  }
  preferences.end();

  if(!hasCredentials){
    initBLE();
    BLEActivated = true;
  } 
  
  while(!hasCredentials){
    delay(300);
    Preferences preferences;
    preferences.begin("WiFiCred", false);
    preferences.putString("ssid", WIFI_SSID);
    preferences.putString("pw", WIFI_PASSWORD);
    preferences.putBool("valid", true);
    preferences.end();
    hasCredentials = true;
  }

  if(BLEActivated){
      stopBLE();
  }

  // Set GPIO for LED Flash
  pinMode(4, INPUT); 
  digitalWrite(4, LOW);
  rtc_gpio_hold_dis(GPIO_NUM_4); // Disable pin if it was enabled before sleep

  // Configure camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if(psramFound())
  {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else 
  {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) 
  {
    Serial.println();
    Serial.println("------------------------------------");
    Serial.println("CAMERA INITIALIZATION FAILED");
    Serial.println("REASON: " + err);
    Serial.println("------------------------------------");
    Serial.println();
    goToSleep();
  }

  // Set camera capture settings
  sensor_t * s = esp_camera_sensor_get();
  s->set_contrast(s, 2);    //min=-2, max=2
  s->set_brightness(s, 2);  //min=-2, max=2
  s->set_saturation(s, 2);  //min=-2, max=2
  delay(100);        
  
  // Mount SD Card
  if(!SD_MMC.begin())
  {
    Serial.println();
    Serial.println("------------------------------------");
    Serial.println("SD MOUNT FAILED");
    Serial.println("------------------------------------");
    Serial.println();
    goToSleep();
  }
  
  uint8_t cardType = SD_MMC.cardType();

  if(cardType == CARD_NONE)
  {
    Serial.println();
    Serial.println("------------------------------------");
    Serial.println("NO SD CARD FOUND");
    Serial.println("------------------------------------");
    Serial.println();
    goToSleep();
  }
  
  // Take image
  Serial.print("Taking image...");
  camera_fb_t * fb = NULL;
  fb = esp_camera_fb_get();
  if (!fb) 
  {
    Serial.println();
    Serial.println("------------------------------------");
    Serial.println("CAMERA CAPTURE FAILED");
    Serial.println("------------------------------------");
    Serial.println();
    goToSleep();
  }

  // Connect to Wi-Fi
    WiFi.disconnect(true);
  WiFi.enableSTA(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println();
  Serial.print("Connecting to Wi-Fi...");
  int failedCounter = 0;
  
  while (WiFi.status() != WL_CONNECTED)
  {
      Serial.print(".");
      delay(300);
      failedCounter = failedCounter + 1;
      if(failedCounter == 20){
        Serial.println();
        Serial.println("------------------------------------");
        Serial.println("CONNECTING TO WI-FI FAILED");
        Serial.println("------------------------------------");
        Serial.println();
        goToSleep();
      }
  } 

  // Store image on SD card using epoch timestamp as name
  Serial.println();
  Serial.print("Storing image on SD card...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  unsigned long timestamp = getTime();
  String path = "/" + String(timestamp) + ".jpg";
  String onlinePath = String(timestamp) + ".jpg";
  fs::FS &fs = SD_MMC;
  File file = fs.open(path.c_str(), FILE_WRITE);
  if(!file)
  {
    Serial.println();
    Serial.println("------------------------------------");
    Serial.println("WRITING IMAGE FILE FAILED");
    Serial.println("------------------------------------");
    Serial.println();
    return;
  }
  file.write(fb->buf, fb->len); 
  file.close();
  esp_camera_fb_return(fb);
  Serial.println();
  Serial.println("------------------------------------");
  Serial.println("WRITING IMAGE FILE PASSED");
  Serial.println("IMAGE NAME: " + path);
  Serial.println("------------------------------------");
  Serial.println();
  
  // Connect to Google Firebase
  fbconfig.host = FIREBASE_HOST;
  fbconfig.api_key = API_KEY;
  fbconfig.service_account.data.client_email = FIREBASE_CLIENT_EMAIL;
  fbconfig.service_account.data.project_id = FIREBASE_PROJECT_ID;
  fbconfig.service_account.data.private_key = PRIVATE_KEY;
  Firebase.begin(&fbconfig, &auth);
  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(1024);

  // Read image from SD and store it on Firebase Storage
  Serial.println();
  Serial.print("Storing image on Firebase Storage...");
  Firebase.GCStorage.upload(&fbdo, STORAGE_BUCKET_ID, path.c_str(), mem_storage_type_sd, gcs_upload_type_resumable, onlinePath.c_str(), "image/jpeg", nullptr, nullptr, nullptr, gcsUploadCallback);
  delay(10000);   
    
  // Read Firebase RTDB for all registered FCM tokens and send each one a notification
  Serial.print("Sending out notifications to all registered FCM token holders...");
  if(Firebase.RTDB.get(&fbdo, "/fcm"))
  {
        if (fbdo.dataType() == "json")
        {
          Serial.println();
          Serial.println("------------------------------------");
          Serial.println("GETTING FCM TOKENS FROM RTDB PASSED");
          Serial.println("------------------------------------");
          Serial.println();
          FirebaseJson &json = fbdo.jsonObject();
          size_t len = json.iteratorBegin();
          String key, value = "";
          int type = 0;
          for (size_t i = 0; i < len; i++)
          {
              json.iteratorGet(i, type, key, value);
              sendFCMMessage(value);
          }
          json.iteratorEnd();
        }

  }else{
    Serial.println();
    Serial.println("------------------------------------");
    Serial.println("GETTING FCM TOKENS FROM RTDB FAILED");
    Serial.println("REASON: " + fbdo.errorReason());
    Serial.println("------------------------------------");
    Serial.println();
  }

  // Go back into deep sleep mode until device is waked by GPIO pin 13 by the PIR sensor making this pin HIGH
  goToSleep();
}

void loop() 
{


}

// Callback for Firebase Storage upload
void gcsUploadCallback(UploadStatusInfo info)
{
    if (info.status == fb_esp_gcs_upload_status_init)
    {
        Serial.printf("Uploading file %s to %s\n", info.localFileName.c_str(), info.remoteFileName.c_str());
    }
    else if (info.status == fb_esp_gcs_upload_status_upload)
    {
        Serial.printf("Uploaded %d%s\n", (int)info.progress, "%");
    }
    else if (info.status == fb_esp_gcs_upload_status_complete)
    {
        Serial.println();
        Serial.println("------------------------------------");
        Serial.println("FILE UPLOAD PASSED");
        FileMetaInfo meta = fbdo.metaData();
        Serial.printf("FILENAME: %s\n", meta.name.c_str());
        Serial.println("------------------------------------");
        Serial.println();
    }
    else if (info.status == fb_esp_gcs_upload_status_error)
    {
        Serial.println();
        Serial.println("------------------------------------");
        Serial.println("FILE UPLOAD FAILED");
        Serial.printf("REASON: %s\n", info.errorMsg.c_str());
        Serial.println("------------------------------------");
        Serial.println();
    }
}

// All elements to perform before going back to dep sleep mode
void goToSleep(){
  Serial.println();
  Serial.println("Entering deep sleep mode...");

  // Disable Wi-Fi
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Set GPIO pin for LED Flash back to OFF
  pinMode(4, OUTPUT);         
  digitalWrite(4, LOW);     
  rtc_gpio_hold_en(GPIO_NUM_4);  // Make sure it is set LOW during sleep 

  // Flush serial connection
  Serial.flush(); 

  // Set deep sleep mode to wake up when GPIO pin 13 goes HIGH 
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 1);

  // Sleep after short delay
  delay(10000);   
  Serial.println();
  Serial.println("Bye!");   
  esp_deep_sleep_start();
}

// Get epoch timestamp from NTP server
unsigned long getTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println();
    Serial.println("------------------------------------");
    Serial.println("GETTING TIME FAILED");
    Serial.println("------------------------------------");
    Serial.println();
    return(0);
  }
  time(&now);
  return now;
}

// Send a Google Firebase message to a registered FCM token device
void sendFCMMessage(String token)
{
    FCM_HTTPv1_JSON_Message msg;
    msg.token = token.c_str();
    msg.notification.body = "Check it out now in BirbBro";
    msg.notification.title = "New Birb friend spotted!";
    FirebaseJson json;
    String payload;

    if (Firebase.FCM.send(&fbdo, &msg)) 
    {
        Serial.println("------------------------------------");
        Serial.println("FCM MESSAGE SEND PASSED");
        Serial.println("TOKEN: " + token);
        Serial.println("------------------------------------");
        Serial.println();
    }
    else
    {
        Serial.println("------------------------------------");
        Serial.println("FCM MESSAGE SEND FAILED");
        Serial.println("TOKEN: " + token);
        Serial.println("REASON: " + fbdo.errorReason());
        Serial.println("------------------------------------");
        Serial.println();
    }
}

void createName() {
  uint8_t baseMac[6];
  // Get MAC address
  esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
  // Write unique name into apName
  sprintf(apName, "BIRBBRO-%02X%02X%02X%02X%02X%02X", baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
}
