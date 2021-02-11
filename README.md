
# BirdBro-esp32

BirdBro is an esp32 script to run on an AIThinker ESP32-CAM to use a PIR sensor to automatically detect new bird friends passing by your birdfeeder and take a few pictures. Which then will automatically send this over Wi-Fi to the Google Firebase cloud. This cloud is connected to the BirdBro android app to then receive a notification about the new bird and see the pictures and identify the bird species. 

You can find more information on the Android app here: https://github.com/WouterJansen/BirbBro-app

# Hardware used

# Dependencies 
 - Made with Arduino IDE.
 - Arduino core for the ESP32 (add the following board manager URL: https://dl.espressif.com/dl/package_esp32_index.json and install the esp32 package from the board manager.
 - Firebase Arduino Client Library for ESP8266 and ESP32 by Mobizt (v1.0.3)

#  Features

  - Use the PIR sensor to wake from deep sleep.
  - Use the camera to take a few pictures when out of deep sleep.
  - Connect to Google Firebase.
  - Send a notification though the Google Firebase to the connected Android phone app.

#  TODO
  - Upload the pictures.
  - Clean everything up and make usable. 
  
#  Usage
  - This requires a bit of a setup on the Firebase side, this will be documented later. 
