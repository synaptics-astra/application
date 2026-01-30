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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include "pm_client.h"

static int running = 1;
static volatile int event_arrived = 0;

void int_handler(int signal)
{
        running = 0;
}

int my_listener_callback(struct pm_notification *event)
{
        if (event == NULL) {
                printf("listener thread exited.\n");
                running = 0;
                return 0;
        }
        printf("%s, %d received event dir %d, id %d\n\n", __func__, __LINE__, event->type, event->id);
        __sync_fetch_and_or(&event_arrived, 1);
        return 0;
}

int main(int argc, char **argv)
{
        struct pm_notification event = {0};

        struct sigaction act;
        act.sa_handler = int_handler;
        sigaction(SIGINT, &act, NULL);


        void *ctx = pm_app_register_listener(&event, &my_listener_callback);
        if (ctx == NULL) {
                printf("pm_app_register_listener failed\n");
                return EXIT_FAILURE;
        }

        while (running) {
                if (__sync_bool_compare_and_swap(&event_arrived, 1, 0)) {
                        printf("received power management service broadcast, state changed to %d, resolution is %d\n", event.id, event.param[0]);
                }
                sleep(1);
        }

        pm_app_unregister_listener(ctx);
        printf("%s exited.\n", argv[0]);
        return EXIT_SUCCESS;
}
