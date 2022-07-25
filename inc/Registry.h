#pragma once

extern FILE *registry;  // our opened file

bool RegLoadString(LPCTSTR, LPCTSTR, bool, char **, unsigned int);

bool RegLoadValue(LPCTSTR, LPCTSTR, bool, unsigned int *);

bool RegLoadBool(LPCTSTR, LPCTSTR, bool, bool *);

void RegSaveString(LPCTSTR, LPCTSTR, bool, LPCTSTR);
void RegSaveValue(LPCTSTR, LPCTSTR, bool, unsigned int);
void RegSaveBool(LPCTSTR, LPCTSTR, bool, bool);

void RegConfPath(const char *);

char *php_trim(char *c, int len);  // trimming string like PHP function trim does!

