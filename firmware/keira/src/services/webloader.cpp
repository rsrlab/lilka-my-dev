#include "webloader.h"
#include "esp_http_server.h"

static const char html[] = R"(
<!DOCTYPE html>
<html>
<head>
  <title>Firmware Upload</title>
</head>
<body>
  <form action="/upload" method="post" enctype="multipart/form-data">
    <input type="file" name="file">
    <button type="submit">Upload & Run</button>
  </form>
</body>
</html>)";

httpd_handle_t stream_httpd = NULL;
static const char* contentLengthHeader = "Content-Length";
static const char* fileHeaderDivider = "\r\n\r\n";

static void restartTask(void* parameter) {
    vTaskDelay(pdMS_TO_TICKS(5000));  // Delay for 5 seconds
    esp_restart();                    // Restart the ESP32
}

static void startRestartTask() {
    xTaskCreate(
        restartTask,   // Task function
        "r",           // Task name
        2048,          // Stack size
        NULL,          // Task input parameter
        1,             // Priority
        NULL           // Task handle
    );
};

static esp_err_t index_handler(httpd_req_t *req)
{
    auto res = httpd_resp_sendstr(req, html);
    return res;
}

static int getContentLength(httpd_req_t *req) {
    char contentLength[64];
    auto contentLenLen = httpd_req_get_hdr_value_len(req, contentLengthHeader);

    auto err = httpd_req_get_hdr_value_str(req, contentLengthHeader, contentLength, sizeof(contentLength));
    if (err != ESP_OK) {
        return -1;
    }

    char *endptr;
    int contentLengthValue = strtol(contentLength, &endptr, 10);

    return contentLengthValue;
}

static esp_err_t upload_handler(httpd_req_t *req) {
    esp_ota_handle_t ota_handle;
    esp_err_t res = httpd_resp_set_type(req, "text/html");

    if (res != ESP_OK){
        return res;
    }

    int contentLength = getContentLength(req);

    lilka::serial.log("WebUpload binary size %d", contentLength);

    auto current_partition = esp_ota_get_running_partition();
    auto ota_partition = esp_ota_get_next_update_partition(current_partition);
    
    esp_err_t err = esp_ota_begin(ota_partition, contentLength, &ota_handle);
    lilka::serial.log("esp_ota_begin end");
    if (err == ESP_OK) {
        const int bufSize = 1024;
        char* buf = (char*)ps_malloc(bufSize);
        bool seekBinary = true;
        lilka::serial.log("Recv begin");

        int len = 0;
        do {
            len = httpd_req_recv(req, buf, bufSize);
            if (len < 0) {
                lilka::serial.log("Recv error %d", len);
                break;
            }

            lilka::serial.log("Recv %d bytes", len);

            char* towrite = buf;
            if (seekBinary) {
                auto beforeBinary = strnstr(buf, fileHeaderDivider, len);
                towrite = beforeBinary + 4;
                len -= towrite - buf;
                seekBinary = false;

                lilka::serial.log("skip header %d %d %d", buf, towrite, len);
            }

            if (len > 0) {
                lilka::serial_log("Write %d bytes", len);
                err = esp_ota_write(ota_handle, towrite, len);
                if (err != ESP_OK) {
                    break;
                }
            }
        } while(len > 0);
        
        if (err == ESP_OK) {
            err = esp_ota_end(ota_handle);
            if (err == ESP_OK) {
                err = esp_ota_set_boot_partition(ota_partition);
            }
        }

        free(buf);
    }

    lilka::serial_log("Upload error %d", err);

    if (err == ESP_OK) {
        // reply & restart
        res = httpd_resp_sendstr(req, html);
        startRestartTask(); // deffered restart
    } else {
        // abort OTA
        esp_ota_abort(ota_handle);
        // reply with error page
        res = httpd_resp_send_500(req);
    }
    
    return res;
}

static void startWebServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    httpd_uri_t index_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = index_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t upload_uri = {
        .uri       = "/upload",
        .method    = HTTP_POST,
        .handler   = upload_handler,
        .user_ctx  = NULL
    };

    lilka::serial_log("Start Web Loader on %d", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &index_uri);
        httpd_register_uri_handler(stream_httpd, &upload_uri);
    }
}

WebLoaderService::WebLoaderService()
    : Service("WebLoader") {
}

WebLoaderService::~WebLoaderService() {
}

void WebLoaderService::run() {
    startWebServer();
    for(;;) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}