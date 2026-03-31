#include <cstdint>
#pragma once

extern FILE *registry;  // our opened file

bool RegLoadString(const char*, const char*, bool, char **, unsigned int);

bool RegLoadValue(const char*, const char*, bool, unsigned int *);

bool RegLoadBool(const char*, const char*, bool, bool *);

void RegSaveString(const char*, const char*, bool, const char*);
void RegSaveValue(const char*, const char*, bool, unsigned int);
void RegSaveBool(const char*, const char*, bool, bool);

void RegConfPath(const char *);

char *php_trim(char *c, int len);  // trimming string like PHP function trim does!

