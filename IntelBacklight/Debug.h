//
//  Debug.h
//

#include <IOKit/IOLib.h>

#ifndef _DEBUG_H
#define _DEBUG_H

#define DEBUG_PREFIX "IntelBacklight: "

#ifdef DEBUG
#define DebugLog(arg...) do { IOLog(DEBUG_PREFIX arg); } while (0)
#define DebugOnly(expr) do { expr; } while (0)
#else
#define DebugLog(arg...) do { } while (0)
#define DebugOnly(expr) do { } while (0)
#endif

#define AlwaysLog(arg...) do { IOLog(DEBUG_PREFIX arg); } while (0)

#endif // _DEBUG_H
