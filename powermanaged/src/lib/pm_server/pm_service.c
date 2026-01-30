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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#define LOG_TAG "pm_service"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/poll.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <mqueue.h>
#include "pm_client.h"


#define UNIX_PATH_MAX    108
#define MAX_CLIENTS 32
#define NUM_THREADS 2

typedef struct pm_client_t {
        pthread_t thread;
        int client_fd;
        mqd_t mqdes;
}pm_client_t;

int send_msg_fd = -1;
int recv_msg_fd = -1;
int recv_state_changed_fd = -1;
int send_state_changed_fd = -1;
int tj_thrshld_down = -1;
int tj_thrshld_up = -1;
char *pm_sysfs_name = NULL;
char *tsensor_dev_name = NULL;
static mqd_t srv_mqdes;
static pm_client_t *pm_client_list[MAX_CLIENTS];
pthread_mutex_t client_list_lock = PTHREAD_MUTEX_INITIALIZER;

static int create_server_socket(const char* socket_name, int *fd)
{
        int ret = 0;
        struct sockaddr_un unix_addr;

        if (socket_name == NULL || fd == NULL) {
                _ERR("%s: invalid param.\n", __func__);
                return -1;
        }

        if (strlen(socket_name) == 0 || strlen(socket_name) >= UNIX_PATH_MAX ) {
                _ERR("invalid socket_name length.\n");
                return -1;
        }

        *fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (*fd < 0) {
                _ERR("%s: socket error. %s\n", __func__, strerror(errno));
                return -1;
        }

#ifdef _SET_SOCKET_NONBLOCK
        ret = TEMP_FAILURE_RETRY(fcntl(*fd, F_SETFL, O_NONBLOCK));
        if (ret < 0) {
                _ERR("%s: fcntl error. %s\n", __func__, strerror(errno));
                return -1;
        }
#endif //_SET_SOCKET_NONBLOCK


        memset(&unix_addr, 0, sizeof(struct sockaddr_un));
        unix_addr.sun_family = AF_UNIX;
        strncpy(unix_addr.sun_path, socket_name, UNIX_PATH_MAX);
        int len = strlen(unix_addr.sun_path) + sizeof(unix_addr.sun_family );

        ret = unlink(unix_addr.sun_path);
        if (ret < 0) {
                _ERR("%s: unlink error. %s\n", __func__, strerror(errno));
        }

        ret = bind(*fd, &unix_addr, len);
        if (ret < 0) {
                _ERR("%s: bind error. %s\n", __func__, strerror(errno));
                return -1;
        }

        if (listen(*fd, MAX_CLIENTS - 1) < 0) {
                _ERR("%s: listen error. %s\n", __func__, strerror(errno));
                unlink(unix_addr.sun_path);
                return -1;
        }
        return 0;
}

static int get_client_euid(int client_fd, uid_t *client_euid)
{
        struct ucred cred;
        socklen_t cred_len = sizeof(cred);
        if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) < 0) {
                _ERR("%s: getsockopt error. %s\n", __func__, strerror(errno));
                return -1;
        }
        if ((unsigned)(cred_len) < sizeof(cred)) {
                _ERR("Truncated ucred.\n");
                return -1;
        }
        *client_euid = cred.uid;
        return 0;
}

static int authenticate_client(int client_fd)
{
        int ret;
        uid_t client_euid;

        ret = get_client_euid(client_fd, &client_euid);
        if (ret < 0)
                return -1;
        if (client_euid != geteuid()) {
                _ERR("authenticate_client failed.\n");
                return -1;
        }
        return 0;
}

static int add_to_client_list(pm_client_t **list, pm_client_t *client)
{
        int i;

        for (i = 0; i < MAX_CLIENTS; i++) {
                if (*(list + i) == NULL) {
                        *(list + i) = client;
                        return 0;
                }
        }
        return -1;
}

static void remove_from_client_list(pm_client_t **list, pm_client_t *client)
{
        int i;
        for (i = 0; i < MAX_CLIENTS; i++) {
                if (*(list + i) == client) {
                        *(list + i) = NULL;
                        return;
                }
        }
}

static void destroy_client(pm_client_t *c)
{
        pthread_mutex_lock(&client_list_lock);
        remove_from_client_list(pm_client_list, c);
        pthread_mutex_unlock(&client_list_lock);

        close(c->client_fd);
        mq_close(c->mqdes);
        free(c);
        c = NULL;
}

static int process_client_message(pm_client_t *c)
{
        int i = 0;
        struct pm_notification event = {0};
        unsigned char *buf = (unsigned char *)&event;

        do {
                ssize_t n = (TEMP_FAILURE_RETRY(recv(c->client_fd, (void *)(buf + i), sizeof(struct pm_notification) - i, 0)));
                if (n < 0) {
                        _ERR("%s: read error. %s\n", __func__, strerror(errno));
                        destroy_client(c);
                        return -1;
                }
                else if (n == 0) {
                        _DBG("%s, recevied EOF.\n", __func__);
                        destroy_client(c);
                        return -1;
                }
                else {
                        i += n;
                }
        } while(i < sizeof(struct pm_notification));

        if (event.type == PM_NOTIFICATION_TO_SRV) {
                int ret = (TEMP_FAILURE_RETRY(mq_send(srv_mqdes, (void *)&event, sizeof(struct pm_notification), 0)));
                if (ret != 0) {
                        _ERR("%s: mq_send error. %s\n", __func__, strerror(errno));
                        destroy_client(c);
                        return -1;
                }
        }
        return 0;
}

static int process_srv_message(pm_client_t *c)
{
        struct pm_notification event = {0};
        ssize_t ret = (TEMP_FAILURE_RETRY(mq_receive(c->mqdes, (void *)&event, sizeof(struct pm_notification), NULL)));
        if (ret != sizeof(struct pm_notification)) {
                _ERR("mq_receive error, %s\n", strerror(errno));
                destroy_client(c);
                return -1;
        }

        if (event.type == PM_NOTIFICATION_FROM_SRV) {
                ret = TEMP_FAILURE_RETRY(send(c->client_fd, (const void*)&event, sizeof(struct pm_notification), 0));
                if (ret != sizeof(struct pm_notification)) {
                        _ERR("send to %d error. %s\n", c->client_fd, strerror(errno));
                        destroy_client(c);
                        return -1;
                }
        }

        return 0;
}

void *client_func(void *data)
{
        pm_client_t *c = (pm_client_t *)data;
        struct pollfd fds[2];
        while (1) {
                memset(fds, 0 , sizeof(fds));
                fds[0].fd = c->client_fd;
                fds[0].events = POLLIN;
                fds[1].fd = c->mqdes;
                fds[1].events = POLLIN;

                int ret = TEMP_FAILURE_RETRY(poll(fds, 2, 10*1000));
                if (ret == 0)
                        continue;
                if (ret < 0) {
                        _ERR("poll failed, error %s\n", strerror(errno));
                        pthread_exit(NULL);
                }

                if( fds[0].revents & (POLLIN|POLLHUP) || fds[0].revents & POLLPRI) {
                        _DBG("client has message coming.\n");
                        ret = process_client_message(c);
                        if (ret < 0) {
                                pthread_exit(NULL);
                        }
                }

                if( fds[1].revents & (POLLIN|POLLHUP) || fds[1].revents & POLLPRI) {
                        _DBG("server has message coming.\n");
                        ret = process_srv_message(c);
                        if (ret < 0) {
                                pthread_exit(NULL);
                        }
                }
        }
}

static int create_client(int client_fd)
{
        pm_client_t *c = (pm_client_t *)calloc(1, sizeof(pm_client_t));
        if (c == NULL) {
                _ERR("%s: calloc error. %s\n", __func__, strerror(errno));
                close(client_fd);
                return -1;
        }

        pthread_mutex_lock(&client_list_lock);
        int ret = add_to_client_list(&pm_client_list[0], c);
        pthread_mutex_unlock(&client_list_lock);
        if (ret < 0) {
                _ERR("%s: client list is full, drop this connection.\n", __func__);
                close(client_fd);
                free(c);
                return -1;
        }

        struct mq_attr attr;
        attr.mq_msgsize = sizeof(struct pm_notification);
        attr.mq_curmsgs = 0;
        attr.mq_flags = 0;
        attr.mq_maxmsg = 10;
        c->mqdes = mq_open("/pm_event_queue", O_RDWR | O_CREAT, 0664, &attr);
        if (c->mqdes < 0) {
                _ERR("%s: mq_open error. %s\n", __func__, strerror(errno));
                close(client_fd);
                free(c);
                return -1;
        }
        mq_unlink ("/pm_event_queue");

        c->client_fd = client_fd;
        ret = pthread_create(&c->thread, NULL, client_func, (void *)c);
        if (ret) {
                _ERR("pthread_create() failed with %s\n", strerror(errno));
                return -1;
        }

        return 0;
}

static int accept_client(int server_fd)
{
        int ret;
        struct sockaddr_un client_addr;
        socklen_t len = sizeof(client_addr);
        int t = TEMP_FAILURE_RETRY(accept(server_fd, (struct sockaddr *)&client_addr, &len));
        if (t == -1) {
                _ERR("accept error%s\n", strerror(errno));
                return -1;

        } else {
                ret = authenticate_client(t);
                if (ret < 0) {
                        close(t);
                        return -1;
                } else {
                        _DBG("Client authenticated.\n");
                        ret = create_client(t);
                }
        }

        return ret;
}

static int get_new_state(const int pipe_recv_fd, struct pm_notification *event)
{
        int i = 0;
        unsigned char *buf = (unsigned char *)event;

        do {
                ssize_t n = TEMP_FAILURE_RETRY(read(pipe_recv_fd, (void *)(buf + i), sizeof(struct pm_notification) - i));
                if (n < 0) {
                        _ERR("%s: read error. %s\n", __func__, strerror(errno));
                        return -1;
                }
                else if (n == 0) {
                        _DBG("%s, recevied EOF.\n", __func__);
                        return -1;
                }
                else {
                        i += n;
                }
        } while(i < sizeof(struct pm_notification));

        _DBG("recevied new pm state %d\n", (int)event->id);
        return 0;
}

static void broadcast_state_change(const struct pm_notification *event)
{
        int i, ret;

        for (i = 0; i < MAX_CLIENTS; i++) {
                if (pm_client_list[i] != NULL) {
                        struct mq_attr attr, old_attr;
                        attr.mq_flags = O_NONBLOCK;
                        mq_setattr (pm_client_list[i]->mqdes, &attr, &old_attr);
                        ret = TEMP_FAILURE_RETRY(mq_send(pm_client_list[i]->mqdes, (const void*)event, sizeof(struct pm_notification), 0));
                        if (ret < 0) {
                                _ERR("send to %d error. %s\n", pm_client_list[i]->client_fd, strerror(errno));
                        }
                        mq_setattr (pm_client_list[i]->mqdes, &old_attr, 0);
                }
        }
}

static int notify_pm_state_machine(const int pipe_send_fd, const struct pm_notification *event )
{
        ssize_t ret = (TEMP_FAILURE_RETRY(write(pipe_send_fd, (void *)event, sizeof(struct pm_notification))));
        if (ret != sizeof(struct pm_notification)) {
                _ERR("write to %d error. %s\n", pipe_send_fd, strerror(errno));
        }
        return 0;
}

static int get_client_message(struct pm_notification *event)
{
        ssize_t ret = (TEMP_FAILURE_RETRY(mq_receive(srv_mqdes, (void *)event, sizeof(struct pm_notification), NULL)));
        if (ret != sizeof(struct pm_notification)) {
                _ERR("mq_receive error, %s\n", strerror(errno));
                return -1;
        }

        _DBG("recevied new pm state %d\n", (int)event->id);
        return 0;
}

static int process_select_result(const struct pollfd *fds, const int server_fd)
{
        int i, ret;
        struct pm_notification event = {0};
        struct pollfd *p = (struct pollfd *)fds;

        for ( i = 0 ; i < 3 ; i++) {
                if( p->revents & (POLLIN|POLLHUP) || p->revents & POLLPRI) {
                        if (p->fd == server_fd) {
                                accept_client(server_fd);
                        }
                        else if (p->fd == recv_state_changed_fd) {
                                ret = get_new_state(recv_state_changed_fd, &event);
                                if (ret < 0)
                                        return -1;
                                broadcast_state_change(&event);
                        }
                        else if (p->fd == srv_mqdes) {
                                ret = get_client_message(&event);
                                if (ret < 0)
                                        return -1;
                                notify_pm_state_machine(send_msg_fd, &event);
                        }
                }
                p++;
        }

        return 0;
}

static void server_loop(const int server_fd)
{
        if (server_fd < 0) {
                _ERR("%s: invalid param.\n", __func__);
                return;
        }

        struct pollfd fds[3];
        while (1) {
                memset(fds, 0 , sizeof(fds));
                fds[0].fd = server_fd;
                fds[0].events = POLLIN;
                fds[1].fd = recv_state_changed_fd;
                fds[1].events = POLLIN;
                fds[2].fd = srv_mqdes;
                fds[2].events = POLLIN;

                int ret = TEMP_FAILURE_RETRY(poll(fds, 3, 10*1000));
                if (ret == 0)
                        continue;
                if (ret < 0) {
                        _ERR("poll failed, error %s\n", strerror(errno));
                        return;
                }
                ret = process_select_result(fds, server_fd);
                if (ret < 0) {
                        return;
                }
        }
}

static void *socket_srv_func(void *data)
{
        int ret, server_fd;
        struct mq_attr attr;

        attr.mq_msgsize = sizeof(struct pm_notification);
        attr.mq_curmsgs = 0;
        attr.mq_flags = 0;
        attr.mq_maxmsg = 10;
        srv_mqdes = mq_open("/pm_srv_queue", O_RDWR | O_CREAT, 0664, &attr);
        if (srv_mqdes < 0) {
                _ERR("mq_open error. %s\n",strerror(errno));
                pthread_exit(NULL);
        }
        mq_unlink ("/pm_srv_queue");

        ret = create_server_socket(PM_SERVICE_SOCKET_NAME, &server_fd);
        if (ret < 0) {
                pthread_exit(NULL);
        }

        server_loop(server_fd);

        mq_close(srv_mqdes);
        pthread_exit(NULL);
}

extern void *state_machine_func(void *data);

int main(int argc, char **argv)
{
        int fds1[2], fds2[1], i, ret;
        pthread_t thread[NUM_THREADS];

        if (argc != 5) {
                _ERR("Usage: %s $PM_SYSFS_FILE_NAME $TSENSOR_DEV_NAME $T_MAX $T_MIN\n", argv[0]);
                _ERR("Example: %s /sys/devices/platform/berlin_ppm/change_profile /sys/bus/iio/devices/iio\\:device1/in_temp_input 55 50\n", argv[0]);
                return EXIT_FAILURE;
        }
        pm_sysfs_name = argv[1];
        tsensor_dev_name = argv[2];
        tj_thrshld_down = strtol(argv[3], NULL, 10);
        tj_thrshld_up = strtol(argv[4], NULL, 10);

        pipe(fds1);
        recv_msg_fd = fds1[0];
        send_msg_fd = fds1[1];

        pipe(fds2);
        recv_state_changed_fd = fds2[0];
        send_state_changed_fd = fds2[1];

        ret = pthread_create(&thread[0], NULL, socket_srv_func, NULL);
        if (ret) {
                _ERR("pthread_create() failed with %s\n", strerror(errno));
                return EXIT_FAILURE;
        }

        ret = pthread_create(&thread[0], NULL, state_machine_func, NULL);
        if (ret) {
                _ERR("pthread_create() failed with %s\n", strerror(errno));
                return EXIT_FAILURE;
        }

        for (i = 0; i < NUM_THREADS; i++) {
                ret = pthread_join(thread[i], NULL);
                if (ret) {
                        _ERR("pthread_join() failed with %s\n", strerror(errno));
                        return EXIT_FAILURE;
                }
        }

        close(fds1[0]);
        close(fds1[1]);
        close(fds2[0]);
        close(fds2[1]);

        return EXIT_SUCCESS;
}
