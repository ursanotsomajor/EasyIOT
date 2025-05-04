#include <EasyIOT.h> // Contains Arduino.h as well

// A representaion of an iot device
struct Model
{
    bool updateWithJSON(JsonObject json)
    {
        // Parse received json 
        return true;
    }

    void populateJSON(JsonObject json)
    {
        // Fill json with variables from the structure 
    }
};

#define JSON_SIZE 512

Model model = Model();
EasyIOT device("device_type", "hotspot_name", "hotspot_pass", JSON_SIZE);

void setup()
{
    device.connectionStateChangedCallback = []() {
        digitalWrite(LED_BUILTIN, device.clientConnected);
    };

    device.modelUpdateReceivedCallback = [](JsonObject &json) {
        bool stateUpdated = model.updateWithJSON(json);
        updateDeviceState();
        return stateUpdated;
    };

    device.makeJsonRequestCallback = [](JsonObject &json) {
        model.populateJSON(json);
    };

    device.setup();
}

void loop()
{
    device.loop();
}

void updateDeviceState()
{

}