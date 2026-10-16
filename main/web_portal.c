#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include "web_portal.h"
#include "ws2812.h"
#include "wifi_manager.h"
#include "config_store.h"
#include "remoteplay_stub.h"
#include "bluepad32_bridge.h"
#include "input_config.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char *TAG = "web_portal";
static httpd_handle_t s_server = NULL;
static volatile bool s_connect_task_running;

/* Dark theme portal with controller status, IP ping, drift, registration */
static const char *FORM_HTML =
"<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>ESP32 PS5 Remote</title>"
"<style>"
":root{--bg:#0f1115;--card:#1a1d24;--border:#2a2f3a;--text:#e8eaed;--muted:#9aa0a6;"
"--accent:#5b8def;--ok:#3dd68c;--warn:#f5a524;--danger:#e85d5d;--input:#12151c}"
"*{box-sizing:border-box}body{font-family:system-ui,-apple-system,sans-serif;background:var(--bg);"
"color:var(--text);max-width:560px;margin:0 auto;padding:20px 14px;line-height:1.45}"
"h2{margin:0 0 6px;font-size:1.35rem;color:var(--text)}"
"h3{margin:0 0 10px;font-size:1.05rem;color:var(--accent)}"
".card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:16px;margin-top:14px}"
"label{display:block;margin-top:12px;font-weight:600;font-size:.9rem;color:var(--text)}"
"input[type=text],input[type=password],input[type=number],input[type=search],select,textarea{"
"width:100%;padding:10px 12px;margin-top:4px;border-radius:8px;border:1px solid var(--border);"
"background:#12151c!important;color:#e8eaed!important;-webkit-appearance:none;appearance:none;"
"caret-color:#e8eaed}"
"input[type=checkbox]{width:18px!important;height:18px;margin:0;accent-color:var(--ok);"
"-webkit-appearance:auto;appearance:auto;background:transparent!important;flex-shrink:0}"
"input:-webkit-autofill,input:-webkit-autofill:hover,input:-webkit-autofill:focus{"
"-webkit-text-fill-color:#e8eaed;"
"-webkit-box-shadow:0 0 0 1000px #12151c inset!important;"
"box-shadow:0 0 0 1000px #12151c inset!important;"
"transition:background-color 99999s ease-in-out 0s}"
"select option{background:#12151c;color:#e8eaed}"
"button,.btn{display:block;width:100%;margin-top:14px;padding:11px 16px;border:0;border-radius:8px;"
"background:var(--ok);color:#0a1a12;font-weight:600;cursor:pointer;font-size:.95rem}"
"button:disabled{opacity:.6;cursor:wait}"
"button.accent{background:var(--accent);color:#fff}"
".hint{color:var(--muted);font-size:.85rem;margin:8px 0 0}"
".row{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-top:10px;color:var(--text)}"
".maprow{display:flex;align-items:center;gap:10px;margin-top:10px}"
".maprow .xname{min-width:78px;font-weight:600;color:var(--text)}"
".maprow .arrow{color:var(--muted)}"
".maprow select{flex:1;margin-top:0}"
".badge{display:inline-block;padding:3px 10px;border-radius:999px;background:#243044;font-size:.8rem;color:var(--text)}"
".grid2{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
".grid3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px}"
".switch{position:relative;width:48px;height:26px;flex-shrink:0}"
".switch input{opacity:0;width:0;height:0}"
".slider{position:absolute;cursor:pointer;inset:0;background:#3a4150;border-radius:26px;transition:.2s}"
".slider:before{content:'';position:absolute;height:20px;width:20px;left:3px;bottom:3px;background:#fff;border-radius:50%;transition:.2s}"
"input:checked+.slider{background:var(--ok)}input:checked+.slider:before{transform:translateX(22px)}"
".chk{display:flex;align-items:center;gap:8px;margin-top:10px;font-size:.9rem;color:var(--text)}"
".chk input{width:auto;accent-color:var(--ok)}"
"#pingMsg,#rumbleMsg{margin-top:8px;font-size:.9rem;min-height:1.2em}"
".okmsg{color:var(--ok)}.failmsg{color:var(--danger)}"
"a{color:var(--accent)}"
"</style></head><body>"
"<h2>PS5 Remote Play</h2>"
"<p class='hint'>ESP32-S3 &middot; Xbox controller bridge</p>"

/* 1) Remote Play */
"<div class='card'><h3>Remote Play</h3>"
"<button type='button' onclick='doConnect()'>Connect to PS5</button>"
"<p id='connMsg' class='hint'>Uses stored registration. Hardware button GPIO14 also works while portal is up.</p></div>"

/* 2) Wi-Fi */
"<div class='card'><h3>Wi-Fi</h3>"
"<label>SSID</label><input id='ssid' type='text' value='%s' autocomplete='off'>"
"<label>Password</label><input id='pass' type='text' value='%s' autocomplete='off'>"
"<button type='button' onclick='saveWifi()'>Save Wi-Fi</button>"
"<p id='wifiMsg' class='hint'></p></div>"

/* 3) PS5 Console IP */
"<div class='card'><h3>PS5 Console IP</h3>"
"<p class='hint'>Enter the local IP of the console. Ping checks reachability before saving.</p>"
"<label>IP address</label>"
"<input type='text' id='ps5ip' value='%s' placeholder='192.168.1.2'>"
"<button type='button' class='ok' onclick='pingAndSave()'>Ping &amp; Save IP</button>"
"<p id='pingMsg' class='hint'></p></div>"

/* 4) Registration */
"<div class='card'><h3>PS5 registration</h3>"
"<p class='hint'>PIN from Settings &gt; System &gt; Remote Play &gt; Link Device. Uses the IP saved above.</p>"
"<label>Registration PIN</label><input id='regist' type='text' value='%s' inputmode='numeric' autocomplete='off' placeholder='12345678'>"
"<label>PSN Account ID (base64)</label><input id='psnid' type='text' value='%s' autocomplete='off'>"
"<button type='button' onclick='doRegister()'>Save &amp; Register</button>"
"<p id='regMsg' class='hint'></p></div>"

/* 5) Controller */
"<div class='card'><h3>Controller</h3>"
"<div class='row'><span>Device</span><span class='badge'>%s</span></div>"
"<div class='row'><span>Battery</span><span class='badge'>%s</span></div>"
"<div class='row'><span>Rumble</span>"
"<label class='switch'><input type='checkbox' id='rumbleTog' %s><span class='slider'></span></label></div>"
"<button type='button' onclick='saveRumble()'>Save rumble setting</button>"
"<p class='hint' id='rumbleMsg'></p></div>"

/* 6) Stick deadzone (drift) */
"<div class='card'><h3>Stick deadzone (drift)</h3>"
"<form id='inputForm' onsubmit='return false'>"
"<label>Global deadzone %%</label>"
"<input type='number' name='dz_g' min='0' max='40' value='%d'>"
"<div class='grid2'>"
"<div><label>LX %%</label><input type='number' name='dz_lx' min='0' max='40' value='%d'></div>"
"<div><label>LY %%</label><input type='number' name='dz_ly' min='0' max='40' value='%d'></div>"
"<div><label>RX %%</label><input type='number' name='dz_rx' min='0' max='40' value='%d'></div>"
"<div><label>RY %%</label><input type='number' name='dz_ry' min='0' max='40' value='%d'></div>"
"</div>"
"<div class='chk'><input type='checkbox' name='inv_lx' value='1' %s> Invert LX</div>"
"<div class='chk'><input type='checkbox' name='inv_ly' value='1' %s> Invert LY</div>"
"<div class='chk'><input type='checkbox' name='inv_rx' value='1' %s> Invert RX</div>"
"<div class='chk'><input type='checkbox' name='inv_ry' value='1' %s> Invert RY</div>"

"<h3 style='margin-top:18px;color:var(--accent)'>Left Trigger</h3>"
"<div class='grid3'>"
"<div><label>Deadzone %%</label><input type='number' name='lt_dz' min='0' max='40' value='%d'></div>"
"<div><label>Idle raw</label><input type='number' name='lt_idle' min='0' max='1023' value='%d'></div>"
"<div><label>Max raw</label><input type='number' name='lt_max' min='1' max='1023' value='%d'></div>"
"</div>"
"<h3 style='margin-top:14px;color:var(--accent)'>Right Trigger</h3>"
"<div class='grid3'>"
"<div><label>Deadzone %%</label><input type='number' name='rt_dz' min='0' max='40' value='%d'></div>"
"<div><label>Idle raw</label><input type='number' name='rt_idle' min='0' max='1023' value='%d'></div>"
"<div><label>Max raw</label><input type='number' name='rt_max' min='1' max='1023' value='%d'></div>"
"</div>"
"<p class='hint'>Bluepad32 trigger raw is typically 0..1023. Idle≈14, Max≈1023.</p>"

"<h3 style='margin-top:18px;color:var(--accent)'>Button remap</h3>"
"<p class='hint'>Xbox button &rarr; PlayStation action</p>"
"%s"
"<button type='button' onclick='saveInput()'>Save mapping &amp; deadzone</button>"
"<p id='inputMsg' class='hint'>Default: A→Cross, B→Circle, X→Square, Y→Triangle.</p></form></div>"

/* 7) Hardware & defaults */
"<div class='card'><h3>Hardware &amp; defaults</h3>"
"<p class='hint'>GPIO changes need a reboot. Physical factory-reset button: GPIO13 (momentary, active-low).</p>"
"<label>RGB LED GPIO</label>"
"<input type='number' id='gpioRgb' min='1' max='48' value='%d'>"
"<label>Remote button GPIO</label>"
"<input type='number' id='gpioBtn' min='0' max='48' value='%d'>"
"<label>AP password (min 8 chars)</label>"
"<input type='text' id='apPass' value='%s' autocomplete='off'>"
"<button type='button' onclick='saveHw()'>Save hardware settings</button>"
"<p id='hwMsg' class='hint'></p>"
"<button type='button' onclick='resetDefaults()' style='margin-top:18px;background:var(--warn)'>Reset drift / mapping / AP to defaults</button>"
"<p id='defMsg' class='hint'></p></div>"

"<script>"
"function setMsg(id,text,ok){var m=document.getElementById(id);if(!m)return;m.textContent=text;m.className='hint '+(ok===true?'okmsg':ok===false?'failmsg':'');}"
"function saveRumble(){"
"  var on=document.getElementById('rumbleTog').checked;"
"  fetch('/api/rumble?en='+(on?1:0)+'&save=1').then(function(r){return r.text()}).then(function(t){"
"    setMsg('rumbleMsg',t,true);"
"  }).catch(function(){setMsg('rumbleMsg','Save failed',false);});"
"}"
"function pingAndSave(){"
"  var ip=document.getElementById('ps5ip').value.trim();"
"  setMsg('pingMsg','Pinging…',null);"
"  fetch('/api/ping_host?ip='+encodeURIComponent(ip)).then(function(r){return r.json()}).then(function(j){"
"    if(j.ok)setMsg('pingMsg','Reachable - IP saved.',true);"
"    else setMsg('pingMsg','Failed: '+(j.err||'unreachable'),false);"
"  }).catch(function(){setMsg('pingMsg','Request failed',false);});"
"}"
"function doRegister(){"
"  var pin=document.getElementById('regist').value.trim();"
"  var psn=document.getElementById('psnid').value.trim();"
"  setMsg('regMsg','Registering…',null);"
"  fetch('/api/register',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
"    body:'regist='+encodeURIComponent(pin)+'&psnid='+encodeURIComponent(psn)})"
"  .then(function(r){return r.json()}).then(function(j){"
"    if(j.ok)setMsg('regMsg',j.msg||'Registered and saved.',true);"
"    else setMsg('regMsg',j.err||'Registration failed',false);"
"  }).catch(function(){setMsg('regMsg','Request failed',false);});"
"}"
"function doConnect(){"
"  setMsg('connMsg','Connecting…',null);"
"  fetch('/api/connect',{method:'POST'}).then(function(r){return r.json()}).then(function(j){"
"    if(j.ok)setMsg('connMsg',j.msg||'Connecting - portal will stop. Watch serial log.',true);"
"    else setMsg('connMsg',j.err||'Connect failed',false);"
"  }).catch(function(){setMsg('connMsg','Request failed',false);});"
"}"
"function saveInput(){"
"  var f=document.getElementById('inputForm');"
"  var fd=new FormData(f);"
"  var body=new URLSearchParams(fd).toString();"
"  setMsg('inputMsg','Saving…',null);"
"  fetch('/api/save_input',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})"
"  .then(function(r){return r.json()}).then(function(j){"
"    if(j.ok)setMsg('inputMsg',j.msg||'Mapping & deadzone saved.',true);"
"    else setMsg('inputMsg',j.err||'Save failed',false);"
"  }).catch(function(){setMsg('inputMsg','Request failed',false);});"
"}"
"function saveWifi(){"
"  var ssid=document.getElementById('ssid').value;"
"  var pass=document.getElementById('pass').value;"
"  setMsg('wifiMsg','Saving…',null);"
"  fetch('/api/save_wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
"    body:'ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass)})"
"  .then(function(r){return r.json()}).then(function(j){"
"    if(j.ok)setMsg('wifiMsg',j.msg||'Wi-Fi saved. Reboot to apply.',true);"
"    else setMsg('wifiMsg',j.err||'Save failed',false);"
"  }).catch(function(){setMsg('wifiMsg','Request failed',false);});"
"}"
"function saveHw(){"
"  var rgb=document.getElementById('gpioRgb').value;"
"  var btn=document.getElementById('gpioBtn').value;"
"  var ap=document.getElementById('apPass').value;"
"  setMsg('hwMsg','Saving…',null);"
"  fetch('/api/save_hw',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
"    body:'gpio_rgb='+encodeURIComponent(rgb)+'&gpio_btn='+encodeURIComponent(btn)+'&ap_pass='+encodeURIComponent(ap)})"
"  .then(function(r){return r.json()}).then(function(j){"
"    if(j.ok)setMsg('hwMsg',j.msg||'Saved. Reboot for GPIO.',true);"
"    else setMsg('hwMsg',j.err||'Save failed',false);"
"  }).catch(function(e){setMsg('hwMsg','Request failed',false);});"
"}"
"function resetDefaults(){"
"  if(!confirm('Reset drift, mapping and AP password to defaults? Board will reboot.'))return;"
"  setMsg('defMsg','Resetting…',null);"
"  fetch('/api/reset_defaults',{method:'POST'}).then(function(r){"
"    if(!r.ok) throw new Error('http '+r.status);"
"    return r.json();"
"  }).then(function(j){"
"    if(j.ok)setMsg('defMsg',j.msg||'Defaults restored. Rebooting…',true);"
"    else setMsg('defMsg',j.err||'Failed',false);"
"  }).catch(function(){"
"    fetch('/api/reset_defaults').then(function(r){return r.json()}).then(function(j){"
"      if(j.ok)setMsg('defMsg',j.msg||'Defaults restored. Rebooting…',true);"
"      else setMsg('defMsg',j.err||'Failed',false);"
"    }).catch(function(){setMsg('defMsg','Request failed',false);});"
"  });"
"}"
"</script>"
"</body></html>";

static void url_decode(char *dst, const char *src, size_t dst_len)
{
    size_t di = 0;
    for (size_t si = 0; src[si] && di + 1 < dst_len; si++) {
        if (src[si] == '+') {
            dst[di++] = ' ';
        } else if (src[si] == '%' && src[si + 1] && src[si + 2]) {
            char hex[3] = { src[si + 1], src[si + 2], 0 };
            dst[di++] = (char)strtol(hex, NULL, 16);
            si += 2;
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
}

static bool form_get(const char *body, const char *key, char *out, size_t out_len)
{
    size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            p += klen + 1;
            const char *end = strchr(p, '&');
            size_t n = end ? (size_t)(end - p) : strlen(p);
            char tmp[128];
            if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
            memcpy(tmp, p, n);
            tmp[n] = '\0';
            url_decode(out, tmp, out_len);
            return true;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    out[0] = '\0';
    return false;
}


static void build_map_rows(char *buf, size_t cap, const input_profile_t *prof)
{
    size_t off = 0;
    buf[0] = '\0';
    for (int i = 0; i < XBOX_BTN_COUNT; ++i) {
        if (cap <= off + 64) break;
        int n = snprintf(buf + off, cap - off,
            "<div class='maprow'>"
            "<span class='xname'>%s</span>"
            "<span class='arrow'>&rarr;</span>"
            "<select name='map_%d'>",
            xbox_btn_name((xbox_btn_t)i), i);
        if (n < 0 || (size_t)n >= cap - off) break;
        off += (size_t)n;
        for (int j = 0; j < PS_BTN_COUNT; ++j) {
            if (cap <= off + 48) break;
            n = snprintf(buf + off, cap - off,
                "<option value='%d'%s>%s</option>",
                j, (prof->map[i] == (uint8_t)j) ? " selected" : "", ps_btn_name((ps_btn_t)j));
            if (n < 0 || (size_t)n >= cap - off) break;
            off += (size_t)n;
        }
        if (cap <= off + 16) break;
        n = snprintf(buf + off, cap - off, "</select></div>");
        if (n < 0 || (size_t)n >= cap - off) break;
        off += (size_t)n;
    }
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    app_config_t cfg;
    config_store_load(&cfg);

    char *buf = malloc(32768);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    const char *cname = bluepad32_bridge_get_controller_name();
    int batt = bluepad32_bridge_get_battery();
    char batt_s[16];
    if (batt < 0) snprintf(batt_s, sizeof(batt_s), "n/a");
    else snprintf(batt_s, sizeof(batt_s), "%d%%", batt);
    const char *rumble_chk = cfg.rumble_enabled ? "checked" : "";
    bluepad32_bridge_set_rumble_enabled(cfg.rumble_enabled);

    input_profile_t prof;
    input_config_load(&prof);
    char *maprows = malloc(24576);
    if (!maprows) { free(buf); httpd_resp_send_500(req); return ESP_FAIL; }
    build_map_rows(maprows, 24576, &prof);

    int n = snprintf(buf, 32768, FORM_HTML,
                     cfg.wifi_ssid, cfg.wifi_pass,
                     cfg.ps5_host,
                     cfg.regist_key, cfg.psn_account_id,
                     cname, batt_s, rumble_chk,
                     (int)prof.dz_global, (int)prof.dz_lx, (int)prof.dz_ly, (int)prof.dz_rx, (int)prof.dz_ry,
                     prof.inv_lx ? "checked" : "", prof.inv_ly ? "checked" : "",
                     prof.inv_rx ? "checked" : "", prof.inv_ry ? "checked" : "",
                     (int)prof.lt_dz, (int)prof.lt_idle, (int)prof.lt_max,
                     (int)prof.rt_dz, (int)prof.rt_idle, (int)prof.rt_max,
                     maprows,
                     (int)(cfg.gpio_rgb ? cfg.gpio_rgb : 48),
                     (int)(cfg.gpio_btn ? cfg.gpio_btn : 14),
                     (cfg.ap_pass[0] ? cfg.ap_pass : "12345678"));
    free(maprows);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, buf, n > 0 ? n : 0);
    free(buf);
    return ESP_OK;
}

static esp_err_t api_rumble_handler(httpd_req_t *req)
{
    char query[64] = {0};
    bool on = true;
    bool save = false;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char en[4] = {0}, sv[4] = {0};
        if (httpd_query_key_value(query, "en", en, sizeof(en)) == ESP_OK)
            on = (en[0] == '1');
        if (httpd_query_key_value(query, "save", sv, sizeof(sv)) == ESP_OK)
            save = (sv[0] == '1');
    }
    bluepad32_bridge_set_rumble_enabled(on);
    if (save) {
        config_store_set_rumble(on);
        ESP_LOGI(TAG, "Portal: Rumble %s (saved to NVS)", on ? "enabled" : "disabled");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, on ? "Rumble ON - saved" : "Rumble OFF - saved");
    } else {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "ok");
    }
    return ESP_OK;
}

/* TCP connect probe to PS5 control port (9295) with short timeout */
static bool ping_ps5_host(const char *host, char *err, size_t err_len)
{
    if (!host || !host[0]) {
        snprintf(err, err_len, "empty IP");
        return false;
    }
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int gai = getaddrinfo(host, "9295", &hints, &res);
    if (gai != 0 || !res) {
        snprintf(err, err_len, "DNS/resolve failed");
        return false;
    }
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        snprintf(err, err_len, "socket error");
        return false;
    }
    /* non-blocking connect + select timeout */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int c = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    bool ok = false;
    if (c == 0) {
        ok = true;
    } else if (errno == EINPROGRESS) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
        int s = select(fd + 1, NULL, &wfds, NULL, &tv);
        if (s > 0) {
            int soerr = 0;
            socklen_t sl = sizeof(soerr);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
            ok = (soerr == 0);
            if (!ok) snprintf(err, err_len, "connect refused/error %d", soerr);
        } else {
            snprintf(err, err_len, "timeout");
        }
    } else {
        snprintf(err, err_len, "connect errno %d", errno);
    }
    close(fd);
    if (ok) err[0] = '\0';
    return ok;
}

static esp_err_t api_ping_host_handler(httpd_req_t *req)
{
    char query[96] = {0};
    char ip[64] = {0};
    char err[64] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
        httpd_query_key_value(query, "ip", ip, sizeof(ip));

    httpd_resp_set_type(req, "application/json");
    if (!ip[0]) {
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"missing ip\"}");
        return ESP_OK;
    }
    bool ok = ping_ps5_host(ip, err, sizeof(err));
    if (ok) {
        config_store_set_host(ip);
        ESP_LOGI(TAG, "PS5 host %s reachable - saved", ip);
        httpd_resp_sendstr(req, "{\"ok\":true}");
    } else {
        char json[128];
        snprintf(json, sizeof(json), "{\"ok\":false,\"err\":\"%s\"}", err[0] ? err : "unreachable");
        httpd_resp_sendstr(req, json);
    }
    return ESP_OK;
}

static esp_err_t save_input_post_handler(httpd_req_t *req)
{
    char body[2048] = {0};
    int r = httpd_req_recv(req, body, sizeof(body) - 1);
    if (r <= 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"empty body\"}");
        return ESP_OK;
    }
    input_profile_t prof;
    input_config_load(&prof);
    char tmp[16];
    if (form_get(body, "dz_g", tmp, sizeof(tmp))) prof.dz_global = (uint8_t)atoi(tmp);
    if (form_get(body, "dz_lx", tmp, sizeof(tmp))) prof.dz_lx = (uint8_t)atoi(tmp);
    if (form_get(body, "dz_ly", tmp, sizeof(tmp))) prof.dz_ly = (uint8_t)atoi(tmp);
    if (form_get(body, "dz_rx", tmp, sizeof(tmp))) prof.dz_rx = (uint8_t)atoi(tmp);
    if (form_get(body, "dz_ry", tmp, sizeof(tmp))) prof.dz_ry = (uint8_t)atoi(tmp);
    if (form_get(body, "lt_dz", tmp, sizeof(tmp))) prof.lt_dz = (uint8_t)atoi(tmp);
    if (form_get(body, "rt_dz", tmp, sizeof(tmp))) prof.rt_dz = (uint8_t)atoi(tmp);
    if (form_get(body, "lt_idle", tmp, sizeof(tmp))) prof.lt_idle = (uint16_t)atoi(tmp);
    if (form_get(body, "rt_idle", tmp, sizeof(tmp))) prof.rt_idle = (uint16_t)atoi(tmp);
    if (form_get(body, "lt_max", tmp, sizeof(tmp))) prof.lt_max = (uint16_t)atoi(tmp);
    if (form_get(body, "rt_max", tmp, sizeof(tmp))) prof.rt_max = (uint16_t)atoi(tmp);
    prof.inv_lx = form_get(body, "inv_lx", tmp, sizeof(tmp));
    prof.inv_ly = form_get(body, "inv_ly", tmp, sizeof(tmp));
    prof.inv_rx = form_get(body, "inv_rx", tmp, sizeof(tmp));
    prof.inv_ry = form_get(body, "inv_ry", tmp, sizeof(tmp));
    for (int i = 0; i < XBOX_BTN_COUNT; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "map_%d", i);
        if (form_get(body, key, tmp, sizeof(tmp))) {
            int v = atoi(tmp);
            if (v >= 0 && v < PS_BTN_COUNT) prof.map[i] = (uint8_t)v;
        }
    }
    input_config_save(&prof);
    bluepad32_bridge_reload_input_tuning();
    ESP_LOGI(TAG, "Input profile saved dz_g=%u map A→%s X→%s",
             prof.dz_global, ps_btn_name((ps_btn_t)prof.map[XBOX_BTN_A]),
             ps_btn_name((ps_btn_t)prof.map[XBOX_BTN_X]));

    httpd_resp_set_type(req, "application/json");
    ESP_LOGI(TAG, "Portal: input profile saved (mapping & deadzone)");
    httpd_resp_sendstr(req, "{\"ok\":true,\"msg\":\"Mapping & deadzone saved.\"}");
    return ESP_OK;
}

/* ---- existing registration / wifi / connect handlers (kept, host from NVS) ---- */

static void connect_task(void *arg)
{
    (void)arg;
    app_config_t cfg;
    config_store_load(&cfg);
    if (!cfg.registered || !cfg.ps5_host[0]) {
        ESP_LOGE(TAG, "Connect: not registered or no host");
        s_connect_task_running = false;
        vTaskDelete(NULL);
        return;
    }
    web_portal_stop();
    esp_err_t e = remoteplay_stub_connect_registered(cfg.ps5_host, cfg.rp_regist_key, cfg.rp_key);
    ESP_LOGI(TAG, "Connect result: %s", esp_err_to_name(e));
    if (e != ESP_OK) {
        /* Connect failed – bring portal back so user can retry without reset. */
        ws2812_set_remote_connected(false);
        web_portal_start();
    }
    s_connect_task_running = false;
    vTaskDelete(NULL);
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char body[768] = {0};
    int r = httpd_req_recv(req, body, sizeof(body) - 1);
    if (r <= 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"empty body\"}");
        return ESP_OK;
    }
    app_config_t cfg;
    config_store_load(&cfg);
    char regist[CFG_MAX_STR] = {0}, psnid[CFG_MAX_STR] = {0};
    form_get(body, "regist", regist, sizeof(regist));
    form_get(body, "psnid", psnid, sizeof(psnid));

    httpd_resp_set_type(req, "application/json");
    if (!cfg.ps5_host[0] || !regist[0] || !psnid[0]) {
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"Missing PS5 IP (save IP first), PIN, or PSN Account ID\"}");
        return ESP_OK;
    }
    strncpy(cfg.regist_key, regist, sizeof(cfg.regist_key) - 1);
    strncpy(cfg.psn_account_id, psnid, sizeof(cfg.psn_account_id) - 1);

    ChiakiRegisteredHost rh = {0};
    esp_err_t e = remoteplay_stub_register(cfg.ps5_host, cfg.psn_account_id, cfg.regist_key, &rh);
    if (e != ESP_OK) {
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"Registration failed - check PIN and serial log\"}");
        return ESP_OK;
    }
    memcpy(cfg.rp_regist_key, rh.rp_regist_key, sizeof(cfg.rp_regist_key));
    memcpy(cfg.rp_key, rh.rp_key, sizeof(cfg.rp_key));
    cfg.rp_key_type = rh.rp_key_type;
    strncpy(cfg.server_nickname, rh.server_nickname, sizeof(cfg.server_nickname) - 1);
    cfg.registered = true;
    cfg.provisioned = true;
    config_store_save(&cfg);
    ws2812_set_wifi_status(true, true);

    ESP_LOGI(TAG, "Portal: Register OK nick=%s host=%s", cfg.server_nickname, cfg.ps5_host);
    char response[256];
    snprintf(response, sizeof(response),
             "{\"ok\":true,\"msg\":\"Registered: %s (%s). Now press Connect.\"}",
             cfg.server_nickname[0] ? cfg.server_nickname : cfg.ps5_host, cfg.ps5_host);
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

static esp_err_t save_wifi_post_handler(httpd_req_t *req)
{
    char body[384] = {0};
    int r = httpd_req_recv(req, body, sizeof(body) - 1);
    httpd_resp_set_type(req, "application/json");
    if (r <= 0) {
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"empty body\"}");
        return ESP_OK;
    }
    app_config_t cfg;
    config_store_load(&cfg);
    form_get(body, "ssid", cfg.wifi_ssid, sizeof(cfg.wifi_ssid));
    form_get(body, "pass", cfg.wifi_pass, sizeof(cfg.wifi_pass));
    config_store_save(&cfg);
    ESP_LOGI(TAG, "Portal: Wi-Fi saved SSID=%s", cfg.wifi_ssid);
    httpd_resp_sendstr(req, "{\"ok\":true,\"msg\":\"Wi-Fi saved. Reboot ESP32 to apply.\"}");
    return ESP_OK;
}

esp_err_t web_portal_connect_from_button(void)
{
    if (!web_portal_is_running()) {
        ESP_LOGI(TAG, "Hardware Connect button ignored: portal inactive or PS5 not registered");
        return ESP_ERR_INVALID_STATE;
    }
    app_config_t cfg;
    config_store_load(&cfg);
    if (!cfg.registered) {
        ESP_LOGI(TAG, "Hardware Connect button ignored: portal inactive or PS5 not registered");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_connect_task_running)
        return ESP_ERR_INVALID_STATE;
    s_connect_task_running = true;
    ESP_LOGI(TAG, "Hardware Connect button: starting Connect to PS5");
    xTaskCreate(connect_task, "rp_connect", 8192, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t connect_post_handler(httpd_req_t *req)
{
    app_config_t cfg;
    config_store_load(&cfg);
    httpd_resp_set_type(req, "application/json");
    if (!cfg.registered) {
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"PS5 is not registered. Register first.\"}");
        return ESP_OK;
    }
    if (!s_connect_task_running) {
        s_connect_task_running = true;
        xTaskCreate(connect_task, "rp_connect", 8192, NULL, 5, NULL);
    }
    ESP_LOGI(TAG, "Portal: Connect requested");
    httpd_resp_sendstr(req, "{\"ok\":true,\"msg\":\"Connecting - portal will stop. Watch serial log.\"}");
    return ESP_OK;
}


static esp_err_t save_hw_post_handler(httpd_req_t *req)
{
    char body[256] = {0};
    int r = httpd_req_recv(req, body, sizeof(body) - 1);
    httpd_resp_set_type(req, "application/json");
    if (r <= 0) {
        ESP_LOGW(TAG, "save_hw: empty body");
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"empty body\"}");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "save_hw body: %s", body);
    app_config_t cfg;
    config_store_load(&cfg);
    char tmp[64];
    if (form_get(body, "gpio_rgb", tmp, sizeof(tmp))) {
        int v = atoi(tmp);
        if (v < 1 || v > 48) {
            ESP_LOGW(TAG, "save_hw: bad gpio_rgb=%s", tmp);
            httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"RGB GPIO must be 1..48\"}");
            return ESP_OK;
        }
        cfg.gpio_rgb = (uint8_t)v;
    }
    if (form_get(body, "gpio_btn", tmp, sizeof(tmp))) {
        int v = atoi(tmp);
        if (v < 0 || v > 48) {
            ESP_LOGW(TAG, "save_hw: bad gpio_btn=%s", tmp);
            httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"Button GPIO must be 0..48\"}");
            return ESP_OK;
        }
        cfg.gpio_btn = (uint8_t)v;
    }
    if (form_get(body, "ap_pass", tmp, sizeof(tmp))) {
        if (strlen(tmp) < 8) {
            ESP_LOGW(TAG, "save_hw: AP pass too short");
            httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"AP password min 8 characters\"}");
            return ESP_OK;
        }
        strncpy(cfg.ap_pass, tmp, sizeof(cfg.ap_pass) - 1);
        cfg.ap_pass[sizeof(cfg.ap_pass) - 1] = ' ';
    }
    if (config_store_save(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "save_hw: NVS save failed");
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"NVS save failed\"}");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Hardware saved: RGB_GPIO=%u BTN_GPIO=%u AP_pass_len=%u",
             (unsigned)cfg.gpio_rgb, (unsigned)cfg.gpio_btn, (unsigned)strlen(cfg.ap_pass));
    httpd_resp_sendstr(req, "{\"ok\":true,\"msg\":\"Saved. Reboot to apply GPIO. AP password applies next AP start.\"}");
    return ESP_OK;
}

static void delayed_reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(800));
    ESP_LOGW(TAG, "Rebooting after defaults reset…");
    esp_restart();
}

static esp_err_t reset_defaults_post_handler(httpd_req_t *req)
{
    /* Drain body if any (browser may send Content-Length) */
    char drain[64];
    while (httpd_req_recv(req, drain, sizeof(drain)) > 0) { /* discard */ }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    ESP_LOGW(TAG, "Portal: reset defaults requested");
    if (config_store_reset_defaults() != ESP_OK) {
        ESP_LOGE(TAG, "reset_defaults failed");
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"Reset failed\"}");
        return ESP_OK;
    }
    bluepad32_bridge_reload_input_tuning();
    ESP_LOGW(TAG, "Defaults restored (drift, mapping, AP pass, GPIO) — rebooting");
    httpd_resp_sendstr(req, "{\"ok\":true,\"msg\":\"Defaults restored. Rebooting…\"}");
    xTaskCreate(delayed_reboot_task, "reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t reset_defaults_get_handler(httpd_req_t *req)
{
    return reset_defaults_post_handler(req);
}

esp_err_t web_portal_start(void)
{
    if (s_server != NULL)
        return ESP_OK;

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.max_open_sockets = 3;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 8;
    config.send_wait_timeout = 8;
    config.stack_size = 6144;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
    httpd_uri_t save_uri = { .uri = "/api/register", .method = HTTP_POST, .handler = save_post_handler };
    httpd_uri_t save_wifi_uri = { .uri = "/api/save_wifi", .method = HTTP_POST, .handler = save_wifi_post_handler };
    httpd_uri_t save_input_uri = { .uri = "/api/save_input", .method = HTTP_POST, .handler = save_input_post_handler };
    httpd_uri_t connect_uri = { .uri = "/api/connect", .method = HTTP_POST, .handler = connect_post_handler };
    httpd_uri_t rumble_uri = { .uri = "/api/rumble", .method = HTTP_GET, .handler = api_rumble_handler };
    httpd_uri_t ping_uri = { .uri = "/api/ping_host", .method = HTTP_GET, .handler = api_ping_host_handler };
    httpd_uri_t hw_uri = { .uri = "/api/save_hw", .method = HTTP_POST, .handler = save_hw_post_handler };
    httpd_uri_t reset_uri = { .uri = "/api/reset_defaults", .method = HTTP_POST, .handler = reset_defaults_post_handler };
    httpd_uri_t reset_get_uri = { .uri = "/api/reset_defaults", .method = HTTP_GET, .handler = reset_defaults_get_handler };

    const httpd_uri_t *uris[] = {
        &root_uri, &save_uri, &save_wifi_uri, &save_input_uri, &connect_uri,
        &rumble_uri, &ping_uri, &hw_uri, &reset_uri, &reset_get_uri
    };
    for (size_t i = 0; i < sizeof(uris)/sizeof(uris[0]); ++i) {
        esp_err_t re = httpd_register_uri_handler(server, uris[i]);
        if (re != ESP_OK)
            ESP_LOGE(TAG, "register URI %s failed: %s", uris[i]->uri, esp_err_to_name(re));
    }

    s_server = server;
    ESP_LOGI(TAG, "web portal running on port 80");
    return ESP_OK;
}

bool web_portal_is_running(void)
{
    return s_server != NULL;
}

esp_err_t web_portal_stop(void)
{
    if (!s_server)
        return ESP_OK;
    httpd_handle_t s = s_server;
    s_server = NULL;
    httpd_stop(s);
    ESP_LOGI(TAG, "web portal stopped; HTTP sockets released for Remote Play");
    return ESP_OK;
}
