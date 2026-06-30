/*
 * Copyright (c) 2024 Alexandre Bailon
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef COAP_UTILS_H
#define COAP_UTILS_H

#include "zephyr/sys/byteorder.h"
#include <openthread/coap.h>
#include <openthread/ip6.h>
#include <zephyr/net/openthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COAP_MAX_BUF_SIZE 128
#define COAP_DEVICE_ID_SIZE 25

typedef struct {
  union {
    otIp6Address addr;
    char const *str;
  } u;
  bool is_str;
} coap_addr_t;

typedef int (*coap_req_handler_put)(void *ctx, uint8_t *buf, int size);
typedef int (*coap_req_handler_get)(void *ctx, otMessage *msg,
                                    const otMessageInfo *msg_info);

int coap_init(void);

int coap_req_handler(void *ctx, otMessage *msg, const otMessageInfo *msg_info,
                     coap_req_handler_put put_fn, coap_req_handler_get get_fn);

int coap_resp_send(otMessage *req, const otMessageInfo *req_info,
                   uint8_t const *buf, int len);

int coap_put_req_send(coap_addr_t addr, const char *uri, uint8_t const *buf,
                      int len, otCoapResponseHandler handler, void *ctx);

int coap_get_req_send(coap_addr_t addr, const char *uri, uint8_t const *buf,
                      int len, otCoapResponseHandler handler, void *ctx);

const char *coap_device_id(void);

int coap_get_data(otMessage *msg, void *buf, int *len);

#ifdef __cplusplus
}
#endif

#endif /* COAP_UTILS_H */
