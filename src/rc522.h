#ifndef TC_RC522_H
#define TC_RC522_H
int rc522_open(const char *device,int speed);
/* 1 full 4/7/10-byte UID, 0 no card, -1 hardware/transport error. */
int rc522_poll(int fd,char uid[24]);
#endif
