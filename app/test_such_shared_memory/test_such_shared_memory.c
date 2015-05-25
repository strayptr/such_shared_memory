#include <such_shared_memory.h>

#include <assert.h>
#include <stdlib.h>

//------------------------------------------------------------------------------
int
main()
{
  int verbosity = SSMV_ALL;

  // test invalid version.
  {
    ssm_t* ssm = such_shared_memory_open( -1, verbosity, "ssm_test_version_mismatch", 10000, 0 );
    assert( ssm == NULL );
  }

  // test basic usage.
  {
    ssm_t* ssm = such_shared_memory_open( SSM_VERSION, verbosity, "ssm_test", 10000, 0 );
    assert( ssm != NULL );

    such_shared_memory_close( ssm );
  }

  {
    ssm_t* ssmA;
    ssm_t* ssmB;
    
    ssmA = such_shared_memory_open( SSM_VERSION, verbosity, "ssm_test2", 1*10000, 0 );
    assert( ssmA != NULL );
    such_shared_memory_close( ssmA );

    ssmB = such_shared_memory_open( SSM_VERSION, verbosity, "ssm_test2", 2*10000, 0 );
    assert( ssmB != NULL );
    such_shared_memory_close( ssmB );
  }

  {
    ssm_t* ssmA;
    ssm_t* ssmB;
    
    ssmA = such_shared_memory_open( SSM_VERSION, verbosity, "ssm_test3", 1*10000, 0 );
    assert( ssmA != NULL );

    ssmB = such_shared_memory_open( SSM_VERSION, verbosity, "ssm_test3", 2*10000, 0 );
    assert( ssmB != NULL );

    such_shared_memory_close( ssmB );
    such_shared_memory_close( ssmA );
  }

  {
    ssm_t* ssmA;
    ssm_t* ssmB;
    ssm_t* ssmC;
    
    ssmA = such_shared_memory_open( SSM_VERSION, verbosity, "ssm_test4", 2*10000, 0 );
    assert( ssmA != NULL );

    ssmB = such_shared_memory_open( SSM_VERSION, verbosity, "ssm_test4", 1*10000, 0 );
    assert( ssmB != NULL );

    ssmC = such_shared_memory_open( SSM_VERSION, verbosity, "ssm_test4", 0*10000, 0 );
    assert( ssmC != NULL );

    such_shared_memory_close( ssmC );
    such_shared_memory_close( ssmB );
    such_shared_memory_close( ssmA );
  }

  {
    ssm_t* ssm = such_shared_memory_open( SSM_VERSION, verbosity, "ssm_test5", 0, 0 );
    assert( ssm == NULL );

    such_shared_memory_close( ssm );
  }

  {
    ssm_t* ssmA;
    ssm_t* ssmB;
    
    ssmA = such_shared_memory_open( SSM_VERSION, verbosity, "ssm_test6", 1*10000, 0 );
    assert( ssmA != NULL );

    ssmB = such_shared_memory_open( SSM_VERSION, verbosity, "ssm_test6", 0, 0 );
    assert( ssmB != NULL );

    such_shared_memory_close( ssmB );
    such_shared_memory_close( ssmA );
  }

  {
    ssm_t* ssmA;
    ssm_t* ssmB;
    
    ssmA = such_shared_memory_open( SSM_VERSION, verbosity, "ssm_test7", 1*10000, 0 );
    assert( ssmA != NULL );
    such_shared_memory_close( ssmA );

    ssmB = such_shared_memory_open( SSM_VERSION, verbosity, "ssm_test7", 0, 0 );
    assert( ssmB == NULL );
    such_shared_memory_close( ssmB );
  }

  return 0;
}

