/*
 * NDA AND NEED-TO-KNOW REQUIRED
 *
 * Copyright © 2013-2017 Synaptics Incorporated. All rights reserved.
 *
 * This file contains information that is proprietary to Synaptics
 * Incorporated (“Synaptics”). The holder of this file shall treat all
 * information contained herein as confidential, shall use the
 * information only for its intended purpose, and shall not duplicate,
 * disclose, or disseminate any of this information in any manner
 * unless Synaptics has otherwise provided express, written
 * permission.
 *
 * Use of the materials may require a license of intellectual property
 * from a third party or from Synaptics. This file conveys no express
 * or implied licenses to any intellectual property rights belonging
 * to Synaptics.
 *
 * INFORMATION CONTAINED IN THIS DOCUMENT IS PROVIDED "AS-IS,” AND
 * SYNAPTICS EXPRESSLY DISCLAIMS ALL EXPRESS AND IMPLIED WARRANTIES,
 * INCLUDING ANY IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE, AND ANY WARRANTIES OF NON-INFRINGEMENT OF ANY
 * INTELLECTUAL PROPERTY RIGHTS. IN NO EVENT SHALL SYNAPTICS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, PUNITIVE, OR
 * CONSEQUENTIAL DAMAGES ARISING OUT OF OR IN CONNECTION WITH THE USE
 * OF THE INFORMATION CONTAINED IN THIS DOCUMENT, HOWEVER CAUSED AND
 * BASED ON ANY THEORY OF LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, AND EVEN IF SYNAPTICS WAS
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. IF A TRIBUNAL OF
 * COMPETENT JURISDICTION DOES NOT PERMIT THE DISCLAIMER OF DIRECT
 * DAMAGES OR ANY OTHER DAMAGES, SYNAPTICS’ TOTAL CUMULATIVE LIABILITY
 * TO ANY PARTY SHALL NOT EXCEED ONE HUNDRED U.S. DOLLARS.
 */

////////////////////////////////////////////////////////////////////////////////
//! \file pm_client.c
//!
//! \brief This file implement client code for connect to mrvl power management service.
//!
//!
//! Purpose:
//!
//!
//!     Version    Date                     Author
//!     V 1.00,    Sep 1 2016,              yongbing2marvell.com
//!
//! Note:
////////////////////////////////////////////////////////////////////////////////


#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/poll.h>
#include "pm_client.h"

#define UNIX_PATH_MAX    108
struct pm_listener_context {
        pthread_t listener;
        int client_fd;
        int fds[2];
        struct pm_notification *event;
        listener_callback callback;
        int running;
};

static int connect_to_server(int *client_fd)
{
        if (client_fd == NULL) {
                _ERR("%s: invalid param.\n", __func__);
                return -1;
        }

        int ret;
        struct sockaddr_un unix_addr;
        if ((*client_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
                _ERR("%s: socket error. %s\n", __func__, strerror(errno));
                return -1;
        }

        _DBG("Trying to connect...\n");

        memset(&unix_addr, 0, sizeof(struct sockaddr_un));
        unix_addr.sun_family = AF_UNIX;
        strncpy(unix_addr.sun_path, PM_SERVICE_SOCKET_NAME, UNIX_PATH_MAX);
        int len = strlen(unix_addr.sun_path) + sizeof(unix_addr.sun_family);

        ret = TEMP_FAILURE_RETRY(connect(*client_fd, (struct sockaddr *)&unix_addr, len));
        if (ret < 0) {
                _ERR("%s: connect error. %s\n", __func__, strerror(errno));
                close(*client_fd);
                return -1;
        }

        _DBG("Connected.\n");
        return 0;
}

static void *listener_func(void *data)
{
        struct pm_listener_context *ctx = (struct pm_listener_context *)data;
        struct pollfd fds[2];

        while (ctx->running) {
                memset(fds, 0 , sizeof(fds));
                fds[0].fd = ctx->fds[0];
                fds[0].events = POLLIN;
                fds[1].fd = ctx->client_fd;
                fds[1].events = POLLIN;

                int ret = TEMP_FAILURE_RETRY(poll(fds, 2, 1*1000));
                if (ret == 0)
                        continue;
                if (ret < 0) {
                        _ERR("poll failed, error %s\n", strerror(errno));
                        ctx->running = 0;
                        ctx->callback(NULL);
                        pthread_exit(NULL);
                }

                if( fds[0].revents & (POLLIN|POLLHUP) || fds[0].revents & POLLPRI) {
                        _DBG("listener asking to exit.\n");
                        ctx->running = 0;
                        ctx->callback(NULL);
                        pthread_exit(NULL);
                }

                if( fds[1].revents & (POLLIN|POLLHUP) || fds[1].revents & POLLPRI) {
                        ssize_t n = TEMP_FAILURE_RETRY(read(fds[1].fd, (void *)ctx->event, sizeof(struct pm_notification)));
                        if (n < 0) {
                                _ERR("%s: read error. %s\n", __func__, strerror(errno));
                                ctx->running = 0;
                                ctx->callback(NULL);
                                pthread_exit(NULL);
                        }
                        else if (n == 0) {
                                _DBG("%s, recevied EOF, server closed the socket.\n", __func__);
                                ctx->running = 0;
                                ctx->callback(NULL);
                                pthread_exit(NULL);
                        }
                        else {
                                ctx->callback(ctx->event);
                        }
                }
        }

        pthread_exit(NULL);
}

void *pm_app_register_listener(struct pm_notification *event, listener_callback callback)
{
        if (event == NULL || callback == NULL) {
                _ERR("%s: invalid param.\n", __func__);
                return NULL;
        }

        struct pm_listener_context *ctx = (struct pm_listener_context *)calloc(1, sizeof(struct pm_listener_context));
        if (ctx == NULL) {
                return NULL;
        }

        int ret = connect_to_server(&ctx->client_fd);
        if (ret < 0) {
                return NULL;
        }

        ctx->event = event;
        ctx->callback = callback;
        pipe(ctx->fds);
        ctx->running = 1;
        ret = pthread_create(&ctx->listener, NULL, listener_func, ctx);
        if (ret) {
                _ERR("pthread_create() failed with %s\n", strerror(errno));
                return NULL;
        }

        return ctx;
}

int pm_app_unregister_listener(void *context)
{
        if (context == NULL) {
                _ERR("%s: invalid param.\n", __func__);
                return -1;
        }

        struct pm_listener_context *ctx = (struct pm_listener_context *)context;

        //let the listener_func exit.
        if (ctx->running) {
                TEMP_FAILURE_RETRY(write(ctx->fds[1], (void *)"exit", sizeof("exit")));
                pthread_join(ctx->listener, NULL);
        }

        close(ctx->fds[0]);
        close(ctx->fds[1]);
        close(ctx->client_fd);
        free(context);

        return 0;
}

int pm_client_send_notice(const struct pm_notification *notice, const int timeout)
{
        int ret, client_fd;
        if (notice == NULL || notice->id < (uint32_t)PM_VIDEO_PLAY_BACK || notice->id >= (uint32_t)PM_APP_STATE_INVALID) {
                _ERR("%s: invalid param.\n", __func__);
                return -1;
        }

        ret = connect_to_server(&client_fd);
        if (ret < 0) {
                return -1;
        }

        struct timeval ts;
        ts.tv_sec = timeout;
        ts.tv_usec = 0;
        ret = setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&ts, sizeof(struct timeval));
        if (ret < 0) {
                _ERR("%s: setsockopt error. %s\n", __func__, strerror(errno));
                close(client_fd);
                return -1;
        }

        ret = TEMP_FAILURE_RETRY(send(client_fd, (void *)notice, sizeof(struct pm_notification), 0));
        if (ret < 0) {
                _ERR("%s: send error. %s\n", __func__, strerror(errno));
                close(client_fd);
                return -1;
        }

        close(client_fd);

        return 0;
}

void *notify_func(void *data)
{
        int ret, client_fd;
        struct pm_notification *notice = (struct pm_notification *)data;
        _DBG("send new app state %u to power management service.\n", (uint32_t)notice->id);
        ret = connect_to_server(&client_fd);
        if (ret < 0) {
                free(notice);
                pthread_exit(NULL);
        }

        ret = TEMP_FAILURE_RETRY(send(client_fd, (void *)notice, sizeof(struct pm_notification), 0));
        if (ret < 0) {
                _ERR("%s: send error. %s\n", __func__, strerror(errno));
                close(client_fd);
                free(notice);
                pthread_exit(NULL);
        }

        close(client_fd);
        free(notice);
        pthread_exit(NULL);
}

int pm_client_send_notice_async(const struct pm_notification *notice)
{
        pthread_t notifier;
        if (notice == NULL || notice->id < (uint32_t)PM_VIDEO_PLAY_BACK || notice->id >= (uint32_t)PM_APP_STATE_INVALID) {
                _ERR("%s: invalid param.\n", __func__);
                return -1;
        }


        struct pm_notification *notice_c = (struct pm_notification *)calloc(1, sizeof(struct pm_notification));
        memcpy(notice_c, notice, sizeof(struct pm_notification));
        int ret = pthread_create(&notifier, NULL, notify_func, (void *)notice_c);
        if (ret) {
                _ERR("pthread_create() failed with %s\n", strerror(errno));
                return -1;
        }

        return 0;
}
