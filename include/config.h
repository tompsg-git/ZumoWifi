#pragma once

// ── ZumoWifi Configuration ──────────────────────────────────────────

// WiFi Access Point defaults
#define AP_SSID       "ZumoWifi"
#define AP_PASSWORD   "zumowifi123"

// Web server
#define WEB_PORT      80

// USB Host
#define USB_HOST_TASK_STACK  4096
#define USB_HOST_TASK_PRIO   5

// File system root for Garmin storage
#define GARMIN_MOUNT_POINT  "/garmin"

// LittleFS for web files
#define WEB_FS_MOUNT        "/littlefs"

// Max file upload size (16 MB)
#define MAX_UPLOAD_SIZE     (16 * 1024 * 1024)
