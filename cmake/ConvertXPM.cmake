file(READ "${XPM_FILE}" XPM_CONTENT)
string(REPLACE "static char *${BASE_NAME}[]" "static const char * const ${BASE_NAME}_xpm[]" XPM_CONTENT "${XPM_CONTENT}")
string(REPLACE "static const char *${BASE_NAME}[]" "static const char * const ${BASE_NAME}_xpm[]" XPM_CONTENT "${XPM_CONTENT}")
file(WRITE "${XPM_FILE}" "${XPM_CONTENT}")
