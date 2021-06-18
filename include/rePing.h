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
  uint32_t duration;
  float loss; 
} ping_result_t;

ping_result_t pingHost(const char* hostname, const uint32_t count, const uint32_t interval, const uint32_t timeout, const uint32_t datasize);
