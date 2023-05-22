#include "EasyIOT.h"

// ------------------------------------------- Public -----------------------------------

String EasyIOT::ip()
{
    String result = "";

    for (int i = 0; i < 4; i++)
        result += i ? "." + String(WiFi.localIP()[i]) : String(WiFi.localIP()[i]);

    return result;
}

EasyIOT::EasyIOT(String ssid, String password, int stateDocSize)
{
    _ssid = ssid;
    _password = password;
    _stateDocSize = stateDocSize;
}

void EasyIOT::setup()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid, _password);

    log("Connecting with SSID: " + _ssid + ", password: " + _password);

    while (WiFi.status() != WL_CONNECTED)
    {
        log(".", false);
        delay(100);
    }

    log("\nConnected. IP address: " + ip());

    server.begin();
    webSocket.begin();
    webSocket.onEvent(std::bind(&EasyIOT::onSocketEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));

    ArduinoOTA.setPort(8266);
    ArduinoOTA.begin();

    loadStateFromFS();
}

void EasyIOT::loop()
{
    server.handleClient();
    webSocket.loop();
    ArduinoOTA.handle();
}

// -------------- Logger ------------------

void EasyIOT::log(String str)
{
    log(str, true);
}

void EasyIOT::log(String str, bool nextLine)
{
#ifdef DEBUG
    if (nextLine)
        Serial.println(str);
    else
        Serial.print(str);
#endif

    if (!clientConnected)
        return;

    String st = "log:" + str;
    webSocket.broadcastTXT(st);
}

// ------------------------------------------- Private ------------------------------------------

// -------------- Web ------------------

void EasyIOT::onSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length)
{
    switch (type)
    {
    case WStype_CONNECTED:
    {
        clientConnected = true;
        connectionStateChangedCallback();
        log("Device connected");
        sendStateJSON();
        break;
    }

    case WStype_TEXT:
    {
        String string = String((char *)payload);

        if (string == "reboot")
        {
            ESP.restart();
            return;
        }
        updateStateWithJSONString(string);
        break;
    }

    case WStype_DISCONNECTED:
    {
        log("Device disconnected");
        clientConnected = false;
        connectionStateChangedCallback();
        break;
    }

    case WStype_ERROR:
    case WStype_BIN:
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
    case WStype_PING:
    case WStype_PONG:
    {
        log("Socket event: " + String(type) + ". Payload: " + String((char *)payload));
        break;
    }
    }
}

void EasyIOT::updateStateWithJSONString(String string)
{
    DynamicJsonDocument doc(_stateDocSize);
    DeserializationError error = deserializeJson(doc, string);
    
    if (error)
    {
        log("State JSON deserialization error: " + String(error.c_str()));
        return;
    }

    JsonObject root = doc.as<JsonObject>();
    bool updated = modelUpdateReceivedCallback(root);

    if (updated)
    {
        saveStateToFS(doc);
        sendStateJSON();
    }
}

void EasyIOT::sendStateJSON()
{
    if (!clientConnected)
        return;

    DynamicJsonDocument doc(_stateDocSize);
    JsonObject root = doc.to<JsonObject>();
    root["ip"] = ip();

    makeJsonRequestCallback(root);

    String state;
    serializeJsonPretty(doc, state);

    webSocket.broadcastTXT(state);
}

// -------------- Storage ------------------
String ConfigFileName = "/config.json";

void EasyIOT::loadStateFromFS()
{
    LittleFS.begin();

    if (!LittleFS.exists(ConfigFileName))
    {
        log("No config file");
        return;
    }

    File configFile = LittleFS.open(ConfigFileName, "r");

    delay(10);

    if (configFile)
    {
        String content = configFile.readString();
        DynamicJsonDocument doc(_stateDocSize);
        DeserializationError error = deserializeJson(doc, content);

        if (error)
        {
            log("State model deserialization error: " + String(error.c_str()));
            LittleFS.remove(ConfigFileName);
        }
        else
        {
            log("State model loaded from SPIFFS\n" + content);
            JsonObject json = doc.as<JsonObject>();
            modelUpdateReceivedCallback(json);
        }
    }

    configFile.close();
}

void EasyIOT::saveStateToFS(DynamicJsonDocument doc)
{
    File configFile = LittleFS.open(ConfigFileName, "w");

    if (configFile)
    {
        uint16 size = serializeJson(doc, configFile);
        log("Saving state. Size = " + String(size));
    }
    else
    {
        log("Couldn't open file named " + ConfigFileName);
    }

    configFile.close();
}