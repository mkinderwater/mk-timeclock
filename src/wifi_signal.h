#ifndef TC_WIFI_SIGNAL_H
#define TC_WIFI_SIGNAL_H
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/wireless.h>
/* A cheap, nonblocking snapshot. No scanning, external commands or network requests. */
struct tc_wifi {int dbm; int bars; int associated; int bitrate_mbps; char ssid[IW_ESSID_MAX_SIZE+1];};
static inline struct tc_wifi tc_wifi_read(void){
 struct tc_wifi w={0}; FILE *f=fopen("/proc/net/wireless","r");
 if(f){char line[256];while(fgets(line,sizeof line,f)){
  char name[32];float quality,level,noise;
  if(sscanf(line," %31[^:]: %*x %f %f %f",name,&quality,&level,&noise)==4 && !strcmp(name,"wlan0")){
   (void)quality;(void)noise;
   if(level <= -10 && level >= -120){w.dbm=(int)level;w.associated=1;}
   break;
  }
 }fclose(f);}
 if(w.associated){
  w.bars=w.dbm>=-55?4:w.dbm>=-65?3:w.dbm>=-75?2:w.dbm>=-85?1:0;
  int sock=socket(AF_INET,SOCK_DGRAM,0);
  if(sock>=0){struct iwreq req;memset(&req,0,sizeof req);snprintf(req.ifr_name,IFNAMSIZ,"wlan0");
   req.u.essid.pointer=w.ssid;req.u.essid.length=IW_ESSID_MAX_SIZE;req.u.essid.flags=0;
   if(ioctl(sock,SIOCGIWESSID,&req)<0)w.ssid[0]=0;
   w.ssid[IW_ESSID_MAX_SIZE]=0;
   memset(&req,0,sizeof req);snprintf(req.ifr_name,IFNAMSIZ,"wlan0");
   if(ioctl(sock,SIOCGIWRATE,&req)==0)w.bitrate_mbps=req.u.bitrate.value/1000000;
   close(sock);
  }
 }
 return w;
}
#endif
