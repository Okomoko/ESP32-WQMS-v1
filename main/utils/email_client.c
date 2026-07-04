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
    int sock = -1;
    esp_tls_t *tls = NULL;
    char buf[BUFFER_SIZE];
    int ret = -1;
    
    // 1. Connect
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_config.smtp_port);
    
    struct hostent *he = gethostbyname(g_config.smtp_server);
    if (!he) { NOTIFICATION_LOG_E("DNS failed"); goto cleanup; }
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) goto cleanup;
    
    struct timeval tv = {TIMEOUT_MS/1000, (TIMEOUT_MS%1000)*1000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) goto cleanup;
    if (recv_response(sock, buf, sizeof(buf)) != 0) goto cleanup;
    
    // 2. EHLO
    if (send_cmd_recv(sock, "EHLO", "localhost", buf, sizeof(buf)) != 0) {
        if (send_cmd_recv(sock, "HELO", "localhost", buf, sizeof(buf)) != 0) goto cleanup;
    }
    
    // 3. STARTTLS (if enabled)
    if (g_config.use_tls && g_config.smtp_port != 465) {
        if (send_cmd_recv(sock, "STARTTLS", NULL, buf, sizeof(buf)) == 0) {
            NOTIFICATION_LOG_I("Starting TLS...");
            
            esp_tls_cfg_t cfg = {
                .timeout_ms = TIMEOUT_MS,
                .non_block = false,
            };
            
            tls = esp_tls_init();
            if (!tls) goto cleanup;
            
            if (esp_tls_conn_new_sync(g_config.smtp_server, strlen(g_config.smtp_server),
                                      g_config.smtp_port, &cfg, tls) != 1) {
                esp_tls_conn_destroy(tls);
                tls = NULL;
                goto cleanup;
            }
            
            NOTIFICATION_LOG_I("TLS established");
            
            if (tls_send_cmd_recv(tls, "EHLO", "localhost", buf, sizeof(buf)) != 0) goto cleanup;
        }
    }
    
    // 4. AUTH LOGIN
    if (strlen(g_config.username) > 0 && strlen(g_config.password) > 0) {
        char b64[256];
        
        if (tls) {
            if (tls_send_cmd_recv(tls, "AUTH", "LOGIN", buf, sizeof(buf)) != 0) goto cleanup;
        } else {
            if (send_cmd_recv(sock, "AUTH", "LOGIN", buf, sizeof(buf)) != 0) goto cleanup;
        }
        
        base64_encode((const unsigned char*)g_config.username, strlen(g_config.username), b64);
        if (tls) {
            if (tls_send_cmd_recv(tls, b64, NULL, buf, sizeof(buf)) != 0) goto cleanup;
        } else {
            if (send_cmd_recv(sock, b64, NULL, buf, sizeof(buf)) != 0) goto cleanup;
        }
        
        base64_encode((const unsigned char*)g_config.password, strlen(g_config.password), b64);
        if (tls) {
            if (tls_send_cmd_recv(tls, b64, NULL, buf, sizeof(buf)) != 0) goto cleanup;
        } else {
            if (send_cmd_recv(sock, b64, NULL, buf, sizeof(buf)) != 0) goto cleanup;
        }
    }
    
    // 5. MAIL FROM
    char from[BUFFER_SIZE];
    snprintf(from, sizeof(from), "<%s>", g_config.from_email);
    if (tls) {
        if (tls_send_cmd_recv(tls, "MAIL FROM", from, buf, sizeof(buf)) != 0) goto cleanup;
    } else {
        if (send_cmd_recv(sock, "MAIL FROM", from, buf, sizeof(buf)) != 0) goto cleanup;
    }
    
    // 6. RCPT TO
    char to_copy[256];
    strncpy(to_copy, g_config.to_emails, sizeof(to_copy)-1);
    to_copy[sizeof(to_copy)-1] = '\0';
    char *token = strtok(to_copy, ",");
    while (token) {
        while (*token == ' ') token++;
        char to[BUFFER_SIZE];
        snprintf(to, sizeof(to), "<%s>", token);
        if (tls) {
            if (tls_send_cmd_recv(tls, "RCPT TO", to, buf, sizeof(buf)) != 0) goto cleanup;
        } else {
            if (send_cmd_recv(sock, "RCPT TO", to, buf, sizeof(buf)) != 0) goto cleanup;
        }
        token = strtok(NULL, ",");
    }
    
    // 7. DATA
    if (tls) {
        if (tls_send_cmd_recv(tls, "DATA", NULL, buf, sizeof(buf)) != 0) goto cleanup;
    } else {
        if (send_cmd_recv(sock, "DATA", NULL, buf, sizeof(buf)) != 0) goto cleanup;
    }
    
    // 8. Email content - use larger buffer to avoid truncation warning
    char email[2048];
    char date_str[64];
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
        NOTIFICATION_LOG_E("Email content too large");
        goto cleanup;
    }
    
    if (tls) {
        if (tls_write(tls, email) <= 0) goto cleanup;
        if (tls_recv_response(tls, buf, sizeof(buf)) != 0) goto cleanup;
    } else {
        if (send(sock, email, strlen(email), 0) < 0) goto cleanup;
        if (recv_response(sock, buf, sizeof(buf)) != 0) goto cleanup;
    }
    
    ret = 0;
    
cleanup:
    if (sock >= 0) {
        if (tls) {
            tls_send_cmd(tls, "QUIT", NULL);
            tls_recv_response(tls, buf, sizeof(buf));
            esp_tls_conn_destroy(tls);
        } else {
            send_cmd_recv(sock, "QUIT", NULL, buf, sizeof(buf));
        }
        close(sock);
    }
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
    if (!g_config.enabled) return ESP_ERR_INVALID_STATE;
    if (strlen(g_config.username) == 0 || strlen(g_config.password) == 0 ||
        strlen(g_config.to_emails) == 0 || strlen(g_config.from_email) == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    char full_body[2048];
    snprintf(full_body, sizeof(full_body),
        "%s\n\n--- System ---\nHeap: %lu bytes\nUptime: %llu sec",
        message->body,
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