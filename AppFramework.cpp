//
// Created by Melanie on 18/03/2025.
//

#include <esp_wifi.h>
#include "AppFramework.h"

AppFramework::AppFramework(WiFi &wifi)
    :m_wifi(wifi)
{
}

esp_err_t AppFramework::staDisconnected(wifi_event_sta_disconnected_t *info)
{
    if (m_staActive)
        esp_wifi_connect();
}

esp_err_t AppFramework::staGotIp(ip_event_got_ip_t *info)
{

}

esp_err_t AppFramework::apStart()
{
    return WiFiEventHandler::apStart();
}

esp_err_t AppFramework::apStop()
{
    return WiFiEventHandler::apStop();
}

void AppFramework::init()
{

}

void AppFramework::init(bool enableApConfiguration)
{
    this->enableApConfiguration(enableApConfiguration);

    init();
}

void AppFramework::init(bool enableApConfiguration, std::string spiffsPartitionName)
{
    this->enableApConfiguration(enableApConfiguration);
    setSpiffsPartitionName(spiffsPartitionName);

    init();
}
