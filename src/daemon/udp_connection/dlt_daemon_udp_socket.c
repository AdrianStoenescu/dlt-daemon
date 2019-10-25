/*
 * Copyright (c) 2019 LG Electronics Inc.
 * SPDX-License-Identifier: MPL-2.0
 *
 * This file is part of GENIVI Project DLT - Diagnostic Log and Trace.
 * If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * For further information see http://www.genivi.org/.
 */

/*!
 * \author
 * Guruprasad KN <guruprasad.kn@lge.com>
 * Sachin Sudhakar Shetty <sachin.shetty@lge.com>
 * Sunil Kovila Sampath <sunil.s@lge.com>
 *
 * \copyright Copyright (c) 2019 LG Electronics Inc.
 * License MPL-2.0: Mozilla Public License version 2.0 http://mozilla.org/MPL/2.0/.
 *
 * \file dlt_daemon_udp_socket.c
 */

#include "dlt_daemon_udp_common_socket.h"

static void dlt_daemon_udp_clientmsg_send(DltDaemonClientSockInfo *clientinfo,
                                          void *data1, int size1, void *data2, int size2, int verbose);
static int g_udp_sock_fd = -1;
static DltDaemonClientSockInfo g_udpmulticast_addr;

/* ************************************************************************** */
/* Function   : dlt_daemon_udp_init_clientstruct */
/* In Param   : UDP client_info struct to be initilzed */
/* Out Param  : NIL */
/* Description: client struct to be initilized to copy control/connect/disconnect */
/*                client addr */
/* ************************************************************************** */
void dlt_daemon_udp_init_clientstruct(DltDaemonClientSockInfo *clientinfo_struct)
{
    if (clientinfo_struct == NULL) {
        dlt_vlog(LOG_ERR, "%s: NULL arg\n", __func__);
        return;
    }

    memset(&clientinfo_struct->clientaddr, 0x00, sizeof(clientinfo_struct->clientaddr));
    clientinfo_struct->clientaddr_size = sizeof(clientinfo_struct->clientaddr);
    clientinfo_struct->isvalidflag = ADDRESS_INVALID; /* info is invalid */
    dlt_vlog(LOG_DEBUG, "%s: client addr struct init success \n", __func__);
}

/* ************************************************************************** */
/* Function   : dlt_daemon_udp_setmulticast_addr */
/* In Param   : NIL */
/* Out Param  : NIL */
/* Description: set the multicast addr to global variables */
/* ************************************************************************** */
void dlt_daemon_udp_setmulticast_addr(DltDaemonLocal *daemon_local)
{
    if (daemon_local == NULL) {
        dlt_vlog(LOG_ERR, "%s: NULL arg\n", __func__);
        return;
    }

    dlt_daemon_udp_init_clientstruct(&g_udpmulticast_addr);

    struct sockaddr_in clientaddr;
    clientaddr.sin_family = AF_INET;
    inet_pton(AF_INET, daemon_local->UDPMulticastIPAddress, &clientaddr.sin_addr);
    clientaddr.sin_port = htons(daemon_local->UDPMulticastIPPort);
    memcpy(&g_udpmulticast_addr.clientaddr, &clientaddr, sizeof(struct sockaddr_in));
    g_udpmulticast_addr.clientaddr_size = sizeof(g_udpmulticast_addr.clientaddr);
    g_udpmulticast_addr.isvalidflag = ADDRESS_VALID;
}

/* ************************************************************************** */
/* Function   : dlt_daemon_udp_connection_setup */
/* In Param   : contains daemon param values used globally */
/* Out Param  : status of udp connection setup and fd registration */
/* Description: DataGram socket fd connection is setup */
/* ************************************************************************** */
DltReturnValue dlt_daemon_udp_connection_setup(DltDaemonLocal *daemon_local)
{
    int fd = DLT_FD_INIT;
    DltReturnValue ret_val = DLT_RETURN_WRONG_PARAMETER;

    if (daemon_local == NULL)
        return ret_val;

    if ((ret_val = dlt_daemon_udp_socket_open(&fd, daemon_local->flags.port, daemon_local->UDPBindIPAddress)) != DLT_RETURN_OK) {
        dlt_log(LOG_ERR, "Could not initialize udp socket.\n");
    }
    else {
        /* assign to global udp fd */
        g_udp_sock_fd = fd;
        /* set global multicast addr */
        dlt_daemon_udp_setmulticast_addr(daemon_local);
        dlt_log(LOG_DEBUG, "initialize udp socket success\n");
    }

    return ret_val;
}

/* ************************************************************************** */
/* Function   : dlt_daemon_udp_socket_open */
/* In Param   : contains udp port number and bind address */
/* Out Param  : status of udp connection setup */
/* Description: This funtion is used to setup DGRAM connection */
/*              does socket()->bind() on udp port */
/* ************************************************************************** */
DltReturnValue dlt_daemon_udp_socket_open(int *sock, unsigned int servPort, char *ip)
{
    int yes = 1;
    int sockbuffer = DLT_DAEMON_RCVBUFSIZESOCK;
    int ret_inet_pton = 1;

    dlt_vlog(LOG_INFO, "dlt_daemon_udp_socket_open: ip is %s\n", ip);

#ifdef DLT_USE_IPv6

    /* create socket */
    if ((*sock = socket(AF_INET6, SOCK_DGRAM, 0)) == SYSTEM_CALL_ERROR) {
        dlt_vlog(LOG_WARNING, "dlt_daemon_udp_socket_open: socket() error %d: %s\n", errno, strerror(errno));
    }

#else

    if ((*sock = socket(AF_INET, SOCK_DGRAM, 0)) == SYSTEM_CALL_ERROR) {
        dlt_vlog(LOG_WARNING, "dlt_daemon_socket_open: socket() error %d: %s\n", errno, strerror(errno));
    }

#endif

    dlt_vlog(LOG_INFO, "%s: Socket udp created\n", __FUNCTION__);

    /* setsockpt SO_REUSEADDR */
    if (setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == SYSTEM_CALL_ERROR) {
        dlt_vlog(LOG_WARNING, "dlt_daemon_udp_socket_open: Setsockopt error %d : %s\n", errno, strerror(errno));
            close(*sock);
        }

    if (setsockopt(*sock, SOL_SOCKET, SO_RCVBUF, &sockbuffer, sizeof(sockbuffer)) == SYSTEM_CALL_ERROR) {
        dlt_vlog(LOG_WARNING, "dlt_daemon_udp_socket_open: Setsockopt error %d : %s\n", errno, strerror(errno));
        close(*sock);
    }

    /* bind */
#ifdef DLT_USE_IPv6
    struct sockaddr_in6 forced_addr;
    memset(&forced_addr, 0, sizeof(forced_addr));
    forced_addr.sin6_family = AF_INET6;
    forced_addr.sin6_port = htons(servPort);
    if (0 == strcmp(ip, "0.0.0.0"))
        forced_addr.sin6_addr = in6addr_any;
    else
        ret_inet_pton = inet_pton(AF_INET6, ip, &forced_addr.sin6_addr);
#else
    struct sockaddr_in forced_addr;
    memset(&forced_addr, 0, sizeof(forced_addr));
    forced_addr.sin_family = AF_INET;
    forced_addr.sin_port = htons(servPort);
    ret_inet_pton = inet_pton(AF_INET, ip, &forced_addr.sin_addr);
#endif

    /* inet_pton returns 1 on success */
    if (ret_inet_pton != 1) {
        dlt_vlog(LOG_WARNING,
                 "dlt_daemon_udp_socket_open: inet_pton() error %d: %s. Cannot convert IP address: %s\n",
                 errno,
                 strerror(errno),
                 ip);
        return -1;
    }

    if (bind(*sock, (struct sockaddr *)&forced_addr, sizeof(forced_addr)) == SYSTEM_CALL_ERROR) {
        dlt_vlog(LOG_WARNING, "dlt_daemon_udp_socket_open: bind() error %d: %s\n", errno, strerror(errno));
        close(*sock);
    }

    return DLT_RETURN_OK; /* OK */
}

/* ************************************************************************** */
/* Function   : dlt_daemon_udp_dltmsg_multicast */
/* In Param   : data bytes in dlt format */
/* Out Param  : NIL */
/* Description: multicast UDP dlt-message packets to dlt-client */
/* ************************************************************************** */
void dlt_daemon_udp_dltmsg_multicast(void *data1, int size1,
                                     void *data2, int size2,
                                     int verbose)
{
    PRINT_FUNCTION_VERBOSE(verbose);

    /*
     * When UDP Buffer is implemented then data2 would be expected to be NULL
     * as the data comes from buffer directly. In that case data2 should
     * not be checked for NULL
     */
    if ((data1 == NULL) || (data2 == NULL)) {
        dlt_vlog(LOG_ERR, "%s: NULL arg\n", __func__);
        return;
    }

    dlt_daemon_udp_clientmsg_send(&g_udpmulticast_addr, data1, size1,
                                  data2, size2, verbose);
}

/* ************************************************************************** */
/* Function   : dlt_daemon_udp_clientmsg_send */
/* In Param   : data bytes & respective size in dlt format */
/* Out Param  : NIL */
/* Description: common interface to send data via UDP protocol */
/* ************************************************************************** */
void dlt_daemon_udp_clientmsg_send(DltDaemonClientSockInfo *clientinfo,
                                   void *data1, int size1, void *data2, int size2, int verbose)
{
    PRINT_FUNCTION_VERBOSE(verbose);

    if ((clientinfo->isvalidflag == ADDRESS_VALID) &&
        (size1 > 0) && (size2 > 0)) {
        void *data = (void *)calloc(size1 + size2, sizeof(char));

        if (data == NULL) {
            dlt_vlog(LOG_ERR, "%s: calloc failure\n", __func__);
            return;
        }

        memcpy(data, data1, size1);
        memcpy(data + size1, data2, size2);

        if (sendto(g_udp_sock_fd, data, size1 + size2, 0, (struct sockaddr *)&clientinfo->clientaddr,
                   clientinfo->clientaddr_size) < 0)
            dlt_vlog(LOG_ERR, "%s: Send UDP Packet Data failed\n", __func__);

        free(data);
        data = NULL;

    }
    else {
        if (clientinfo->isvalidflag != ADDRESS_VALID)
            dlt_vlog(LOG_ERR, "%s: clientinfo->isvalidflag != ADDRESS_VALID %d\n", __func__, clientinfo->isvalidflag);

        if (size1 <= 0)
            dlt_vlog(LOG_ERR, "%s: size1 <= 0\n", __func__);

        if (size2 <= 0)
            dlt_vlog(LOG_ERR, "%s: size2 <= 0\n", __func__);
    }
}

/* ************************************************************************** */
/* Function   : dlt_daemon_udp_close_connection */
/* In Param   : NIL */
/* Out Param  : NIL */
/* Description: Closes UDP Connection */
/* ************************************************************************** */
void dlt_daemon_udp_close_connection(void)
{
    if (close(g_udp_sock_fd) == SYSTEM_CALL_ERROR)
        dlt_vlog(LOG_WARNING, "[%s:%d] close error %s\n", __func__, __LINE__,
                 strerror(errno));
}
