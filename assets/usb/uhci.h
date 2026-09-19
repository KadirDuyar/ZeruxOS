/* =============================================================================
 * ZeruX OS — UHCI (Universal Host Controller Interface, USB 1.1) Header
 * File: kernel/include/uhci.h
 * =============================================================================
 *
 * TASARIM NOTU (neden sadece UHCI, EHCI değil?):
 * H55/Ibex Peak gibi PCH'lerde EHCI (USB 2.0) controller'ı, kendisine
 * bağlı Full-Speed/Low-Speed cihazları (klavye, fare gibi neredeyse tüm HID
 * cihazları bu sınıftadır) OTOMATİK olarak "companion" UHCI controller'ına
 * yönlendirir — bunun için EHCI sürücüsünün hiç çalışmasına/port sahipliği
 * almasına gerek yoktur (bu, port-routing lojiği varsayılan/reset durumudur).
 * Yani: EHCI'ye hiç dokunmadan, sadece UHCI'yi initialize ederek gerçek
 * donanımdaki bir USB fare/klavye ile konuşabiliriz. Bu, kapsamı ciddi
 * şekilde daraltan ama gerçek donanımda çalışan, bilinçli bir tasarım kararı.
 *
 * TASARIM NOTU (neden IRQ değil, polling?):
 * UHCI'nin PCI kesme hattı genellikle başka aygıtlarla paylaşılır (IRQ
 * sharing). Bunu yanlış yönetmek "QEMU'da çalışıyor, gerçek donanımda
 * çalışmıyor" hatalarının klasik kaynaklarından biridir. Bunun yerine bu
 * sürücü tamamen polling tabanlıdır: periyodik interrupt-IN transferi
 * frame list'e bir kere yerleştirilir, tamamlanıp tamamlanmadığı
 * uhci_poll() ile (timer tick'inden, ~10ms'de bir) kontrol edilir.
 * ============================================================================= */

#ifndef UHCI_H
#define UHCI_H

/* PCI'de UHCI controller'ı bulur, resetler, frame list'i kurar ve root hub
 * portlarını tarayıp bağlı cihazları usb_enumerate_device() ile devreder.
 * usb_init()'ten SONRA çağrılmalıdır. Controller bulunamazsa sessizce
 * (sadece log ile) çıkar — sistemin geri kalanını etkilemez. */
void uhci_init(void);

/* Bekleyen interrupt-IN transferlerini kontrol eder; tamamlanmış olanları
 * işler ve bir sonraki frame için yeniden kurar (re-arm). timer_callback()
 * içinden (her tick, ~10ms) çağrılmalıdır. UHCI hiç bulunamadıysa no-op'tur. */
void uhci_poll(void);

#endif /* UHCI_H */
