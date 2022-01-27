/*
 * Static config needed to build things in the thirdparty folder
 * in environments without autoconf/automake, such as XCode
 */

#ifndef _STATIC_CONFIG_H
#define _STATIC_CONFIG_H


/* for UWSC (libuwsc) */

#define UWSC_VERSION_MAJOR	3
#define UWSC_VERSION_MINOR 	3
#define UWSC_VERSION_PATCH 	2
#define UWSC_VERSION_STRING "3.3.2"

#define UWSC_SSL_SUPPORT	1

#define UWSC_HAVE_OPENSSL 	1
#define UWSC_HAVE_WOLFSSL 	0
#define UWSC_HAVE_MBEDTLS 	0

/* for libmodbus which otherwise would try to redefine strlcpy on macOS */
#define HAVE_STRLCPY 1

#endif /* _STATIC_CONFIG_H */
