#include <string>
#include <utility>
#include <sstream>
#include <esp_wifi.h>
#include <esp_http_server.h>

#include "log.h"

#define MIN(a,b) ((a) < (b) ? (a) : (b))

using namespace std;

const char* indexHtml =
R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, minimum-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>Checker WiFi Settings</title>
    <style>
        tr > td > input{
            padding: 0;
            width: 100%;
            line-height: 24px;
            //font-size: 16px;
            border: 1px solid black;
        }
        table{
            width: 92%;
            margin: auto;
            border: 1px solid black;
            border-collapse: collapse;
        }
        td{
            padding: 10px;
            border: 1px solid black;
        }
    </style>
</head>
<body>
    <h1><center>Checker WiFi Settings</center></h1>
    <form method="POST" action="save">
        <table>
            <tr>
                <td width="24%">SSID</td>
                <td>${ssid}</td>
            </tr>
            <tr>
                <td>Password</td>
                <td><input type="password" required minlength="8" maxlength="100" name="password"></td>
            </tr>
            <tr>
                <td colspan='2'><center><input style="width: 50%; font-weight: bold" type="submit" value="Save Data"></center></td>
            </tr>
        </table>
    </form>
</body>
</html>
)rawliteral";

const char* saveHtml =
R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, minimum-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>Checker WiFi Settings</title>
</head>
<body>
    <center>
        <h1>Checker WiFi Settings</h1>
        <div>Checker WiFi AP settings have been successful.</div>
    </center>
</body>
</html>
)rawliteral";

const char* saveHtmlError =
R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, minimum-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>Checker WiFi Settings</title>
</head>
<body>
    <center>
        <h1>FAILED</h1>
    </center>
</body>
</html>
)rawliteral";

string url_decode(const string& encoded){
    ostringstream decoded;
    for(size_t i = 0; i < encoded.length(); ++i){
        if(encoded[i] == '%'){
            if(i + 2 < encoded.length()){
                int decoded_char;
                char hexStr[3] = {encoded[i + 1], encoded[i + 2], '\0'};
                istringstream(hexStr) >> hex >> decoded_char;
                decoded << static_cast<char>(decoded_char);
                i += 2;
            }else{
                return "";
            }
        }else if(encoded[i] == '+'){
            decoded << ' ';
        }else{
            decoded << encoded[i];
        }
    }
    return decoded.str();
}

pair<string, string> parseParameter(char* data){
    string token;
    istringstream iss(data);
    pair<string, string> result = make_pair("", "");
    while(getline(iss, token, '&')){
        size_t equalPos = token.find('=');
        if(equalPos == string::npos){
            continue;
        }
        auto key = token.substr(0, equalPos);
        if(key == "ssid"){
            result.first = url_decode(token.substr(equalPos + 1));
        }
        if(key == "password"){
            result.second = url_decode(token.substr(equalPos + 1));
        }
    }
    return result;
}

string getIndexPage(bool scan){
    string index = indexHtml;

    uint16_t length = 0;
    wifi_ap_record_t apInfo[32];
    if(scan){
        esp_wifi_scan_start(NULL, true);
        esp_wifi_scan_get_ap_num(&length);
        length = MIN(length, 32);

        esp_wifi_scan_get_ap_records(&length, apInfo);
        esp_wifi_scan_stop();
    }

    if(length > 0){
        string ssidInput = "<select name='ssid' style='width: 100%'>";
        for(uint16_t i = 0; i < length; i++){
            ssidInput += "<option>" + string((char*) apInfo[i].ssid) + "</option>";
        }
        index.replace(index.find("${ssid}"), 7, ssidInput + "</select>");
    }else{
        index.replace(index.find("${ssid}"), 7, "<input type='text' required maxlength='100' name='ssid'>");
    }
    return index;
}

esp_err_t indexPage(httpd_req_t* req){
    return httpd_resp_send(req, getIndexPage(true).c_str(), HTTPD_RESP_USE_STRLEN);
}

esp_err_t savePage(httpd_req_t* req){
    char content[req->content_len + 1] = {0};
    int ret = httpd_req_recv(req, content, req->content_len);
    if(ret > 0){
        auto data = parseParameter(content);
        if(data.first.length() > 0 && data.second.length() > 7){
            httpd_resp_send(req, saveHtml, HTTPD_RESP_USE_STRLEN);

            wifi_config_t staConfig = {0};
            strcpy((char*) staConfig.sta.ssid, data.first.c_str());
            strcpy((char*) staConfig.sta.password, data.second.c_str());
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &staConfig));
            esp_wifi_connect();
        }else{
            httpd_resp_send(req, saveHtmlError, HTTPD_RESP_USE_STRLEN);
        }
    }else{
        if(ret == HTTPD_SOCK_ERR_TIMEOUT){
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, NULL);
        }
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t startWebServer(httpd_handle_t* server){
    debugPrint("[Web] Start Server\n");
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    esp_err_t err = httpd_start(server, &config);
    if(err == ESP_OK){
        httpd_uri_t index = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = indexPage,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(*server, &index);

        httpd_uri_t saveUri = {
            .uri = "/save",
            .method = HTTP_POST,
            .handler = savePage,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(*server, &saveUri);
    }
    return err;
}

esp_err_t stopWebServer(httpd_handle_t* server){
    debugPrint("[Web] Stop Server\n");
    esp_err_t err = httpd_stop(*server);
    *server = NULL;
    return err;
}