#include "EasyIOT.h"

// ------------------------------------------- Public -----------------------------------

String EasyIOT::ip()
{
    return WiFi.localIP().toString();
}

EasyIOT::EasyIOT(String name, String ssid, String password, int stateDocSize)
{
    _name = name;
    _ssid = ssid;
    _password = password;
    _stateDocSize = stateDocSize;
}

void EasyIOT::setup()
{
    if (!LittleFS.begin()) 
    { 
        log("Failed to mount LittleFS");
        startAPMode(); 
        return;
    }

    WiFi.persistent(false); 
    WiFi.setAutoReconnect(true);

    loadWiFiConfigFromFS();
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid, _password);

    log("Connecting with SSID: " + _ssid + ", password: " + _password);

    uint32_t startTime = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
        delay(500);
        log(".", false);
    }

    if (WiFi.status() != WL_CONNECTED) {
        startAPMode(); 
        return;
    }

    log("\nConnected. IP address: " + ip());

    server.begin();
    webSocket.begin();
    webSocket.onEvent(std::bind(&EasyIOT::onSocketEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));

    ArduinoOTA.setPort(8266);
    ArduinoOTA.begin();
    ArduinoOTA.onError([](ota_error_t error) {
        ESP.restart();
    });

    loadStateFromFS();
}

void EasyIOT::loop()
{
    if (WiFi.getMode() == WIFI_AP) 
    {
        dnsServer.processNextRequest();  // Handle captive portal
        server.handleClient();           // Handle web requests
    }

    server.handleClient();
    webSocket.loop();
    ArduinoOTA.handle();
}

void EasyIOT::requestSendingJSON()
{
    sendStateJSON();
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
        if (string == "reboot") ESP.restart();
        else updateStateWithJSONString(string);
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
    if (!LittleFS.exists(ConfigFileName)) 
    {
        log("No config file");
        return;
    }

    File configFile = LittleFS.open(ConfigFileName, "r");

    delay(10);

    if (!configFile) return;

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
        log("State model loaded from LittleFS\n" + content);
        JsonObject json = doc.as<JsonObject>();
        modelUpdateReceivedCallback(json);
    }

    configFile.close();
}

void EasyIOT::saveStateToFS(DynamicJsonDocument doc)
{
    File configFile = LittleFS.open(ConfigFileName, "w");

    if (configFile)
    {
        uint16 size = serializeJson(doc, configFile);
        log("Saving state to LittleFS. Size = " + String(size));
    }
    else
    {
        log("Couldn't open file named " + ConfigFileName);
    }

    configFile.close();
}

String wifiConfigPath = "/wifi_config.json";

void EasyIOT::loadWiFiConfigFromFS() 
{
    if (!LittleFS.exists(wifiConfigPath)) return;

    File file = LittleFS.open(wifiConfigPath, "r");
    DynamicJsonDocument doc(256);
    deserializeJson(doc, file);
    _ssid = doc["ssid"].as<String>();
    _password = doc["pass"].as<String>();
    log("Loaded WiFi config: " + _ssid);
    file.close();
}

void EasyIOT::saveWiFiConfigToFS(const String &ssid, const String &pass) 
{
    File file = LittleFS.open(wifiConfigPath, "w");

    if (file) {
        DynamicJsonDocument doc(256);
        doc["ssid"] = ssid;
        doc["pass"] = pass;
        serializeJson(doc, file);
        log("Saved WiFi config to LittleFS");
    }

    file.close();
}

// -------------------- AP Mode & Web Config --------------------
void EasyIOT::startAPMode() 
{
    log("WiFi failed, starting AP mode...");

    WiFi.mode(WIFI_AP);

    String apName = _name + " - " + WiFi.softAPmacAddress().substring(12);
    WiFi.softAP(apName, "12345678");

    IPAddress apIP = WiFi.softAPIP();
    dnsServer.start(53, "*", apIP);

    // Set up HTTP server
    server.on("/", HTTP_GET, std::bind(&EasyIOT::handleRoot, this));
    server.on("/save", HTTP_POST, std::bind(&EasyIOT::handleSave, this));
    
    // Captive portal detection URLs for various devices
    server.on("/generate_204", HTTP_GET, [this]() { handleRoot(); });  // Android
    server.on("/connecttest.txt", HTTP_GET, [this]() { server.send(200, "text/plain", "Microsoft NCSI"); });  // Windows
    server.on("/ncsi.txt", HTTP_GET, [this]() { server.send(200, "text/plain", "Microsoft NCSI"); });  // Windows
    server.on("/hotspot-detect.html", HTTP_GET, [this]() { handleRoot(); });  // Apple
    server.on("/library/test/success.html", HTTP_GET, [this]() { handleRoot(); });  // Apple
    server.on("/success.txt", HTTP_GET, [this]() { server.send(200, "text/plain", "success"); });  // Other Apple devices
    server.on("/kindle-wifi/wifistub.html", HTTP_GET, [this]() { handleRoot(); });  // Kindle
    
    // Redirect all other requests to root
    server.onNotFound([this]() {
        if (isCaptivePortalRequest(server.hostHeader())) {
            handleRoot();
        } else {
            server.send(404, "text/plain", "File Not Found");
        }
    });

    server.begin();
    log("AP Mode IP: " + WiFi.softAPIP().toString());
}

bool EasyIOT::isCaptivePortalRequest(String hostHeader) 
{
    return !hostHeader.equals(WiFi.softAPIP().toString()) && 
           !hostHeader.equals(_name + ".local") && 
           !hostHeader.equals("") &&
           hostHeader.indexOf(".") < 0;
}

void EasyIOT::handleRoot() 
{
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.send(200, "text/html",
                "<html><body>"
                "<h1>EasyIOT Wi-Fi Setup</h1>"
                "<form action='/save' method='POST'>"
                "SSID: <input type='text' name='ssid'><br>"
                "Password: <input type='password' name='pass'><br>"
                "<input type='submit' value='Save & Restart'>"
                "</form></body></html>");
}

void EasyIOT::handleSave() 
{
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    
    if (ssid.length() > 0) 
    {
        saveWiFiConfigToFS(ssid, pass);
        server.send(200, "text/html", "<p>Saved! Restarting...</p>");
        delay(1000);
        ESP.restart();
    } 
    else {
        server.send(400, "text/html", "<p>Error: SSID cannot be empty!</p>");
    }
}