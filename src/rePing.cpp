#include "rePing.h"
#include "reWiFi.h"
#include "rLog.h"
#include "reLed.h"
#include "reLedSys.h"
#include <stdio.h>
#include <string.h>
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "esp_ping.h"
#include "ping/ping_sock.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h" 
#include "freertos/event_groups.h"
#include "project_config.h"
#if CONFIG_TELEGRAM_ENABLE
#include "reTgSend.h"
#endif // CONFIG_TELEGRAM_ENABLE

static const char* tagPING = "PING";

typedef struct {
  ping_result_t result;
  EventGroupHandle_t lock = nullptr;
} ping_lock_t;
typedef ping_lock_t *ping_handle_t;

#if CONFIG_PING_SESSION_SHOW_INTERMEDIATE

static void pingOnSuccess(esp_ping_handle_t hdl, void *args)
{
  uint8_t ttl;
  uint16_t seqno;
  uint32_t elapsed_time, recv_len;
  ip_addr_t target_addr;
  esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
  esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
  esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
  esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
  esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
  rlog_d(tagPING, "Received of %d bytes from [%s] : icmp_seq = %d, ttl = %d, time = %d ms",
    recv_len, ipaddr_ntoa((ip_addr_t*)&target_addr), seqno, ttl, elapsed_time);
}

static void pingOnTimeout(esp_ping_handle_t hdl, void *args)
{
  uint16_t seqno;
  ip_addr_t target_addr;
  esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
  esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
  rlog_w(tagPING, "Packet loss for [%s]: icmp_seq = %d", 
    ipaddr_ntoa((ip_addr_t*)&target_addr), seqno);
}

#endif // CONFIG_PING_SESSION_SHOW_INTERMEDIATE

static void pingOnEnd(esp_ping_handle_t hdl, void *args)
{
  ip_addr_t target_addr;
  uint32_t transmitted = 0;
  uint32_t received = 0;
  ping_handle_t data = (ping_handle_t)args;
  // We get the results
  esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
  esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(uint32_t));
  esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(uint32_t));
  esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &data->result.duration, sizeof(uint32_t));
  // Calculating loss and average response time
  data->result.loss = 0;
  if (transmitted > 0) {
    data->result.duration = data->result.duration / transmitted;
    data->result.loss = (float)((1 - ((float)received) / transmitted) * 100);
  };
  // Display log
  rlog_i(tagPING, "Ping statistics for [%s]: %d packets transmitted, %d received, %.1f%% packet loss, average time %d ms",
    ipaddr_ntoa((ip_addr_t*)&target_addr), transmitted, received, data->result.loss, data->result.duration);
  // Set the flag that the ping is complete
  if (data->lock) xEventGroupSetBits(data->lock, BIT0);
}

ping_result_t pingHost(const char* hostname, const uint32_t count, const uint32_t interval, const uint32_t timeout, const uint32_t datasize)
{
  // Buffer for results
  ping_lock_t result;
  memset(&result.result, 0, sizeof(ping_result_t));
  result.lock = xEventGroupCreate();
  if (!result.lock) {
    rlog_e(tagPING, "Failed to create lock object!");
    return result.result;
  };

  // Convert hostname to IP address
  ip_addr_t target_addr;
  struct addrinfo hint;
  struct addrinfo *res = NULL;
  memset(&target_addr, 0, sizeof(target_addr));
  memset(&hint, 0, sizeof(hint));
  if (getaddrinfo(hostname, NULL, &hint, &res) != 0) {
    rlog_w(tagPING, "Unknown host [%s]!", hostname);
    return result.result;
  };
  struct in_addr addr4 = ((struct sockaddr_in *) (res->ai_addr))->sin_addr;
  inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &addr4);
  freeaddrinfo(res);

  // Configuring ping
  esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
  ping_config.task_stack_size = CONFIG_PING_TASK_STACK_SIZE;
  ping_config.task_prio = CONFIG_PING_TASK_PRIORITY;
  ping_config.target_addr = target_addr;
  ping_config.interval_ms = interval;
  ping_config.timeout_ms = timeout;
  ping_config.data_size = datasize;
  ping_config.count = count;

  // Set callback functions
  esp_ping_callbacks_t cbs;
  #if CONFIG_PING_SESSION_SHOW_INTERMEDIATE
  cbs.on_ping_success = pingOnSuccess;
  cbs.on_ping_timeout = pingOnTimeout;
  #endif // CONFIG_PING_SESSION_SHOW_INTERMEDIATE
  cbs.on_ping_end = pingOnEnd;
  cbs.cb_args = (void*)&result;
  xEventGroupClearBits(result.lock, BIT0);

  // Start ping
  esp_err_t err;
  esp_ping_handle_t ping;
  err = esp_ping_new_session(&ping_config, &cbs, &ping);
  if (err != ESP_OK) {
    rlog_e(tagPING, "Failed to create ping session!");
    return result.result;
  };
  err = esp_ping_start(ping);
  if (err != ESP_OK) {
    rlog_e(tagPING, "Failed to start ping!");
    return result.result;
  };
  
  // We are waiting for the results
  rlog_i(tagPING, "Ping started for host [%s]...", hostname);
  xEventGroupWaitBits(result.lock, BIT0, pdFALSE, pdFALSE, portMAX_DELAY);

  // Free resources
  vEventGroupDelete(result.lock);
  esp_ping_delete_session(ping);

  return result.result;
}

bool checkHost(const char* hostname, const bool use_ping, const char* log_tag, const int led_bit,
  #if CONFIG_TELEGRAM_ENABLE
  const char* template_notify_ok, const char* template_notify_failed, 
  #endif // CONFIG_TELEGRAM_ENABLE
  const uint32_t count, const uint32_t interval, const uint32_t timeout, const uint32_t datasize)
{
  // Check WiFi connection
  bool ret = (wifiIsConnected() && wifiWaitConnection());
  if (!ret) {
    rlog_w(log_tag, "No Internet access, waiting...");
    ledSysStateSet(led_bit, false);
    ret = wifiIsConnected() && wifiWaitConnection(portMAX_DELAY);
    if (ret) {
      rlog_d(log_tag, "Internet access restored");
      ledSysStateClear(led_bit, false);
    };
  };
  
  // Check host
  if (ret && use_ping) {
    bool send_notify = false;
    rlog_d(log_tag, "%s availability check...", hostname);
    ret = (pingHost(hostname, count, interval, timeout, datasize).loss < 100);
    while (!ret) {
      if (!send_notify) {
        rlog_w(log_tag, "No access to %s, waiting...", hostname);
        send_notify = true;
        ledSysStateSet(led_bit, false);
        #if CONFIG_TELEGRAM_ENABLE
          if (template_notify_failed) {
            tgSend(true, CONFIG_TELEGRAM_DEVICE, template_notify_failed, hostname);
          };
        #endif // CONFIG_TELEGRAM_ENABLE
      };
      ret = (pingHost(hostname, count, interval, timeout, datasize).loss < 100);
      if (ret) {
        rlog_d(log_tag, "Access to %s restored", hostname);
        ledSysStateClear(led_bit, false);
        #if CONFIG_TELEGRAM_ENABLE
          if (template_notify_ok) {
            tgSend(true, CONFIG_TELEGRAM_DEVICE, template_notify_ok, hostname);
          };
        #endif // CONFIG_TELEGRAM_ENABLE
      } else {
        vTaskDelay(CONFIG_INTERNET_PING_INTERVAL_UNAVAILABLE / portTICK_PERIOD_MS);
      };
    };
  };

  return ret;
}