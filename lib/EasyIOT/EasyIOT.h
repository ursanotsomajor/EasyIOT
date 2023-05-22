#ifndef EasyIOT_h
#define EasyIOT_h

#include <Arduino.h>

#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

typedef void (*EasyIOTCallback)(void);
typedef bool (*EasyIOTModelRequestCallback)(JsonObject& json);
typedef void (*EasyIOTJsonRequestCallback)(JsonObject& json);

class EasyIOT
{
public:
    EasyIOTCallback connectionStateChangedCallback;
    EasyIOTModelRequestCallback modelUpdateReceivedCallback;
    EasyIOTJsonRequestCallback makeJsonRequestCallback;

    EasyIOT(String ssid, String password, int stateDocSize);

    String ip();
    
    bool clientConnected = false;

    void setup();
    void loop();

    void log(String str);
    void log(String str, bool nextLine);

private:
    String _ssid;
    String _password;
    int _stateDocSize;

    ESP8266WebServer server;
    WebSocketsServer webSocket = WebSocketsServer(81);

    void onSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
    void updateStateWithJSONString(String string);
    void sendStateJSON();

    void loadStateFromFS();
    void saveStateToFS(DynamicJsonDocument doc);
};
 
#endif