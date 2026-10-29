#include <netdb.h>
#include "version.h"
/* Ask the routing table which local address reaches the configured Primary.
   UDP connect selects a route without sending any packets. */
static void health_local_ip(const char *base,char *out,size_t size){
 out[0]=0;CURLU *url=curl_url();char *host=NULL,*port=NULL;
 struct addrinfo hints={0},*addresses=NULL;
 if(!url)return;
 if(curl_url_set(url,CURLUPART_URL,base,0)!=CURLUE_OK||
    curl_url_get(url,CURLUPART_HOST,&host,0)!=CURLUE_OK)goto done;
 if(curl_url_get(url,CURLUPART_PORT,&port,CURLU_DEFAULT_PORT)!=CURLUE_OK)goto done;
 /* libcurl retains brackets around IPv6 URL hosts; getaddrinfo expects none. */
 if(host[0]=='['){size_t n=strlen(host);if(n>2&&host[n-1]==']'){memmove(host,host+1,n-2);host[n-2]=0;}}
 hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_DGRAM;
 if(getaddrinfo(host,port,&hints,&addresses))goto done;
 for(struct addrinfo *a=addresses;a;a=a->ai_next){
  int fd=socket(a->ai_family,a->ai_socktype,a->ai_protocol);if(fd<0)continue;
  struct sockaddr_storage local;socklen_t len=sizeof local;
  if(!connect(fd,a->ai_addr,a->ai_addrlen)&&!getsockname(fd,(struct sockaddr*)&local,&len)){
   if(!getnameinfo((struct sockaddr*)&local,len,out,(socklen_t)size,NULL,0,NI_NUMERICHOST)){
    char *scope=strchr(out,'%');if(scope)*scope=0;close(fd);break;
   }
  }
  close(fd);
 }
 done:if(addresses)freeaddrinfo(addresses);curl_free(host);curl_free(port);curl_url_cleanup(url);
}
/* Independent 30-second heartbeat. Network thread only; never blocks RFID input. */
static void publish_health(const char*base,const char*key,const char*clock){
 sqlite3_stmt*q=NULL;int pending=0,rejected=0,healthy=1;char oldest[40]="";
 if(sqlite3_prepare_v2(db,"SELECT count(*),coalesce(min(swiped_at),'') FROM pending",-1,&q,NULL)!=SQLITE_OK)healthy=0;
 else if(sqlite3_step(q)!=SQLITE_ROW)healthy=0;else{pending=sqlite3_column_int(q,0);snprintf(oldest,sizeof oldest,"%s",(const char*)sqlite3_column_text(q,1));}
 sqlite3_finalize(q);q=NULL;
 if(sqlite3_prepare_v2(db,"SELECT count(*) FROM rejected WHERE synced_at IS NULL",-1,&q,NULL)!=SQLITE_OK)healthy=0;
 else if(sqlite3_step(q)!=SQLITE_ROW)healthy=0;else rejected=sqlite3_column_int(q,0);
 sqlite3_finalize(q);
 char ip[64];health_local_ip(base,ip,sizeof ip);
 char url[768],header[256],body[512];snprintf(url,sizeof url,"%s/api/v1/clock/heartbeat",base);snprintf(header,sizeof header,"X-Device-Key: %s",key);
 snprintf(body,sizeof body,"clock_id=%s&pending=%d&oldest=%s&rejected=%d&healthy=%d&synced=%d&ip_address=%s&software_version=%s",clock,pending,oldest,rejected,healthy,time_synchronized(),ip,TIMECLOCK_VERSION);
 struct buffer reply={0};CURL*c=curl_easy_init();if(!c)return;struct curl_slist*h=curl_slist_append(NULL,header);h=curl_slist_append(with_clock_id(h),"Content-Type: application/x-www-form-urlencoded");curl_easy_setopt(c,CURLOPT_URL,url);curl_easy_setopt(c,CURLOPT_HTTPHEADER,h);curl_easy_setopt(c,CURLOPT_POSTFIELDS,body);curl_easy_setopt(c,CURLOPT_TIMEOUT_MS,1500L);curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,collect);curl_easy_setopt(c,CURLOPT_WRITEDATA,&reply);CURLcode result=curl_easy_perform(c);long status=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&status);
 if(result==CURLE_OK&&status==200&&reply.data&&strstr(reply.data,"\"status\":\"received\"")){comms_mark_ok();atomic_store(&server_online,1);}free(reply.data);curl_slist_free_all(h);curl_easy_cleanup(c);
}
