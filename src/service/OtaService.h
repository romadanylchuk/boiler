#pragma once
#include <ElegantOTA.h>
#include "../model/AppState.h"

class WebView;  // forward declaration — OTA piggybacks on WebView's server

class OtaService {
public:
    explicit OtaService(AppState& state);
    void begin();   // registers OTA routes on web server
    void update();  // call from loop

private:
    void onStart();
    void onEnd(bool success);

    AppState& _state;
};
