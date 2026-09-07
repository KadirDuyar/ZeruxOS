/* =============================================================================
 * ZeruX OS - Minimal FTP Sunucusu (ftpd)
 * File: user_apps/bin/ftpd.c
 * =============================================================================
 * Kullanim:
 *   run /disk/fat0/BIN/FTPD.ELF        (veya shell'den dogrudan: ftpd)
 *
 * Desteklenen komutlar: USER, PASS, SYST, TYPE, PWD, CWD, NOOP,
 *                        LIST, RETR <dosya>, STOR <dosya>, QUIT
 *
 * Kimlik dogrulama: FTP_USER / FTP_PASS (ftp_common.h - varsayilan zerux/zerux)
 * Dosya sistemi   : sadece /disk/fat0 kok dizini (alt klasor yok, FAT32 flat)
 *
 * Mimari: tek thread'li, ayni anda tek istemciyi kontrol kanalindan kabul
 * eder ve tamamen isleyip kapatana kadar bir sonraki istemciyi beklemez.
 * Veri kanali (LIST/RETR/STOR) sabit FTP_DATA_PORT uzerinden - detay icin
 * ftp_common.h basindaki protokol notuna bakin.
 * =============================================================================
 */
#include "stdio.h"
#include "syscall.h"
#include "string.h"
#include "dirent.h"
#include "ftp_common.h"

#define LINE_BUF  256
#define XFER_BUF  512

/* -----------------------------------------------------------------------
 * Kucuk yardimcilar
 * ----------------------------------------------------------------------- */

/* "USER admin\r" -> line="USER" (buyuk harfe cevrilir), donus: "admin" e
 * isaret eden pointer (bosluklar atlanmis, arg yoksa bos string "\0"). */
static char *split_cmd(char *line) {
    char *p = line;
    while (*p && *p != ' ') {
        if (*p >= 'a' && *p <= 'z') *p -= 32;
        p++;
    }
    if (*p == ' ') {
        *p = '\0';
        p++;
        while (*p == ' ') p++;
        return p;
    }
    return p; /* zaten '\0' */
}

/* -----------------------------------------------------------------------
 * Komut isleyicileri
 * ----------------------------------------------------------------------- */

static void do_list(int ctrl, int data_listen) {
    ftp_send_line(ctrl, "150 Dizin listesi gonderiliyor");

    int d = accept(data_listen, 0, 0);
    if (d < 0) {
        ftp_send_line(ctrl, "425 Veri baglantisi kurulamadi");
        return;
    }

    int fd = open("/disk/fat0", O_RDONLY);
    if (fd < 0) {
        ftp_send_line(ctrl, "550 Dizin acilamadi");
        close(d);
        return;
    }

    uint32_t idx = 0;
    struct dirent *de;
    while ((de = readdir(fd, idx++)) != NULL) {
        write(d, de->name, strlen(de->name));
        write(d, "\r\n", 2);
    }

    close(fd);
    close(d);
    ftp_send_line(ctrl, "226 Aktarim tamamlandi");
}

static void do_retr(int ctrl, int data_listen, const char *fname) {
    char path[300];
    ftp_build_path(path, sizeof(path), fname);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        ftp_send_line(ctrl, "550 Dosya bulunamadi");
        return;
    }

    ftp_send_line(ctrl, "150 Dosya gonderiliyor");

    int d = accept(data_listen, 0, 0);
    if (d < 0) {
        ftp_send_line(ctrl, "425 Veri baglantisi kurulamadi");
        close(fd);
        return;
    }

    char buf[XFER_BUF];
    int r;
    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        int off = 0;
        while (off < r) {
            int w = write(d, buf + off, r - off);
            if (w <= 0) break;
            off += w;
        }
    }

    close(fd);
    close(d);
    ftp_send_line(ctrl, "226 Aktarim tamamlandi");
}

static void do_stor(int ctrl, int data_listen, const char *fname) {
    /* Alt dizin girisimlerini reddet (kernel sys_creat_handler zaten
     * reddediyor, ama erken ve acik bir hata mesaji vermek daha iyi) */
    for (const char *p = fname; *p; p++) {
        if (*p == '/') {
            ftp_send_line(ctrl, "553 Gecersiz dosya adi (alt dizin desteklenmiyor)");
            return;
        }
    }

    char path[300];
    ftp_build_path(path, sizeof(path), fname);

    if (creat_fat0(path) != 0) {
        ftp_send_line(ctrl, "550 Dosya olusturulamadi");
        return;
    }

    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        ftp_send_line(ctrl, "550 Dosya acilamadi");
        return;
    }

    ftp_send_line(ctrl, "150 Veri baglantisi bekleniyor");

    int d = accept(data_listen, 0, 0);
    if (d < 0) {
        ftp_send_line(ctrl, "425 Veri baglantisi kurulamadi");
        close(fd);
        return;
    }

    char buf[XFER_BUF];
    int r;
    while ((r = read(d, buf, sizeof(buf))) > 0) {
        int off = 0;
        while (off < r) {
            int w = write(fd, buf + off, r - off);
            if (w <= 0) break;
            off += w;
        }
    }

    close(d);
    close(fd);
    ftp_send_line(ctrl, "226 Aktarim tamamlandi");
}

/* -----------------------------------------------------------------------
 * Bir istemci oturumunu yonet (QUIT gelene ya da baglanti kopana kadar)
 * ----------------------------------------------------------------------- */
static void handle_session(int ctrl, int data_listen) {
    int authed = 0;
    char user[64];
    user[0] = '\0';

    ftp_send_line(ctrl, "220 ZeruX FTP Sunucusuna Hos Geldiniz");

    char line[LINE_BUF];
    while (1) {
        int n = ftp_recv_line(ctrl, line, sizeof(line));
        if (n < 0) break; /* istemci baglantiyi kesti */
        if (n == 0) continue;

        char *arg = split_cmd(line);

        if (strcmp(line, "USER") == 0) {
            int i = 0;
            while (arg[i] && i < 63) { user[i] = arg[i]; i++; }
            user[i] = '\0';
            ftp_send_line(ctrl, "331 Sifre gerekli");

        } else if (strcmp(line, "PASS") == 0) {
            if (strcmp(user, FTP_USER) == 0 && strcmp(arg, FTP_PASS) == 0) {
                authed = 1;
                ftp_send_line(ctrl, "230 Giris basarili");
            } else {
                ftp_send_line(ctrl, "530 Giris basarisiz - kullanici adi/sifre yanlis");
            }

        } else if (strcmp(line, "SYST") == 0) {
            ftp_send_line(ctrl, "215 ZeruX OS");

        } else if (strcmp(line, "TYPE") == 0) {
            ftp_send_line(ctrl, "200 Tip ayarlandi");

        } else if (strcmp(line, "PWD") == 0) {
            ftp_send_line(ctrl, "257 \"/\" guncel dizin");

        } else if (strcmp(line, "CWD") == 0) {
            if (arg[0] == '/' && arg[1] == '\0') {
                ftp_send_line(ctrl, "250 Dizin degistirildi");
            } else {
                ftp_send_line(ctrl, "550 Alt dizin desteklenmiyor (sadece /)");
            }

        } else if (strcmp(line, "NOOP") == 0) {
            ftp_send_line(ctrl, "200 OK");

        } else if (strcmp(line, "LIST") == 0) {
            if (!authed) { ftp_send_line(ctrl, "530 Once giris yapin (USER/PASS)"); continue; }
            do_list(ctrl, data_listen);

        } else if (strcmp(line, "RETR") == 0) {
            if (!authed) { ftp_send_line(ctrl, "530 Once giris yapin (USER/PASS)"); continue; }
            if (arg[0] == '\0') { ftp_send_line(ctrl, "501 Dosya adi gerekli"); continue; }
            do_retr(ctrl, data_listen, arg);

        } else if (strcmp(line, "STOR") == 0) {
            if (!authed) { ftp_send_line(ctrl, "530 Once giris yapin (USER/PASS)"); continue; }
            if (arg[0] == '\0') { ftp_send_line(ctrl, "501 Dosya adi gerekli"); continue; }
            do_stor(ctrl, data_listen, arg);

        } else if (strcmp(line, "QUIT") == 0) {
            ftp_send_line(ctrl, "221 Gorusuruz");
            break;

        } else {
            ftp_send_line(ctrl, "502 Komut desteklenmiyor");
        }
    }
}

/* -----------------------------------------------------------------------
 * main()
 * ----------------------------------------------------------------------- */
int main(int argc, char **argv) {
    (void)argc; (void)argv;

    printf("ZeruX FTP Sunucusu baslatiliyor...\n");

    /* --- Kontrol kanali soketi --- */
    int ctrl_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (ctrl_listen < 0) {
        printf("ftpd: socket() basarisiz (kontrol)\n");
        return -1;
    }

    struct sockaddr_in ctrl_addr;
    memset(&ctrl_addr, 0, sizeof(ctrl_addr));
    ctrl_addr.sin_family = AF_INET;
    ctrl_addr.sin_port   = ftp_htons(FTP_CTRL_PORT);
    ctrl_addr.sin_addr   = 0; /* INADDR_ANY */

    if (bind(ctrl_listen, (struct sockaddr *)&ctrl_addr, sizeof(ctrl_addr)) < 0) {
        printf("ftpd: bind() basarisiz (kontrol portu %d)\n", FTP_CTRL_PORT);
        return -1;
    }
    if (listen(ctrl_listen, 4) < 0) {
        printf("ftpd: listen() basarisiz (kontrol)\n");
        return -1;
    }

    /* --- Veri kanali soketi (sabit port, PASV muadili - bkz ftp_common.h) --- */
    int data_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (data_listen < 0) {
        printf("ftpd: socket() basarisiz (veri)\n");
        return -1;
    }

    struct sockaddr_in data_addr;
    memset(&data_addr, 0, sizeof(data_addr));
    data_addr.sin_family = AF_INET;
    data_addr.sin_port   = ftp_htons(FTP_DATA_PORT);
    data_addr.sin_addr   = 0;

    if (bind(data_listen, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0) {
        printf("ftpd: bind() basarisiz (veri portu %d)\n", FTP_DATA_PORT);
        return -1;
    }
    if (listen(data_listen, 4) < 0) {
        printf("ftpd: listen() basarisiz (veri)\n");
        return -1;
    }

    printf("ftpd: dinleniyor - kontrol:%d veri:%d (kullanici: %s)\n",
           FTP_CTRL_PORT, FTP_DATA_PORT, FTP_USER);

    while (1) {
        int c = accept(ctrl_listen, 0, 0);
        if (c < 0) {
            sleep(50);
            continue;
        }
        printf("ftpd: yeni istemci baglandi\n");
        handle_session(c, data_listen);
        close(c);
        printf("ftpd: istemci baglantisi kapatildi, yeni istemci bekleniyor...\n");
    }

    close(data_listen);
    close(ctrl_listen);
    return 0;
}
