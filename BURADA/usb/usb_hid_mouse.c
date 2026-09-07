/* =============================================================================
 * ZeruX OS — USB HID Boot-Protocol Mouse Class Driver
 * File: kernel/drivers/usb_hid_mouse.c
 * =============================================================================
 *
 * usb.c, config descriptor'ında bir HID interrupt-IN endpoint bulduğunda
 * usb_hid_mouse_attach()'i çağırır. Bu dosya HID Report Descriptor'ı hiç
 * ayrıştırmaz — bunun yerine cihazdan Boot Protocol (HID 1.11 Spec §7.2.1 —
 * "This report layout is fixed and MUST match the layout below") istemiştir
 * (usb.c'de SET_PROTOCOL çağrısı), bu sayede rapor formatı sabittir:
 *
 *   byte 0 : Buton bitmask (bit0=Sol, bit1=Sağ, bit2=Orta)
 *   byte 1 : X delta (signed 8-bit)
 *   byte 2 : Y delta (signed 8-bit)
 *   byte 3 : (opsiyonel) tekerlek delta — tüm fareler göndermez
 *
 * Mevcut PS/2 sürücüsüyle (mouse.c) AYNI global durumu (mouse_x, mouse_y,
 * mouse_left_btn, ...) günceller — GUI/compositor tarafında hiçbir değişiklik
 * gerekmez, iki sürücü de aynı arayüzü besler.
 * ============================================================================= */

#include "usb.h"
#include "mouse.h"
#include "vbe.h"
#include "gui.h"
#include "serial.h"

static void usb_hid_mouse_report(usb_device_t *dev, const uint8_t *data, uint32_t len) {
    (void)dev;
    if (len < 3) return; /* eksik/bozuk paket, yok say */

    int8_t dx = (int8_t)data[1];
    int8_t dy = (int8_t)data[2];

    mouse_left_btn   = (data[0] & 0x01) != 0;
    mouse_right_btn  = (data[0] & 0x02) != 0;
    mouse_middle_btn = (data[0] & 0x04) != 0;

    mouse_x += dx;
    mouse_y -= dy; /* PS/2 sürücüsüyle aynı eksen kuralı: Y ters çevrilir */

    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= g_vbe_width)  mouse_x = g_vbe_width - 1;
    if (mouse_y >= g_vbe_height) mouse_y = g_vbe_height - 1;

    /* İmleç çizimi GUI render loop'unun sorumluluğunda (mouse.c'deki notla
     * aynı prensip) — burada sadece durumu güncelliyoruz. */
}

void usb_hid_mouse_attach(usb_device_t *dev,
                           const usb_interface_descriptor_t *iface,
                           const usb_endpoint_descriptor_t *ep) {
    (void)iface;

    if (!dev->hcd_ops || !dev->hcd_ops->setup_interrupt_in) {
        serial_printf("[USB-HID] HCD does not support interrupt transfers.\n");
        return;
    }

    int rc = dev->hcd_ops->setup_interrupt_in(dev, ep->bEndpointAddress,
                                               ep->wMaxPacketSize, ep->bInterval,
                                               usb_hid_mouse_report);
    if (rc != 0) {
        serial_printf("[USB-HID] Failed to arm interrupt endpoint.\n");
        return;
    }

    if (g_vbe_enabled && g_desktop_ready) {
        mouse_x = g_vbe_width / 2;
        mouse_y = g_vbe_height / 2;
    }

    serial_printf("[USB-HID] USB Boot-Protocol mouse attached and polling.\n");
}
