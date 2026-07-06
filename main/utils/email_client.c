#include "email_client.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "log_levels.h"
#include "logger.h"
#include "nvs_config.h"
#include "wifi_manager.h"

// Configuration
static email_config_t g_config = {
    .smtp_server = "smtp.gmail.com",
    .smtp_port = 587,
    .username = "",
    .password = "",
    .from_email = "",
    .to_emails = "",
    .use_tls = true,
    .enabled = false
};

// NVS Keys
#define NVS_KEY_EMAIL_ENABLED      "email_enabled"
#define NVS_KEY_EMAIL_SERVER       "email_server"
#define NVS_KEY_EMAIL_PORT         "email_port"
#define NVS_KEY_EMAIL_USERNAME     "email_user"
#define NVS_KEY_EMAIL_PASSWORD     "email_pass"
#define NVS_KEY_EMAIL_FROM         "email_from"
#define NVS_KEY_EMAIL_TO           "email_to"
#define NVS_KEY_EMAIL_TLS          "email_tls"

#define BUFFER_SIZE 512
#define TIMEOUT_MS 10000

extern const char r1_pem_start[] asm("_binary_r1_pem_start");
extern const char r1_pem_end[]   asm("_binary_r1_pem_end");

// ============================================
// BASE64 ENCODING
// ============================================
static void base64_encode(const unsigned char *input, int len, char *output) {
    const char *table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i = 0, j = 0;
    unsigned char a, b, c;
    
    while (len >= 3) {
        a = input[i++]; b = input[i++]; c = input[i++];
        output[j++] = table[a >> 2];
        output[j++] = table[((a & 0x03) << 4) | (b >> 4)];
        output[j++] = table[((b & 0x0f) << 2) | (c >> 6)];
        output[j++] = table[c & 0x3f];
        len -= 3;
    }
    if (len == 2) {
        a = input[i++]; b = input[i++];
        output[j++] = table[a >> 2];
        output[j++] = table[((a & 0x03) << 4) | (b >> 4)];
        output[j++] = table[(b & 0x0f) << 2];
        output[j++] = '=';
    } else if (len == 1) {
        a = input[i++];
        output[j++] = table[a >> 2];
        output[j++] = table[(a & 0x03) << 4];
        output[j++] = '=';
        output[j++] = '=';
    }
    output[j] = '\0';
}

// ============================================
// SOCKET HELPERS
// ============================================
static int recv_response(int sock, char *buf, int size) {
    int len = recv(sock, buf, size - 1, 0);
    if (len <= 0) return -1;
    buf[len] = '\0';
    return (buf[0] == '2' || buf[0] == '3') ? 0 : -1;
}

static int send_cmd(int sock, const char *cmd, const char *arg) {
    char buf[BUFFER_SIZE];
    snprintf(buf, sizeof(buf), arg ? "%s %s\r\n" : "%s\r\n", cmd, arg ? arg : "");
    return (send(sock, buf, strlen(buf), 0) > 0) ? 0 : -1;
}

static int send_cmd_recv(int sock, const char *cmd, const char *arg, char *buf, int size) {
    if (send_cmd(sock, cmd, arg) != 0) return -1;
    return recv_response(sock, buf, size);
}

// ============================================
// TLS READ/WRITE HELPERS
// ============================================
static int tls_write(esp_tls_t *tls, const char *data) {
    return esp_tls_conn_write(tls, data, strlen(data));
}

static int tls_read(esp_tls_t *tls, char *buf, int size) {
    return esp_tls_conn_read(tls, buf, size - 1);
}

static int tls_recv_response(esp_tls_t *tls, char *buf, int size) {
    int len = tls_read(tls, buf, size);
    if (len <= 0) return -1;
    buf[len] = '\0';
    return (buf[0] == '2' || buf[0] == '3') ? 0 : -1;
}

static int tls_send_cmd(esp_tls_t *tls, const char *cmd, const char *arg) {
    char buf[BUFFER_SIZE];
    snprintf(buf, sizeof(buf), arg ? "%s %s\r\n" : "%s\r\n", cmd, arg ? arg : "");
    return (tls_write(tls, buf) > 0) ? 0 : -1;
}

static int tls_send_cmd_recv(esp_tls_t *tls, const char *cmd, const char *arg, char *buf, int size) {
    if (tls_send_cmd(tls, cmd, arg) != 0) return -1;
    return tls_recv_response(tls, buf, size);
}

// ============================================
// SMTP SEND
// ============================================
static esp_err_t send_email_smtp(const email_message_t *msg) {
    static char buf[BUFFER_SIZE];
    static char from[BUFFER_SIZE];
    static char to[BUFFER_SIZE];
    static char email[2048];
    static char to_copy[256];
    static char date_str[64];
    static char b64[256];
    
    int sock = -1;
    esp_tls_t *tls = NULL;
    int ret = -1;
    int len;
    
//    NOTIFICATION_LOG_I("=== EMAIL SEND START ===");
//    NOTIFICATION_LOG_I("Server: %s:%d", g_config.smtp_server, g_config.smtp_port);
//    NOTIFICATION_LOG_I("From: %s", g_config.from_email);
//    NOTIFICATION_LOG_I("To: %s", g_config.to_emails);
//    NOTIFICATION_LOG_I("Subject: %s", msg->subject);

    // 1. Connect - use TLS directly if port is 465
    if (g_config.use_tls && g_config.smtp_port == 465) {
//        NOTIFICATION_LOG_I("Using direct TLS on port 465");
        
        esp_tls_cfg_t cfg = {
            .timeout_ms = TIMEOUT_MS,
            .non_block = false,
            .skip_common_name = true,
            .cacert_pem_buf = (const unsigned char *)r1_pem_start,
            .cacert_pem_bytes = r1_pem_end - r1_pem_start,
        };
        
        tls = esp_tls_init();
        if (!tls) { 
            NOTIFICATION_LOG_E("TLS init failed"); 
            goto cleanup; 
        }
        
//        NOTIFICATION_LOG_I("Connecting to %s:%d", g_config.smtp_server, g_config.smtp_port);
        
        if (esp_tls_conn_new_sync(g_config.smtp_server, strlen(g_config.smtp_server),
                                  g_config.smtp_port, &cfg, tls) != 1) {
            NOTIFICATION_LOG_E("TLS connect failed");
            esp_tls_conn_destroy(tls);
            tls = NULL;
            goto cleanup;
        }
        
//        NOTIFICATION_LOG_I("TLS connected successfully");
        
        // ✅ Read the greeting after TLS handshake
//        NOTIFICATION_LOG_I("Waiting for greeting...");
        len = tls_read(tls, buf, sizeof(buf) - 1);
        if (len <= 0) {
            NOTIFICATION_LOG_E("Failed to receive greeting (len=%d)", len);
            goto cleanup;
        }
        buf[len] = '\0';
//        NOTIFICATION_LOG_I("Received greeting: %s", buf);
        
        // Check if it's a valid greeting (220 is standard SMTP greeting)
        if (buf[0] != '2') {
            NOTIFICATION_LOG_E("Invalid greeting: %s", buf);
            goto cleanup;
        }
        
        // ✅ Send EHLO
//        NOTIFICATION_LOG_I("Sending EHLO");
        if (tls_send_cmd_recv(tls, "EHLO", "localhost", buf, sizeof(buf)) != 0) {
            NOTIFICATION_LOG_W("EHLO failed, trying HELO");
            if (tls_send_cmd_recv(tls, "HELO", "localhost", buf, sizeof(buf)) != 0) {
                NOTIFICATION_LOG_E("HELO also failed: %s", buf);
                goto cleanup;
            }
        }
//        NOTIFICATION_LOG_I("EHLO/HELO successful");
        
        // Skip STARTTLS since we're already using TLS
        goto after_starttls;
    }
    
    // Plain TCP connection for STARTTLS (port 587)
//    NOTIFICATION_LOG_I("Connecting to %s:%d", g_config.smtp_server, g_config.smtp_port);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_config.smtp_port);
    
    struct hostent *he = gethostbyname(g_config.smtp_server);
    if (!he) { 
        NOTIFICATION_LOG_E("DNS failed for %s", g_config.smtp_server); 
        goto cleanup; 
    }
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { 
        NOTIFICATION_LOG_E("Socket creation failed"); 
        goto cleanup; 
    }
    
    struct timeval tv = {TIMEOUT_MS/1000, (TIMEOUT_MS%1000)*1000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        NOTIFICATION_LOG_E("Connect failed to %s:%d", g_config.smtp_server, g_config.smtp_port);
        goto cleanup;
    }
//    NOTIFICATION_LOG_I("Connected successfully");
    
    if (recv_response(sock, buf, sizeof(buf)) != 0) {
        NOTIFICATION_LOG_E("Failed to receive greeting: %s", buf);
        goto cleanup;
    }
//    NOTIFICATION_LOG_I("Received greeting: %s", buf);

    // 2. EHLO
//    NOTIFICATION_LOG_I("Sending EHLO");
    if (send_cmd_recv(sock, "EHLO", "localhost", buf, sizeof(buf)) != 0) {
        NOTIFICATION_LOG_W("EHLO failed, trying HELO");
        if (send_cmd_recv(sock, "HELO", "localhost", buf, sizeof(buf)) != 0) {
            NOTIFICATION_LOG_E("HELO also failed: %s", buf);
            goto cleanup;
        }
    }
//    NOTIFICATION_LOG_I("EHLO/HELO successful");
    
    // 3. STARTTLS (if enabled and port is 587)
    if (g_config.use_tls && g_config.smtp_port == 587) {
//        NOTIFICATION_LOG_I("Sending STARTTLS");
        if (send_cmd_recv(sock, "STARTTLS", NULL, buf, sizeof(buf)) == 0) {
//            NOTIFICATION_LOG_I("Starting TLS...");
            
            int sock_fd = sock;
            sock = -1;
            
            esp_tls_cfg_t cfg = {
                .timeout_ms = TIMEOUT_MS,
                .non_block = false,
                .skip_common_name = true,
                .cacert_pem_buf = (const unsigned char *)r1_pem_start,
                .cacert_pem_bytes = r1_pem_end - r1_pem_start,
            };
            
            tls = esp_tls_init();
            if (!tls) {
                NOTIFICATION_LOG_E("TLS init failed");
                close(sock_fd);
                goto cleanup;
            }
            
            esp_tls_set_conn_sockfd(tls, sock_fd);
            
            if (esp_tls_conn_new_sync(g_config.smtp_server, strlen(g_config.smtp_server),
                                      g_config.smtp_port, &cfg, tls) != 1) {
                NOTIFICATION_LOG_E("TLS handshake failed");
                esp_tls_conn_destroy(tls);
                tls = NULL;
                close(sock_fd);
                goto cleanup;
            }
            
//            NOTIFICATION_LOG_I("TLS established");
            
            // Re-EHLO after TLS
//            NOTIFICATION_LOG_I("Re-sending EHLO after TLS");
            if (tls_send_cmd_recv(tls, "EHLO", "localhost", buf, sizeof(buf)) != 0) {
                NOTIFICATION_LOG_E("EHLO after TLS failed: %s", buf);
                goto cleanup;
            }
//            NOTIFICATION_LOG_I("EHLO after TLS successful");
        } else {
            NOTIFICATION_LOG_W("STARTTLS failed: %s", buf);
        }
    }

after_starttls:
    // 4. AUTH LOGIN
    if (strlen(g_config.username) > 0 && strlen(g_config.password) > 0) {
//        NOTIFICATION_LOG_I("Starting AUTH LOGIN");
        
        if (tls) {
            if (tls_send_cmd_recv(tls, "AUTH", "LOGIN", buf, sizeof(buf)) != 0) {
                NOTIFICATION_LOG_E("AUTH LOGIN failed: %s", buf);
                goto cleanup;
            }
        } else {
            if (send_cmd_recv(sock, "AUTH", "LOGIN", buf, sizeof(buf)) != 0) {
                NOTIFICATION_LOG_E("AUTH LOGIN failed: %s", buf);
                goto cleanup;
            }
        }
//        NOTIFICATION_LOG_I("AUTH LOGIN accepted");
        
        base64_encode((const unsigned char*)g_config.username, strlen(g_config.username), b64);
//        NOTIFICATION_LOG_I("Username (base64): %s", b64);
        
        if (tls) {
            if (tls_send_cmd_recv(tls, b64, NULL, buf, sizeof(buf)) != 0) {
                NOTIFICATION_LOG_E("Username rejected: %s", buf);
                goto cleanup;
            }
        } else {
            if (send_cmd_recv(sock, b64, NULL, buf, sizeof(buf)) != 0) {
                NOTIFICATION_LOG_E("Username rejected: %s", buf);
                goto cleanup;
            }
        }
//        NOTIFICATION_LOG_I("Username accepted");
        
        base64_encode((const unsigned char*)g_config.password, strlen(g_config.password), b64);
//        NOTIFICATION_LOG_I("Password (base64): %s", b64);
        
        if (tls) {
            if (tls_send_cmd_recv(tls, b64, NULL, buf, sizeof(buf)) != 0) {
                NOTIFICATION_LOG_E("Password rejected: %s", buf);
                goto cleanup;
            }
        } else {
            if (send_cmd_recv(sock, b64, NULL, buf, sizeof(buf)) != 0) {
                NOTIFICATION_LOG_E("Password rejected: %s", buf);
                goto cleanup;
            }
        }
//        NOTIFICATION_LOG_I("Authentication successful");
    }
    
    // 5. MAIL FROM
//    NOTIFICATION_LOG_I("=== MAIL FROM ===");
//    NOTIFICATION_LOG_I("From email: '%s'", g_config.from_email);
    
    snprintf(from, sizeof(from), "MAIL FROM:<%s>", g_config.from_email);
    
    if (tls) {
        if (tls_send_cmd_recv(tls, from, NULL, buf, sizeof(buf)) != 0) {
            NOTIFICATION_LOG_E("MAIL FROM failed with response: %s", buf);
            if (strstr(buf, "Sender") != NULL || strstr(buf, "address") != NULL) {
                NOTIFICATION_LOG_E("The from address '%s' may be invalid", g_config.from_email);
            }
            goto cleanup;
        }
    } else {
        if (send_cmd_recv(sock, from, NULL, buf, sizeof(buf)) != 0) {
            NOTIFICATION_LOG_E("MAIL FROM failed with response: %s", buf);
            if (strstr(buf, "Sender") != NULL || strstr(buf, "address") != NULL) {
                NOTIFICATION_LOG_E("The from address '%s' may be invalid", g_config.from_email);
            }
            goto cleanup;
        }
    }
//    NOTIFICATION_LOG_I("MAIL FROM successful");
    
    // 6. RCPT TO
//    NOTIFICATION_LOG_I("=== RCPT TO ===");
    strncpy(to_copy, g_config.to_emails, sizeof(to_copy)-1);
    to_copy[sizeof(to_copy)-1] = '\0';
    char *token = strtok(to_copy, ",");
    int rcpt_count = 0;
    while (token) {
        while (*token == ' ') token++;
        snprintf(to, sizeof(to), "RCPT TO:<%s>", token);
//        NOTIFICATION_LOG_I("RCPT TO %d: %s", ++rcpt_count, to);
        
        if (tls) {
            if (tls_send_cmd_recv(tls, to, NULL, buf, sizeof(buf)) != 0) {
                NOTIFICATION_LOG_E("RCPT TO failed for '%s': %s", token, buf);
                goto cleanup;
            }
        } else {
            if (send_cmd_recv(sock, to, NULL, buf, sizeof(buf)) != 0) {
                NOTIFICATION_LOG_E("RCPT TO failed for '%s': %s", token, buf);
                goto cleanup;
            }
        }
//        NOTIFICATION_LOG_I("RCPT TO %d successful", rcpt_count);
        token = strtok(NULL, ",");
    }
    
    // 7. DATA
//    NOTIFICATION_LOG_I("=== DATA ===");
    if (tls) {
        if (tls_send_cmd_recv(tls, "DATA", NULL, buf, sizeof(buf)) != 0) {
            NOTIFICATION_LOG_E("DATA command failed: %s", buf);
            goto cleanup;
        }
    } else {
        if (send_cmd_recv(sock, "DATA", NULL, buf, sizeof(buf)) != 0) {
            NOTIFICATION_LOG_E("DATA command failed: %s", buf);
            goto cleanup;
        }
    }
//    NOTIFICATION_LOG_I("DATA command accepted");
    
    // 8. Email content
    time_t now = time(NULL);
    strftime(date_str, sizeof(date_str), "%a, %d %b %Y %H:%M:%S %z", localtime(&now));
    
    int written = snprintf(email, sizeof(email),
        "Date: %s\r\n"
        "From: %s\r\n"
        "To: %s\r\n"
        "Subject: %s\r\n"
        "MIME-Version: 1.0\r\n"
        "Content-Type: text/plain; charset=UTF-8\r\n"
        "\r\n"
        "%s\r\n"
        ".\r\n",
        date_str, g_config.from_email, g_config.to_emails,
        msg->subject, msg->body);
    
    if (written < 0 || written >= (int)sizeof(email)) {
        NOTIFICATION_LOG_E("Email content too large (written=%d)", written);
        goto cleanup;
    }
//    NOTIFICATION_LOG_I("Email content prepared (%d bytes)", written);
    
    if (tls) {
        if (tls_write(tls, email) <= 0) {
            NOTIFICATION_LOG_E("Failed to send email content over TLS");
            goto cleanup;
        }
        if (tls_recv_response(tls, buf, sizeof(buf)) != 0) {
            NOTIFICATION_LOG_E("Failed to get final response: %s", buf);
            goto cleanup;
        }
    } else {
        if (send(sock, email, strlen(email), 0) < 0) {
            NOTIFICATION_LOG_E("Failed to send email content over TCP");
            goto cleanup;
        }
        if (recv_response(sock, buf, sizeof(buf)) != 0) {
            NOTIFICATION_LOG_E("Failed to get final response: %s", buf);
            goto cleanup;
        }
    }
    
    NOTIFICATION_LOG_I("Email sent successfully");
    ret = 0;
    
cleanup:
//    NOTIFICATION_LOG_I("=== EMAIL SEND CLEANUP ===");
    if (sock >= 0) {
        send_cmd_recv(sock, "QUIT", NULL, buf, sizeof(buf));
        close(sock);
    }
    if (tls) {
        tls_send_cmd(tls, "QUIT", NULL);
        tls_recv_response(tls, buf, sizeof(buf));
        esp_tls_conn_destroy(tls);
    }
//    NOTIFICATION_LOG_I("=== EMAIL SEND END (ret=%d) ===", ret);
    return (ret == 0) ? ESP_OK : ESP_FAIL;
}

// ============================================
// PUBLIC API
// ============================================

esp_err_t email_client_init(void) {
    NOTIFICATION_LOG_I("Initializing email client");
    esp_err_t err = email_load_config(&g_config);
    if (err != ESP_OK) {
        email_save_config(&g_config);
    }
    return ESP_OK;
}

esp_err_t email_load_config(email_config_t *config) {
    if (!config) return ESP_ERR_INVALID_ARG;

    // First, set ALL defaults
    memset(config, 0, sizeof(email_config_t));
    strcpy(config->smtp_server, "smtp.gmail.com");
    config->smtp_port = 587;
    strcpy(config->username, "");
    strcpy(config->password, "");
    strcpy(config->from_email, "");
    strcpy(config->to_emails, "");
    config->use_tls = true;
    config->enabled = false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open("email", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        NOTIFICATION_LOG_E("Failed to open NVS: %s", esp_err_to_name(err));
        return err;  // Return defaults
    }

    // Read each parameter with proper error handling
    uint8_t val;
    size_t len;

    // enabled
    err = nvs_get_u8(handle, NVS_KEY_EMAIL_ENABLED, &val);
    if (err == ESP_OK) {
        config->enabled = (val != 0);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        NOTIFICATION_LOG_E("Error reading %s: %s", NVS_KEY_EMAIL_ENABLED, esp_err_to_name(err));
    }

    // use_tls
    err = nvs_get_u8(handle, NVS_KEY_EMAIL_TLS, &val);
    if (err == ESP_OK) {
        config->use_tls = (val != 0);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        NOTIFICATION_LOG_E("Error reading %s: %s", NVS_KEY_EMAIL_TLS, esp_err_to_name(err));
    }

    // smtp_server
    err = nvs_get_str(handle, NVS_KEY_EMAIL_SERVER, NULL, &len);
    if (err == ESP_OK && len > 0 && len < sizeof(config->smtp_server)) {
        nvs_get_str(handle, NVS_KEY_EMAIL_SERVER, config->smtp_server, &len);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        NOTIFICATION_LOG_E("Error reading %s: %s", NVS_KEY_EMAIL_SERVER, esp_err_to_name(err));
    }

    // username
    err = nvs_get_str(handle, NVS_KEY_EMAIL_USERNAME, NULL, &len);
    if (err == ESP_OK && len > 0 && len < sizeof(config->username)) {
        nvs_get_str(handle, NVS_KEY_EMAIL_USERNAME, config->username, &len);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        NOTIFICATION_LOG_E("Error reading %s: %s", NVS_KEY_EMAIL_USERNAME, esp_err_to_name(err));
    }
    
    // password
    err = nvs_get_str(handle, NVS_KEY_EMAIL_PASSWORD, NULL, &len);
    if (err == ESP_OK && len > 0 && len < sizeof(config->password)) {
        nvs_get_str(handle, NVS_KEY_EMAIL_PASSWORD, config->password, &len);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        NOTIFICATION_LOG_E("Error reading %s: %s", NVS_KEY_EMAIL_PASSWORD, esp_err_to_name(err));
    }
    
    // from_email
    err = nvs_get_str(handle, NVS_KEY_EMAIL_FROM, NULL, &len);
    if (err == ESP_OK && len > 0 && len < sizeof(config->from_email)) {
        nvs_get_str(handle, NVS_KEY_EMAIL_FROM, config->from_email, &len);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        NOTIFICATION_LOG_E("Error reading %s: %s", NVS_KEY_EMAIL_FROM, esp_err_to_name(err));
    }
    
    // to_emails
    err = nvs_get_str(handle, NVS_KEY_EMAIL_TO, NULL, &len);
    if (err == ESP_OK && len > 0 && len < sizeof(config->to_emails)) {
        nvs_get_str(handle, NVS_KEY_EMAIL_TO, config->to_emails, &len);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        NOTIFICATION_LOG_E("Error reading %s: %s", NVS_KEY_EMAIL_TO, esp_err_to_name(err));
    }
    
    // smtp_port
    uint16_t port;
    err = nvs_get_u16(handle, NVS_KEY_EMAIL_PORT, &port);
    if (err == ESP_OK) {
        config->smtp_port = port;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        NOTIFICATION_LOG_E("Error reading %s: %s", NVS_KEY_EMAIL_PORT, esp_err_to_name(err));
    }
    
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t email_save_config(const email_config_t *config) {
    if (!config) return ESP_ERR_INVALID_ARG;
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open("email", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    
    nvs_set_u8(handle, NVS_KEY_EMAIL_ENABLED, config->enabled ? 1 : 0);
    nvs_set_u8(handle, NVS_KEY_EMAIL_TLS, config->use_tls ? 1 : 0);
    nvs_set_str(handle, NVS_KEY_EMAIL_SERVER, config->smtp_server);
    nvs_set_str(handle, NVS_KEY_EMAIL_USERNAME, config->username);
    nvs_set_str(handle, NVS_KEY_EMAIL_PASSWORD, config->password);
    nvs_set_str(handle, NVS_KEY_EMAIL_FROM, config->from_email);
    nvs_set_str(handle, NVS_KEY_EMAIL_TO, config->to_emails);
    nvs_set_u16(handle, NVS_KEY_EMAIL_PORT, config->smtp_port);
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err == ESP_OK) {
        memcpy(&g_config, config, sizeof(email_config_t));
    }
    return err;
}

esp_err_t email_send(const email_message_t *message) {
    if (!message) return ESP_ERR_INVALID_ARG;
    if (!g_config.enabled) {
        NOTIFICATION_LOG_W("Configuration is not enabled!");
        return ESP_ERR_INVALID_STATE;
    }
    if (strlen(g_config.username) == 0 || strlen(g_config.password) == 0 ||
        strlen(g_config.to_emails) == 0 || strlen(g_config.from_email) == 0) {
        email_client_init();
    }
    if (strlen(g_config.username) == 0 || strlen(g_config.password) == 0 ||
        strlen(g_config.to_emails) == 0 || strlen(g_config.from_email) == 0) {
        NOTIFICATION_LOG_E("Configuration is not complete!");
        return ESP_ERR_INVALID_STATE;
    }
    
    char full_body[2048];
    snprintf(full_body, sizeof(full_body),
        "%s\n\n-----o-----\nSystem:%s\nMAC:%s\nHeap: %lu bytes\nUptime: %llu sec",
        message->body,
        nvs_get_system_name(),
        wifi_get_mac(),
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long long)(esp_timer_get_time() / 1000000));
    
    email_message_t enriched = *message;
    strncpy(enriched.body, full_body, sizeof(enriched.body)-1);
    enriched.body[sizeof(enriched.body)-1] = '\0';
    return send_email_smtp(&enriched);
}

esp_err_t email_send_notification(const char *subject, const char *body) {
    email_message_t msg = {0};
    strncpy(msg.subject, subject, sizeof(msg.subject)-1);
    msg.subject[sizeof(msg.subject)-1] = '\0';
    strncpy(msg.body, body, sizeof(msg.body)-1);
    msg.body[sizeof(msg.body)-1] = '\0';
    return email_send(&msg);
}

esp_err_t email_send_test(void) {
    return email_send_notification("ESP32 Test", "Test email from ESP32");
}

bool email_is_configured(void) {
    return g_config.enabled && 
           strlen(g_config.username) > 0 &&
           strlen(g_config.password) > 0 &&
           strlen(g_config.to_emails) > 0 &&
           strlen(g_config.from_email) > 0;
}