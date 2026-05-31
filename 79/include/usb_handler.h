#ifndef USB_HANDLER_H
#define USB_HANDLER_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <libusb-1.0/libusb.h>

#define MAX_USB_DEVICES   10
#define MAX_SN_LENGTH     64
#define DEFAULT_TIMEOUT   2000
#define BULK_EP_SIZE      64
#define MAX_RECONNECT     5
#define RECONNECT_INTERVAL 1000

typedef enum {
    DEV_STATE_CONNECTED = 0,
    DEV_STATE_DISCONNECTED,
    DEV_STATE_RECONNECTING,
    DEV_STATE_FAILED,
    DEV_STATE_DONE
} device_state_t;

typedef void (*progress_cb_t)(int index, int op, uint32_t current, uint32_t total);
typedef void (*hotplug_cb_t)(int index, int event, void *user_data);

typedef struct {
    libusb_device_handle *handle;
    libusb_device        *device;
    uint8_t               ep_in;
    uint8_t               ep_out;
    int                   interface;
    char                  sn[MAX_SN_LENGTH];
    uint16_t              vid;
    uint16_t              pid;
    int                   index;
    volatile device_state_t state;
    pthread_mutex_t       state_mutex;
    int                   reconnect_count;
    uint32_t              write_offset;
    uint32_t              verify_offset;
    int                   needs_resume;
    hotplug_cb_t          hotplug_cb;
    void                 *hotplug_user_data;
} usb_device_t;

int  usb_init(void);
void usb_cleanup(void);

int  usb_enumerate(uint16_t vid, uint16_t pid, usb_device_t *devices, int max_count);
int  usb_open_device(usb_device_t *dev);
void usb_close_device(usb_device_t *dev);

int  usb_register_hotplug(uint16_t vid, uint16_t pid, hotplug_cb_t cb, void *user_data);
void usb_unregister_hotplug(void);
void usb_hotpoll_events(void);

int  usb_is_connected(usb_device_t *dev);
device_state_t usb_get_state(usb_device_t *dev);
int  usb_wait_reconnect(usb_device_t *dev, int max_attempts, int interval_ms);
void usb_set_state(usb_device_t *dev, device_state_t state);

int  usb_erase(usb_device_t *dev, progress_cb_t cb);
int  usb_write(usb_device_t *dev, const uint8_t *data, size_t size, progress_cb_t cb);
int  usb_write_resume(usb_device_t *dev, const uint8_t *data, size_t size,
                      uint32_t resume_offset, progress_cb_t cb);
int  usb_read(usb_device_t *dev, uint8_t *data, size_t size, progress_cb_t cb);
int  usb_verify(usb_device_t *dev, const uint8_t *expected, size_t size, progress_cb_t cb);
int  usb_verify_resume(usb_device_t *dev, const uint8_t *expected, size_t size,
                       uint32_t resume_offset, progress_cb_t cb);
int  usb_backup(usb_device_t *dev, uint8_t *data, size_t max_size, size_t *out_size, progress_cb_t cb);

int  usb_get_sn(usb_device_t *dev, char *sn, size_t sn_size);

uint32_t usb_get_write_offset(usb_device_t *dev);
uint32_t usb_get_verify_offset(usb_device_t *dev);
void     usb_set_write_offset(usb_device_t *dev, uint32_t offset);
void     usb_set_verify_offset(usb_device_t *dev, uint32_t offset);

#endif
