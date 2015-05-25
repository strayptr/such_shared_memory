#include <such_shared_memory.h>

//------------------------------------------------------------------------------
int
main()
{
  ssm_t* ssm = such_shared_memory_open( "ssm_test", 8192 );

  such_shared_memory_close( ssm );

  return 0;
}

