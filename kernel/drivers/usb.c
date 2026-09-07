/* =============================================================================
 * ZeruX OS — Generic USB Core Implementation
 * File: kernel/drivers/usb.c
 * =============================================================================
 *
 * Bu dosya HİÇBİR donanım register'ına dokunmaz. Sadece USB 2.0 Spec'in
 * "Chapter 9: USB Device Framework" bölümünde tanımlı standart enumeration
 * sırasını, HCD soyutlaması (usb_hcd_ops_t) üzerinden yürütür.
 *
 * Enumeration akışı (bkz. usb_enumerate_device):
 *   1) Adres 0'da 8 baytlık kısmi GET_DESCRIPTOR(DEVICE) -> gerçek EP0
 *      max paket boyutunu öğren.
 *   2) SET_ADDRESS ile cihaza kalıcı bir adres ata.
 *   3) Yeni adresten tam (18 bayt) GET_DESCRIPTOR(DEVICE).
 *   4) GET_DESCRIPTOR(CONFIGURATION) — önce 9 baytlık header (wTotalLength
 *      öğrenmek için), sonra tam boy.
 *   5) Config açıklayıcısını ayrıştırıp bInterfaceClass == HID olan ilk
 *      interface + onun interrupt-IN endpoint'ini bul.
 *   6) SET_CONFIGURATION.
 *   7) HID sınıfına özel SET_PROTOCOL(Boot Protocol) — Report Descriptor
 *      ayrıştırma karmaşıklığından kaçınmak için bilinçli bir tasarım
 *      kararı: neredeyse tüm USB fareler Boot Protocol'ü destekler ve bu
 *      protokolde rapor formatı sabittir (bkz. usb_hid_mouse.c).
 *   8) Bulunan interrupt-IN endpoint'i usb_hid_mouse_attach()'e devret.
 * ============================================================================= */

#include "usb.h"
#include "serial.h"
#include <stddef.h>

/* usb_hid_mouse.c bu fonksiyonu sağlar; burada sadece ileri bildirim var,
 * böylece usb.c bir HID sürücüsüne sıkı sıkıya bağlı olmaz. */
extern void usb_hid_mouse_attach(usb_device_t *dev,
                                  const usb_interface_descriptor_t *iface,
                                  const usb_endpoint_descriptor_t *ep);

#define USB_MAX_DEVICES 4
static usb_device_t g_devices[USB_MAX_DEVICES];
static uint32_t     g_device_count = 0;
static uint8_t      g_next_address = 1; /* 0 = "henüz adreslenmemiş" için ayrılmış */

static void usb_memzero(void *p, uint32_t n) {
    uint8_t *b = (uint8_t*)p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

void usb_init(void) {
    g_device_count = 0;
    g_next_address = 1;
    serial_printf("[USB] Generic USB core initialized.\n");
}

/* Küçük yardımcı: standart bir GET_DESCRIPTOR kontrol transferi kurar. */
static int usb_get_descriptor(usb_device_t *dev, uint8_t type, uint8_t index,
                               void *buf, uint16_t len) {
    usb_setup_packet_t setup;
    setup.bmRequestType = USB_RT_DIR_IN | USB_RT_TYPE_STANDARD | USB_RT_RECIP_DEVICE;
    setup.bRequest      = USB_REQ_GET_DESCRIPTOR;
    setup.wValue        = (uint16_t)((type << 8) | index);
    setup.wIndex        = 0;
    setup.wLength       = len;
    return dev->hcd_ops->control_transfer(dev, &setup, buf);
}

static int usb_set_address(usb_device_t *dev, uint8_t addr) {
    usb_setup_packet_t setup;
    setup.bmRequestType = USB_RT_DIR_OUT | USB_RT_TYPE_STANDARD | USB_RT_RECIP_DEVICE;
    setup.bRequest      = USB_REQ_SET_ADDRESS;
    setup.wValue        = addr;
    setup.wIndex        = 0;
    setup.wLength       = 0;
    return dev->hcd_ops->control_transfer(dev, &setup, NULL);
}

static int usb_set_configuration(usb_device_t *dev, uint8_t cfg_value) {
    usb_setup_packet_t setup;
    setup.bmRequestType = USB_RT_DIR_OUT | USB_RT_TYPE_STANDARD | USB_RT_RECIP_DEVICE;
    setup.bRequest      = USB_REQ_SET_CONFIGURATION;
    setup.wValue        = cfg_value;
    setup.wIndex        = 0;
    setup.wLength       = 0;
    return dev->hcd_ops->control_transfer(dev, &setup, NULL);
}

static int usb_hid_set_boot_protocol(usb_device_t *dev, uint8_t iface_num) {
    usb_setup_packet_t setup;
    setup.bmRequestType = USB_RT_DIR_OUT | USB_RT_TYPE_CLASS | USB_RT_RECIP_IFACE;
    setup.bRequest      = HID_REQ_SET_PROTOCOL;
    setup.wValue        = HID_PROTOCOL_BOOT;
    setup.wIndex        = iface_num;
    setup.wLength       = 0;
    return dev->hcd_ops->control_transfer(dev, &setup, NULL);
}

#define HID_REQ_SET_IDLE 0x0A

static int usb_hid_set_idle(usb_device_t *dev, uint8_t iface_num, uint8_t duration, uint8_t report_id) {
    usb_setup_packet_t setup;
    setup.bmRequestType = USB_RT_DIR_OUT | USB_RT_TYPE_CLASS | USB_RT_RECIP_IFACE;
    setup.bRequest      = HID_REQ_SET_IDLE;
    setup.wValue        = (duration << 8) | report_id;
    setup.wIndex        = iface_num;
    setup.wLength       = 0;
    return dev->hcd_ops->control_transfer(dev, &setup, NULL);
}

void usb_enumerate_device(const usb_hcd_ops_t *ops, void *hcd_data, bool low_speed) {
    if (g_device_count >= USB_MAX_DEVICES) {
        serial_printf("[USB] Device table full, ignoring new device.\n");
        return;
    }

    usb_device_t *dev = &g_devices[g_device_count];
    usb_memzero(dev, sizeof(*dev));
    dev->address          = 0; /* default pipe */
    dev->max_packet_size0 = 8; /* ilk temas için USB spec'in garantili min. değeri */
    dev->low_speed        = low_speed;
    dev->hcd_data          = hcd_data;
    dev->hcd_ops           = ops;

    /* --- 1) Kısmi (8 bayt) device descriptor: gerçek EP0 max paket boyutu için --- */
    usb_device_descriptor_t dd;
    usb_memzero(&dd, sizeof(dd));
    if (usb_get_descriptor(dev, USB_DESC_DEVICE, 0, &dd, 8) < 8) {
        serial_printf("[USB] Initial GET_DESCRIPTOR(DEVICE,8) failed.\n");
        return;
    }
    if (dd.bMaxPacketSize0 == 8 || dd.bMaxPacketSize0 == 16 ||
        dd.bMaxPacketSize0 == 32 || dd.bMaxPacketSize0 == 64) {
        dev->max_packet_size0 = dd.bMaxPacketSize0;
    }

    /* --- 2) SET_ADDRESS --- */
    uint8_t new_addr = g_next_address++;
    if (usb_set_address(dev, new_addr) < 0) {
        serial_printf("[USB] SET_ADDRESS failed.\n");
        return;
    }
    dev->address = new_addr;

    /* --- 3) Tam device descriptor (18 bayt) --- */
    if (usb_get_descriptor(dev, USB_DESC_DEVICE, 0, &dd, sizeof(dd)) < (int)sizeof(dd)) {
        serial_printf("[USB] Full GET_DESCRIPTOR(DEVICE) failed.\n");
        return;
    }
    serial_printf("[USB] Device addr=%d VID=%x PID=%x class=%x\n",
                  dev->address, dd.idVendor, dd.idProduct, dd.bDeviceClass);

    /* --- 4) Configuration descriptor: önce 9 baytlık header --- */
    usb_config_descriptor_t cfg_hdr;
    if (usb_get_descriptor(dev, USB_DESC_CONFIGURATION, 0, &cfg_hdr, sizeof(cfg_hdr)) < (int)sizeof(cfg_hdr)) {
        serial_printf("[USB] GET_DESCRIPTOR(CONFIG header) failed.\n");
        return;
    }

    uint16_t total_len = cfg_hdr.wTotalLength;
    if (total_len > USB_MAX_CONFIG_DESC_LEN) total_len = USB_MAX_CONFIG_DESC_LEN;

    static uint8_t cfg_buf[USB_MAX_CONFIG_DESC_LEN];
    usb_memzero(cfg_buf, sizeof(cfg_buf));
    if (usb_get_descriptor(dev, USB_DESC_CONFIGURATION, 0, cfg_buf, total_len) < (int)total_len) {
        serial_printf("[USB] GET_DESCRIPTOR(CONFIG full) failed.\n");
        return;
    }

    /* --- 5) Config buffer'ı gez: interface + endpoint descriptor'larını bul --- */
    const usb_interface_descriptor_t *hid_iface = NULL;
    const usb_endpoint_descriptor_t  *int_in_ep  = NULL;

    uint32_t off = sizeof(usb_config_descriptor_t);
    const usb_interface_descriptor_t *cur_iface = NULL;

    while (off + 2 <= total_len) {
        uint8_t len  = cfg_buf[off];
        uint8_t type = cfg_buf[off + 1];
        if (len == 0) break; /* bozuk descriptor, sonsuz döngüden kaçın */

        if (type == USB_DESC_INTERFACE && off + sizeof(usb_interface_descriptor_t) <= total_len) {
            cur_iface = (const usb_interface_descriptor_t*)&cfg_buf[off];
            if (cur_iface->bInterfaceClass == USB_CLASS_HID && 
                cur_iface->bInterfaceSubClass == 1 /* Boot Interface */ &&
                cur_iface->bInterfaceProtocol == 2 /* Mouse */ && 
                hid_iface == NULL) {
                hid_iface = cur_iface;
            }
        } else if (type == USB_DESC_ENDPOINT && off + sizeof(usb_endpoint_descriptor_t) <= total_len) {
            const usb_endpoint_descriptor_t *ep = (const usb_endpoint_descriptor_t*)&cfg_buf[off];
            /* Şu an ayrıştırdığımız interface, bizim seçtiğimiz HID interface ise
             * ve bu bir Interrupt IN endpoint'i ise, kullan. */
            if (cur_iface == hid_iface && hid_iface != NULL &&
                (ep->bEndpointAddress & 0x80) &&
                (ep->bmAttributes & USB_EP_ATTR_TYPE_MASK) == USB_EP_ATTR_INTERRUPT &&
                int_in_ep == NULL) {
                int_in_ep = ep;
            }
        }
        off += len;
    }

    if (hid_iface == NULL || int_in_ep == NULL) {
        serial_printf("[USB] No HID interrupt-IN endpoint found (not a boot-protocol HID device?).\n");
        return;
    }

    /* --- 6) SET_CONFIGURATION --- */
    if (usb_set_configuration(dev, cfg_hdr.bConfigurationValue) < 0) {
        serial_printf("[USB] SET_CONFIGURATION failed.\n");
        return;
    }

    /* --- 7) Boot Protocol iste ve Set Idle(0) ile sürekli dinle --- */
    usb_hid_set_idle(dev, hid_iface->bInterfaceNumber, 0, 0);
    usb_hid_set_boot_protocol(dev, hid_iface->bInterfaceNumber);

    serial_printf("[USB] HID device ready: iface=%d ep=0x%x maxpkt=%d interval=%d\n",
                  hid_iface->bInterfaceNumber, int_in_ep->bEndpointAddress,
                  int_in_ep->wMaxPacketSize, int_in_ep->bInterval);

    /* --- 8) Sınıf sürücüsüne (HID mouse) devret --- */
    g_device_count++;
    usb_hid_mouse_attach(dev, hid_iface, int_in_ep);
}
