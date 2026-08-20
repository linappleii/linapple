#include <cstddef>

#include "Debugger_Commands.h"
#include "Debugger_Types.h"
#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

auto CmdDisk(int nArgs) -> Update_t {
  (void)nArgs;
  // TODO
  return UPDATE_CONSOLE_DISPLAY;
}
