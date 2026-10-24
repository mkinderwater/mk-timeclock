#define main mailer_program_main
#include "../src/mailer.c"
#undef main
int main(int argc,char **argv){
 const char *body="Missed punches for shifts due 2026-10-17:\n- Rylie Kinderwater: No IN\nRésumé: vérifié\n";
 const unsigned char xlsx[]={0x50,0x4b,0x03,0x04,0x00,0xff,0x01};
 char *msg=build_message("timeclock@example.com","payroll@example.com","Attendance: résumé",body,argc>1?xlsx:NULL,argc>1?sizeof(xlsx):0,"timesheet.xlsx");
 if(!msg)return 1;
 fputs(msg,stdout);free(msg);(void)argv;return 0;
}
