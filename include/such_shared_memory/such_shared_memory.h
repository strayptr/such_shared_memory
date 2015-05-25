#pragma once

#if defined( __cplusplus )
extern "C" {
#endif

#define SSM_VERSION     0x00000001


#if defined( _WIN32 ) || defined( __WIN32__ )
# ifdef such_shared_memory_EXPORTS  // added by CMake.
#  define SSM_API extern __declspec(dllexport)
# else
#  define SSM_API extern __declspec(dllimport)
# endif
#else
# define SSM_API 
#endif

#include <stdint.h>

struct ssm_s;
typedef struct ssm_s ssm_t;

#define SSM_OPEN_MUST_CREATE        (1 << 0)
#define SSM_OPEN_MUST_NOT_CREATE    (1 << 1)

SSM_API ssm_t*      such_shared_memory_open( int version, const char* name, int64_t size, int flags );
SSM_API void        such_shared_memory_close( ssm_t* ssm );


#if defined( __cplusplus )
}
#endif

