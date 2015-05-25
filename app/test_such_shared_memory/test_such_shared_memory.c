#include <such_shared_memory.h>

#include <assert.h>
#include <stdlib.h>

//------------------------------------------------------------------------------
int
main()
{
  // test invalid version.
  {
    ssm_t* ssm = such_shared_memory_open( -1, "ssm_test", 8192 );
    assert( ssm == NULL );
  }

  // test basic usage.
  {
    ssm_t* ssm = such_shared_memory_open( SSM_VERSION, "ssm_test", 8192 );

    such_shared_memory_close( ssm );
  }

  return 0;
}

