//
// Created by Melanie on 18/03/2025.
//

#ifndef FILTERMETER_APPFRAMEWORK_H
#define FILTERMETER_APPFRAMEWORK_H

#include <string>
#include <utility>
#include "WiFi.h"

#define APP_RUN(x) extern "C" { void app_main() { x.run(); }}

class AppFrameworkHandler
{
public:
    virtual void apActive() {};
    virtual void apStopped() {};
    virtual void staActive() {};
    virtual void staStopped() {};
};

class AppFramework : public WiFiEventHandler
{
public:
    explicit AppFramework(WiFi *wifi);
    void init();
    inline void setHandler(AppFrameworkHandler *handler) { m_handler = handler; };
    inline void setAuthentication(std::string ssid, std::string password) { m_ssid = std::move(ssid); m_password = std::move(password); };
#ifdef CONFIG_USE_SETUP_WEB_INTERFACE
    inline std::string hostname() { return m_hostname; };
#endif

    esp_err_t staStart() override;
    esp_err_t staStop() override;
    esp_err_t staDisconnected(wifi_event_sta_disconnected_t *info) override;
    esp_err_t staGotIp(ip_event_got_ip_t *info) override;
    esp_err_t apStart() override;
    esp_err_t apStop() override;

private:
    std::string generateHostname();

    bool m_staActive = false;
    bool m_apActive = false;
    std::string m_ssid = {};
    std::string m_password = {};
#ifdef CONFIG_USE_SETUP_WEB_INTERFACE
    std::string m_hostname;
#endif
    std::string m_spiffsPartitionName = CONFIG_WEB_INTERFACE_SPIFFS_PARTITION_NAME;
    WiFi *m_wifi = nullptr;
    AppFrameworkHandler *m_handler = nullptr;

};


#endif //FILTERMETER_APPFRAMEWORK_H
