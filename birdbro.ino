#include "esp_camera.h"
#include "FS.h"
#include "SPI.h"
#include "SD_MMC.h"
#include "EEPROM.h"
#include "driver/rtc_io.h"

#include <WiFi.h>
#include <Firebase_ESP_Client.h>

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

#define ID_ADDRESS            0x00
#define COUNT_ADDRESS         0x01
#define ID_BYTE               0xAA
#define EEPROM_SIZE           0x0F

uint16_t nextImageNumber = 0;

#define WIFI_SSID "YOUR_SSID"
#define WIFI_PASSWORD "YOUR_PW"
#define FIREBASE_HOST "https://YOUR_HOSTNAME.ZONE.firebasedatabase.app"
#define API_KEY "YOUR_API_KEY"
#define FIREBASE_PROJECT_ID "YOUR_PROJECT_ID"
#define FIREBASE_CLIENT_EMAIL "YOUR_FIREBASE_CLIENT_EMAIL"
const char PRIVATE_KEY[] PROGMEM = "-----BEGIN PRIVATE KEY-----\nYOUR_PRIVATE_KEY\n-----END PRIVATE KEY-----\n";
#define DEVICE_REGISTRATION_ID_TOKEN "FCM_TOKEN_OF_YOUR_ANDROID_PHONE"
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig fbconfig;
unsigned long lastTime = 0;
int count = 0;
void sendMessage();

void setup() 
{
  Serial.begin(115200);
  Serial.println();
  Serial.println("Booting...");

  pinMode(4, INPUT);              //GPIO for LED flash
  digitalWrite(4, LOW);
  rtc_gpio_hold_dis(GPIO_NUM_4);  //diable pin hold if it was enabled before sleeping
  
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
  
  //init with high specs to pre-allocate larger buffers
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

  //initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) 
  {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  
  //set the camera parameters
  sensor_t * s = esp_camera_sensor_get();
  s->set_contrast(s, 2);    //min=-2, max=2
  s->set_brightness(s, 2);  //min=-2, max=2
  s->set_saturation(s, 2);  //min=-2, max=2
  delay(100);               //wait a little for settings to take effect
  
  //initialize & mount SD card
  if(!SD_MMC.begin())
  {
    Serial.println("Card Mount Failed");
    return;
  }
  
  uint8_t cardType = SD_MMC.cardType();

  if(cardType == CARD_NONE)
  {
    Serial.println("No SD card attached");
    return;
  }

  //initialize EEPROM & get file number
  if (!EEPROM.begin(EEPROM_SIZE))
  {
    Serial.println("Failed to initialise EEPROM"); 
    Serial.println("Exiting now"); 
    while(1);   //wait here as something is not right
  }

/*
  EEPROM.get(COUNT_ADDRESS, nextImageNumber);
  Serial.println(nextImageNumber);
  nextImageNumber += 1;
  EEPROM.put(COUNT_ADDRESS, nextImageNumber);
  EEPROM.commit();
  while(1);
  */
  
  /*ERASE EEPROM BYTES START*/
  /*
  Serial.println("Erasing EEPROM...");
  for(int i = 0; i < EEPROM_SIZE; i++)
  {
    EEPROM.write(i, 0xFF);
    EEPROM.commit();
    delay(20);
  }
  Serial.println("Erased");
  while(1);
  */
  /*ERASE EEPROM BYTES END*/  

  if(EEPROM.read(ID_ADDRESS) != ID_BYTE)    //there will not be a valid picture number
  {
    Serial.println("Initializing ID byte & restarting picture count");
    nextImageNumber = 0;
    EEPROM.write(ID_ADDRESS, ID_BYTE);  
    EEPROM.commit(); 
  }
  else                                      //obtain next picture number
  {
    EEPROM.get(COUNT_ADDRESS, nextImageNumber);
    nextImageNumber +=  1;    
    Serial.print("Next image number:");
    Serial.println(nextImageNumber);
  }

  //take new image
  camera_fb_t * fb = NULL;
  //obtain camera frame buffer
  fb = esp_camera_fb_get();
  if (!fb) 
  {
    Serial.println("Camera capture failed");
    Serial.println("Exiting now"); 
    while(1);   //wait here as something is not right
  }

  //save to SD card
  //generate file path
  String path = "/IMG" + String(nextImageNumber) + ".jpg";
    
  fs::FS &fs = SD_MMC;

  //create new file
  File file = fs.open(path.c_str(), FILE_WRITE);
  if(!file)
  {
    Serial.println("Failed to create file");
    Serial.println("Exiting now"); 
    while(1);   //wait here as something is not right    
  } 
  else 
  {
    file.write(fb->buf, fb->len); 
    EEPROM.put(COUNT_ADDRESS, nextImageNumber);
    EEPROM.commit();
  }
  file.close();

  //return camera frame buffer
  esp_camera_fb_return(fb);
  Serial.printf("Image saved: %s\n", path.c_str());


  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
      Serial.print(".");
      delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();
  fbconfig.host = FIREBASE_HOST;
  fbconfig.api_key = API_KEY;
  fbconfig.service_account.data.client_email = FIREBASE_CLIENT_EMAIL;
  fbconfig.service_account.data.project_id = FIREBASE_PROJECT_ID;
  fbconfig.service_account.data.private_key = PRIVATE_KEY;
  Firebase.begin(&fbconfig, &auth);
  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(1024);
  sendMessage();
    

  pinMode(4, OUTPUT);              //GPIO for LED flash
  digitalWrite(4, LOW);            //turn OFF flash LED
  rtc_gpio_hold_en(GPIO_NUM_4);    //make sure flash is held LOW in sleep
  Serial.println("Entering deep sleep mode");
  Serial.flush(); 
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 0);   //wake up when pin 13 goes LOW
  delay(3000);                                    //wait for 3 seconds to let PIR sensor settle
  esp_deep_sleep_start();
}

void loop() 
{


}

void sendMessage()
{

    Serial.println("------------------------------------");
    Serial.println("Send Firebase Cloud Messaging...");

    //Read more details about HTTP v1 API here https://firebase.google.com/docs/reference/fcm/rest/v1/projects.messages
    FCM_HTTPv1_JSON_Message msg;

    msg.token = DEVICE_REGISTRATION_ID_TOKEN;

    msg.notification.body = "Notification body";
    msg.notification.title = "Notification title";

    FirebaseJson json;
    String payload;

    //all data key-values should be string
    json.add("temp", "28");
    json.add("unit", "celsius");
    json.add("timestamp", "1609815454");
    json.toString(payload);
    msg.data = payload.c_str();

    if (Firebase.FCM.send(&fbdo, &msg)) //send message to recipient
    {

        Serial.println("PASSED");
        Serial.println(Firebase.FCM.payload(&fbdo));
        Serial.println("------------------------------------");
        Serial.println();
    }
    else
    {
        Serial.println("FAILED");
        Serial.println("REASON: " + fbdo.errorReason());
        Serial.println("------------------------------------");
        Serial.println();
    }

    count++;
}
