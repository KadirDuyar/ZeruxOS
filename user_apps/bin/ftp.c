/* =============================================================================
 * ZeruX OS - Minimal FTP Istemcisi (ftp)
 * File: user_apps/bin/ftp.c
 * =============================================================================
 * Kullanim:
 *   run /disk/fat0/BIN/FTP.ELF <sunucu_ip>     (veya shell'den: ftp <ip>)
 *
 * Baglandiktan sonra interaktif komut satiri acilir:
 *   user <ad>        - FTP USER komutu gonderir
 *   pass <sifre>      - FTP PASS komutu gonderir (varsayilan: zerux/zerux)
 *   pwd               - guncel dizini sorar
 *   ls                - /disk/fat0 icerigini listeler (veri kanali uzerinden)
 *   get <dosya>       - sunucudan dosya indirir, yerel /disk/fat0'a yazar
 *   put <dosya>       - yerel /disk/fat0'daki dosyayi sunucuya yukler
 *   quit / exit       - QUIT gonderir ve cikar
 *
 * Not: indirilen/yuklenen dosyalar HER ZAMAN /disk/fat0 (flat) altina
 * yazilir/okunur - ayni isim istemci ve sunucuda kullanilir.
 * =============================================================================
 */
#include "stdio.h"
#include "syscall.h"
#include "string.h"
#include "ftp_common.h"

#define LINE_BUF  256
#define XFER_BUF  512

/* Kontrol baglantisi kurulunca burada saklanir; veri kanali soketleri
 * her komutta bu adrese (ayni IP, sabit FTP_DATA_PORT) baglanir. */
static uint32_t g_server_ip_net;

/* -----------------------------------------------------------------------
 * Kucuk yardimcilar
 * ----------------------------------------------------------------------- */

/* Komut kelimesini kucuk harfe cevirir, argumana isaret eden pointer doner */
static char *split_local(char *line) {
    char *p = line;
    while (*p && *p != ' ') {
        if (*p >= 'A' && *p <= 'Z') *p += 32;
        p++;
    }
    if (*p == ' ') {
        *p = '\0';
        p++;
        while (*p == ' ') p++;
        return p;
    }
    return p;
}

/* Kontrol kanalindan yeni bir veri soketi acip sunucunun sabit veri
 * portuna baglar. Basarisizsa -1 doner. */
static int open_data_socket(void) {
    int d = socket(AF_INET, SOCK_STREAM, 0);
    if (d < 0) return -1;

    struct sockaddr_in daddr;
    memset(&daddr, 0, sizeof(daddr));
    daddr.sin_family = AF_INET;
    daddr.sin_port   = ftp_htons(FTP_DATA_PORT);
    daddr.sin_addr   = g_server_ip_net;

    if (connect(d, (struct sockaddr *)&daddr, sizeof(daddr)) < 0) {
        close(d);
        return -1;
    }

    /* El sikismasinin (SYN/SYN-ACK/ACK) tamamlanmasi icin kisa bekleme -
     * userland'de baglanti durumunu sorgulayan bir syscall yok. */
    sleep(100);
    return d;
}

/* "CMD" veya "CMD arg" gonderir, tek satirlik cevabi okuyup ekrana basar */
static void do_simple(int ctrl, const char *cmd, const char *arg) {
    char line[LINE_BUF];
    int i = 0;
    while (cmd[i] && i < LINE_BUF - 2) { line[i] = cmd[i]; i++; }
    if (arg[0]) {
        line[i++] = ' ';
        int j = 0;
        while (arg[j] && i < LINE_BUF - 1) line[i++] = arg[j++];
    }
    line[i] = '\0';

    ftp_send_line(ctrl, line);

    char resp[LINE_BUF];
    int n = ftp_recv_line(ctrl, resp, sizeof(resp));
    if (n >= 0) printf("%s\n", resp);
    else printf("ftp: sunucu baglantiyi kapatti\n");
}

/* -----------------------------------------------------------------------
 * ls / get / put
 * ----------------------------------------------------------------------- */

static void cmd_ls(int ctrl) {
    ftp_send_line(ctrl, "LIST");

    char resp[LINE_BUF];
    ftp_recv_line(ctrl, resp, sizeof(resp)); /* "150 ..." bekleniyor */
    printf("%s\n", resp);
    if (resp[0] != '1') return; /* 530/550 gibi bir hata */

    int d = open_data_socket();
    if (d < 0) {
        printf("ftp: veri kanali kurulamadi\n");
        return;
    }

    char buf[XFER_BUF];
    int r;
    while ((r = read(d, buf, sizeof(buf) - 1)) > 0) {
        buf[r] = '\0';
        printf("%s", buf);
    }
    close(d);

    ftp_recv_line(ctrl, resp, sizeof(resp)); /* "226 ..." */
    printf("%s\n", resp);
}

static void cmd_get(int ctrl, const char *fname) {
    char path[300];
    ftp_build_path(path, sizeof(path), fname);

    if (creat_fat0(path) != 0) {
        printf("ftp: yerel dosya olusturulamadi: %s\n", path);
        return;
    }
    int lf = open(path, O_WRONLY);
    if (lf < 0) {
        printf("ftp: yerel dosya acilamadi: %s\n", path);
        return;
    }

    char cmdline[300];
    int i = 0;
    const char *pfx = "RETR ";
    while (pfx[i]) { cmdline[i] = pfx[i]; i++; }
    int j = 0;
    while (fname[j] && i < 290) cmdline[i++] = fname[j++];
    cmdline[i] = '\0';
    ftp_send_line(ctrl, cmdline);

    char resp[LINE_BUF];
    ftp_recv_line(ctrl, resp, sizeof(resp));
    printf("%s\n", resp);
    if (resp[0] != '1') { close(lf); return; }

    int d = open_data_socket();
    if (d < 0) {
        printf("ftp: veri kanali kurulamadi\n");
        close(lf);
        return;
    }

    char buf[XFER_BUF];
    int r, total = 0;
    while ((r = read(d, buf, sizeof(buf))) > 0) {
        int off = 0;
        while (off < r) {
            int w = write(lf, buf + off, r - off);
            if (w <= 0) break;
            off += w;
        }
        total += r;
    }
    close(d);
    close(lf);

    ftp_recv_line(ctrl, resp, sizeof(resp));
    printf("%s (%d bayt alindi)\n", resp, total);
}

static void cmd_put(int ctrl, const char *fname) {
    char path[300];
    ftp_build_path(path, sizeof(path), fname);

    int lf = open(path, O_RDONLY);
    if (lf < 0) {
        printf("ftp: yerel dosya bulunamadi: %s\n", path);
        return;
    }

    char cmdline[300];
    int i = 0;
    const char *pfx = "STOR ";
    while (pfx[i]) { cmdline[i] = pfx[i]; i++; }
    int j = 0;
    while (fname[j] && i < 290) cmdline[i++] = fname[j++];
    cmdline[i] = '\0';
    ftp_send_line(ctrl, cmdline);

    char resp[LINE_BUF];
    ftp_recv_line(ctrl, resp, sizeof(resp));
    printf("%s\n", resp);
    if (resp[0] != '1') { close(lf); return; }

    int d = open_data_socket();
    if (d < 0) {
        printf("ftp: veri kanali kurulamadi\n");
        close(lf);
        return;
    }

    char buf[XFER_BUF];
    int r, total = 0;
    while ((r = read(lf, buf, sizeof(buf))) > 0) {
        int off = 0;
        while (off < r) {
            int w = write(d, buf + off, r - off);
            if (w <= 0) break;
            off += w;
        }
        total += r;
    }
    close(lf);
    close(d);

    ftp_recv_line(ctrl, resp, sizeof(resp));
    printf("%s (%d bayt gonderildi)\n", resp, total);
}

/* -----------------------------------------------------------------------
 * main()
 * ----------------------------------------------------------------------- */
int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Kullanim: ftp <sunucu_ip>\n");
        printf("Ornek   : ftp 10.0.2.15\n");
        return -1;
    }

    uint32_t ip;
    if (!ftp_parse_ip(argv[1], &ip)) {
        printf("ftp: gecersiz IP adresi: %s\n", argv[1]);
        return -1;
    }
    g_server_ip_net = ftp_htonl(ip);

    int ctrl = socket(AF_INET, SOCK_STREAM, 0);
    if (ctrl < 0) {
        printf("ftp: socket() basarisiz\n");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = ftp_htons(FTP_CTRL_PORT);
    addr.sin_addr   = g_server_ip_net;

    printf("ftp: %s baglaniliyor...\n", argv[1]);

    if (connect(ctrl, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("ftp: baglanti baslatilamadi\n");
        return -1;
    }

    /* El sikismasi icin kisa bekleme (bkz open_data_socket yorumu) */
    sleep(200);

    char line[LINE_BUF];
    int n = ftp_recv_line(ctrl, line, sizeof(line));
    if (n < 0) {
        printf("ftp: sunucuya baglanilamadi (port %d acik mi?)\n", FTP_CTRL_PORT);
        close(ctrl);
        return -1;
    }
    printf("%s\n", line);

    printf("Komutlar: user <ad>, pass <sifre>, pwd, ls, get <dosya>, put <dosya>, quit\n");

    char cmdbuf[LINE_BUF];
    while (1) {
        printf("ftp> ");
        int len = read(STDIN, cmdbuf, sizeof(cmdbuf) - 1);
        if (len <= 0) continue;
        cmdbuf[len] = '\0';
        ftp_strip_newline(cmdbuf);

        char *arg = split_local(cmdbuf);

        if (cmdbuf[0] == '\0') {
            continue;
        } else if (strcmp(cmdbuf, "user") == 0) {
            do_simple(ctrl, "USER", arg);
        } else if (strcmp(cmdbuf, "pass") == 0) {
            do_simple(ctrl, "PASS", arg);
        } else if (strcmp(cmdbuf, "pwd") == 0) {
            do_simple(ctrl, "PWD", "");
        } else if (strcmp(cmdbuf, "syst") == 0) {
            do_simple(ctrl, "SYST", "");
        } else if (strcmp(cmdbuf, "ls") == 0 || strcmp(cmdbuf, "dir") == 0) {
            cmd_ls(ctrl);
        } else if (strcmp(cmdbuf, "get") == 0) {
            if (arg[0] == '\0') { printf("Kullanim: get <dosya>\n"); continue; }
            cmd_get(ctrl, arg);
        } else if (strcmp(cmdbuf, "put") == 0) {
            if (arg[0] == '\0') { printf("Kullanim: put <dosya>\n"); continue; }
            cmd_put(ctrl, arg);
        } else if (strcmp(cmdbuf, "quit") == 0 || strcmp(cmdbuf, "exit") == 0) {
            ftp_send_line(ctrl, "QUIT");
            char resp[LINE_BUF];
            ftp_recv_line(ctrl, resp, sizeof(resp));
            printf("%s\n", resp);
            break;
        } else if (strcmp(cmdbuf, "help") == 0) {
            printf("user <ad>, pass <sifre>, pwd, ls, get <dosya>, put <dosya>, quit\n");
        } else {
            printf("Bilinmeyen komut: %s (yardim icin 'help')\n", cmdbuf);
        }
    }

    close(ctrl);
    return 0;
}
