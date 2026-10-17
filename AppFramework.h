//
// Created by Melanie on 18/03/2025.
//

#ifndef ESP32_APP_FRAMEWORK_APPFRAMEWORK_H
#define ESP32_APP_FRAMEWORK_APPFRAMEWORK_H

#include <string>
#include <utility>
#include <functional>
#include <esp_timer.h>
#include "WiFi.h"
#include "esp_http_server.h"
#include "ApiHttpRequest.h"
#include "ApiHttpResponse.h"
#include "nlohmann/json.hpp"

#define APP_RUN(x) extern "C" { void app_main() { x.run(); }}

#if defined CONFIG_USE_SETUP_WEB_INTERFACE || defined CONFIG_START_WEBSERVER_IN_STATION_MODE
#define APP_FRAMEWORK_WEBSERVER 1
#endif
#if defined CONFIG_USE_SETUP_WEB_INTERFACE || defined CONFIG_ENABLE_DEVICE_API_IN_STATION_MODE
#define APP_FRAMEWORK_DEVICE_API 1
#endif

// Called from the wifi event task: return quickly, don't block
class AppFrameworkHandler
{
public:
    virtual void apActive() {};
    virtual void apStopped() {};
    virtual void staActive() {};
    virtual void staStopped() {};
    // The setup page tests new wifi credentials. Called from the web server task.
    virtual void setupConnectStarted() {};
    virtual void setupConnectFinished(bool connected) {};
};

class AppFramework : public WiFiEventHandler
{
public:
    explicit AppFramework(WiFi *wifi);
    void init();
    inline void setHandler(AppFrameworkHandler *handler) { m_handler = handler; };
    // Credentials to use instead of the stored ones, call before init()
    inline void setAuthentication(std::string ssid, std::string password) { m_ssid = std::move(ssid); m_password = std::move(password); };
    inline void setWebAppIsSpa(bool spa) { m_webAppIsSpa = spa; };
    // Adds the app's own fields to the /deviceData reply
    inline void setDeviceDataHandler(std::function<void(nlohmann::json &reply)> handler) { m_deviceDataHandler = std::move(handler); };
    // mDNS instance name, the hostname if not set. Call before init().
    inline void setMdnsInstance(std::string instance) { m_mdnsInstance = std::move(instance); };
    // Open the setup access point instead of joining the network. Before init(), init() does it.
    void startSetup();
    // Before an update overwrites the web interface: the web server stays down until restart
    void stopWebServer();

    // Available before init()
    std::string hostname();
    inline std::string ipAddress() { return m_ipAddress; };
    [[nodiscard]] inline bool stationActive() const { return m_staActive; };
    [[nodiscard]] inline bool apActive() const { return m_apActive; };
    // Erase the stored wifi credentials and restart, the device comes up in setup mode
    void forgetWifi();

    esp_err_t staStart() override;
    esp_err_t staStop() override;
    esp_err_t staDisconnected(wifi_event_sta_disconnected_t *info) override;
    esp_err_t staGotIp(ip_event_got_ip_t *info) override;
    esp_err_t apStart() override;
    esp_err_t apStop() override;

private:
    std::string generateHostname();
    void startSetupAp();
    void startRetryTimer();
    static void retryTimerThunk(void *arg);
    void retryStation();
    void startMdns();
    void stationLost();
#ifdef APP_FRAMEWORK_WEBSERVER
    esp_err_t mountStorage();
    void deviceData(const ApiHttpRequest& req, ApiHttpResponse& resp);
#endif
#ifdef APP_FRAMEWORK_DEVICE_API
    void scan(const ApiHttpRequest& req, ApiHttpResponse& resp);
    void connect(const ApiHttpRequest& req, ApiHttpResponse& resp);
    void deviceControl(const ApiHttpRequest& req, ApiHttpResponse& resp);
    esp_err_t tryConnectWifi(const std::string& ssid, const std::string& password);
    bool deviceApiAllowed(ApiHttpResponse& resp);
    static void wifiModeTask(void *arg);
#endif

    volatile bool m_testConnection = false;
    volatile bool m_staActive = false;
    volatile bool m_apActive = false;
    // The setup access point was opened because the network couldn't be joined
    volatile bool m_apFallback = false;
    volatile int m_failedAttempts = 0;
    volatile bool m_webserverStarted = false;
    volatile bool m_webserverStopped = false;
    // Setup was asked for: no joining the network until the setup page has new credentials
    volatile bool m_forceSetup = false;
    bool m_initDone = false;
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
    std::string m_mdnsInstance;
    std::function<void(nlohmann::json &reply)> m_deviceDataHandler;
#ifdef APP_FRAMEWORK_WEBSERVER
    std::string m_spiffsPartitionName = CONFIG_WEB_INTERFACE_SPIFFS_PARTITION_NAME;
#endif
    WiFi *m_wifi;
    MDNS *m_mdns = nullptr;
    AppFrameworkHandler *m_handler = nullptr;
    httpd_handle_t m_webserver = nullptr;
    esp_timer_handle_t m_retryTimer = nullptr;
};


#endif //ESP32_APP_FRAMEWORK_APPFRAMEWORK_H
