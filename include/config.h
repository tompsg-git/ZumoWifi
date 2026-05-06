#pragma once

// ── ZumoWifi Configuration ──────────────────────────────────────────

// WiFi Access Point defaults
#define AP_SSID       "ZumoWifi"
#define AP_PASSWORD   "zumowifi123"

// Web server
#define WEB_PORT      80

// USB-Stick Mount-Punkt (FAT über USB MSC Host)
#define USB_MOUNT_POINT  "/usb"

// USB Host-Task Konfiguration
#define USB_HOST_TASK_STACK  4096
#define USB_HOST_TASK_PRIO   5
#define MSC_TASK_STACK       4096
#define MSC_TASK_PRIO        5

// Max. Dateigröße für Text-Editor (64 KB)
#define EDITOR_MAX_SIZE      (64 * 1024)

// Max. Upload-Dateigröße (64 MB)
#define MAX_UPLOAD_SIZE      (64 * 1024 * 1024)
