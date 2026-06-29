// js/api_docs.js
// API Docs page - loaded only when needed

// ============================================================
// Initialize API Docs
// ============================================================
async function initApiDocs() {
    await updateHeader();
    
    // Set base URL
    const baseUrl = window.location.origin;
    const urlElem = document.getElementById('api-base-url');
    if (urlElem) {
        urlElem.textContent = baseUrl;
    }
    
    // Download buttons
    document.getElementById('download-postman')?.addEventListener('click', function() {
        // Generate Postman collection
        downloadPostmanCollection();
    });
    
    // Auto-refresh header
    setInterval(updateHeader, REFRESH_INTERVAL);
}

// ============================================================
// Download Postman Collection
// ============================================================
function downloadPostmanCollection() {
    const baseUrl = window.location.origin;
    
    const collection = {
        "info": {
            "name": "WQMS API",
            "description": "Water Quality Monitoring System API",
            "schema": "https://schema.getpostman.com/json/collection/v2.1.0/collection.json"
        },
        "item": [
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
                        "name": "Echo",
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
            {
                "name": "Sensors",
                "item": [
                    {
                        "name": "Get All Sensors",
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
                        "name": "Get Sensor Config",
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
                        "name": "Update Sensor Config",
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
                                "raw": "{\n    \"sensors\": [\n        {\"id\": 0, \"name\": \"pH Sensor\", \"enabled\": true, \"calibration_factor\": 1000}\n    ]\n}"
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
            {
                "name": "Relays",
                "item": [
                    {
                        "name": "Get All Relays",
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
                        "name": "Trigger Relay",
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
                                "raw": "{\n    \"duration\": 5000\n}"
                            },
                            "url": {
                                "raw": baseUrl + "/api/relays/0/trigger",
                                "host": [baseUrl],
                                "path": ["api", "relays", "0", "trigger"]
                            }
                        }
                    },
                    {
                        "name": "Turn Off Relay",
                        "request": {
                            "method": "POST",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/relays/0/off",
                                "host": [baseUrl],
                                "path": ["api", "relays", "0", "off"]
                            }
                        }
                    },
                    {
                        "name": "Get Relay Config",
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
                        "name": "Update Relay Config",
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
                                "raw": "{\n    \"relays\": [\n        {\"id\": 0, \"name\": \"Pump 1\", \"enabled\": true, \"duration_ms\": 5000, \"off_delay_ms\": 2000}\n    ]\n}"
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
            {
                "name": "Configuration",
                "item": [
                    {
                        "name": "Get Config",
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
                        "name": "Update Config",
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
                                "raw": "{\n    \"system_name\": \"WQMS-PlantLab\",\n    \"timezone\": \"EET-3\",\n    \"sample_interval_ms\": 1000,\n    \"modbus_interval_ms\": 1000\n}"
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
                        "name": "Scan WiFi",
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
                        "name": "Connect WiFi",
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
                                "raw": "{\n    \"ssid\": \"PlantLab\",\n    \"password\": \"mysecret\"\n}"
                            },
                            "url": {
                                "raw": baseUrl + "/api/wifi/connect",
                                "host": [baseUrl],
                                "path": ["api", "wifi", "connect"]
                            }
                        }
                    },
                    {
                        "name": "Forget WiFi",
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
            {
                "name": "NTP",
                "item": [
                    {
                        "name": "Sync NTP",
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
            {
                "name": "Logging",
                "item": [
                    {
                        "name": "List Logs",
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
                        "name": "Download Log",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/logs?name=system_0.log",
                                "host": [baseUrl],
                                "path": ["api", "logs"],
                                "query": [
                                    {"key": "name", "value": "system_0.log"}
                                ]
                            }
                        }
                    },
                    {
                        "name": "Delete Log",
                        "request": {
                            "method": "DELETE",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/logs?name=system_0.log",
                                "host": [baseUrl],
                                "path": ["api", "logs"],
                                "query": [
                                    {"key": "name", "value": "system_0.log"}
                                ]
                            }
                        }
                    },
                    {
                        "name": "Delete All Logs",
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
            {
                "name": "Notifications",
                "item": [
                    {
                        "name": "Get Webhook Config",
                        "request": {
                            "method": "GET",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/webhook/config",
                                "host": [baseUrl],
                                "path": ["api", "webhook", "config"]
                            }
                        }
                    },
                    {
                        "name": "Update Webhook Config",
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
                                "raw": "{\n    \"url\": \"https://maker.ifttt.com/trigger/...\",\n    \"recipients\": \"admin@example.com\",\n    \"subject\": \"WQMS Alert\",\n    \"enabled\": true\n}"
                            },
                            "url": {
                                "raw": baseUrl + "/api/webhook/config",
                                "host": [baseUrl],
                                "path": ["api", "webhook", "config"]
                            }
                        }
                    },
                    {
                        "name": "Test Webhook",
                        "request": {
                            "method": "POST",
                            "header": [],
                            "url": {
                                "raw": baseUrl + "/api/webhook/test",
                                "host": [baseUrl],
                                "path": ["api", "webhook", "test"]
                            }
                        }
                    }
                ]
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
    a.click();
    URL.revokeObjectURL(url);
}