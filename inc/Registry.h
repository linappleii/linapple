#pragma once

extern FILE * registry;  // our opened file

BOOL    RegLoadString (LPCTSTR,LPCTSTR,BOOL,char**,DWORD);
BOOL    RegLoadValue (LPCTSTR,LPCTSTR,BOOL,DWORD *);
BOOL  RegLoadBool(LPCTSTR,LPCTSTR,BOOL,BOOL *);

void    RegSaveString (LPCTSTR,LPCTSTR,BOOL,LPCTSTR);
void    RegSaveValue (LPCTSTR,LPCTSTR,BOOL,DWORD);
void    RegSaveBool (LPCTSTR,LPCTSTR,BOOL,BOOL);

char  *php_trim(char *c, int len);  // trimming string like PHP function trim does!

