// Windows client configuration header

#ifndef CONFIG_H_INCLUDED
#define CONFIG_H_INCLUDED

// include common Windows header
#include "config_common.h"

// defines for data directories in Windows
#ifndef DEBUG
#define USER_DATA_DIR  "${APPDATA}/Armagetron"
#define SCREENSHOT_DIR "${MYPICTURES}/Armagetron"
#else
#define USER_DATA_DIR  "."
#endif

#endif
