#ifndef __UNEMA_DFB_H__
#define __UNEMA_DFB_H__

/// Enables debug messages for graphics operations
//#define DEBUG_OPERATIONS

/// Enables debug messages on procedure entry.
//#define DEBUG_PROCENTRY

//#define DEBUG_ASSERT

#ifdef DEBUG_ASSERT
#include <assert.h>
#define DEBUG_ASSERT(args...) assert(args)
#else
#define DEBUG_ASSERT(args...)
#endif

#ifdef DEBUG_OPERATIONS
#define DEBUG_OP(args...)    printf(args)
#else
#define DEBUG_OP(args...)
#endif

#ifdef DEBUG_PROCENTRY
#define DEBUG_PROC_ENTRY    do { printf("* %s\n", __FUNCTION__); } while (0)
#else
#define DEBUG_PROC_ENTRY
#endif



#endif
