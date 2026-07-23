// js/api_docs.js
// API Docs page - loaded only when needed

// ============================================================
// Initialize API Docs
// ============================================================
async function initApiDocs() {
//    await updateHeader();
    
    // Set base URL
    const baseUrl = window.location.origin;
    const urlElem = document.getElementById('api-base-url');
    if (urlElem) {
        urlElem.textContent = baseUrl;
    }
    
    // Download buttons
    document.getElementById('download-postman')?.addEventListener('click', function() {
        downloadPostmanCollection();
    });
    
    // Auto-refresh header
//    setInterval(updateHeader, REFRESH_INTERVAL);
}

// ============================================================
// Download Postman Collection (UPDATED)
// ============================================================
function downloadPostmanCollection() {
    const baseUrl = window.location.origin;
    
    const collection = {
        "info": {
            "name": "WQMS API",
            "description": "Water Quality Monitoring System API",
            "schema": "https://schema.getpostman.com/json/collection/v2.1.0/collection.json",
            "version": "1.1.0"
        },
        "item": [
            // ============================================================
            // System Endpoints
            // ============================================================
            {
                "name": "System",
                "item": [
                    {
                        "name": "Get Status",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/status",
                                "host": [baseUrl],
                                "path": ["api", "status"]
                            }
                        }
                    },
                    {
                        "name": "Health Check (Echo)",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/echo",
                                "host": [baseUrl],
                                "path": ["api", "echo"]
                            }
                        }
                    },
                    {
                        "name": "Reboot System",
                        "request": {
                            "method": "POST",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/system/reboot",
                                "host": [baseUrl],
                                "path": ["api", "system", "reboot"]
                            }
                        }
                    }
                ]
            },
            // ============================================================
            // Sensors Endpoints
            // ============================================================
            {
                "name": "Sensors",
                "item": [
                    {
                        "name": "Get All Sensor Readings",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/sensors",
                                "host": [baseUrl],
                                "path": ["api", "sensors"]
                            }
                        }
                    },
                    {
                        "name": "Get Sensor Configuration",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/sensors/config",
                                "host": [baseUrl],
                                "path": ["api", "sensors", "config"]
                            }
                        }
                    },
                    {
                        "name": "Update Sensor Configuration",
                        "request": {
                            "method": "POST",
                            "header": [
                                {
                                    "key": "Content-Type",
                                    "value": "application/json"
                                }
                            ],
                            "body": {
                                "mode": "raw",
                                "raw": "{\n    \"sensors\": [\n        {\"id\": 0, \"name\": \"pH Sensor\", \"enabled\": true, \"calibration_factor\": 1000, \"unit\": 2}\n    ]\n}"
                            },
                            "url": {
                                "raw": baseUrl + "/api/sensors/config",
                                "host": [baseUrl],
                                "path": ["api", "sensors", "config"]
                            }
                        }
                    }
                ]
            },
            // ============================================================
            // Relays Endpoints
            // ============================================================
            {
                "name": "Relays",
                "item": [
                    {
                        "name": "Get All Relay Status",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/relays",
                                "host": [baseUrl],
                                "path": ["api", "relays"]
                            }
                        }
                    },
                    {
                        "name": "Trigger Relay (with duration)",
                        "request": {
                            "method": "POST",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/relays/trigger?relay=0&duration=500",
                                "host": [baseUrl],
                                "path": ["api", "relays", "trigger"],
                                "query": [
                                    {"key": "relay", "value": "0"},
                                    {"key": "duration", "value": "500"}
                                ]
                            }
                        }
                    },
                    {
                        "name": "Turn Relay Off",
                        "request": {
                            "method": "POST",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/relays/off?relay=0",
                                "host": [baseUrl],
                                "path": ["api", "relays", "off"],
                                "query": [
                                    {"key": "relay", "value": "0"}
                                ]
                            }
                        }
                    },
                    {
                        "name": "Get Relay Configuration",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/relays/config",
                                "host": [baseUrl],
                                "path": ["api", "relays", "config"]
                            }
                        }
                    },
                    {
                        "name": "Update Relay Configuration",
                        "request": {
                            "method": "POST",
                            "header": [
                                {
                                    "key": "Content-Type",
                                    "value": "application/json"
                                }
                            ],
                            "body": {
                                "mode": "raw",
                                "raw": "{\n    \"relays\": [\n        {\"id\": 0, \"name\": \"Pump 1\", \"enabled\": true, \"duration_ms\": 200, \"off_delay_ms\": 100}\n    ]\n}"
                            },
                            "url": {
                                "raw": baseUrl + "/api/relays/config",
                                "host": [baseUrl],
                                "path": ["api", "relays", "config"]
                            }
                        }
                    }
                ]
            },
            // ============================================================
            // System Configuration Endpoints
            // ============================================================
            {
                "name": "Configuration",
                "item": [
                    {
                        "name": "Get System Configuration",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/config",
                                "host": [baseUrl],
                                "path": ["api", "config"]
                            }
                        }
                    },
                    {
                        "name": "Update System Configuration",
                        "request": {
                            "method": "POST",
                            "header": [
                                {
                                    "key": "Content-Type",
                                    "value": "application/json"
                                }
                            ],
                            "body": {
                                "mode": "raw",
                                "raw": "{\n    \"system_name\": \"WQMS-Greenhouse\",\n    \"system_location\": \"Lab 2\",\n    \"sample_interval_ms\": 2000,\n    \"modbus_interval_ms\": 500,\n    \"automation_interval_sec\": 30,\n    \"ntp_servers\": \"pool.ntp.org,time.google.com\"\n}"
                            },
                            "url": {
                                "raw": baseUrl + "/api/config",
                                "host": [baseUrl],
                                "path": ["api", "config"]
                            }
                        }
                    }
                ]
            },
            // ============================================================
            // WiFi Endpoints
            // ============================================================
            {
                "name": "WiFi",
                "item": [
                    {
                        "name": "Get WiFi Status",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/wifi/status",
                                "host": [baseUrl],
                                "path": ["api", "wifi", "status"]
                            }
                        }
                    },
                    {
                        "name": "Scan WiFi Networks",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/wifi/scan",
                                "host": [baseUrl],
                                "path": ["api", "wifi", "scan"]
                            }
                        }
                    },
                    {
                        "name": "Connect to WiFi (STA)",
                        "request": {
                            "method": "POST",
                            "header": [
                                {
                                    "key": "Content-Type",
                                    "value": "application/json"
                                }
                            ],
                            "body": {
                                "mode": "raw",
                                "raw": "{\n    \"ssid\": \"MyNetwork\",\n    \"password\": \"mysecret\"\n}"
                            },
                            "url": {
                                "raw": baseUrl + "/api/wifi/connect",
                                "host": [baseUrl],
                                "path": ["api", "wifi", "connect"]
                            }
                        }
                    },
                    {
                        "name": "Set WiFi Mode (AP/STA)",
                        "request": {
                            "method": "POST",
                            "header": [
                                {
                                    "key": "Content-Type",
                                    "value": "application/json"
                                }
                            ],
                            "body": {
                                "mode": "raw",
                                "raw": "{\n    \"mode\": 1\n}"
                            },
                            "url": {
                                "raw": baseUrl + "/api/wifi/connect",
                                "host": [baseUrl],
                                "path": ["api", "wifi", "connect"]
                            }
                        }
                    },
                    {
                        "name": "Forget WiFi Network",
                        "request": {
                            "method": "POST",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/wifi/forget",
                                "host": [baseUrl],
                                "path": ["api", "wifi", "forget"]
                            }
                        }
                    }
                ]
            },
            // ============================================================
            // NTP Endpoints
            // ============================================================
            {
                "name": "NTP",
                "item": [
                    {
                        "name": "Force NTP Sync",
                        "request": {
                            "method": "POST",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/ntp/sync",
                                "host": [baseUrl],
                                "path": ["api", "ntp", "sync"]
                            }
                        }
                    },
                    {
                        "name": "Set Manual Time",
                        "request": {
                            "method": "POST",
                            "header": [
                                {
                                    "key": "Content-Type",
                                    "value": "application/json"
                                }
                            ],
                            "body": {
                                "mode": "raw",
                                "raw": "{\n    \"timestamp\": 1718622225\n}"
                            },
                            "url": {
                                "raw": baseUrl + "/api/ntp/manual",
                                "host": [baseUrl],
                                "path": ["api", "ntp", "manual"]
                            }
                        }
                    }
                ]
            },
            // ============================================================
            // MODBUS Endpoints
            // ============================================================
            {
                "name": "MODBUS",
                "item": [
                    {
                        "name": "Get MODBUS Map",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/modbus/map",
                                "host": [baseUrl],
                                "path": ["api", "modbus", "map"]
                            }
                        }
                    },
                    {
                        "name": "Update MODBUS Map",
                        "request": {
                            "method": "POST",
                            "header": [
                                {
                                    "key": "Content-Type",
                                    "value": "application/json"
                                }
                            ],
                            "body": {
                                "mode": "raw",
                                "raw": "{\n    \"entries\": [\n        {\"index\": 0, \"address\": 0x0000}\n    ]\n}"
                            },
                            "url": {
                                "raw": baseUrl + "/api/modbus/map",
                                "host": [baseUrl],
                                "path": ["api", "modbus", "map"]
                            }
                        }
                    }
                ]
            },
            // ============================================================
            // Calibration Endpoints
            // ============================================================
            {
                "name": "Calibration",
                "item": [
                    {
                        "name": "Start Calibration",
                        "request": {
                            "method": "POST",
                            "header": [
                                {
                                    "key": "Content-Type",
                                    "value": "application/json"
                                }
                            ],
                            "body": {
                                "mode": "raw",
                                "raw": "{\n    \"sensor_id\": 0\n}"
                            },
                            "url": {
                                "raw": baseUrl + "/api/calibrate/start",
                                "host": [baseUrl],
                                "path": ["api", "calibrate", "start"]
                            }
                        }
                    },
                    {
                        "name": "Add Calibration Sample",
                        "request": {
                            "method": "POST",
                            "header": [
                                {
                                    "key": "Content-Type",
                                    "value": "application/json"
                                }
                            ],
                            "body": {
                                "mode": "raw",
                                "raw": "{\n    \"known_value\": 4.01\n}"
                            },
                            "url": {
                                "raw": baseUrl + "/api/calibrate/sample",
                                "host": [baseUrl],
                                "path": ["api", "calibrate", "sample"]
                            }
                        }
                    },
                    {
                        "name": "Apply Calibration",
                        "request": {
                            "method": "POST",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/calibrate/apply",
                                "host": [baseUrl],
                                "path": ["api", "calibrate", "apply"]
                            }
                        }
                    },
                    {
                        "name": "Cancel Calibration",
                        "request": {
                            "method": "POST",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/calibrate/cancel",
                                "host": [baseUrl],
                                "path": ["api", "calibrate", "cancel"]
                            }
                        }
                    },
                    {
                        "name": "Get Calibration Factor",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/calibrate/factor",
                                "host": [baseUrl],
                                "path": ["api", "calibrate", "factor"]
                            }
                        }
                    }
                ]
            },
            // ============================================================
            // Logging Endpoints
            // ============================================================
            {
                "name": "Logging",
                "item": [
                    {
                        "name": "List Log Files",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/logs",
                                "host": [baseUrl],
                                "path": ["api", "logs"]
                            }
                        }
                    },
                    {
                        "name": "Download Log File",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/logs?name=system.log",
                                "host": [baseUrl],
                                "path": ["api", "logs"],
                                "query": [
                                    {"key": "name", "value": "system.log"}
                                ]
                            }
                        }
                    },
                    {
                        "name": "Delete Log File",
                        "request": {
                            "method": "DELETE",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/logs?name=system.log",
                                "host": [baseUrl],
                                "path": ["api", "logs"],
                                "query": [
                                    {"key": "name", "value": "system.log"}
                                ]
                            }
                        }
                    },
                    {
                        "name": "Delete All Log Files",
                        "request": {
                            "method": "DELETE",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/logs/all",
                                "host": [baseUrl],
                                "path": ["api", "logs", "all"]
                            }
                        }
                    }
                ]
            },
            // ============================================================
            // Email Endpoints
            // ============================================================
            {
                "name": "Email",
                "item": [
                    {
                        "name": "Get Email Configuration",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/email/config",
                                "host": [baseUrl],
                                "path": ["api", "email", "config"]
                            }
                        }
                    },
                    {
                        "name": "Update Email Configuration",
                        "request": {
                            "method": "POST",
                            "header": [
                                {
                                    "key": "Content-Type",
                                    "value": "application/json"
                                }
                            ],
                            "body": {
                                "mode": "raw",
                                "raw": "{\n    \"enabled\": true,\n    \"smtp_server\": \"smtp.gmail.com\",\n    \"smtp_port\": 587,\n    \"username\": \"user@gmail.com\",\n    \"password\": \"your_password\",\n    \"from_email\": \"user@gmail.com\",\n    \"to_emails\": \"admin@example.com\",\n    \"use_tls\": true\n}"
                            },
                            "url": {
                                "raw": baseUrl + "/api/email/config",
                                "host": [baseUrl],
                                "path": ["api", "email", "config"]
                            }
                        }
                    },
                    {
                        "name": "Send Test Email",
                        "request": {
                            "method": "POST",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/email/test",
                                "host": [baseUrl],
                                "path": ["api", "email", "test"]
                            }
                        }
                    }
                ]
            },
            // ============================================================
            // Automation Rules Endpoints
            // ============================================================
            {
                "name": "Automation Rules",
                "item": [
                    {
                        "name": "Get All Rules",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/rules",
                                "host": [baseUrl],
                                "path": ["api", "rules"]
                            }
                        }
                    },
                    {
                        "name": "Export Rules",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/rules/export",
                                "host": [baseUrl],
                                "path": ["api", "rules", "export"]
                            }
                        }
                    },
                    {
                        "name": "Create New Rule",
                        "request": {
                            "method": "POST",
                            "header": [
                                {
                                    "key": "Content-Type",
                                    "value": "application/json"
                                }
                            ],
                            "body": {
                                "mode": "raw",
                                "raw": "{\n    \"name\": \"Temperature Alert\",\n    \"enabled\": true,\n    \"logic_type\": 0,\n    \"conditions\": [\n        {\"sensor_id\": 0, \"comparator\": 0, \"threshold\": 30.0}\n    ],\n    \"outputs\": [\n        {\"type\": 0, \"id\": 0, \"duration\": 0}\n    ],\n    \"cooldown_seconds\": 60,\n    \"email_recipient\": \"admin@example.com\",\n    \"email_subject\": \"Alert\"\n}"
                            },
                            "url": {
                                "raw": baseUrl + "/api/rules",
                                "host": [baseUrl],
                                "path": ["api", "rules"]
                            }
                        }
                    },
                    {
                        "name": "Update Existing Rule",
                        "request": {
                            "method": "POST",
                            "header": [
                                {
                                    "key": "Content-Type",
                                    "value": "application/json"
                                }
                            ],
                            "body": {
                                "mode": "raw",
                                "raw": "{\n    \"id\": 0,\n    \"name\": \"Updated Alert\",\n    \"enabled\": false\n}"
                            },
                            "url": {
                                "raw": baseUrl + "/api/rules",
                                "host": [baseUrl],
                                "path": ["api", "rules"]
                            }
                        }
                    },
                    {
                        "name": "Import Rules",
                        "request": {
                            "method": "POST",
                            "header": [
                                {
                                    "key": "Content-Type",
                                    "value": "application/json"
                                }
                            ],
                            "body": {
                                "mode": "raw",
                                "raw": "{\n    \"rules\": [\n        {\n            \"name\": \"Temperature Alert\",\n            \"enabled\": true,\n            \"logic_type\": 0,\n            \"conditions\": [\n                {\"sensor_id\": 0, \"comparator\": 0, \"threshold\": 30.0}\n            ],\n            \"outputs\": [\n                {\"type\": 0, \"id\": 0, \"duration\": 0}\n            ],\n            \"cooldown_seconds\": 60,\n            \"email_recipient\": \"admin@example.com\",\n            \"email_subject\": \"Alert\"\n        }\n    ]\n}"
                            },
                            "url": {
                                "raw": baseUrl + "/api/rules/import",
                                "host": [baseUrl],
                                "path": ["api", "rules", "import"]
                            }
                        }
                    },
                    {
                        "name": "Delete Rule",
                        "request": {
                            "method": "DELETE",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/rules/delete?id=0",
                                "host": [baseUrl],
                                "path": ["api", "rules", "delete"],
                                "query": [
                                    {"key": "id", "value": "0"}
                                ]
                            }
                        }
                    }
                ]
            },
            // ============================================================
            // Sensor History Endpoints
            // ============================================================
            {
                "name": "Sensor History",
                "item": [
                    {
                        "name": "Get History Data",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/history?limit=60",
                                "host": [baseUrl],
                                "path": ["api", "history"],
                                "query": [
                                    {"key": "limit", "value": "60"}
                                ]
                            }
                        }
                    },
                    {
                        "name": "Get History Configuration",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/history/config",
                                "host": [baseUrl],
                                "path": ["api", "history", "config"]
                            }
                        }
                    },
                    {
                        "name": "Get History Statistics",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/history/stats",
                                "host": [baseUrl],
                                "path": ["api", "history", "stats"]
                            }
                        }
                    },
                    {
                        "name": "Export History as CSV",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/history/export?limit=100",
                                "host": [baseUrl],
                                "path": ["api", "history", "export"],
                                "query": [
                                    {"key": "limit", "value": "100"}
                                ]
                            }
                        }
                    }
                ]
            },
            // ============================================================
            // ADC Endpoints
            // ============================================================
            {
                "name": "ADC",
                "item": [
                    {
                        "name": "Get ADC Pin Mapping",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/adc/pins",
                                "host": [baseUrl],
                                "path": ["api", "adc", "pins"]
                            }
                        }
                    }
                ]
            },
            // ============================================================
            // Partition Management Endpoints
            // ============================================================
            {
                "name": "Partition Management",
                "item": [
                    {
                        "name": "Delete Logs Partition",
                        "request": {
                            "method": "DELETE",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/partition/logs",
                                "host": [baseUrl],
                                "path": ["api", "partition", "logs"]
                            }
                        }
                    },
                    {
                        "name": "Delete Sensors Partition",
                        "request": {
                            "method": "DELETE",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/partition/sensors",
                                "host": [baseUrl],
                                "path": ["api", "partition", "sensors"]
                            }
                        }
                    }
                ]
            }
        ],
        // ============================================================
        // Variables
        // ============================================================
        "variable": [
            {
                "key": "base_url",
                "value": baseUrl,
                "type": "string"
            }
        ]
    };
    
    // Create and download JSON file
    const json = JSON.stringify(collection, null, 2);
    const blob = new Blob([json], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'WQMS_API_Postman_Collection.json';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
}