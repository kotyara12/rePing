/* 
   EN: The module for checking the Internet availability by regular ping of public servers
   RU: Модуль проверки доступности сети Интернет путем обычного пинга публичных серверов
   --------------------------
   (с) 2021 Разживин Александр | Razzhivin Alexander
   kotyara12@yandex.ru | https://kotyara12.ru | tg: @kotyara1971
*/

#ifndef __RE_PING_H__
#define __RE_PING_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "project_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t transmitted;
  uint32_t received;
  uint32_t duration;
  float loss; 
  bool available;
} ping_result_t;

ping_result_t pingHost(const char* hostname, 
  const uint32_t count, const uint32_t interval, const uint32_t timeout, const uint32_t datasize,
  const float max_loss, const uint32_t max_duration);

#if CONFIG_INTERNET_PING_ENABLE

typedef struct {
  #ifdef CONFIG_INTERNET_PING_HOST_1
  ping_result_t host1;
  #endif // CONFIG_INTERNET_PING_HOST_1
  #ifdef CONFIG_INTERNET_PING_HOST_2
  ping_result_t host2;
  #endif // CONFIG_INTERNET_PING_HOST_2
  #ifdef CONFIG_INTERNET_PING_HOST_3
  ping_result_t host3;
  #endif // CONFIG_INTERNET_PING_HOST_3
  ping_result_t internet;
} ping_inet_t;

ping_inet_t pingCheckInternet();

#endif // CONFIG_INTERNET_PING_ENABLE

#ifdef __cplusplus
}
#endif

#endif // __RE_PING_H__
