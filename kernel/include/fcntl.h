/* =============================================================================
 * ZeruX OS - File Control Options
 * File: kernel/include/fcntl.h
 * =============================================================================
 */

#ifndef FCNTL_H
#define FCNTL_H

/* File access modes */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_ACCMODE   0x0003

/* File creation and status flags */
#define O_CREAT     0x0100
#define O_EXCL      0x0200
#define O_NOCTTY    0x0400
#define O_TRUNC     0x1000
#define O_APPEND    0x2000
#define O_NONBLOCK  0x4000

/* fcntl commands */
#define F_DUPFD     0
#define F_GETFD     1
#define F_SETFD     2
#define F_GETFL     3
#define F_SETFL     4

#endif /* FCNTL_H */
