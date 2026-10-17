//
// Created by Melanie on 18/03/2025.
//

#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_spiffs.h>
#include "AppFramework.h"
#include "nvs_flash.h"
#include "CPPNVS.h"
#include "SimpleWebServer.h"
#include "esp_log.h"
#include "captdns.h"
#include "ApiHttpRequest.h"
#include "ApiHttpResponse.h"
#include "UrlMapper.h"
#include "nlohmann/json.hpp"

using namespace std;
using namespace nlohmann;

static const char *TAG = "appframework";

// Without the setup access point, keep trying to join the network forever
#ifdef CONFIG_STA_CONNECT_ATTEMPTS
#define STA_CONNECT_ATTEMPTS CONFIG_STA_CONNECT_ATTEMPTS
#else
#define STA_CONNECT_ATTEMPTS 5
#endif

CaptiveDns captDns;

AppFramework::AppFramework(WiFi *wifi)
    :m_wifi(wifi)
{}

void AppFramework::init()
{
    // Before the wifi starts, the station netif takes the name when it's created
    m_wifi->setStationHostname(hostname());

#ifdef APP_FRAMEWORK_WEBSERVER
    UrlMapper::AddMapping("POST", "/deviceData", [this](auto& request, auto& response){this->deviceData(request, response);});
#endif
#ifdef APP_FRAMEWORK_DEVICE_API
    UrlMapper::AddMapping("POST", "/scan", [this](auto& request, auto& response){this->scan(request, response);});
    UrlMapper::AddMapping("POST", "/connect", [this](auto& request, auto& response){this->connect(request, response);});
    UrlMapper::AddMapping("POST", "/deviceControl", [this](auto& request, auto& response){this->deviceControl(request, response);});
#endif

#ifdef APP_FRAMEWORK_WEBSERVER
    mountStorage();
#endif

    SimpleWebServer::setHostname(m_hostname);

    NVS nvs("wifi");

    m_wifi->setWifiEventHandler(this);
#ifdef CONFIG_USE_SETUP_WEB_INTERFACE
    // Before the wifi starts: when the setup AP is needed, it comes up configured right away
    string apPassword = CONFIG_AP_MODE_PASSWORD;
    m_wifi->setAPConfig(m_hostname, apPassword, apPassword.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_WPA3_PSK);
#endif

    if (m_ssid.empty())
    {
        if (nvs.get("ssid", &m_ssid))
            m_ssid.clear();
        if (nvs.get("password", &m_password))
            m_password.clear();
    }

    if (!m_ssid.empty() && !m_forceSetup)
    {
        ESP_LOGI(TAG, "Connecting to %s as %s", m_ssid.c_str(), m_hostname.c_str());
        // Don't wait: the outcome arrives as events, failures open the setup AP
        m_wifi->connectSTA(m_ssid, m_password, false);
    }
    else
    {
#ifdef CONFIG_USE_SETUP_WEB_INTERFACE
        ESP_LOGI(TAG, "No wifi configured, starting setup access point %s", m_hostname.c_str());
        startSetupAp();
#else
        ESP_LOGW(TAG, "No wifi configured and no setup web interface");
#endif
    }

    m_initDone = true;
}

string AppFramework::hostname()
{
    if (m_hostname.empty())
        m_hostname = generateHostname();
    return m_hostname;
}

void AppFramework::startSetup()
{
#ifdef CONFIG_USE_SETUP_WEB_INTERFACE
    m_forceSetup = true;
    if (!m_initDone)
        return;

    ESP_LOGI(TAG, "Setup asked for, starting setup access point %s", m_hostname.c_str());
    if (m_retryTimer)
        esp_timer_stop(m_retryTimer);
    m_apFallback = false;
    m_wifi->disconnectSTA();
    if (!m_apActive)
        startSetupAp();
#endif
}

void AppFramework::stopWebServer()
{
#ifdef APP_FRAMEWORK_WEBSERVER
    m_webserverStopped = true;
    if (m_webserverStarted)
    {
        SimpleWebServer::stop_webserver(m_webserver);
        m_webserver = nullptr;
        m_webserverStarted = false;
    }
#endif
    captDns.stop();
}

void AppFramework::forgetWifi()
{
    NVS nvs("wifi");
    nvs.erase("ssid");
    nvs.erase("password");
    nvs.commit();

    esp_restart();
}

string AppFramework::generateHostname()
{
    uint8_t chipid[6];
    esp_read_mac(chipid, ESP_MAC_WIFI_STA);
    char hostname[32];
    // Not '_', it isn't valid in host names
    snprintf(hostname, sizeof(hostname), "%s-%02x%02x%02x", CONFIG_AP_MODE_HOSTNAME_PREFIX, chipid[3], chipid[4], chipid[5]);

    return {hostname};
}

void AppFramework::startSetupAp()
{
#ifdef CONFIG_USE_SETUP_WEB_INTERFACE
    auto auth = WIFI_AUTH_WPA2_WPA3_PSK;
    string password = CONFIG_AP_MODE_PASSWORD;
    if (password.empty())
        auth = WIFI_AUTH_OPEN;

    m_wifi->startAP(m_hostname, password, auth);
#endif
}

void AppFramework::startRetryTimer()
{
    if (!m_retryTimer)
    {
        esp_timer_create_args_t args = {};
        args.callback = retryTimerThunk;
        args.arg = this;
        args.name = "sta_retry";
        esp_timer_create(&args, &m_retryTimer);
    }
    if (!esp_timer_is_active(m_retryTimer))
        esp_timer_start_periodic(m_retryTimer, (uint64_t) CONFIG_STA_RETRY_INTERVAL_SECONDS * 1000000);
}

void AppFramework::retryTimerThunk(void *arg)
{
    static_cast<AppFramework *>(arg)->retryStation();
}

void AppFramework::retryStation()
{
    if (m_staActive || m_testConnection || m_forceSetup || m_ssid.empty())
        return;

    // Joining scans all channels, which takes the AP off its channel. Leave anyone using it alone.
    wifi_sta_list_t clients;
    if (m_apActive && esp_wifi_ap_get_sta_list(&clients) == ESP_OK && clients.num > 0)
        return;

    ESP_LOGI(TAG, "Trying to join %s again", m_ssid.c_str());
    esp_wifi_connect();
}

void AppFramework::startMdns()
{
#ifdef CONFIG_ENABLE_MDNS
    if (m_mdns)
        return;

    m_mdns = new MDNS();
    m_mdns->setHostname(m_hostname);
    m_mdns->setInstance(m_mdnsInstance.empty() ? m_hostname : m_mdnsInstance);
    if (m_startWebserverInStationMode)
        m_mdns->serviceAdd((const char *) nullptr, "_http", "_tcp", 80);
#endif
}

esp_err_t AppFramework::staStart()
{
    return ESP_OK;
}

// The station stopped or lost the network
void AppFramework::stationLost()
{
    if (!m_staActive)
        return;

    m_staActive = false;
#ifdef APP_FRAMEWORK_WEBSERVER
    if (m_webserverStarted && m_startWebserverInStationMode && !m_apActive)
    {
        SimpleWebServer::stop_webserver(m_webserver);
        m_webserver = nullptr;
        m_webserverStarted = false;
    }
#endif
    SimpleWebServer::setRedirectToCaptive(m_apActive);
    if (m_handler)
        m_handler->staStopped();
}

esp_err_t AppFramework::staStop()
{
    stationLost();
    return ESP_OK;
}

esp_err_t AppFramework::staDisconnected(wifi_event_sta_disconnected_t *info)
{
    // A test connection is handled by tryConnectWifi. Without a network configured there's nothing to retry.
    if (m_testConnection || m_ssid.empty())
        return ESP_OK;

    stationLost();

    // Dropped for setup, no retrying
    if (m_forceSetup)
        return ESP_OK;

    m_failedAttempts = m_failedAttempts + 1;
    ESP_LOGW(TAG, "Disconnected from %s, reason %d, attempt %d", m_ssid.c_str(), info->reason, m_failedAttempts);

    if (m_failedAttempts < STA_CONNECT_ATTEMPTS)
    {
        esp_wifi_connect();
        return ESP_OK;
    }

    // From here the retry timer tries again
#ifdef CONFIG_USE_SETUP_WEB_INTERFACE
    if (!m_apActive)
    {
        ESP_LOGW(TAG, "Can't join %s, starting setup access point %s", m_ssid.c_str(), m_hostname.c_str());
        m_apFallback = true;
        startSetupAp();
    }
#endif
    startRetryTimer();

    return ESP_OK;
}

esp_err_t AppFramework::staGotIp(ip_event_got_ip_t *info)
{
    m_ipAddress = ip4addr_ntoa((const ip4_addr_t *) &info->ip_info.ip);

    if (m_testConnection)
        return ESP_OK;

    ESP_LOGI(TAG, "Connected to %s, ip %s", m_ssid.c_str(), m_ipAddress.c_str());

    m_failedAttempts = 0;
    if (m_retryTimer)
        esp_timer_stop(m_retryTimer);

    if (!m_staActive)
    {
#ifdef APP_FRAMEWORK_WEBSERVER
        if (m_startWebserverInStationMode && !m_webserverStarted && !m_webserverStopped)
        {
            m_webserver = SimpleWebServer::start_webserver(CONFIG_SPIFFS_VFS_BASE_PATH, m_webAppIsSpa);
            m_webserverStarted = m_webserver != nullptr;
        }
#endif
        SimpleWebServer::setRedirectToCaptive(false);
        startMdns();
        m_staActive = true;
        if (m_handler)
            m_handler->staActive();
    }

    // Back on the network after a failure, the setup AP isn't needed anymore
    if (m_apFallback)
    {
        m_apFallback = false;
        m_wifi->stopAP();
    }

    return ESP_OK;
}

esp_err_t AppFramework::apStart()
{
    if (!m_apActive)
    {
#ifdef CONFIG_USE_SETUP_WEB_INTERFACE
        if (!m_webserverStarted && !m_webserverStopped)
        {
            m_webserver = SimpleWebServer::start_webserver(CONFIG_SPIFFS_VFS_BASE_PATH, m_webAppIsSpa);
            m_webserverStarted = m_webserver != nullptr;
        }
        SimpleWebServer::setCloseCaptive(false);
        SimpleWebServer::setRedirectToCaptive(!m_staActive);
        captDns.start();
#endif

        m_apActive = true;
        if (m_handler)
            m_handler->apActive();
    }
    return ESP_OK;
}

esp_err_t AppFramework::apStop()
{
    if (m_apActive)
    {
        m_apActive = false;
#ifdef APP_FRAMEWORK_WEBSERVER
        if (m_webserverStarted && !(m_startWebserverInStationMode && m_staActive))
        {
            SimpleWebServer::stop_webserver(m_webserver);
            m_webserver = nullptr;
            m_webserverStarted = false;
        }
#endif
        captDns.stop();
        SimpleWebServer::setRedirectToCaptive(false);
        if (m_handler)
            m_handler->apStopped();
    }
    return ESP_OK;
}

#ifdef APP_FRAMEWORK_WEBSERVER
esp_err_t AppFramework::mountStorage()
{
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = CONFIG_SPIFFS_VFS_BASE_PATH,
        .partition_label = m_spiffsPartitionName.c_str(),
        .max_files = 5,   // This sets the maximum number of files that can be open at the same time
        .format_if_mount_failed = false
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(m_spiffsPartitionName.c_str(), &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    return ESP_OK;
}

void AppFramework::deviceData(const ApiHttpRequest &req, ApiHttpResponse &resp)
{
    json reply = {
        {"connected", m_staActive},
        {"ssid", m_ssid},
        {"ip", m_ipAddress},
        {"hostname", m_hostname + ".local"},
    };
    if (m_deviceDataHandler)
        m_deviceDataHandler(reply);

    resp.AddHeader("Content-type", "application/json");

    resp.setBody(reply.dump());
}
#endif

#ifdef APP_FRAMEWORK_DEVICE_API
// The device API is for the setup access point. On the network only with
// CONFIG_ENABLE_DEVICE_API_IN_STATION_MODE, it changes the wifi without a password.
bool AppFramework::deviceApiAllowed(ApiHttpResponse &resp)
{
#ifndef CONFIG_ENABLE_DEVICE_API_IN_STATION_MODE
    if (!m_apActive)
    {
        resp.setStatusCode(HttpStatus::Code::NotFound);
        return false;
    }
#endif
    return true;
}

void AppFramework::scan(const ApiHttpRequest &req, ApiHttpResponse &resp)
{
    if (!deviceApiAllowed(resp))
        return;

    auto apRecords = m_wifi->scan(); // Will block until done

    resp.AddHeader("Content-type", "application/json");

    vector<string> aplist;
    for (auto ap : apRecords)
    {
        aplist.push_back(ap.getSSID());
    }

    json reply = {
        {"apCount", apRecords.size()},
        {"aplist", aplist},
    };

    resp.setBody(reply.dump());
}

void AppFramework::connect(const ApiHttpRequest &req, ApiHttpResponse &resp)
{
    if (!deviceApiAllowed(resp))
        return;

    resp.AddHeader("Content-type", "application/json");

    try {
        auto jsonData = json::parse(req.body());
        if (jsonData.contains("ssid") && jsonData.contains("password"))
        {
            string ssid = jsonData["ssid"].get<string>();
            string password = jsonData["password"].get<string>();
            esp_err_t err = tryConnectWifi(ssid, password);

            if (err == ESP_OK)
                SimpleWebServer::setCloseCaptive(true);

            json reply = {
                {"connected", err == ESP_OK},
            };
            resp.setBody(reply.dump());
            return;
        }

        json reply = {
            {"connected", false},
        };
        resp.setBody(reply.dump());
        return;
    }
    catch (...) {
        resp.setStatusCode(HttpStatus::Code::BadRequest);
    }
}

void AppFramework::deviceControl(const ApiHttpRequest &req, ApiHttpResponse &resp)
{
    if (!deviceApiAllowed(resp))
        return;

    try {
        auto jsonData = json::parse(req.body());

        string command = jsonData.value("command", "");
        if (command == "wifimode")
        {
            // In a task of its own: the response has to go out before the AP goes down
            xTaskCreate(wifiModeTask, "wifimode", 4096, this, 5, nullptr);
        }
        else
        {
            resp.setStatusCode(HttpStatus::Code::BadRequest);
        }
    }
    catch (...) {
        resp.setStatusCode(HttpStatus::Code::BadRequest);
    }
}

// Leave setup: drop the access point and join the network that was tested
void AppFramework::wifiModeTask(void *arg)
{
    auto framework = static_cast<AppFramework *>(arg);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Stopping AP");
    framework->m_apFallback = false;
    framework->m_forceSetup = false;
    framework->m_failedAttempts = 0;
    framework->m_wifi->stopAP();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Starting STA, ssid %s", framework->m_ssid.c_str());
    framework->m_wifi->connectSTA(framework->m_ssid, framework->m_password, false);

    vTaskDelete(nullptr);
}

esp_err_t AppFramework::tryConnectWifi(const std::string& ssid, const std::string& password)
{
    if (m_testConnection)
        return ESP_ERR_INVALID_STATE;

    m_testConnection = true;
    if (m_handler)
        m_handler->setupConnectStarted();

    ESP_LOGI(TAG,"Attempting to connect to wifi, ssid %s", ssid.c_str());
    // Only a test, the access point stays up so the setup page can show the outcome
    auto ret = m_wifi->connectSTA(ssid, password, true, true);
    ESP_LOGI(TAG, "Connect done, result %d", ret);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    m_testConnection = false;

    if (ret)
    {
        m_wifi->disconnectSTA();
        if (m_handler)
            m_handler->setupConnectFinished(false);
        return ret;
    }

    m_ssid = ssid;
    m_password = password;

    NVS nvs("wifi");

    nvs.set("ssid", ssid);
    nvs.set("password", password);

    nvs.commit();

    captDns.stop();
    SimpleWebServer::setRedirectToCaptive(false);

    if (m_handler)
        m_handler->setupConnectFinished(true);

    return ESP_OK;
}

#endif
