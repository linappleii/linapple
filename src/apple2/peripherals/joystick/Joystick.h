#include <array>
#include <cstdint>

#include "core/Common.h"
#pragma once

typedef struct tagSS_IO_Joystick SS_IO_Joystick;

enum JOYNUM { JN_JOYSTICK0 = 0, JN_JOYSTICK1 };

// Joystick peripheral now uses the standardized Peripheral ABI.
// All interactions should go through Peripheral_Command and Peripheral_Query
// using the IDs defined in JoystickCommands.h.
