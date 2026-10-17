# esp32-app-framework

The boring parts of an ESP32 app: wifi with a web based setup, captive portal,
web server for a single page web app, mDNS. Used by
[esp32-app-template](https://github.com/MelanieT/esp32-app-template).

## Use

```cpp
WiFi wifi;
AppFramework framework(&wifi);

framework.setHandler(this);      // AppFrameworkHandler: apActive, apStopped, staActive, staStopped
framework.setWebAppIsSpa(true);  // Unknown paths serve index.html
framework.init();                // Doesn't block, the outcome arrives through the handler
```

The handler is called from the wifi event task: return quickly. `setupConnectStarted()`
and `setupConnectFinished(bool)` come from the web server task, while the setup page
tests new credentials.

More, all optional:

```cpp
framework.setDeviceDataHandler([](nlohmann::json &reply) { reply["mine"] = 42; }); // Own fields in /deviceData
framework.setMdnsInstance("My Device");  // Before init(), else the hostname
framework.startSetup();                  // Open the setup AP instead of joining the network
framework.stopWebServer();               // Before an update overwrites the web interface
framework.hostname();                    // Also before init()
```

## Behaviour

- Wifi credentials are stored in NVS, namespace `wifi`, keys `ssid` and `password`.
  `setAuthentication()` before `init()` overrides them. `forgetWifi()` erases them and restarts.
- Without credentials the setup access point `<prefix>-xxxxxx` opens, with a captive
  portal serving the web interface from SPIFFS.
- When the network can't be joined `CONFIG_STA_CONNECT_ATTEMPTS` times in a row, the
  setup access point opens too. The network is retried every
  `CONFIG_STA_RETRY_INTERVAL_SECONDS` while no one is connected to the access point,
  which closes once the network is back.
- On the network the device is `<prefix>-xxxxxx.local` (`CONFIG_ENABLE_MDNS`), and with
  `CONFIG_START_WEBSERVER_IN_STATION_MODE` it serves the web interface there as well.

## HTTP API

| Endpoint | Request | Reply |
|---|---|---|
| `POST /deviceData` | | `{"connected", "ssid", "ip", "hostname"}` |
| `POST /scan` | | `{"apCount", "aplist": [ssid, …]}` |
| `POST /connect` | `{"ssid", "password"}` | `{"connected"}`, tests and stores the credentials |
| `POST /deviceControl` | `{"command": "wifimode"}` | Leaves setup: closes the access point and joins the network |

`/scan`, `/connect` and `/deviceControl` only answer while the setup access point is up,
unless `CONFIG_ENABLE_DEVICE_API_IN_STATION_MODE` is set. That's a security risk: they
change the wifi without a password.
