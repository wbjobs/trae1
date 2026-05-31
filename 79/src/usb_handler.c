#include "usb_handler.h"
#include "logger.h"
#include "progress.h"
#include "compat.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define VENDOR_SPECIFIC_CMD  0x40
#define FLASH_CMD_ERASE      0x01
#define FLASH_CMD_WRITE      0x02
#define FLASH_CMD_READ       0x03
#define FLASH_CMD_VERIFY     0x04
#define FLASH_CMD_BACKUP     0x05
#define FLASH_CMD_GET_SN     0x06
#define FLASH_CMD_RESUME     0x07

#define HOTPLUG_EVENT_REMOVED  0
#define HOTPLUG_EVENT_INSERTED 1

extern volatile int g_running;

static libusb_context       *g_ctx = NULL;
static libusb_hotplug_callback_handle g_hotplug_handle = 0;
static usb_device_t         *g_device_list = NULL;
static int                   g_device_count = 0;
static hotplug_cb_t          g_global_hotplug_cb = NULL;
static void                 *g_global_hotplug_user_data = NULL;

int usb_init(void)
{
    int ret = libusb_init(&g_ctx);
    if (ret < 0) {
        LOG_ERROR("libusb_init failed: %s", libusb_strerror((enum libusb_error)ret));
        return -1;
    }
    libusb_set_option(g_ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING);
    return 0;
}

void usb_cleanup(void)
{
    if (g_hotplug_handle) {
        libusb_hotplug_deregister_callback(g_ctx, g_hotplug_handle);
        g_hotplug_handle = 0;
    }
    if (g_ctx) {
        libusb_exit(g_ctx);
        g_ctx = NULL;
    }
}

static int LIBUSB_CALL hotplug_callback(libusb_context *ctx,
                                         libusb_device *device,
                                         libusb_hotplug_event event,
                                         void *user_data)
{
    (void)ctx;
    (void)user_data;

    struct libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(device, &desc) < 0) {
        return 0;
    }

    uint8_t bus_num  = libusb_get_bus_number(device);
    uint8_t dev_addr = libusb_get_device_address(device);

    if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT) {
        for (int i = 0; i < g_device_count; i++) {
            if (g_device_list[i].device &&
                libusb_get_bus_number(g_device_list[i].device) == bus_num &&
                libusb_get_device_address(g_device_list[i].device) == dev_addr) {

                pthread_mutex_lock(&g_device_list[i].state_mutex);
                if (g_device_list[i].state == DEV_STATE_CONNECTED ||
                    g_device_list[i].state == DEV_STATE_RECONNECTING) {
                    g_device_list[i].state = DEV_STATE_DISCONNECTED;
                }
                pthread_mutex_unlock(&g_device_list[i].state_mutex);

                LOG_WARN("Device %d (SN=%s) disconnected unexpectedly", i, g_device_list[i].sn);

                if (g_global_hotplug_cb) {
                    g_global_hotplug_cb(i, HOTPLUG_EVENT_REMOVED, g_global_hotplug_user_data);
                }
                break;
            }
        }
    } else if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) {
        if (g_global_hotplug_cb) {
            g_global_hotplug_cb(-1, HOTPLUG_EVENT_INSERTED, g_global_hotplug_user_data);
        }
    }

    return 0;
}

int usb_register_hotplug(uint16_t vid, uint16_t pid, hotplug_cb_t cb, void *user_data)
{
    if (!g_ctx) return -1;

    if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
        LOG_WARN("libusb hotplug not supported on this platform");
        return -1;
    }

    g_global_hotplug_cb = cb;
    g_global_hotplug_user_data = user_data;

    int ret = libusb_hotplug_register_callback(
        g_ctx,
        (libusb_hotplug_event)(LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
                               LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT),
        LIBUSB_HOTPLUG_NO_FLAGS,
        vid, pid,
        LIBUSB_HOTPLUG_MATCH_ANY,
        hotplug_callback,
        NULL,
        &g_hotplug_handle
    );

    if (ret < 0) {
        LOG_ERROR("Failed to register hotplug callback: %s",
                  libusb_strerror((enum libusb_error)ret));
        return -1;
    }

    LOG_INFO("Hotplug callback registered for VID=%04X PID=%04X", vid, pid);
    return 0;
}

void usb_unregister_hotplug(void)
{
    if (g_ctx && g_hotplug_handle) {
        libusb_hotplug_deregister_callback(g_ctx, g_hotplug_handle);
        g_hotplug_handle = 0;
    }
    g_global_hotplug_cb = NULL;
}

void usb_hotpoll_events(void)
{
    if (!g_ctx) return;
    struct timeval tv = { 0, 100000 };
    libusb_handle_events_timeout(g_ctx, &tv);
}

static int find_endpoints(libusb_device *dev, uint8_t *ep_in, uint8_t *ep_out)
{
    struct libusb_config_descriptor *cfg;
    int ret = libusb_get_active_config_descriptor(dev, &cfg);
    if (ret < 0) return -1;

    *ep_in = 0;
    *ep_out = 0;

    for (int i = 0; i < cfg->bNumInterfaces; i++) {
        const struct libusb_interface *iface = &cfg->interface[i];
        for (int j = 0; j < iface->num_altsetting; j++) {
            const struct libusb_interface_descriptor *altset = &iface->altsetting[j];
            for (int k = 0; k < altset->bNumEndpoints; k++) {
                const struct libusb_endpoint_descriptor *ep = &altset->endpoint[k];
                if ((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_BULK) {
                    if (ep->bEndpointAddress & LIBUSB_ENDPOINT_IN) {
                        if (!*ep_in) *ep_in = ep->bEndpointAddress;
                    } else {
                        if (!*ep_out) *ep_out = ep->bEndpointAddress;
                    }
                }
            }
        }
    }

    libusb_free_config_descriptor(cfg);
    return (*ep_in && *ep_out) ? 0 : -1;
}

int usb_enumerate(uint16_t vid, uint16_t pid, usb_device_t *devices, int max_count)
{
    if (!g_ctx) {
        LOG_ERROR("USB context not initialized");
        return -1;
    }

    libusb_device **devs;
    ssize_t count = libusb_get_device_list(g_ctx, &devs);
    if (count < 0) {
        LOG_ERROR("libusb_get_device_list failed: %s", libusb_strerror((enum libusb_error)count));
        return -1;
    }

    int found = 0;
    for (ssize_t i = 0; i < count && found < max_count; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(devs[i], &desc) < 0) continue;

        if (desc.idVendor != vid || desc.idProduct != pid) continue;

        uint8_t ep_in = 0, ep_out = 0;
        if (find_endpoints(devs[i], &ep_in, &ep_out) < 0) {
            LOG_WARN("Device found but no bulk endpoints found, skipping");
            continue;
        }

        memset(&devices[found], 0, sizeof(usb_device_t));
        devices[found].device    = devs[i];
        devices[found].handle    = NULL;
        devices[found].ep_in     = ep_in;
        devices[found].ep_out    = ep_out;
        devices[found].interface = 0;
        devices[found].vid       = vid;
        devices[found].pid       = pid;
        devices[found].index     = found;
        devices[found].state     = DEV_STATE_CONNECTED;
        pthread_mutex_init(&devices[found].state_mutex, NULL);

        libusb_ref_device(devs[i]);
        found++;
    }

    g_device_list = devices;
    g_device_count = found;

    libusb_free_device_list(devs, 1);
    LOG_INFO("Found %d device(s) with VID=%04X PID=%04X", found, vid, pid);
    return found;
}

int usb_open_device(usb_device_t *dev)
{
    int ret = libusb_open(dev->device, &dev->handle);
    if (ret < 0) {
        LOG_ERROR("libusb_open failed for device %d: %s", dev->index,
                  libusb_strerror((enum libusb_error)ret));
        return -1;
    }

    ret = libusb_detach_kernel_driver(dev->handle, dev->interface);
    if (ret < 0 && ret != LIBUSB_ERROR_NOT_FOUND) {
        LOG_WARN("detach_kernel_driver failed: %s", libusb_strerror((enum libusb_error)ret));
    }

    ret = libusb_claim_interface(dev->handle, dev->interface);
    if (ret < 0) {
        LOG_ERROR("claim_interface failed: %s", libusb_strerror((enum libusb_error)ret));
        libusb_close(dev->handle);
        dev->handle = NULL;
        return -1;
    }

    return 0;
}

void usb_close_device(usb_device_t *dev)
{
    if (dev->handle) {
        libusb_release_interface(dev->handle, dev->interface);
        libusb_attach_kernel_driver(dev->handle, dev->interface);
        libusb_close(dev->handle);
        dev->handle = NULL;
    }
    if (dev->device) {
        libusb_unref_device(dev->device);
        dev->device = NULL;
    }
    pthread_mutex_destroy(&dev->state_mutex);
}

int usb_is_connected(usb_device_t *dev)
{
    if (!dev) return 0;
    device_state_t state;
    pthread_mutex_lock(&dev->state_mutex);
    state = dev->state;
    pthread_mutex_unlock(&dev->state_mutex);
    return (state == DEV_STATE_CONNECTED);
}

device_state_t usb_get_state(usb_device_t *dev)
{
    if (!dev) return DEV_STATE_FAILED;
    device_state_t state;
    pthread_mutex_lock(&dev->state_mutex);
    state = dev->state;
    pthread_mutex_unlock(&dev->state_mutex);
    return state;
}

void usb_set_state(usb_device_t *dev, device_state_t state)
{
    if (!dev) return;
    pthread_mutex_lock(&dev->state_mutex);
    dev->state = state;
    pthread_mutex_unlock(&dev->state_mutex);
}

static int try_find_and_open_device(usb_device_t *dev)
{
    if (!g_ctx) return -1;

    libusb_device **devs;
    ssize_t count = libusb_get_device_list(g_ctx, &devs);
    if (count < 0) return -1;

    int found = -1;
    for (ssize_t i = 0; i < count; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(devs[i], &desc) < 0) continue;

        if (desc.idVendor != dev->vid || desc.idProduct != dev->pid) continue;

        uint8_t ep_in = 0, ep_out = 0;
        if (find_endpoints(devs[i], &ep_in, &ep_out) < 0) continue;

        if (ep_in != dev->ep_in || ep_out != dev->ep_out) continue;

        libusb_device_handle *handle = NULL;
        if (libusb_open(devs[i], &handle) < 0) continue;

        libusb_detach_kernel_driver(handle, dev->interface);
        if (libusb_claim_interface(handle, dev->interface) < 0) {
            libusb_close(handle);
            continue;
        }

        if (dev->device) {
            libusb_unref_device(dev->device);
        }
        dev->device = devs[i];
        libusb_ref_device(devs[i]);
        dev->handle = handle;

        found = 0;
        break;
    }

    libusb_free_device_list(devs, 1);
    return found;
}

int usb_wait_reconnect(usb_device_t *dev, int max_attempts, int interval_ms)
{
    if (!dev) return -1;

    LOG_INFO("Attempting to reconnect device %d (SN=%s)...", dev->index, dev->sn);

    usb_set_state(dev, DEV_STATE_RECONNECTING);

    for (int attempt = 0; attempt < max_attempts; attempt++) {
        dev->reconnect_count = attempt + 1;

        usb_hotpoll_events();

        if (try_find_and_open_device(dev) == 0) {
            char new_sn[MAX_SN_LENGTH];
            if (usb_get_sn(dev, new_sn, sizeof(new_sn)) == 0) {
                if (strcmp(new_sn, dev->sn) == 0) {
                    LOG_INFO("Device %d reconnected successfully (attempt %d)",
                             dev->index, attempt + 1);
                    usb_set_state(dev, DEV_STATE_CONNECTED);
                    dev->needs_resume = 1;
                    return 0;
                } else {
                    LOG_INFO("Reconnected device SN mismatch: expected %s, got %s",
                             dev->sn, new_sn);
                    libusb_release_interface(dev->handle, dev->interface);
                    libusb_close(dev->handle);
                    dev->handle = NULL;
                }
            } else {
                LOG_INFO("Device reconnected but cannot read SN (attempt %d)", attempt + 1);
                usb_set_state(dev, DEV_STATE_CONNECTED);
                dev->needs_resume = 1;
                return 0;
            }
        }

        if (attempt < max_attempts - 1) {
#ifdef _WIN32
            Sleep((DWORD)interval_ms);
#else
            usleep((useconds_t)(interval_ms * 1000));
#endif
        }
    }

    LOG_ERROR("Device %d reconnection failed after %d attempts", dev->index, max_attempts);
    usb_set_state(dev, DEV_STATE_FAILED);
    return -1;
}

static int ctrl_transfer(usb_device_t *dev, uint8_t cmd, uint16_t value,
                         uint8_t *data, uint16_t length)
{
    if (!dev->handle) return LIBUSB_ERROR_NO_DEVICE;
    return libusb_control_transfer(
        dev->handle,
        VENDOR_SPECIFIC_CMD,
        cmd,
        value,
        0,
        data,
        length,
        DEFAULT_TIMEOUT
    );
}

static int bulk_write(usb_device_t *dev, const uint8_t *data, size_t len)
{
    if (!dev->handle) return LIBUSB_ERROR_NO_DEVICE;

    int      transferred;
    size_t   offset = 0;

    while (offset < len) {
        if (usb_get_state(dev) != DEV_STATE_CONNECTED) {
            return LIBUSB_ERROR_NO_DEVICE;
        }
        size_t chunk = (len - offset > BULK_EP_SIZE) ? BULK_EP_SIZE : (len - offset);
        int ret = libusb_bulk_transfer(dev->handle, dev->ep_out,
                                       (uint8_t *)(data + offset),
                                       (int)chunk, &transferred, DEFAULT_TIMEOUT);
        if (ret < 0) return ret;
        offset += (size_t)transferred;
    }
    return 0;
}

static int bulk_read(usb_device_t *dev, uint8_t *data, size_t len)
{
    if (!dev->handle) return LIBUSB_ERROR_NO_DEVICE;

    int      transferred;
    size_t   offset = 0;

    while (offset < len) {
        if (usb_get_state(dev) != DEV_STATE_CONNECTED) {
            return LIBUSB_ERROR_NO_DEVICE;
        }
        size_t chunk = (len - offset > BULK_EP_SIZE) ? BULK_EP_SIZE : (len - offset);
        int ret = libusb_bulk_transfer(dev->handle, dev->ep_in,
                                       data + offset,
                                       (int)chunk, &transferred, DEFAULT_TIMEOUT);
        if (ret < 0) return ret;
        offset += (size_t)transferred;
    }
    return 0;
}

int usb_erase(usb_device_t *dev, progress_cb_t cb)
{
    if (usb_get_state(dev) != DEV_STATE_CONNECTED) return LIBUSB_ERROR_NO_DEVICE;

    if (cb) cb(dev->index, OP_ERASE, 0, 100);

    uint8_t resp[64];
    int ret = ctrl_transfer(dev, FLASH_CMD_ERASE, 0, resp, sizeof(resp));

    if (ret < 0) {
        LOG_ERROR("Erase failed for device %d: %s", dev->index,
                  libusb_strerror((enum libusb_error)ret));
        if (ret == LIBUSB_ERROR_NO_DEVICE) {
            usb_set_state(dev, DEV_STATE_DISCONNECTED);
        }
        return -1;
    }

    for (int i = 1; i <= 100 && g_running; i++) {
        if (cb) cb(dev->index, OP_ERASE, (uint32_t)i, 100);
    }

    dev->write_offset = 0;
    dev->verify_offset = 0;
    return 0;
}

int usb_write(usb_device_t *dev, const uint8_t *data, size_t size, progress_cb_t cb)
{
    if (size == 0) return 0;
    if (usb_get_state(dev) != DEV_STATE_CONNECTED) return LIBUSB_ERROR_NO_DEVICE;

    if (cb) cb(dev->index, OP_WRITE, 0, (uint32_t)size);

    uint8_t hdr[8];
    hdr[0] = (uint8_t)(size & 0xFF);
    hdr[1] = (uint8_t)((size >> 8) & 0xFF);
    hdr[2] = (uint8_t)((size >> 16) & 0xFF);
    hdr[3] = (uint8_t)((size >> 24) & 0xFF);

    int ret = ctrl_transfer(dev, FLASH_CMD_WRITE, 0, hdr, sizeof(hdr));
    if (ret < 0) {
        LOG_ERROR("Write header failed: %s", libusb_strerror((enum libusb_error)ret));
        if (ret == LIBUSB_ERROR_NO_DEVICE) usb_set_state(dev, DEV_STATE_DISCONNECTED);
        return -1;
    }

    size_t offset = 0;
    while (offset < size) {
        if (usb_get_state(dev) != DEV_STATE_CONNECTED) {
            dev->write_offset = (uint32_t)offset;
            return LIBUSB_ERROR_NO_DEVICE;
        }

        size_t chunk = (size - offset > BULK_EP_SIZE) ? BULK_EP_SIZE : (size - offset);
        ret = bulk_write(dev, data + offset, chunk);
        if (ret < 0) {
            LOG_ERROR("Bulk write failed at offset %zu: %s", offset,
                      libusb_strerror((enum libusb_error)ret));
            dev->write_offset = (uint32_t)offset;
            if (ret == LIBUSB_ERROR_NO_DEVICE) usb_set_state(dev, DEV_STATE_DISCONNECTED);
            return -1;
        }
        offset += chunk;
        dev->write_offset = (uint32_t)offset;
        if (cb) cb(dev->index, OP_WRITE, (uint32_t)offset, (uint32_t)size);
    }

    return 0;
}

int usb_write_resume(usb_device_t *dev, const uint8_t *data, size_t size,
                     uint32_t resume_offset, progress_cb_t cb)
{
    if (usb_get_state(dev) != DEV_STATE_CONNECTED) return LIBUSB_ERROR_NO_DEVICE;

    if (resume_offset >= size) {
        if (cb) cb(dev->index, OP_WRITE, (uint32_t)size, (uint32_t)size);
        return 0;
    }

    LOG_INFO("Resuming write at offset %u/%zu for device %d", resume_offset, size, dev->index);

    if (cb) cb(dev->index, OP_WRITE, resume_offset, (uint32_t)size);

    uint8_t hdr[12];
    hdr[0] = (uint8_t)(FLASH_CMD_RESUME);
    hdr[1] = (uint8_t)(size & 0xFF);
    hdr[2] = (uint8_t)((size >> 8) & 0xFF);
    hdr[3] = (uint8_t)((size >> 16) & 0xFF);
    hdr[4] = (uint8_t)((size >> 24) & 0xFF);
    hdr[5] = (uint8_t)(resume_offset & 0xFF);
    hdr[6] = (uint8_t)((resume_offset >> 8) & 0xFF);
    hdr[7] = (uint8_t)((resume_offset >> 16) & 0xFF);
    hdr[8] = (uint8_t)((resume_offset >> 24) & 0xFF);

    int ret = ctrl_transfer(dev, FLASH_CMD_WRITE, 0, hdr, sizeof(hdr));
    if (ret < 0) {
        LOG_ERROR("Resume write header failed: %s", libusb_strerror((enum libusb_error)ret));
        if (ret == LIBUSB_ERROR_NO_DEVICE) usb_set_state(dev, DEV_STATE_DISCONNECTED);
        return -1;
    }

    size_t offset = resume_offset;
    while (offset < size) {
        if (usb_get_state(dev) != DEV_STATE_CONNECTED) {
            dev->write_offset = (uint32_t)offset;
            return LIBUSB_ERROR_NO_DEVICE;
        }

        size_t chunk = (size - offset > BULK_EP_SIZE) ? BULK_EP_SIZE : (size - offset);
        ret = bulk_write(dev, data + offset, chunk);
        if (ret < 0) {
            LOG_ERROR("Resume write failed at offset %zu: %s", offset,
                      libusb_strerror((enum libusb_error)ret));
            dev->write_offset = (uint32_t)offset;
            if (ret == LIBUSB_ERROR_NO_DEVICE) usb_set_state(dev, DEV_STATE_DISCONNECTED);
            return -1;
        }
        offset += chunk;
        dev->write_offset = (uint32_t)offset;
        if (cb) cb(dev->index, OP_WRITE, (uint32_t)offset, (uint32_t)size);
    }

    return 0;
}

int usb_read(usb_device_t *dev, uint8_t *data, size_t size, progress_cb_t cb)
{
    if (size == 0) return 0;
    if (usb_get_state(dev) != DEV_STATE_CONNECTED) return LIBUSB_ERROR_NO_DEVICE;

    uint8_t hdr[8];
    hdr[0] = (uint8_t)(size & 0xFF);
    hdr[1] = (uint8_t)((size >> 8) & 0xFF);
    hdr[2] = (uint8_t)((size >> 16) & 0xFF);
    hdr[3] = (uint8_t)((size >> 24) & 0xFF);

    int ret = ctrl_transfer(dev, FLASH_CMD_READ, 0, hdr, sizeof(hdr));
    if (ret < 0) {
        LOG_ERROR("Read header failed: %s", libusb_strerror((enum libusb_error)ret));
        if (ret == LIBUSB_ERROR_NO_DEVICE) usb_set_state(dev, DEV_STATE_DISCONNECTED);
        return -1;
    }

    size_t offset = 0;
    while (offset < size) {
        if (usb_get_state(dev) != DEV_STATE_CONNECTED) {
            return LIBUSB_ERROR_NO_DEVICE;
        }

        size_t chunk = (size - offset > BULK_EP_SIZE) ? BULK_EP_SIZE : (size - offset);
        ret = bulk_read(dev, data + offset, chunk);
        if (ret < 0) {
            LOG_ERROR("Bulk read failed at offset %zu: %s", offset,
                      libusb_strerror((enum libusb_error)ret));
            if (ret == LIBUSB_ERROR_NO_DEVICE) usb_set_state(dev, DEV_STATE_DISCONNECTED);
            return -1;
        }
        offset += chunk;
        if (cb) cb(dev->index, OP_READ, (uint32_t)offset, (uint32_t)size);
    }

    return 0;
}

int usb_verify(usb_device_t *dev, const uint8_t *expected, size_t size, progress_cb_t cb)
{
    if (size == 0) return 0;
    if (usb_get_state(dev) != DEV_STATE_CONNECTED) return LIBUSB_ERROR_NO_DEVICE;

    if (cb) cb(dev->index, OP_VERIFY, 0, (uint32_t)size);

    uint8_t *buffer = (uint8_t *)malloc(size);
    if (!buffer) {
        LOG_ERROR("Memory allocation failed for verify buffer");
        return -1;
    }

    int ret = usb_read(dev, buffer, size, cb);
    if (ret < 0) {
        free(buffer);
        return ret;
    }

    if (memcmp(expected, buffer, size) != 0) {
        LOG_ERROR("Verification failed for device %d", dev->index);
        free(buffer);
        return -2;
    }

    free(buffer);
    dev->verify_offset = (uint32_t)size;
    if (cb) cb(dev->index, OP_VERIFY, (uint32_t)size, (uint32_t)size);
    return 0;
}

int usb_verify_resume(usb_device_t *dev, const uint8_t *expected, size_t size,
                      uint32_t resume_offset, progress_cb_t cb)
{
    if (usb_get_state(dev) != DEV_STATE_CONNECTED) return LIBUSB_ERROR_NO_DEVICE;

    if (resume_offset >= size) {
        if (cb) cb(dev->index, OP_VERIFY, (uint32_t)size, (uint32_t)size);
        return 0;
    }

    LOG_INFO("Resuming verify at offset %u/%zu for device %d", resume_offset, size, dev->index);

    if (cb) cb(dev->index, OP_VERIFY, resume_offset, (uint32_t)size);

    size_t remaining = size - resume_offset;
    uint8_t *buffer = (uint8_t *)malloc(remaining);
    if (!buffer) {
        LOG_ERROR("Memory allocation failed for verify buffer");
        return -1;
    }

    uint8_t hdr[12];
    hdr[0] = (uint8_t)(FLASH_CMD_RESUME);
    hdr[1] = (uint8_t)(remaining & 0xFF);
    hdr[2] = (uint8_t)((remaining >> 8) & 0xFF);
    hdr[3] = (uint8_t)((remaining >> 16) & 0xFF);
    hdr[4] = (uint8_t)((remaining >> 24) & 0xFF);
    hdr[5] = (uint8_t)(resume_offset & 0xFF);
    hdr[6] = (uint8_t)((resume_offset >> 8) & 0xFF);
    hdr[7] = (uint8_t)((resume_offset >> 16) & 0xFF);
    hdr[8] = (uint8_t)((resume_offset >> 24) & 0xFF);

    int ret = ctrl_transfer(dev, FLASH_CMD_READ, 0, hdr, sizeof(hdr));
    if (ret < 0) {
        LOG_ERROR("Resume verify header failed: %s", libusb_strerror((enum libusb_error)ret));
        free(buffer);
        if (ret == LIBUSB_ERROR_NO_DEVICE) usb_set_state(dev, DEV_STATE_DISCONNECTED);
        return -1;
    }

    size_t offset = 0;
    while (offset < remaining) {
        if (usb_get_state(dev) != DEV_STATE_CONNECTED) {
            dev->verify_offset = resume_offset + (uint32_t)offset;
            free(buffer);
            return LIBUSB_ERROR_NO_DEVICE;
        }

        size_t chunk = (remaining - offset > BULK_EP_SIZE) ? BULK_EP_SIZE : (remaining - offset);
        ret = bulk_read(dev, buffer + offset, chunk);
        if (ret < 0) {
            LOG_ERROR("Resume verify read failed at offset %zu: %s", offset,
                      libusb_strerror((enum libusb_error)ret));
            dev->verify_offset = resume_offset + (uint32_t)offset;
            free(buffer);
            if (ret == LIBUSB_ERROR_NO_DEVICE) usb_set_state(dev, DEV_STATE_DISCONNECTED);
            return -1;
        }
        offset += chunk;
        dev->verify_offset = resume_offset + (uint32_t)offset;
        if (cb) cb(dev->index, OP_VERIFY, resume_offset + (uint32_t)offset, (uint32_t)size);
    }

    if (memcmp(expected + resume_offset, buffer, remaining) != 0) {
        LOG_ERROR("Resume verification failed for device %d", dev->index);
        free(buffer);
        return -2;
    }

    free(buffer);
    return 0;
}

int usb_backup(usb_device_t *dev, uint8_t *data, size_t max_size,
               size_t *out_size, progress_cb_t cb)
{
    if (usb_get_state(dev) != DEV_STATE_CONNECTED) return LIBUSB_ERROR_NO_DEVICE;

    if (cb) cb(dev->index, OP_BACKUP, 0, (uint32_t)max_size);

    uint8_t hdr[8];
    int ret = ctrl_transfer(dev, FLASH_CMD_BACKUP, 0, hdr, sizeof(hdr));
    if (ret < 0) {
        LOG_ERROR("Backup command failed: %s", libusb_strerror((enum libusb_error)ret));
        if (ret == LIBUSB_ERROR_NO_DEVICE) usb_set_state(dev, DEV_STATE_DISCONNECTED);
        return -1;
    }

    uint32_t flash_size = (uint32_t)hdr[0] |
                          ((uint32_t)hdr[1] << 8) |
                          ((uint32_t)hdr[2] << 16) |
                          ((uint32_t)hdr[3] << 24);

    if (flash_size > max_size) flash_size = (uint32_t)max_size;
    if (flash_size == 0) flash_size = (uint32_t)max_size;

    ret = usb_read(dev, data, flash_size, cb);
    if (ret < 0) return -1;

    if (out_size) *out_size = flash_size;
    return 0;
}

int usb_get_sn(usb_device_t *dev, char *sn, size_t sn_size)
{
    if (!dev->handle) {
        if (dev->sn[0]) {
            strncpy(sn, dev->sn, sn_size - 1);
            sn[sn_size - 1] = '\0';
            return 0;
        }
        return -1;
    }

    struct libusb_device_descriptor desc;
    int ret = libusb_get_device_descriptor(dev->device, &desc);
    if (ret < 0) return -1;

    if (desc.iSerialNumber > 0) {
        unsigned char data[256];
        int len = libusb_get_string_descriptor_ascii(
            dev->handle, desc.iSerialNumber, data, sizeof(data));
        if (len > 0) {
            strncpy(sn, (char *)data, sn_size - 1);
            sn[sn_size - 1] = '\0';
            return 0;
        }
    }

    uint8_t resp[64];
    ret = ctrl_transfer(dev, FLASH_CMD_GET_SN, 0, resp, sizeof(resp));
    if (ret >= 0 && resp[0] > 0) {
        size_t len = (resp[0] < sn_size) ? resp[0] : sn_size - 1;
        memcpy(sn, resp + 1, len);
        sn[len] = '\0';
        return 0;
    }

    sprintf_s(sn, sn_size, "DEV%03d", dev->index);
    return 0;
}

uint32_t usb_get_write_offset(usb_device_t *dev)
{
    return dev ? dev->write_offset : 0;
}

uint32_t usb_get_verify_offset(usb_device_t *dev)
{
    return dev ? dev->verify_offset : 0;
}

void usb_set_write_offset(usb_device_t *dev, uint32_t offset)
{
    if (dev) dev->write_offset = offset;
}

void usb_set_verify_offset(usb_device_t *dev, uint32_t offset)
{
    if (dev) dev->verify_offset = offset;
}
