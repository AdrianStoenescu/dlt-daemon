/*
 * @licence app begin@
 * SPDX license identifier: MPL-2.0
 *
 * Copyright (C) 2011-2015, BMW AG
 *
 * This file is part of GENIVI Project DLT - Diagnostic Log and Trace.
 *
 * This Source Code Form is subject to the terms of the
 * Mozilla Public License (MPL), v. 2.0.
 * If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * For further information see http://www.genivi.org/.
 * @licence end@
 */

/*!
 * \author
 * Alexander Wenzel <alexander.aw.wenzel@bmw.de>
 * Markus Klein <Markus.Klein@esk.fraunhofer.de>
 * Mikko Rapeli <mikko.rapeli@bmw.de>
 *
 * \copyright Copyright © 2011-2015 BMW AG. \n
 * License MPL-2.0: Mozilla Public License version 2.0 http://mozilla.org/MPL/2.0/.
 *
 * \file dlt_daemon_socket.c
 */


#include <netdb.h>
#include <ctype.h>
#include <stdio.h>      /* for printf() and fprintf() */
#include <sys/socket.h> /* for socket(), connect(), (), and recv() */
#include <arpa/inet.h>  /* for sockaddr_in and inet_addr() */
#include <stdlib.h>     /* for atoi() and exit() */
#include <string.h>     /* for memset() */
#include <unistd.h>     /* for close() */
#include <fcntl.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <net/if.h>

#ifdef linux
#   include <sys/timerfd.h>
#endif
#include <sys/stat.h>
#include <sys/time.h>
#if defined(linux) && defined(__NR_statx)
#   include <linux/stat.h>
#endif

#include "dlt_types.h"
#include "dlt-daemon.h"
#include "dlt-daemon_cfg.h"
#include "dlt_daemon_common_cfg.h"

#include "dlt_daemon_socket.h"

/** Global text output buffer, mainly used for creation of error/warning strings */
static char str[DLT_DAEMON_TEXTBUFSIZE];

int dlt_daemon_socket_open(int *sock, unsigned int servPort, char* ip)
{
   int yes = 1;
   char portnumbuffer[33];
   snprintf(portnumbuffer, 32, "%d", servPort);

#ifdef DLT_IPv6
   // create socket
    if ((*sock = socket(AF_INET6, SOCK_STREAM, 0)) == -1) {
      const int lastErrno = errno;
      dlt_vlog(LOG_WARNING, "dlt_daemon_socket_open: socket() error %d: %s\n", lastErrno, strerror(lastErrno));
    }
#else
    if ((*sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
          const int lastErrno = errno;
          dlt_vlog(LOG_WARNING, "dlt_daemon_socket_open: socket() error %d: %s\n", lastErrno, strerror(lastErrno));
    }
#endif

   snprintf(str, DLT_DAEMON_TEXTBUFSIZE, "%s: Socket created\n", __FUNCTION__);
   dlt_log(LOG_INFO, str);

   //setsockpt SO_REUSEADDR
   if (setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
      const int lastErrno = errno;
      dlt_vlog(LOG_WARNING, "dlt_daemon_socket_open: Setsockopt error %d in dlt_daemon_local_connection_init: %s\n", lastErrno, strerror(lastErrno));
   }

   // bind
#ifdef DLT_IPv6
   struct sockaddr_in6 forced_addr;
   memset(&forced_addr, 0, sizeof(forced_addr));
   forced_addr.sin6_family = AF_INET6;
   forced_addr.sin6_port = htons(servPort);
   inet_pton(AF_INET6, ip, &forced_addr.sin6_addr);
#else
   struct sockaddr_in forced_addr;
   memset(&forced_addr, 0, sizeof(forced_addr));
   forced_addr.sin_family = AF_INET;
   forced_addr.sin_port = htons(servPort);
   inet_pton(AF_INET, ip, &forced_addr.sin_addr);
#endif

   if (bind(*sock, (struct sockaddr *) &forced_addr, sizeof(forced_addr)) == -1) {
      const int lastErrno = errno; /*close() may set errno too */
      close(*sock);
      dlt_vlog(LOG_WARNING, "dlt_daemon_socket_open: bind() error %d: %s\n", lastErrno, strerror(lastErrno));
   }

   //listen
   snprintf(str, DLT_DAEMON_TEXTBUFSIZE, "%s: Listening on ip %s and port: %u\n", __FUNCTION__, ip, servPort);
   dlt_log(LOG_INFO, str);

   /* get socket buffer size */
   snprintf(str,
             DLT_DAEMON_TEXTBUFSIZE,
             "dlt_daemon_socket_open: Socket send queue size: %d\n",
             dlt_daemon_socket_get_send_qeue_max_size(*sock));
   dlt_log(LOG_INFO, str);

    if (listen(*sock, 3) < 0) {
      const int lastErrno = errno;
        dlt_vlog(LOG_WARNING, "dlt_daemon_socket_open: listen() failed with error %d: %s\n", lastErrno,
                 strerror(lastErrno));
      return -1;
   }

   return 0; /* OK */
}

int dlt_daemon_socket_close(int sock)
{
    close(sock);

    return 0;
}

int dlt_daemon_socket_send(int sock, void *data1, int size1, void *data2, int size2, char serialheader)
{
    int ret = DLT_RETURN_OK;
    int bytes_sent = 0;

    /* Optional: Send serial header, if requested */
    if (serialheader) {
        ret = dlt_daemon_socket_sendreliable(sock,
                                             (void *)dltSerialHeader,
                                             sizeof(dltSerialHeader),
                                             &bytes_sent);

        if (ret != DLT_RETURN_OK)
            return ret;
    }

    /* Send data */
    if ((data1 != NULL) && (size1 > 0)) {
        ret = dlt_daemon_socket_sendreliable(sock, data1, size1, &bytes_sent);

        if (ret != DLT_RETURN_OK)
            return ret;
    }

    if ((data2 != NULL) && (size2 > 0))
        ret = dlt_daemon_socket_sendreliable(sock, data2, size2, &bytes_sent);

    return ret;
}

int dlt_daemon_socket_get_send_qeue_max_size(int sock)
{
    int n = 0;
    socklen_t m = sizeof(n);
    getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (void *)&n, &m);

    return n;
}

int dlt_daemon_socket_sendreliable(int sock, void *data_buffer, int message_size, int *bytes_sent)
{
    int data_sent = 0;

    while (data_sent < message_size) {
        ssize_t ret = send(sock, data_buffer + data_sent, message_size - data_sent, 0);

        if (ret < 0) {
            dlt_vlog(LOG_WARNING,
                     "dlt_daemon_socket_sendreliable: socket send failed [errno: %d]!\n",
                     errno);
            *bytes_sent = data_sent;
            return DLT_DAEMON_ERROR_SEND_FAILED;
        }
        else {
            data_sent += ret;
        }
    }

    *bytes_sent = data_sent;
    return DLT_DAEMON_ERROR_OK;
}

