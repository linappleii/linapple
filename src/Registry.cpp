/*
AppleWin : An Apple //e emulator for Windows

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2007, Tom Charlesworth, Michael Pohoreski

AppleWin is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

AppleWin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with AppleWin; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Description: Registry module
 *
 * Author: Various
 */

/* Adaptation for SDL and POSIX (l) by beom beotiger, Nov-Dec 2007 */

#include "stdafx.h"
//#pragma  hdrstop

FILE * registry;

// the following 3 functions are from PHP 5.0 with Zend engine sources
// I'll tell, folks, PHP group is just great! -- bb
void php_charmask(char *input, int len, char *mask)
{
  char *end;
  char c;

  memset(mask, 0, 256);
  for (end = input+len; input < end; input++) {
                c=*input;
                mask[(unsigned int)c]=1;
  }

}

char *estrndup(const char *s, uint length)
{
  char *p;

  p = (char *) malloc(length+1);
  if (!p) {
    return (char *)NULL;
  }
  memcpy(p, s, length);
  p[length] = 0;
  return p;
}

char *php_trim(char *c, int len)
{
  register int i;
  int trimmed = 0;
  char mask[256];
  static char maskVal[] = " \n\r\t\v\0";

  
  php_charmask(maskVal, 6, mask);

// trim chars from beginning of the line
  for (i = 0; i < len; i++) {
    if (mask[(unsigned char)c[i]]) {
      trimmed++;
    } else {
      break;
    }
  }
  len -= trimmed;
  c += trimmed;

// trim chars from line end
  for (i = len - 1; i >= 0; i--) {
    if (mask[(unsigned char)c[i]]) {
      len--;
    } else {
      break;
    }
  }
  return estrndup(c, len); // from c to c+len
}



BOOL ReturnKeyValue(char * line, char ** key, char ** value)
{
// line should be:  some key  =  some value
// functions returns trimmed key and value
  char * br = strchr(line, '=');
  if(!br) return FALSE; // no sign of '=' sign. Sorry for some kalambur --bb
  *br = '\0'; // cut the string where '=' is (or was)
  br++; //to the value
  *key = php_trim(line, strlen(line)); // trim those strings from beginning and trailing spaces
  if(*key != NULL && **key == '#') return FALSE; // omit comments (lines with #)
  *value = php_trim(br, strlen(br));
//  printf("----- ReturnKeyValue: *key = %s, *value = %s\n", *key, *value);
  if(*key && *value) return TRUE;
  return FALSE;
}

#define BUFSIZE   256

char *ReadRegString(char *key)
{
  // reads key for given value from the registry. Hmmm. What registry in Linux? I donna. --bb
  fseek(registry, 0, SEEK_SET); //to the start of file
  char *mkey;
  char *mvalue;
  char line[BUFSIZE];
  int nkey = strlen(key);  // length of key
  while(fgets(line, BUFSIZE, registry))
    if(ReturnKeyValue(line, &mkey, &mvalue) && (!strncmp(mkey, key, nkey)))
      return mvalue;
  return NULL; // key has not been found in registry?
}


//===========================================================================
BOOL RegLoadString (LPCTSTR section, LPCTSTR key, BOOL peruser,
                    char** buffer, DWORD chars) {

// will ignore section, peruser
  BOOL  success = FALSE;
  char *value;
/*  TCHAR fullkeyname[256];
  wsprintf(fullkeyname,
           TEXT("Software\\AppleWin\\CurrentVersion\\%s"),
           (LPCTSTR)section);
  HKEY keyhandle;
  if (!RegOpenKeyEx((peruser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE),
                    fullkeyname,
                    0,
                    KEY_READ,
                    &keyhandle)) {
    DWORD type;
    DWORD size = chars;
    success = (!RegQueryValueEx(keyhandle,key,0,&type,(LPBYTE)buffer,&size)) &&
                                size;
    RegCloseKey(keyhandle);
  }*/
  value = ReadRegString((char*)key); // read value for a given keyhandle
  if(value) {
    success = TRUE; // success!
    if(strlen(value) > chars) value[chars] = '\0'; // cut string
    *buffer = strdup(value);
  }
  return success;
}

//===========================================================================
BOOL RegLoadValue (LPCTSTR section, LPCTSTR key, BOOL peruser, DWORD *value) {
  if (!value) return 0;

//  TCHAR buffer[32] = TEXT("");
//  printf("Getting value...\n");
  char *sztmp;
  if (!RegLoadString(section, key, peruser, &sztmp, 32))
    return 0;
//  strncpy(buffer, sztmp, 32);
//  buffer[31] = 0;
  *value = (DWORD)atoi(sztmp);
//  printf("Value gotten:%d\n", *value);
  return 1;
}


void RegSaveKeyValue(char * NKey, char * NValue)
{
#ifdef REGISTRY_WRITEABLE
  char MyStr[BUFSIZE];
  char line[BUFSIZE];
  char templine[BUFSIZE];
  char *sztmp;
  FILE * tempf = tmpfile();  // open temp file
//  FILE * tempf = fopen("linapple-temp.conf", "w+");
  if(!tempf) return;
  snprintf(MyStr, BUFSIZE, "\t%s =\t%s\n", NKey, NValue);  // prepare string
  fseek(registry, 0, SEEK_SET);  //
  bool found = false;

  while(fgets(line, BUFSIZE, registry))
  {
//    printf("---1:%s", line);
    strcpy(templine, line);
    if(ReturnKeyValue(templine, &sztmp, &NValue) && !(strcmp(sztmp, NKey)))
    {
      fputs(MyStr, tempf);
      found = true;
//      printf("------ !!!!!!!!!!!!!!!! I FOUND IT!!!!!\n\n");
    }
    else fputs(line, tempf);
//    printf("---2:%s", line);
  }

  if(!found) fputs(MyStr, tempf);
  // now swap tempf and registry
  fclose(registry);

  fflush(tempf);
  fseek(tempf, 0, SEEK_SET);
//  fclose(tempf);
//  return;
  registry = fopen(REGISTRY, "w+t");  // erase if been
  while(fgets(line, BUFSIZE, tempf)) {
    fputs(line, registry);
//    printf("---Saving Line:%s", line);
  }
  fflush(registry);
  fclose(tempf);
//  fflush(registry);  // for chance... --bb
  // do not close registry, it should be open while emu working...
#else
        printf("Attempt to set '%s' to '%s' ignored (registry is read-only)\n", NKey, NValue);
#endif /* REGISTRY_WRITEABLE */
}

//===========================================================================
void RegSaveString (LPCTSTR section, LPCTSTR key, BOOL peruser, LPCTSTR buffer) {
  RegSaveKeyValue((char*)key, (char*)buffer);
/*  TCHAR fullkeyname[256];
  wsprintf(fullkeyname,
           TEXT("Software\\AppleWin\\CurrentVersion\\%s"),
           (LPCTSTR)section);
  HKEY  keyhandle;
  DWORD disposition;
  if (!RegCreateKeyEx((peruser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE),
                      fullkeyname,
                      0,
                      NULL,
                      REG_OPTION_NON_VOLATILE,
                      KEY_READ | KEY_WRITE,
                      (LPSECURITY_ATTRIBUTES)NULL,
                      &keyhandle,
                      &disposition)) {
    RegSetValueEx(keyhandle,
                  key,
                  0,
                  REG_SZ,
                  (CONST BYTE *)buffer,
                  (_tcslen(buffer)+1)*sizeof(TCHAR));
    RegCloseKey(keyhandle);
  }*/
}

//===========================================================================
void RegSaveValue (LPCTSTR section, LPCTSTR key, BOOL peruser, DWORD value) {
  TCHAR buffer[33] = TEXT("");
//  _ultot(value,buffer,10);
//  _itoa(value, buffer, 10);  // convert value to string
  snprintf(buffer, 32, "%d", value);

  RegSaveString(section,key,peruser,buffer);
}
