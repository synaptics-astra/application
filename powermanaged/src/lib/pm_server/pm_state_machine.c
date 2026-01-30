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
//! \file pm_state_machine.c
//!
//! \brief state machine for Marvell BG4CDp power management.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <pthread.h>
#include "pm_client.h"

#define READ_TSENSOR_INTERVAL 10
extern int send_state_changed_fd;
extern int recv_msg_fd;
extern char *pm_sysfs_name;
extern char *tsensor_dev_name;
extern int tj_thrshld_down;
extern int tj_thrshld_up;

static int last_temperature = 0;

static int pause_next_state(const pm_app_state_t new_app_state, const int temperature, int *state_changed, pm_state_t *new_state);
static int gaming_next_state(const pm_app_state_t new_app_state, const int temperature, int *state_changed, pm_state_t *new_state);
static int video_next_state(const pm_app_state_t new_app_state, const int temperature, int *state_changed, pm_state_t *new_state);
static int base_do_state_change(const pm_state_t new_state);


typedef struct power_state_t {
        pm_state_t self;
        char *ppm_name;
        int tj_thrshld_down;
        int tj_thrshld_up;

        int (* next_state)(const pm_app_state_t new_app_state, const int temperature, int *state_changed, pm_state_t *new_state);
        int (* do_state_change)(const pm_state_t new_state);
} power_state_t;

#define GAMING_MAX_NAME "grx_max"
#define GAMING_TYPICAL_NAME "grx_typical"
#define GAMING_MED_NAME "grx_median"
#define GAMING_LOW_NAME "grx_low"
#define PLAY_MAX_NAME "play_max"
#define PLAY_TYPICAL_NAME "play_typical"
#define PLAY_MED_NAME "play_median"
#define PLAY_LOW_NAME "play_low"
#define SYSTEM_POWER_SAVING "power_saving"
#define PLAY_PAUSE_NAME "play_pause"

//the sequence of this array elements indicating power state ladder.
static power_state_t video_power_states[NUM_STATE] = {
        /*self,         name,                   tj_down,tj_up,  next_state,             do_state_change*/
        {PLAY_MAX,      PLAY_MAX_NAME,          55,    50,     video_next_state,       base_do_state_change},
        {PLAY_TYPICAL,  PLAY_TYPICAL_NAME,      55,    50,     video_next_state,       base_do_state_change},
        {PLAY_MEDIAN,   PLAY_MED_NAME,          55,    50,     video_next_state,       base_do_state_change},
        {PLAY_LOW,      PLAY_LOW_NAME,          55,    50,     video_next_state,       base_do_state_change},
        {PLAY_PW_SAVING,SYSTEM_POWER_SAVING,    55,    50,     video_next_state,       base_do_state_change},
        {PLAY_PAUSE,    PLAY_PAUSE_NAME,        55,    50,     pause_next_state,       base_do_state_change},
};

//the sequence of this array elements indicating power state ladder.
static power_state_t gaming_power_states[NUM_STATE] = {
        /*self,         name,                   tj_down,tj_up,  next_state,             do_state_change*/
        {GRX_MAX,       GAMING_MAX_NAME,        55,    50,     gaming_next_state,      base_do_state_change},
        {GRX_TYPICAL,   GAMING_TYPICAL_NAME,    55,    50,     gaming_next_state,      base_do_state_change},
        {GRX_MEDIAN,    GAMING_MED_NAME,        55,    50,     gaming_next_state,      base_do_state_change},
        {GRX_LOW,       GAMING_LOW_NAME,        55,    50,     gaming_next_state,      base_do_state_change},
        {GRX_PW_SAVING, SYSTEM_POWER_SAVING,    55,    50,     gaming_next_state,      base_do_state_change},
};

static power_state_t *curr_state = NULL;
static pm_app_state_t curr_app_state = PM_APP_STATE_INVALID;
static pm_state_t video_state_before_pause = PM_STATE_INVALID;

static pm_state_t up_ladder_video_state(int *state_changed)
{
        int i;
        if (curr_state->self == PLAY_MAX)
                return PLAY_MAX;

        int num_of_state = sizeof(video_power_states) / sizeof(power_state_t);
        for (i = 0; i < num_of_state; i++) {
                if (video_power_states[i].self == curr_state->self) {
                        *state_changed = 1;
                        return video_power_states[i-1].self;
                }
        }
        _ERR("%s, %d, ERROR! shouldn't be here!\n", __func__, __LINE__);
        return PM_STATE_INVALID;
}

static pm_state_t down_ladder_video_state(int *state_changed)
{
        int i;
        if (curr_state->self == PLAY_PW_SAVING)
                return PLAY_PW_SAVING;

        int num_of_state = sizeof(video_power_states) / sizeof(power_state_t);
        for (i = 0; i < num_of_state; i++) {
                if (video_power_states[i].self == curr_state->self) {
                        *state_changed = 1;
                        return video_power_states[i+1].self;
                }
        }
        _ERR("%s, %d, ERROR! shouldn't be here!\n", __func__, __LINE__);
        return PM_STATE_INVALID;
}


static int change_video_state_if_temp_delta_cross_threshold(const int temperature, int *state_changed, pm_state_t *new_state)
{
        int temp_delta = temperature - last_temperature;
        if (temp_delta > 0 && temperature > curr_state->tj_thrshld_down) {
                *new_state = down_ladder_video_state(state_changed);
                _DBG("%s, %d, down state to %d, due to temperature change from %d to %d\n",
                                __func__, __LINE__, *new_state, last_temperature, temperature);
        }
        else if (temp_delta < 0 && temperature < curr_state->tj_thrshld_up) {
                *new_state = up_ladder_video_state(state_changed);
                _DBG("%s, %d, up state to %d, due to temperature change from %d to %d\n",
                                __func__, __LINE__, *new_state, last_temperature, temperature);
        }

        return 0;
}

static int pause_next_state(const pm_app_state_t new_app_state, const int temperature, int *state_changed, pm_state_t *new_state)
{
        if (new_app_state != PM_APP_STATE_INVALID && new_app_state != curr_app_state) {
                if (new_app_state == PM_GAMING || new_app_state == PM_VIDEO_STOP) {
                        *new_state = GRX_TYPICAL;
                        curr_app_state = PM_GAMING;
                }
                else if (new_app_state == PM_VIDEO_RESUME || new_app_state == PM_VIDEO_PLAY_BACK) {
                        if (video_state_before_pause == PM_STATE_INVALID) {
                                _ERR("invalid state change!\n");
                                return -1;
                        }
                        *new_state = video_state_before_pause;
                        video_state_before_pause = PM_STATE_INVALID;
                        curr_app_state = PM_VIDEO_PLAY_BACK;
                }
                else {
                        _ERR("%s, ERROR! unsupported APP mode %d.\n", __func__, (int)new_app_state);
                        return -1;
                }
                _DBG("%s, %d, change state to %d, due to app state change from %d to %d\n",
                                __func__, __LINE__, *new_state, (int)curr_app_state, (int)new_app_state);

                *state_changed = 1;
        }

        return 0;
}

static int video_next_state(const pm_app_state_t new_app_state, const int temperature, int *state_changed, pm_state_t *new_state)
{
        if (new_app_state != PM_APP_STATE_INVALID && new_app_state != curr_app_state) {
                if (new_app_state == PM_GAMING || new_app_state == PM_VIDEO_STOP) {
                        *new_state = GRX_TYPICAL;
                        curr_app_state = PM_GAMING;
                }
                else if (new_app_state == PM_VIDEO_PAUSE) {
                        *new_state = PLAY_PAUSE;
                        video_state_before_pause = curr_state->self;
                        curr_app_state = PM_VIDEO_PAUSE;
                }
                else if (new_app_state == PM_VIDEO_RESUME) {
                        if (video_state_before_pause == PM_STATE_INVALID) {
                                _ERR("invalid state change!\n");
                                return -1;
                        }
                        *new_state = video_state_before_pause;
                        video_state_before_pause = PM_STATE_INVALID;
                        curr_app_state = PM_VIDEO_PLAY_BACK;
                }
                else {
                        _ERR("%s, ERROR! unsupported APP mode %d.\n", __func__, (int)new_app_state);
                        return -1;
                }
                _DBG("%s, %d, change state to %d, due to app state change from %d to %d\n",
                                __func__, __LINE__, *new_state, (int)curr_app_state, (int)new_app_state);

                *state_changed = 1;
        }
        else if (curr_app_state != PM_VIDEO_RESUME){ //no app state change, check if temperature change requires state change.
                change_video_state_if_temp_delta_cross_threshold(temperature, state_changed, new_state);
        }

        return 0;
}

static pm_state_t up_ladder_grx_state(int *state_changed)
{
        int i;
        if (curr_state->self == GRX_MAX)
                return GRX_MAX;

        int num_of_state = sizeof(gaming_power_states) / sizeof(power_state_t);
        for (i = 0; i < num_of_state; i++) {
                if (gaming_power_states[i].self == curr_state->self) {
                        *state_changed = 1;
                        return gaming_power_states[i-1].self;
                }
        }
        _ERR("%s, %d, ERROR! shouldn't be here!\n", __func__, __LINE__);
        return PM_STATE_INVALID;
}

static pm_state_t down_ladder_grx_state(int *state_changed)
{
        int i;
        if (curr_state->self == GRX_PW_SAVING)
                return GRX_PW_SAVING;

        int num_of_state = sizeof(gaming_power_states) / sizeof(power_state_t);
        for (i = 0; i < num_of_state; i++) {
                if (gaming_power_states[i].self == curr_state->self) {
                        *state_changed = 1;
                        return gaming_power_states[i+1].self;
                }
        }
        _ERR("%s, %d, ERROR! shouldn't be here!\n", __func__, __LINE__);
        return PM_STATE_INVALID;
}

static int change_grx_state_if_temp_delta_cross_threshold(const int temperature, int *state_changed, pm_state_t *new_state)
{
        int temp_delta = temperature - last_temperature;
        if (temp_delta > 0 && temperature > curr_state->tj_thrshld_down) {
                *new_state = down_ladder_grx_state(state_changed);
                _DBG("%s, %d, down state to %d, due to temperature change from %d to %d\n",
                                __func__, __LINE__, *new_state, last_temperature, temperature);
        }
        else if (temp_delta < 0 && temperature < curr_state->tj_thrshld_up) {
                *new_state = up_ladder_grx_state(state_changed);
                _DBG("%s, %d, up state to %d, due to temperature change from %d to %d\n",
                                __func__, __LINE__, *new_state, last_temperature, temperature);
        }

        return 0;
}

static int gaming_next_state(const pm_app_state_t new_app_state, const int temperature, int *state_changed, pm_state_t *new_state)
{
        if (new_app_state != PM_APP_STATE_INVALID && new_app_state != curr_app_state) {
                if (new_app_state == PM_VIDEO_PLAY_BACK) {
                        *new_state = PLAY_TYPICAL;
                }
                else {
                        _ERR("%s, ERROR! unsupported APP mode %d change in PM_GAMING.\n", __func__, new_app_state);
                        return -1;
                }
                _DBG("%s, %d, change state to %d, due to app state change from %d to %d\n",
                                __func__, __LINE__, *new_state, (int)curr_app_state, (int)new_app_state);

                *state_changed = 1;
                curr_app_state = new_app_state;
        }
        else { //no app state change, check if temperature change requires state change.
                change_grx_state_if_temp_delta_cross_threshold(temperature, state_changed, new_state);
        }

        return 0;
}

static int sysfs_modify_power_state(const power_state_t *power_state)
{
        int fd = open(pm_sysfs_name, O_RDWR);
        if (fd < 0) {
                _ERR("open %s failed, error %s\n", pm_sysfs_name, strerror(errno));
                return -1;
        }

        ssize_t n = TEMP_FAILURE_RETRY(write(fd, (void *)power_state->ppm_name, strlen(power_state->ppm_name)));
        if (n != strlen(power_state->ppm_name)) {
                _ERR("Error, write %s to %s failed, error %s\n",power_state->ppm_name, pm_sysfs_name, strerror(errno));
                return -1;
        }
        close(fd);

        _DBG("%s, %d, write %s to %s success.\n", __func__, __LINE__, power_state->ppm_name, pm_sysfs_name);
        return 0;
}

static int base_do_state_change(const pm_state_t new_state)
{
        int i, ret;
        int num_of_state = sizeof(video_power_states) / sizeof(power_state_t);
        for (i = 0; i < num_of_state; i++) {
                if (video_power_states[i].self == new_state) {
                        ret = sysfs_modify_power_state(&video_power_states[i]);
                        if (ret == 0) {
                                curr_state  = (power_state_t *)&video_power_states[i];
                                _DBG("change to state %d\n", (int)curr_state->self);
                        }
                        return ret;
                }
        }
        num_of_state = sizeof(gaming_power_states) / sizeof(power_state_t);
        for (i = 0; i < num_of_state; i++) {
                if (gaming_power_states[i].self == new_state) {
                        ret = sysfs_modify_power_state(&gaming_power_states[i]);
                        if (ret == 0) {
                                curr_state  = (power_state_t *)&gaming_power_states[i];
                                _DBG("change to state %d\n", (int)curr_state->self);
                        }
                        return ret;
                }
        }

        _ERR("%s, ERROR! not supported pm state %d\n", __func__, (int)new_state);
        return -1;
}

static void poll_for_recv_msg(const int pipe_recv_fd, const int timeout, pm_app_state_t *new_app_state)
{
        int i = 0;
        struct pm_notification event = {0};
        unsigned char *buf = (unsigned char *)&event;
        struct pollfd fds[1];

        memset(fds, 0 , sizeof(fds));
        fds[0].fd = pipe_recv_fd;
        fds[0].events = POLLIN;

        int ret = TEMP_FAILURE_RETRY(poll(fds, 1, timeout));
        if (ret == 0)
                return;
        if (ret < 0) {
                _ERR("poll failed, error %s\n", strerror(errno));
                return;
        }
        if( fds[0].revents & POLLIN || fds[0].revents & POLLPRI) {
                do {
                        ssize_t n = TEMP_FAILURE_RETRY(read(fds[0].fd, (void *)(buf + i), sizeof(struct pm_notification) - i));
                        if (n < 0) {
                                _ERR("%s: read error. %s\n", __func__, strerror(errno));
                                return;
                        }
                        else if (n == 0) {
                                _DBG("%s, recevied EOF.\n", __func__);
                                return;
                        }
                        else {
                                i += n;
                        }
                } while(i < sizeof(struct pm_notification));

                *new_app_state = (pm_app_state_t)event.id;
                _DBG("recevied new app state %d\n",(int)*new_app_state);
        }
}

static void generate_event(struct pm_notification *event, const pm_state_t new_state)
{
        event->version = 0;
        event->extension = 0;
        event->type = (uint8_t)PM_NOTIFICATION_FROM_SRV;

        if ((int)new_state >= (int)GRX_MAX && (int)new_state <= (int)GRX_PW_SAVING )
                event->id = PM_GAMING;
        else if ((int)new_state >= (int)PLAY_MAX && (int)new_state <= (int)PLAY_PW_SAVING ) {
                event->id = PM_VIDEO_PLAY_BACK;
                if (new_state != PLAY_PW_SAVING ) {
                        event->param[0] = 3840;
                }
                else {
                        event->param[0] = 1920;
                }
        }

}

static void  notify_socket_srv_thread(const int pipe_send_fd, const pm_state_t new_state)
{
        struct pm_notification event = {0};

        generate_event(&event, new_state);
        ssize_t ret = TEMP_FAILURE_RETRY(write(pipe_send_fd, (void *)&event, sizeof(struct pm_notification)));
        if (ret != sizeof(struct pm_notification)) {
                _ERR("write failed, error %s\n", strerror(errno));
        }
}

static void read_tsensor(int *temperature)
{
        int fd = open(tsensor_dev_name, O_RDONLY);
        if (fd < 0) {
                _ERR("open %s failed, error %s\n", tsensor_dev_name, strerror(errno));
                return;
        }

        char buf[128] = {0};
        char *endptr;
        ssize_t n = TEMP_FAILURE_RETRY(read(fd, (void *)buf, sizeof(buf)));
        if (n > 0) {
                *temperature = strtol(buf, &endptr, 10)/1000;
                _DBG("current temperature %d\n", *temperature);
                if (last_temperature == 0)
                        last_temperature = *temperature;
        }
        close(fd);
}

static void init_state_machine(void)
{
        int i;
        int num_of_state = sizeof(gaming_power_states) / sizeof(power_state_t);
        for (i = 0; i < num_of_state; i++) {
                gaming_power_states[i].tj_thrshld_down = tj_thrshld_down;
                gaming_power_states[i].tj_thrshld_up = tj_thrshld_up;
                if (gaming_power_states[i].self == GRX_TYPICAL)
                        curr_state = (power_state_t *)&gaming_power_states[i];
        }

        curr_app_state = PM_GAMING;
        num_of_state = sizeof(video_power_states) / sizeof(power_state_t);
        for (i = 0; i < num_of_state; i++) {
                video_power_states[i].tj_thrshld_down = tj_thrshld_down;
                video_power_states[i].tj_thrshld_up = tj_thrshld_up;
        }

}

void *state_machine_func(void *data)
{
        int ret;
        int temperature = 0;
        int state_changed = 0;
        pm_state_t new_state = PM_STATE_INVALID;
        pm_app_state_t new_app_state = PM_APP_STATE_INVALID;

        init_state_machine();
        while (1) {
                poll_for_recv_msg(recv_msg_fd, READ_TSENSOR_INTERVAL*1000, &new_app_state);
                read_tsensor(&temperature);
                curr_state->next_state(new_app_state, temperature, &state_changed, &new_state);
                if (state_changed) {
                        ret = curr_state->do_state_change(new_state);
                        if (ret == 0) {
                                notify_socket_srv_thread(send_state_changed_fd, curr_state->self);
                        }
                }
                last_temperature = temperature;
                state_changed = 0;
                new_app_state = PM_APP_STATE_INVALID;
                new_state = PM_STATE_INVALID;
        }

        pthread_exit(NULL);
}
