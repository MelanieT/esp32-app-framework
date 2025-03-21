//
// Created by Melanie on 18/03/2025.
//

#include <esp_wifi.h>
#include <esp_mac.h>
#include "AppFramework.h"
#include "nvs_flash.h"
#include "CPPNVS.h"

using namespace std;

AppFramework::AppFramework(WiFi *wifi)
    :m_wifi(wifi)
{}

void AppFramework::init()
{
#ifdef CONFIG_USE_SETUP_WEB_INTERFACE
    m_hostname = generateHostname();
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
        if (m_password.empty())
            auth = WIFI_AUTH_OPEN;

        m_wifi->startAP(m_hostname, m_password, auth);
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
        m_staActive = false;
        if (m_handler)
            m_handler->staStopped();
    }
    return ESP_OK;
}

esp_err_t AppFramework::staDisconnected(wifi_event_sta_disconnected_t *info)
{
    if (m_staActive)
        esp_wifi_connect();
    return ESP_OK;
}

esp_err_t AppFramework::staGotIp(ip_event_got_ip_t *info)
{
    if (!m_staActive)
    {
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
        if (m_handler)
            m_handler->apStopped();
    }
    return ESP_OK;
}
