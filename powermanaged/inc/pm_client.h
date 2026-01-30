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
//! \file pm_client.h
//!
//! \brief This file defined client API for connect to mrvl power management
//!     service.
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

#ifndef PM_CLIENT_H
#define PM_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define NUM_STATE (8)
typedef enum pm_state_t {
        GRX_MAX = 1,
        GRX_TYPICAL,
        GRX_MEDIAN,
        GRX_LOW,
        GRX_PW_SAVING,
        PLAY_MAX,
        PLAY_TYPICAL,
        PLAY_MEDIAN,
        PLAY_LOW,
        PLAY_PAUSE,
        PLAY_PW_SAVING,
        PM_STATE_INVALID,
}pm_state_t;

typedef enum pm_app_state_t {
        PM_VIDEO_PLAY_BACK = 1,
        PM_VIDEO_PAUSE,
        PM_VIDEO_RESUME,
        PM_VIDEO_STOP,
        PM_GAMING,
        PM_APP_STATE_INVALID,
}pm_app_state_t;

typedef enum pm_event_type_t {
        PM_NOTIFICATION_FROM_SRV = 1,
        PM_NOTIFICATION_TO_SRV,
        PM_EVENT_TYPE_INVALID,
} pm_event_type_t;

struct pm_notification {
        uint8_t version;
        uint8_t extension;
        uint8_t type;
        uint8_t reserved[1];
        uint32_t id;
        uint32_t param[2];
        uint8_t data[48];
};

#define MAX_MESSAGE_LEN 128
#define PM_SERVICE_SOCKET_NAME "/dev/socket/mrvl_power_management"

#ifdef _PM_DEBUG_ON_
#define _DBG(fmt, ...)do{ \
        printf("%s, %d " fmt,__func__, __LINE__, ##__VA_ARGS__); \
} while (0);
#else
#define _DBG(...)
#endif //_PM_DEBUG_ON_

#define _HAS_STD_ERR_
#ifdef _HAS_STD_ERR_
#define _ERR(fmt, ...)do{ \
        fprintf(stderr, "%s, %d " fmt,__func__, __LINE__, ##__VA_ARGS__); \
} while (0);
#else
#define _ERR(...)
#endif //_HAS_STD_ERR_

typedef int (* listener_callback)(struct pm_notification *event);

/*******************************************************************************
\fn void* pm_app_register_listener(struct pm_notification *event, listener_callback callback)

\brief This API let client process register a listener to pm_server, when PM event happens in
        pm server side, the callback function will be called, to show a event
        (in seperate thread context as the caller of this function).

\param struct pm_notification *event, allocated by caller, will contain received
        event when callback function got called. event could be NULL if server
        closed.
       listener_callback callback

\return a context pointer if success. NULL if fails.
*******************************************************************************/
void* pm_app_register_listener(struct pm_notification *event, listener_callback callback);

/*******************************************************************************
\fn int pm_app_unregister_listener(void *context)

\brief This API unregister the listener callback.

\param void *context, from return of pm_app_register_listener

\return 0 if success. -1 if fails.
*******************************************************************************/
int pm_app_unregister_listener(void *context);

/*******************************************************************************
\fn int pm_client_send_notice(const struct pm_notification *notice, const int timeout)

\brief This API send a notice to pm server.

\param const struct pm_notification *notice, message from app to pm server.
        const int timeout, send timeout in seconds.

\return 0 if success. -1 if fails.
*******************************************************************************/
int pm_client_send_notice(const struct pm_notification *notice, const int timeout);

/*******************************************************************************
\fn int pm_client_send_notice_async(const struct pm_notification *notice)

\brief This API send a notice to pm server, without block sender.

\param const struct pm_notification *notice, message from app to pm server.

\return 0 if success. -1 if fails.
*******************************************************************************/
int pm_client_send_notice_async(const struct pm_notification *notice);

#ifdef __cplusplus
}
#endif
#endif // PM_CLIENT_H
