/* =============================================================================
 * ZeruX OS — Generic USB Core (Host-Controller-Independent) Header
 * File: kernel/include/usb.h
 * =============================================================================
 *
 * Bu katman, USB'nin donanımdan bağımsız kısmını temsil eder: standart
 * descriptor yapıları, setup paketi formatı, ve bir "Host Controller Driver"
 * (HCD) soyutlaması. UHCI (bu proje), ileride EHCI/OHCI eklenirse onlar da
 * bu arayüzü (usb_hcd_ops_t) implemente ederek aynı enumeration/HID
 * mantığını paylaşabilir — donanıma özel kod SADECE uhci.c'de yaşar.
 * =============================================================================
 */

#ifndef USB_H
#define USB_H

#include <stdint.h>
#include <stdbool.h>

/* =============================================================================
 * USB Setup Packet (Control Transfer, 8 byte)
 * ============================================================================= */
typedef struct __attribute__((packed)) {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_setup_packet_t;

/* bmRequestType bit alanları */
#define USB_RT_DIR_OUT       0x00
#define USB_RT_DIR_IN        0x80
#define USB_RT_TYPE_STANDARD 0x00
#define USB_RT_TYPE_CLASS    0x20
#define USB_RT_RECIP_DEVICE  0x00
#define USB_RT_RECIP_IFACE   0x01
#define USB_RT_RECIP_EP      0x02

/* Standart bRequest kodları (USB 2.0 Spec Table 9-4) */
#define USB_REQ_GET_STATUS        0x00
#define USB_REQ_CLEAR_FEATURE     0x01
#define USB_REQ_SET_FEATURE       0x03
#define USB_REQ_SET_ADDRESS       0x05
#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_SET_INTERFACE     0x0B

/* HID sınıfı özel istekleri (HID 1.11 Spec §7.2) */
#define HID_REQ_SET_IDLE          0x0A
#define HID_REQ_SET_PROTOCOL      0x0B
#define HID_PROTOCOL_BOOT         0x00

/* Descriptor tipleri */
#define USB_DESC_DEVICE           0x01
#define USB_DESC_CONFIGURATION    0x02
#define USB_DESC_STRING           0x03
#define USB_DESC_INTERFACE        0x04
#define USB_DESC_ENDPOINT         0x05

#define USB_CLASS_HID             0x03

/* Endpoint bmAttributes alt sınıfları */
#define USB_EP_ATTR_TYPE_MASK     0x03
#define USB_EP_ATTR_INTERRUPT     0x03

/* =============================================================================
 * Standart USB Descriptor Yapıları (USB 2.0 Spec, Chapter 9)
 * ============================================================================= */
typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} usb_device_descriptor_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} usb_config_descriptor_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} usb_interface_descriptor_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;   /* bit7 = yön (1=IN), bit3:0 = endpoint no */
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} usb_endpoint_descriptor_t;

/* En fazla kaç config-descriptor baytı okuyacağımız (basit sabit sınır,
 * tipik bir HID mouse config+interface+endpoint seti için fazlasıyla yeterli) */
#define USB_MAX_CONFIG_DESC_LEN 128

/* =============================================================================
 * Host Controller Driver (HCD) Soyutlaması
 * =============================================================================
 * UHCI (uhci.c) bu vtable'ı doldurup usb_register_hcd() ile kaydeder.
 * Generic katman (usb.c), donanıma dokunmadan sadece bu fonksiyonları çağırır.
 */
struct usb_device;

typedef struct {
    /* Bir kontrol transferini eksiksiz gerçekleştirir (setup+data+status
     * evrelerinin hepsi bu çağrı dönene kadar tamamlanmış olmalı).
     * data==NULL ise data evresi yoktur (örn. SET_ADDRESS).
     * Dönüş: >=0 aktarılan bayt sayısı, <0 hata. */
    int (*control_transfer)(struct usb_device *dev, const usb_setup_packet_t *setup, void *data);

    /* Belirtilen interrupt-IN endpoint'i periyodik olarak dinlemeye başlar.
     * Her tamamlanan aktarımda callback(dev, data, len) çağrılır.
     * Dönüş: 0 başarı, <0 hata. */
    int (*setup_interrupt_in)(struct usb_device *dev, uint8_t endpoint,
                               uint16_t max_packet_size, uint8_t interval,
                               void (*callback)(struct usb_device *dev, const uint8_t *data, uint32_t len));
} usb_hcd_ops_t;

typedef struct usb_device {
    uint8_t  address;           /* Atanmış USB adresi (enumeration bitene kadar 0) */
    uint8_t  max_packet_size0;  /* EP0 için maksimum paket boyutu (8/16/32/64) */
    bool     low_speed;
    void    *hcd_data;          /* HCD'ye özel durum (örn. UHCI queue head pointer'ı) */
    const usb_hcd_ops_t *hcd_ops;
} usb_device_t;

/* =============================================================================
 * Genel USB API
 * ============================================================================= */

/* USB alt sistemini başlatır (device tablosunu sıfırlar). uhci_init()'ten
 * ÖNCE bir kere çağrılmalıdır. */
void usb_init(void);

/* Bir HCD (örn. UHCI) yeni bir cihaz keşfedip portu resetlediğinde çağırır.
 * Bu fonksiyon tüm enumeration state machine'ini (GET_DESCRIPTOR, SET_ADDRESS,
 * config okuma, sınıf sürücüsüne devretme) yürütür. */
void usb_enumerate_device(const usb_hcd_ops_t *ops, void *hcd_data, bool low_speed);

#endif /* USB_H */
