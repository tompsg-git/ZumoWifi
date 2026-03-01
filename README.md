# ZumoWifi

ESP32-S3 WiFi gateway for **Garmin Zumo** — access your Zumo's file system wirelessly via a web interface.

## Features

- **USB Host** — connects to Garmin Zumo via USB (Mass Storage Class)
- **WiFi AP** — creates its own WiFi network (`ZumoWifi`)
- **Web Interface** — browse, upload, download, and delete files
- **Station Mode** — can also connect to an existing WiFi network

## Hardware

- ESP32-S3 DevKit (with USB OTG support)
- USB OTG cable/adapter to connect to Garmin Zumo

## Quick Start

### 1. Install PlatformIO

```bash
pip install platformio
```

### 2. Build & Flash

```bash
pio run -t upload          # flash firmware
pio run -t uploadfs        # upload web interface (LittleFS)
```

### 3. Connect

1. Power on the ESP32-S3
2. Connect to WiFi network **ZumoWifi** (password: `zumowifi123`)
3. Open **http://192.168.4.1** in your browser
4. Plug the Garmin Zumo into the ESP32-S3 USB Host port

## Web Interface

| Tab | Function |
|---|---|
| **Files** | Browse Garmin file system, upload/download/delete files |
| **WiFi** | Scan and connect to an existing WiFi network |
| **Info** | Device status, memory, uptime |

## Project Structure

```
ZumoWifi/
├── platformio.ini          PlatformIO configuration
├── include/
│   └── config.h            Configuration constants
├── src/
│   ├── main.cpp            Entry point
│   ├── wifi_manager.*      WiFi AP & Station management
│   ├── usb_host.*          USB Host (Garmin MSC)
│   └── webserver.*         Async web server & REST API
└── data/                   Web interface (LittleFS)
    ├── index.html
    ├── style.css
    └── app.js
```

## API Endpoints

| Method | Endpoint | Description |
|---|---|---|
| GET | `/api/status` | Device & WiFi status |
| GET | `/api/files?path=/` | List directory contents |
| GET | `/api/download?path=/file` | Download a file |
| POST | `/api/upload` | Upload file(s) (multipart) |
| POST | `/api/delete` | Delete a file |
| POST | `/api/mkdir` | Create a directory |
| GET | `/api/wifi/scan` | Scan WiFi networks |
| POST | `/api/wifi/connect` | Connect to WiFi (ssid + password) |

## Configuration

Edit `include/config.h` to change defaults:

| Setting | Default | Description |
|---|---|---|
| `AP_SSID` | `ZumoWifi` | WiFi AP name |
| `AP_PASSWORD` | `zumowifi123` | WiFi AP password |
| `WEB_PORT` | `80` | Web server port |

## Status

This project is under active development. The USB Host MSC driver for Garmin Zumo is a work in progress — the current code provides the full scaffold with WiFi, web server, and file management API ready for integration.

## License

MIT
