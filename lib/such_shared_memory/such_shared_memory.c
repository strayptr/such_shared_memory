#include "such_shared_memory.h"

#include <stdlib.h>
#include <stdio.h>

//==============================================================================
// struct ssm_s
//==============================================================================
struct ssm_s
{
  int foo;
};

//------------------------------------------------------------------------------
SSM_API ssm_t*
such_shared_memory_open( int version, const char* name, int64_t size )
{
  if ( version != SSM_VERSION ) {
    fprintf( stderr, "such_shared_memory_open(\"%s\"): version mismatch.  Expected %d, got %d\n", name, SSM_VERSION, version );
    return NULL;
  }

  return NULL;
}

//------------------------------------------------------------------------------
SSM_API void
such_shared_memory_close( ssm_t* ssm )
{
}

//==============================================================================
// private functionality.
//==============================================================================


