#ifndef TIMECLOCK_EMAIL_RENDER_H
#define TIMECLOCK_EMAIL_RENDER_H
#include <stdlib.h>
#include <string.h>
struct email_field {const char *key; const char *value;};
static char *email_render(const char *source,const struct email_field *fields,size_t count){
 if(!source)return NULL;
 size_t cap=32768,pos=0;char *out=malloc(cap);if(!out)return NULL;
 for(const char *p=source;*p;){size_t k;
  for(k=0;k<count;k++){size_t len=strlen(fields[k].key);if(len&&strncmp(p,fields[k].key,len)==0){const char*v=fields[k].value?fields[k].value:"";size_t n=strlen(v);if(n>=cap-pos){free(out);return NULL;}memcpy(out+pos,v,n);pos+=n;p+=len;break;}}
  if(k==count){if(pos+1>=cap){free(out);return NULL;}out[pos++]=*p++;}
 }
 out[pos]=0;return out;
}
#endif
