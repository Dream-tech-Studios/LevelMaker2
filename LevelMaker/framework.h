// framework.h : include file for standard system include files,
// or project specific include files
//

#pragma once

#include "targetver.h"
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#define NOMINMAX                        // keep std::min / std::max usable
// Windows Header Files
#include <windows.h>
#include <windowsx.h>                   // GET_X_LPARAM / GET_Y_LPARAM
#include <commdlg.h>                    // GetOpenFileName / GetSaveFileName
// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
