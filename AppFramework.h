//
// Created by Melanie on 18/03/2025.
//

#ifndef FILTERMETER_APPFRAMEWORK_H
#define FILTERMETER_APPFRAMEWORK_H

#include <string>
#include <utility>
#include "WiFi.h"
#include "esp_http_server.h"
#include "ApiHttpRequest.h"
#include "ApiHttpResponse.h"

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
    inline void setWebAppIsSpa(bool spa) { m_webAppIsSpa = spa; };

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
    esp_err_t mountStorage();
#if defined CONFIG_USE_SETUP_WEB_INTERFACE || CONFIG_START_WEBSERVER_IN_STATION_MODE
    void deviceData(const ApiHttpRequest& req, ApiHttpResponse& resp);
#endif
#if defined CONFIG_USE_SETUP_WEB_INTERFACE || CONFIG_ENABLE_DEVICE_API_IN_STATION_MODE
    void scan(const ApiHttpRequest& req, ApiHttpResponse& resp);
    void connect(const ApiHttpRequest& req, ApiHttpResponse& resp);
    void deviceControl(const ApiHttpRequest& req, ApiHttpResponse& resp);
    esp_err_t tryConnectWifi(const std::string& ssid, const std::string& password);
#endif

    std::vector<WiFiAPRecord> m_apRecords;
    bool m_scanned;
    bool m_scanning;
    bool m_testConnection = false;
    volatile bool m_staActive = false;
    volatile bool m_apActive = false;
    volatile bool m_webserverStarted = false;
    bool m_webAppIsSpa = false;
    std::string m_ipAddress;

#ifdef CONFIG_START_WEBSERVER_IN_STATION_MODE
    const bool m_startWebserverInStationMode = true;
#else
    const bool m_startWebserverInStationMode = false;
#endif
    std::string m_ssid = {};
    std::string m_password = {};
    std::string m_hostname;
#if defined CONFIG_USE_SETUP_WEB_INTERFACE || defined CONFIG_START_WEBSERVER_IN_STATION_MODE
    std::string m_spiffsPartitionName = CONFIG_WEB_INTERFACE_SPIFFS_PARTITION_NAME;
#endif
    WiFi *m_wifi;
    AppFrameworkHandler *m_handler = nullptr;
    httpd_handle_t m_webserver = nullptr;

};


#endif //FILTERMETER_APPFRAMEWORK_H
