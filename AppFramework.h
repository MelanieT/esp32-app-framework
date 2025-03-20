//
// Created by Melanie on 18/03/2025.
//

#ifndef FILTERMETER_APPFRAMEWORK_H
#define FILTERMETER_APPFRAMEWORK_H

#include <string>
#include <utility>
#include "WiFi.h"

class AppFrameworkHandler {
public:
    virtual void apActive() {};
    virtual void apStopped() {};
    virtual void staActive() {};
    virtual void staStopped() {};
};

class AppFramework : public WiFiEventHandler
{
public:
    explicit AppFramework(WiFi& wifi);

    inline void enableApConfiguration(bool ap) { m_useApConfiguration = ap; };
    inline void setSpiffsPartitionName(std::string spiffs) { m_spiffsPartitionName = std::move(spiffs); };
    void init();
    void init(bool enableApConfiguration);
    void init(bool enableApConfiguration, std::string spiffsPartitionName);
    inline void setHandler(AppFrameworkHandler *handler) { m_handler = handler; };

    esp_err_t staDisconnected(wifi_event_sta_disconnected_t *info) override;
    esp_err_t staGotIp(ip_event_got_ip_t *info) override;
    esp_err_t apStart() override;
    esp_err_t apStop() override;

private:
    bool m_staActive = false;
    bool apActive = false;
    std::string m_ssid = {};
    std::string m_password = {};
    bool m_useApConfiguration = false;
    std::string m_spiffsPartitionName = "spiffs";
    WiFi& m_wifi;
    AppFrameworkHandler *m_handler;
};


#endif //FILTERMETER_APPFRAMEWORK_H
