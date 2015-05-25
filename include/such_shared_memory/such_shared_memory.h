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

#define SSMF_MUST_CREATE            0x01

#define SSMV_ALL        -1 /* will log everything. */
#define SSMV_ERRORS      0 /* will log errors only. */
#define SSMV_WARNINGS    1 /* will log errors and warnings only. */
#define SSMV_INFO        2 /* will log errors, warnings, and info messages. */
#define SSMV_QUIET      10 /* will not log anything. */

SSM_API ssm_t*      such_shared_memory_open( int ssm_version, int ssmv_verbosity, const char* name, int64_t size, int ssmf_flags );
SSM_API void        such_shared_memory_close( ssm_t* ssm );


#if defined( __cplusplus )
}
#endif

