#pragma once

void OTAtask(void *parameter) {
  Serial.println("Connecting to saved Wi-Fi network");
  WiFi.mode(WIFI_STA);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(hostName);
  WiFi.begin();

  unsigned long started=millis();
  while(WiFi.status()!=WL_CONNECTED){
    if(millis()-started>30000){Serial.println("Wi-Fi timeout; rebooting");delay(1000);ESP.restart();}
    vTaskDelay(250);
  }

  ArduinoOTA.setHostname(hostName);
  ArduinoOTA.onStart([](){Serial.println("OTA update starting");RUN=false;});
  ArduinoOTA.onEnd([](){Serial.println("\nOTA update complete");});
  ArduinoOTA.onProgress([](unsigned int p,unsigned int t){if(t)Serial.printf("OTA: %u%%\r",(p*100U)/t);});
  ArduinoOTA.onError([](ota_error_t e){Serial.printf("OTA error %u\n",e);});

  Serial.print("Wi-Fi connected, IP: ");Serial.println(WiFi.localIP());
  ArduinoOTA.begin();
  Serial.println("ArduinoOTA service started");
  ota_t=true;
  for(;;){ArduinoOTA.handle();vTaskDelay(10);}
}
