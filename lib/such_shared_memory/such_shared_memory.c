#include "such_shared_memory.h"

#include <stdlib.h>

//==============================================================================
// struct ssm_s
//==============================================================================
struct ssm_s
{
  int foo;
};

//------------------------------------------------------------------------------
SSM_DLL ssm_t*
such_shared_memory_open( const char* name, int64_t size )
{
  return NULL;
}

//------------------------------------------------------------------------------
SSM_DLL void
such_shared_memory_close( ssm_t* ssm )
{
}

