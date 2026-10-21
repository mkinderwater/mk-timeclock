#define _POSIX_C_SOURCE 200809L
#include "rc522.h"
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
enum { CMD=0x01, COMIRQ=0x04, DIVIRQ=0x05, ERR=0x06,
       FIFODATA=0x09, FIFOLEVEL=0x0A, CONTROL=0x0C,
       BITFRAMING=0x0D, COLL=0x0E, MODE=0x11, TXCONTROL=0x14, TXASK=0x15,
       TMODE=0x2A, TPRESCALER=0x2B, TRELOADH=0x2C, TRELOADL=0x2D, VERSION=0x37 };
static void pause_us(long us){struct timespec t={us/1000000,(us%1000000)*1000};while(nanosleep(&t,&t)<0 && errno==EINTR){} }
static uint32_t spi_speed=250000;
static time_t last_health_check=0;
static unsigned error_streak=0;
static unsigned recovery_failures=0;
#define RC522_MAX_RECOVERY_FAILURES 5u
static int transfer(int fd,uint8_t a,uint8_t b,uint8_t *result){
 uint8_t tx[2]={a,b},rx[2]={0};struct spi_ioc_transfer tr={0};
 tr.tx_buf=(uintptr_t)tx;tr.rx_buf=(uintptr_t)rx;tr.len=2;tr.speed_hz=spi_speed;tr.bits_per_word=8;
 if(ioctl(fd,SPI_IOC_MESSAGE(1),&tr)<0)return -1;
 if(result) *result=rx[1];
 return 0;
}
static int write_reg(int fd,uint8_t r,uint8_t v){return transfer(fd,(uint8_t)((r<<1)&0x7E),v,NULL);}
static int read_reg(int fd,uint8_t r){uint8_t v;if(transfer(fd,(uint8_t)(((r<<1)&0x7E)|0x80),0,&v))return -1;return v;}
static int set_bits(int fd,uint8_t r,uint8_t b){int v=read_reg(fd,r);return v<0?-1:write_reg(fd,r,(uint8_t)(v|b));}
static int clear_bits(int fd,uint8_t r,uint8_t b){int v=read_reg(fd,r);return v<0?-1:write_reg(fd,r,(uint8_t)(v&~b));}
static int init_reader(int fd){
 if(write_reg(fd,CMD,0x0F))return -1;
 pause_us(50000);
 if(write_reg(fd,TMODE,0x8D)||write_reg(fd,TPRESCALER,0x3E)||
 write_reg(fd,TRELOADH,0)||write_reg(fd,TRELOADL,30)||
 write_reg(fd,TXASK,0x40)||write_reg(fd,MODE,0x3D)||
 set_bits(fd,TXCONTROL,0x03)||clear_bits(fd,COLL,0x80))return -1;
 return 0;
}
/* SPI writes cannot prove that an RC522 is present. Verify identity and
 * every configured register through independent reads. Do not treat absence
 * of a card as an error; the self-check works with an empty RF field. */
static int valid_version(int value){return value==0x91||value==0x92||value==0x12;}
static int verify_reader(int fd){
 int ver=read_reg(fd,VERSION);
 int mode=read_reg(fd,MODE),tmode=read_reg(fd,TMODE);
 int pre=read_reg(fd,TPRESCALER),hi=read_reg(fd,TRELOADH);
 int lo=read_reg(fd,TRELOADL),ask=read_reg(fd,TXASK);
 int antenna=read_reg(fd,TXCONTROL),coll=read_reg(fd,COLL);
 if(!valid_version(ver)||mode!=0x3D||tmode!=0x8D||pre!=0x3E||
    hi!=0||lo!=30||ask!=0x40||antenna<0||(antenna&0x03)!=0x03||
    coll<0||(coll&0x80)!=0){
  fprintf(stderr,"RC522 verification failed: version=0x%02X%s mode=0x%02X tmode=0x%02X "
   "prescaler=0x%02X reload=0x%02X%02X txask=0x%02X txcontrol=0x%02X coll=0x%02X\n",
   ver&0xFF,valid_version(ver)?"":" (unsupported/unreadable)",mode&0xFF,tmode&0xFF,
   pre&0xFF,hi&0xFF,lo&0xFF,ask&0xFF,antenna&0xFF,coll&0xFF);
  if(ver==0x88||ver==0x90)
   fprintf(stderr,"RC522 clone version 0x%02X is not validated; refusing startup\n",ver);
  errno=ENODEV;
  return -1;
 }
 return 0;
}
static int recover_reader(int fd){
 if(init_reader(fd)<0)return -1;
 return verify_reader(fd);
}
/* Return 1 for response, 0 for no RF reply, -1 for bus/protocol error. */
static int transceive(int fd,const uint8_t *tx,size_t tx_n,uint8_t lastbits,uint8_t *rx,size_t *rx_n,uint8_t *validbits,int verbose){
 if(write_reg(fd,CMD,0)||write_reg(fd,COMIRQ,0x7F)||write_reg(fd,FIFOLEVEL,0x80))return -1;
 for(size_t i=0;i<tx_n;i++)if(write_reg(fd,FIFODATA,tx[i]))return -1;
 if(write_reg(fd,BITFRAMING,lastbits)||write_reg(fd,CMD,0x0C)||set_bits(fd,BITFRAMING,0x80))return -1;
 int irq=0,err=0;for(int i=0;i<280;i++){
  irq=read_reg(fd,COMIRQ);if(irq<0)return -1;
  if(irq&0x31)break;
  pause_us(250);
 }
 if(clear_bits(fd,BITFRAMING,0x80))return -1;
 err=read_reg(fd,ERR);if(err<0)return -1;
 if(verbose)fprintf(stderr,"  IRQ=%02X ERROR=%02X",irq,err);
 if((irq&0x01)||!(irq&0x30)){
  if(verbose)fprintf(stderr," TIMEOUT\n");
  return 0;
 }
 if(err&0x1B){if(verbose)fprintf(stderr," RF_ERROR\n");return 0;}
 int n=read_reg(fd,FIFOLEVEL),ctl=read_reg(fd,CONTROL);
 if(n<0||ctl<0)return -1;
 if(verbose)fprintf(stderr," FIFO=%d bits=%d\n",n,ctl&7);
 if((size_t)n>*rx_n)return -1;
 for(int i=0;i<n;i++){int a=read_reg(fd,FIFODATA);if(a<0)return -1;rx[i]=(uint8_t)a;}
 *rx_n=(size_t)n;*validbits=(uint8_t)(ctl&7);
 return 1;
}
static int crc_reader(int fd,const uint8_t *data,size_t n,uint8_t crc[2]){
 if(write_reg(fd,CMD,0)||write_reg(fd,DIVIRQ,0x04)||write_reg(fd,FIFOLEVEL,0x80))return -1;
 for(size_t i=0;i<n;i++)if(write_reg(fd,FIFODATA,data[i]))return -1;
 if(write_reg(fd,CMD,0x03))return -1;
 int v=0;for(int i=0;i<250;i++){v=read_reg(fd,DIVIRQ);if(v<0)return -1;if(v&0x04)break;pause_us(200);}
 if(!(v&0x04))return -1;
 int low=read_reg(fd,0x22),high=read_reg(fd,0x21);
 if(low<0||high<0)return -1;
 crc[0]=(uint8_t)low;crc[1]=(uint8_t)high;return 0;
}
/* Some clones have erratic on-chip CRC. ISO14443-A software CRC crosscheck. */
static void crc_a(const uint8_t *data,size_t n,uint8_t out[2]){
 uint16_t crc=0x6363;
 for(size_t i=0;i<n;i++){
  uint8_t c=(uint8_t)(data[i]^(crc&0xff));c^=(uint8_t)(c<<4);
  crc=(uint16_t)((crc>>8)^((uint16_t)c<<8)^((uint16_t)c<<3)^((uint16_t)c>>4));
 }
 out[0]=(uint8_t)crc;out[1]=(uint8_t)(crc>>8);
}
/* Return full 4/7/10-byte UID; 0=no response; -1=error */
static int read_card(int fd,uint8_t uid[10],int *uid_n,int verbose){
 uint8_t resp[32],bits=0;size_t n;
 uint8_t wake=0x52; /* WUPA handles idle and HALT; also avoids REQA-only blind spots. */
 n=sizeof resp;int ok=transceive(fd,&wake,1,7,resp,&n,&bits,verbose);
 if(ok<=0)return ok;
 if(n!=2||bits!=0){if(verbose)fprintf(stderr,"  Unexpected ATQA length=%zu bits=%u\n",n,bits);return 0;}
 if(verbose)fprintf(stderr,"  ATQA=%02X %02X\n",resp[0],resp[1]);
 int total=0;
 for(int level=0;level<3;level++){
  uint8_t sel=(uint8_t)(0x93+level*2),ac[2]={sel,0x20};
  n=sizeof resp;bits=0;ok=transceive(fd,ac,2,0,resp,&n,&bits,verbose);
  if(ok<=0)return ok;
  if(n!=5||bits!=0){if(verbose)fprintf(stderr,"  Anticollision invalid size=%zu bits=%u level=%d\n",n,bits,level+1);return 0;}
  uint8_t bcc=(uint8_t)(resp[0]^resp[1]^resp[2]^resp[3]);
  if(bcc!=resp[4]){if(verbose)fprintf(stderr,"  BCC mismatch\n");return 0;}
  int cascade=resp[0]==0x88;
  int amount=cascade?3:4;
  if(total+amount>10)return -1;
  memcpy(uid+total,resp+(cascade?1:0),(size_t)amount);total+=amount;
  uint8_t select_frame[9]={sel,0x70,resp[0],resp[1],resp[2],resp[3],resp[4],0,0},swcrc[2];
  crc_a(select_frame,7,swcrc);
  if(verbose){uint8_t crc[2];if(crc_reader(fd,select_frame,7,crc)==0 && (crc[0]!=swcrc[0]||crc[1]!=swcrc[1]))
   fprintf(stderr,"  Hardware CRC disagrees with software CRC; using software\n");}
  select_frame[7]=swcrc[0];select_frame[8]=swcrc[1];
  n=sizeof resp;bits=0;ok=transceive(fd,select_frame,9,0,resp,&n,&bits,verbose);
  if(ok<=0)return ok;
  if(n!=3||bits!=0){if(verbose)fprintf(stderr,"  SELECT invalid SAK size=%zu bits=%u\n",n,bits);return 0;}
  uint8_t sak_crc[2];crc_a(resp,1,sak_crc);
  if(resp[1]!=sak_crc[0]||resp[2]!=sak_crc[1]){
   if(verbose)fprintf(stderr,"  SELECT SAK CRC mismatch\n");
   return 0;
  }
  if(verbose)fprintf(stderr,"  LEVEL %d SAK=%02X\n",level+1,resp[0]);
  if(!(resp[0]&0x04)){
   if(total!=4&&total!=7&&total!=10)return 0;
   /* HLTA: PICC may not ACK this command. A timeout is expected.
    * WUPA on the next poll reactivates a halted PICC. */
   uint8_t halt[4]={0x50,0x00,0,0};
   crc_a(halt,2,halt+2);
   size_t halt_n=sizeof resp;uint8_t halt_bits=0;
   int halted=transceive(fd,halt,sizeof halt,0,resp,&halt_n,&halt_bits,0);
   if(halted<0 && verbose)fprintf(stderr,"  HLTA SPI error (UID already selected)\n");
   *uid_n=total;return 1;
  }
 }
 return 0;
}

int rc522_open(const char *dev,int speed){
 int fd=open(dev,O_RDWR|O_CLOEXEC);if(fd<0)return -1;
 uint8_t mode=SPI_MODE_0,bits=8;
 /* The card tester was validated at 250 kHz on this RC522 clone. */
 spi_speed=(uint32_t)((speed>0 && speed<=250000)?speed:250000);
 if(ioctl(fd,SPI_IOC_WR_MODE,&mode)<0 || ioctl(fd,SPI_IOC_WR_BITS_PER_WORD,&bits)<0 ||
    ioctl(fd,SPI_IOC_WR_MAX_SPEED_HZ,&spi_speed)<0 || recover_reader(fd)<0){
    close(fd);return -1;
 }
 fprintf(stderr,"RC522 verified version=0x%02X SPI=%u Hz (WUPA + cascade select)\n",read_reg(fd,VERSION),spi_speed);
 return fd;
}
/* Periodic health checks validate the full configuration, not just identity. */
static int health(int fd){
 if(verify_reader(fd)==0)return 0;
 if(recover_reader(fd)==0){
  fprintf(stderr,"RC522 configuration restored and verified\n");
  return 0;
 }
 return -1;
}
/* -2 is a terminal hardware failure: the agent must exit so systemd can
 * restart it. A successful SPI write or unverified reset never clears this. */
static int failed_check(void){
 if(++recovery_failures>=RC522_MAX_RECOVERY_FAILURES){
  fprintf(stderr,"RC522 unavailable after %u failed verified recoveries\n",recovery_failures);
  return -2;
 }
 if(recovery_failures==1)fprintf(stderr,"RC522 unhealthy; retrying verified recovery\n");
 return -1;
}
int rc522_poll(int fd,char uid_text[24]){
 time_t now=time(NULL);
 if(now!=last_health_check){
  last_health_check=now;
  if(health(fd)<0)return failed_check();
  recovery_failures=0;
 }
 uint8_t uid[10]={0};int n=0;
 int status=read_card(fd,uid,&n,0);
 if(status<0){
  if(++error_streak>=5){
   error_streak=0;
   if(recover_reader(fd)<0)return failed_check();
   recovery_failures=0;
   fprintf(stderr,"RC522 communication recovered and verified\n");
  }
  return -1;
 }
 error_streak=0;
 if(status==0)return 0;
 if(n!=4&&n!=7&&n!=10)return 0;
 /* A zero UID can pass BCC; never forward it to attendance/enrollment. */
 int all_zero=1;
 for(int i=0;i<n;i++)if(uid[i]!=0){all_zero=0;break;}
 if(all_zero)return 0;
 for(int i=0;i<n;i++)snprintf(uid_text+2*i,24-2*i,"%02X",uid[i]);
 uid_text[n*2]=0;
 return 1;
}
