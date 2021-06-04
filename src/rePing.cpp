#include "rePing.h"
#include "rLog.h"
#include <stdio.h>
#include <string.h>
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "esp_ping.h"
#include "ping/ping_sock.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h" 
#include "freertos/event_groups.h"
#include "project_config.h"

static const char* tagPING = "PING";

typedef struct {
  float max_loss;
  uint32_t max_duration;
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
  ping_handle_t data = (ping_handle_t)args;
  // We get the results
  esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
  esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &data->result.transmitted, sizeof(uint32_t));
  esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &data->result.received, sizeof(uint32_t));
  esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &data->result.duration, sizeof(uint32_t));
  // Calculating loss and average response time
  data->result.loss = 0;
  if (data->result.transmitted > 0) {
    data->result.loss = (float)((1 - ((float)data->result.received) / data->result.transmitted) * 100);
  };
  if (data->result.transmitted > 0) {
    data->result.duration = data->result.duration / data->result.transmitted;
  };
  data->result.available = (data->result.loss < data->max_loss) && (data->result.duration < data->max_duration);
  // Display log
  rlog_i(tagPING, "Ping statistics for [%s]: %d packets transmitted, %d received, %.1f%% packet loss, average time %d ms",
    ipaddr_ntoa((ip_addr_t*)&target_addr), data->result.transmitted, data->result.received, data->result.loss, data->result.duration);
  // Set the flag that the ping is complete
  if (data->lock) xEventGroupSetBits(data->lock, BIT0);
}

ping_result_t pingHost(const char* hostname, 
  const uint32_t count, const uint32_t interval, const uint32_t timeout, const uint32_t datasize,
  const float max_loss, const uint32_t max_duration)
{
  // Buffer for results
  ping_lock_t result;
  memset(&result.result, 0, sizeof(ping_result_t));
  result.max_loss = max_loss;
  result.max_duration = max_duration;
  result.lock = xEventGroupCreate();
  if (!result.lock) {
    rlog_e(tagPING, "Failed to create lock object!");
    return result.result;
  };

  // Convert hostname to IP address
  ip_addr_t target_addr;
  struct addrinfo hint;
  struct addrinfo *res = NULL;
  memset(&hint, 0, sizeof(hint));
  memset(&target_addr, 0, sizeof(target_addr));
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

#if CONFIG_INTERNET_PING_ENABLE

#ifndef MIN
#define MIN(a, b) ((((a) > 0) && ((a) < (b))) ? (a) : (b))
#endif

ping_inet_t pingCheckInternet() 
{
  ping_inet_t ret;
  rlog_i(tagPING, "Checking access to the Internet ...");
  ret.internet.available = false;
  ret.internet.transmitted = 0;
  ret.internet.received = 0;
  ret.internet.duration = 0;
  ret.internet.loss = 0;

  #ifdef CONFIG_INTERNET_PING_HOST_1
    ret.host1 = pingHost(CONFIG_INTERNET_PING_HOST_1,
      CONFIG_INTERNET_PING_SESSION_COUNT, CONFIG_INTERNET_PING_SESSION_INTERVAL, CONFIG_INTERNET_PING_SESSION_TIMEOUT, CONFIG_INTERNET_PING_SESSION_DATASIZE, 
      CONFIG_INTERNET_PING_SESSION_LOSS_MAX, CONFIG_INTERNET_PING_SESSION_TIME_MAX);
    ret.internet.available |= ret.host1.available;
    ret.internet.transmitted += ret.host1.transmitted;
    ret.internet.received += ret.host1.received;
    ret.internet.loss = ret.host1.loss;
    ret.internet.duration = ret.host1.duration;
    
    #ifdef CONFIG_INTERNET_PING_HOST_2
      ret.host2 = pingHost(CONFIG_INTERNET_PING_HOST_2,
        CONFIG_INTERNET_PING_SESSION_COUNT, CONFIG_INTERNET_PING_SESSION_INTERVAL, CONFIG_INTERNET_PING_SESSION_TIMEOUT, CONFIG_INTERNET_PING_SESSION_DATASIZE, 
        CONFIG_INTERNET_PING_SESSION_LOSS_MAX, CONFIG_INTERNET_PING_SESSION_TIME_MAX);
      ret.internet.available |= ret.host2.available;
      ret.internet.transmitted += ret.host2.transmitted;
      ret.internet.received += ret.host2.received;
      ret.internet.loss = (ret.host1.loss + ret.host2.loss) / 2;
      ret.internet.duration = (ret.host1.duration + ret.host2.duration) / 2;
      
      #ifdef CONFIG_INTERNET_PING_HOST_3
        ret.host3 = pingHost(CONFIG_INTERNET_PING_HOST_3,
          CONFIG_INTERNET_PING_SESSION_COUNT, CONFIG_INTERNET_PING_SESSION_INTERVAL, CONFIG_INTERNET_PING_SESSION_TIMEOUT, CONFIG_INTERNET_PING_SESSION_DATASIZE, 
          CONFIG_INTERNET_PING_SESSION_LOSS_MAX, CONFIG_INTERNET_PING_SESSION_TIME_MAX);
        ret.internet.available |= ret.host3.available;
        ret.internet.transmitted += ret.host3.transmitted;
        ret.internet.received += ret.host3.received;
        ret.internet.loss = (ret.host1.loss + ret.host2.loss + ret.host3.loss) / 3;
        ret.internet.duration = (ret.host1.duration + ret.host2.duration + ret.host3.duration) / 3;
      #endif // CONFIG_INTERNET_PING_HOST_3
    #endif // CONFIG_INTERNET_PING_HOST_2
  #endif // CONFIG_INTERNET_PING_HOST_1

  if (ret.internet.available) {
    rlog_i(tagPING, "Internet access is available");
  } else {
    rlog_e(tagPING, "Internet access is not available!");
  };

  return ret;
};

#endif // CONFIG_INTERNET_PING_ENABLE

// =======================================================================================================================
// =======================================================================================================================
// ======================================================= rPinger =======================================================
// =======================================================================================================================
// =======================================================================================================================

rPinger::rPinger():rSensorX2() 
{
}

// Initialization of internal items
void rPinger::createSensorItems(const sensor_filter_t filterMode1, const uint16_t filterSize1, 
                                const sensor_filter_t filterMode2, const uint16_t filterSize2)
{
  // Timeout
  _item1 = new rSensorItem(this, CONFIG_PING_SENSOR_TIMEOUT_NAME, 
    filterMode1, filterSize1,
    CONFIG_PING_SENSOR_TIMEOUT_FORMAT_VALUE, CONFIG_PING_SENSOR_TIMEOUT_FORMAT_STRING,
    #if CONFIG_SENSOR_TIMESTAMP_ENABLE
    CONFIG_FORMAT_TIMESTAMP_L, 
    #endif // CONFIG_SENSOR_TIMESTAMP_ENABLE
    #if CONFIG_SENSOR_TIMESTRING_ENABLE  
    CONFIG_FORMAT_TIMESTAMP_S, CONFIG_FORMAT_TSVALUE
    #endif // CONFIG_SENSOR_TIMESTRING_ENABLE
  );
  if (_item1) rlog_d(_name, RSENSOR_LOG_MSG_CREATE_ITEM, _item1->getName(), _name);

  // Packet loss
  _item2 = new rSensorItem(this, CONFIG_PING_SENSOR_LOSS_NAME, 
    filterMode2, filterSize2,
    CONFIG_PING_SENSOR_LOSS_FORMAT_VALUE, CONFIG_PING_SENSOR_LOSS_FORMAT_STRING,
    #if CONFIG_SENSOR_TIMESTAMP_ENABLE
    CONFIG_FORMAT_TIMESTAMP_L, 
    #endif // CONFIG_SENSOR_TIMESTAMP_ENABLE
    #if CONFIG_SENSOR_TIMESTRING_ENABLE  
    CONFIG_FORMAT_TIMESTAMP_S, CONFIG_FORMAT_TSVALUE
    #endif // CONFIG_SENSOR_TIMESTRING_ENABLE
  );
  if (_item2) rlog_d(_name, RSENSOR_LOG_MSG_CREATE_ITEM, _item2->getName(), _name);
}

// Register internal parameters
void rPinger::registerItemsParameters(paramsGroupHandle_t parent_group)
{
};

// Displaying multiple values in one topic
#if CONFIG_SENSOR_DISPLAY_ENABLED

void rPinger::initDisplayMode()
{
  _displayMode = SENSOR_MIXED_ITEMS_12;
  _displayFormat = (char*)CONFIG_PING_SENSOR_FORMAT_MIXED;
}

#endif // CONFIG_SENSOR_DISPLAY_ENABLED

#if CONFIG_SENSOR_AS_PLAIN

bool rPinger::publishCustomValues()
{
  bool ret = rSensor::publishCustomValues();

  #if CONFIG_SENSOR_DEWPOINT_ENABLE
    if ((ret) && (_item1) && (_item2)) {
      ret = _item2->publishDataValue(CONFIG_SENSOR_DEWPOINT, 
        calcDewPoint(_item2->getValue().filteredValue, _item1->getValue().filteredValue));
    };
  #endif // CONFIG_SENSOR_DEWPOINT_ENABLE

  return ret;
} 

#endif // CONFIG_SENSOR_AS_PLAIN

#if CONFIG_SENSOR_AS_JSON

char* rPinger::jsonCustomValues()
{
  #if CONFIG_SENSOR_DEWPOINT_ENABLE
    if ((_item1) && (_item2)) {
      char * _dew_point = _item2->jsonDataValue(calcDewPoint(_item2->getValue().filteredValue, _item1->getValue().filteredValue));
      char * ret = malloc_stringf("\"%s\":%s", CONFIG_SENSOR_DEWPOINT, _dew_point);
      if (_dew_point) free(_dew_point);
      return ret;  
    };
  #endif // CONFIG_SENSOR_DEWPOINT_ENABLE
  return nullptr;
}

#endif // CONFIG_SENSOR_AS_JSON


