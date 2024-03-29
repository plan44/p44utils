/*
 * MIT License
 *
 * Copyright (c) 2019 Jianhui Zhao <jianhuizhao329@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
 
#ifndef _UTILS_H
#define _UTILS_H

#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>

#ifdef __APPLE__
  #include "compat_glue/include/sys/socket.h" // we need this wapper to support SOCK_NONBLOCK and SOCK_CLOEXEC
#endif

#include "config.h"

#ifndef container_of
#define container_of(ptr, type, member)                 \
    ({                              \
        const __typeof__(((type *) NULL)->member) *__mptr = (ptr);  \
        (type *) ((char *) __mptr - offsetof(type, member));    \
    })
#endif

int get_nonce(uint8_t *dest, int len);
int parse_url(const char *url, char *host, int host_len,
    int *port, const char **path, bool *ssl);

int tcp_connect(const char *host, int port, int flags, bool *inprogress, int *eai);

int b64_encode(const void *src, size_t srclen, void *dest, size_t destsize);

#endif
