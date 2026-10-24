"""Host-side SSD1309 command-sequence regression. No physical OLED required."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = r'''
#include <stdint.h>
#include <unistd.h>
#include "oled.h"
static int check(unsigned value,uint8_t pre,uint8_t vcom){
  int p[2];if(pipe(p))return 1;
  oled_t o={0};o.fd=p[1];
  if(oled_contrast(&o,value))return 2;
  uint8_t expected[12]={0,0x81,0,(uint8_t)value,0,0xD9,0,pre,0,0xDB,0,vcom};
  uint8_t received[12];
  if(read(p[0],received,sizeof received)!=(ssize_t)sizeof received)return 3;
  for(int i=0;i<12;i++)if(received[i]!=expected[i])return 4;
  close(p[0]);close(p[1]);return 0;
}
int main(void){
  if(check(1,0x11,0x00))return 1;
  if(check(31,0x11,0x00))return 2;
  if(check(32,0x22,0x34))return 3;
  if(check(95,0x22,0x34))return 4;
  if(check(96,0xF1,0x3C))return 5;
  if(check(255,0xF1,0x3C))return 6;
  oled_t invalid={.fd=-1};
  if(oled_contrast(&invalid,10)!=-1)return 7;
  if(oled_contrast(&invalid,256)!=-1)return 8;
  return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    (td/'test.c').write_text(source)
    subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror','-std=c11','-D_POSIX_C_SOURCE=200809L',
                    '-I',str(root/'src'),str(td/'test.c'),str(root/'src/oled.c'),'-o',str(td/'test')],check=True)
    subprocess.run([str(td/'test')],check=True)
