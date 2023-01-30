#include <Arduino.h>
#include "WiFi.h"
#include "MongooseMqttBroker.h"


const char * ssid = "South Africa 2.4G";
const char * password = "redcasa88";

//static const char *s_listen_on = "mqtt://0.0.0.0:1883";
static const char *s_listen_on = "0.0.0.0:1883";

// A list of subscription, held in memory




              


void brokerv7_adapt()
{
  struct mg_mgr mgr;
  struct mg_connection *c;
  mg_mgr_init(&mgr,NULL);

/*
  if ((c = mg_bind(&mgr, s_listen_on, (mg_event_handler_t) ev_handler, NULL)) == NULL) 
  {
    Serial.println("mg_bind failed");
    return;
  }

  Serial.println("Bind ok.");

  //Attaches a built-in MQTT event handler to the given connection.
  mg_set_protocol_mqtt(c);

  Serial.println("Polling.");
  */
  MongooseMqttBroker broker;

  if (!broker.begin(&mgr,s_listen_on))
  {
    Serial.println("Broker error.");
    return;
  }

  Serial.println("Broker started.");

  for (;;) {
    mg_mgr_poll(&mgr, 1000);
  }

}

/***********************************************
 *  Funciones para manejor de conectividad WiFi
 ***********************************************/
int connectWifi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int result = WiFi.waitForConnectResult();
  switch (result)
  {
  case WL_CONNECTED:
    Serial.println("\nWiFi connected.");
    Serial.println(WiFi.localIP());
    return 0;

  case WL_CONNECT_FAILED:
    Serial.println("\nWiFi connect failed.");
    return -1;

  default:
    Serial.println("\nOther WiFi error: ");
    Serial.print(result);
    return -1;
  }
}



void setup() {
  delay(3000);
  Serial.begin(115200);
  if (connectWifi() != 0)
    Serial.println("Wifi error.");

  brokerv7_adapt();

}

void loop() {
  Serial.println("looping");
  delay(2000);
}