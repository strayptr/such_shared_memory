#pragma once

#if defined( __cplusplus )
extern "C" {
#endif


#if defined( _WIN32 ) || defined( __WIN32__ )
# ifdef such_shared_memory_EXPORTS  // added by CMake.
#  define SSM_DLL extern __declspec(dllexport)
# else
#  define SSM_DLL extern __declspec(dllimport)
# endif
#else
# define SSM_DLL 
#endif

#include <stdint.h>

struct ssm_s;
typedef struct ssm_s ssm_t;

SSM_DLL ssm_t*      such_shared_memory_open( const char* name, int64_t size );
SSM_DLL void        such_shared_memory_close( ssm_t* ssm );


#if defined( __cplusplus )
}
#endif

