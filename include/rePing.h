/* 
   EN: Module for checking server availability by pinging and waiting for completion. Wrapper for "esp_ping.h".
   RU: Модуль для проверки доступности сервера путем пинга с ожиданием завершения. Обертка для "esp_ping.h".
   --------------------------
   (с) 2021 Разживин Александр | Razzhivin Alexander
   kotyara12@yandex.ru | https://kotyara12.ru | tg: @kotyara1971
*/

#ifndef __RE_PING_H__
#define __RE_PING_H__

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "lwip/ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char* hostname;
  ip_addr_t hostaddr;
  uint32_t duration;
  float loss; 
} ping_result_t;

ping_result_t pingHost(const char* hostname, const uint32_t count, const uint32_t interval, const uint32_t timeout, const uint32_t datasize);

#ifdef __cplusplus
}
#endif

#endif // __RE_PING_H__
