//
//  Common.h
//

#ifndef _COMMON_H
#define _COMMON_H

#define NOINLINE __attribute__((noinline))
#define EXPORT __attribute__((visibility("default")))
#define PRIVATE __attribute__((visibility("hidden"))) NOINLINE

#endif // _COMMON_H
