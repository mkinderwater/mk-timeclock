#ifndef EMPLOYEE_NUMBER_H
#define EMPLOYEE_NUMBER_H
#include <stdio.h>
#include <sqlite3.h>
/* Public employee identifier; database relationships always use employees.id. */
#define EMPLOYEE_NUMBER_SQL(alias) "coalesce(nullif(" alias ".employee_number,''),cast(" alias ".id AS TEXT))"
static void employee_number(sqlite3 *db,int id,char *out,size_t size){
 snprintf(out,size,"%d",id);sqlite3_stmt *q=NULL;
 if(sqlite3_prepare_v2(db,"SELECT " EMPLOYEE_NUMBER_SQL("employees") " FROM employees WHERE id=?1",-1,&q,NULL)==SQLITE_OK){
  sqlite3_bind_int(q,1,id);if(sqlite3_step(q)==SQLITE_ROW)snprintf(out,size,"%s",sqlite3_column_text(q,0));
 }
 sqlite3_finalize(q);
}
#endif
