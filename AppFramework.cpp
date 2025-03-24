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

CaptiveDns captDns;

AppFramework::AppFramework(WiFi *wifi)
    :m_wifi(wifi)
{}

void AppFramework::init()
{
    m_hostname = generateHostname();

#if defined CONFIG_USE_SETUP_WEB_INTERFACE || CONFIG_START_WEBSERVER_IN_STATION_MODE
    UrlMapper::AddMapping("POST", "/deviceData", [this](auto& request, auto& response){this->deviceData(request, response);});
#endif
#if defined CONFIG_USE_SETUP_WEB_INTERFACE || CONFIG_ENABLE_DEVICE_API_IN_STATION_MODE
    UrlMapper::AddMapping("POST", "/scan", [this](auto& request, auto& response){this->scan(request, response);});
    UrlMapper::AddMapping("POST", "/connect", [this](auto& request, auto& response){this->connect(request, response);});
    UrlMapper::AddMapping("POST", "/deviceControl", [this](auto& request, auto& response){this->deviceControl(request, response);});
#endif

#if defined CONFIG_USE_SETUP_WEB_INTERFACE || defined CONFIG_START_WEBSERVER_IN_STATION_MODE
    mountStorage();
#endif

#ifdef CONFIG_USE_SETUP_WEB_INTERFACE
    SimpleWebServer::setHostname(m_hostname);
#endif

    NVS nvs("wifi");

    m_wifi->setWifiEventHandler(this);

    if (m_ssid.empty())
    {
        if (nvs.get("ssid", &m_ssid))
            m_ssid.clear();
        if (nvs.get("password", &m_password))
            m_password.clear();
    }

    if (!m_ssid.empty())
    {
        m_wifi->connectSTA(m_ssid, m_password);
    }
#ifdef CONFIG_USE_SETUP_WEB_INTERFACE
    else
    {
        auto auth = WIFI_AUTH_WPA2_WPA3_PSK;
        string password = CONFIG_AP_MODE_PASSWORD;
        if (password.empty())
            auth = WIFI_AUTH_OPEN;

        m_wifi->startAP(m_hostname, password, auth);
    }
#endif

}

string AppFramework::generateHostname()
{
    uint8_t chipid[6];
    esp_read_mac(chipid, ESP_MAC_WIFI_STA);
    static char hostname[32] = CONFIG_AP_MODE_HOSTNAME_PREFIX;
    hostname[sizeof(hostname) - 1] = 0;
    snprintf(hostname + strlen(hostname), sizeof(hostname) - strlen(hostname) - 1, "_%02x%02x%02x", chipid[3], chipid[4], chipid[5]);

    return {hostname};
}

esp_err_t AppFramework::staStart()
{
    return ESP_OK;
}

esp_err_t AppFramework::staStop()
{
    if (m_staActive)
    {
        if (m_webserverStarted && m_startWebserverInStationMode && !m_apActive)
        {
            SimpleWebServer::stop_webserver(m_webserver);
            m_webserverStarted = false;
        }
        SimpleWebServer::setRedirectToCaptive(true);
        m_staActive = false;
        if (m_handler)
            m_handler->staStopped();
    }
    return ESP_OK;
}

esp_err_t AppFramework::staDisconnected(wifi_event_sta_disconnected_t *info)
{
    if (m_staActive && !m_testConnection)
        esp_wifi_connect();
    return ESP_OK;
}

esp_err_t AppFramework::staGotIp(ip_event_got_ip_t *info)
{
    m_ipAddress = ip4addr_ntoa((const ip4_addr_t *) &info->ip_info.ip);

    if (!m_staActive && !m_testConnection)
    {
#if defined CONFIG_USE_SETUP_WEB_INTERFACE || defined CONFIG_START_WEBSERVER_IN_STATION_MODE
        if (m_startWebserverInStationMode)
        {
            SimpleWebServer::setRedirectToCaptive(false);
            if (!m_webserverStarted)
            {
                m_webserver = SimpleWebServer::start_webserver(CONFIG_SPIFFS_VFS_BASE_PATH, m_webAppIsSpa);
                m_webserverStarted = true;
            }
        }
        SimpleWebServer::setRedirectToCaptive(false);
#endif
        m_staActive = true;
        if (m_handler)
            m_handler->staActive();
    }
    return ESP_OK;
}

esp_err_t AppFramework::apStart()
{
    if (!m_apActive)
    {
#if defined CONFIG_USE_SETUP_WEB_INTERFACE
        if (!m_webserverStarted)
        {
            SimpleWebServer::setRedirectToCaptive(true);
            m_webserver = SimpleWebServer::start_webserver(CONFIG_SPIFFS_VFS_BASE_PATH, m_webAppIsSpa);
            m_webserverStarted = true;
            captDns.start();
        }
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
        if (m_webserverStarted && !m_startWebserverInStationMode)
        {
            SimpleWebServer::stop_webserver(m_webserver);
            m_webserverStarted = false;
            captDns.stop();
        }
        SimpleWebServer::setRedirectToCaptive(false);
        m_apActive = false;
        if (m_handler)
            m_handler->apStopped();
    }
    return ESP_OK;
}

#if defined CONFIG_USE_SETUP_WEB_INTERFACE || defined CONFIG_START_WEBSERVER_IN_STATION_MODE
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
    ret = esp_spiffs_info("spiffs", &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    return ESP_OK;
}
#endif

#if defined CONFIG_USE_SETUP_WEB_INTERFACE || CONFIG_START_WEBSERVER_IN_STATION_MODE
void AppFramework::deviceData(const ApiHttpRequest &req, ApiHttpResponse &resp)
{
    json reply = {
        {"connected", m_staActive},
        {"ssid", m_ssid},
        {"ip", m_ipAddress},
        {"hostname", m_hostname + ".local"},
    };

    resp.AddHeader("Content-type", "application/json");

    resp.setBody(reply.dump());
}
#endif
#if defined CONFIG_USE_SETUP_WEB_INTERFACE || CONFIG_ENABLE_DEVICE_API_IN_STATION_MODE
void AppFramework::scan(const ApiHttpRequest &req, ApiHttpResponse &resp)
{
    m_scanned = true;
    m_apRecords = m_wifi->scan(); // Will block until done

    resp.AddHeader("Content-type", "application/json");

    vector<string> aplist;
    for (auto ap : m_apRecords)
    {
        aplist.push_back(ap.getSSID());
    }

    json reply = {
        {"apCount", m_apRecords.size()},
        {"aplist", aplist},
    };

    resp.setBody(reply.dump());
}

void AppFramework::connect(const ApiHttpRequest &req, ApiHttpResponse &resp)
{
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
    try {
        auto jsonData = json::parse(req.body());

        string command = jsonData.value("command", "");
        if (command == "wifimode")
        {
            ESP_LOGI(TAG, "Stopping AP\r\n");
            m_wifi->stopAP();
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            ESP_LOGI(TAG, "Starting STA, ssid %s\r\n", m_ssid.c_str());
            m_wifi->connectSTA(m_ssid, m_password, false);
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

esp_err_t AppFramework::tryConnectWifi(const std::string& ssid, const std::string& password)
{
    if (m_testConnection)
        return ESP_ERR_INVALID_ARG;

    m_testConnection = true;

    ESP_LOGI(TAG,"Attempting to connect to wifi, ssid %s\r\n", ssid.c_str());
    auto ret = m_wifi->connectSTA(ssid, password, true, true);
    ESP_LOGI(TAG, "Connect done, result %d\r\n", ret);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    m_testConnection = false;

    if (ret)
    {
        m_wifi->disconnectSTA();
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

    return ESP_OK;
}

#endif
