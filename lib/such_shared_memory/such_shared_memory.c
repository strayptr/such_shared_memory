/*
 * Note: On Windows, it's not possible to resize shared memory after
 * it's created.
 */
#include "such_shared_memory.h"

#if defined( __WIN32__ ) || defined( _WIN32 )
# define ON_WINDOWS 1
#else
# define ON_WINDOWS 0
#endif

// platform-specific headers.
#if ON_WINDOWS
#include <windows.h>
typedef int     bool;
#define false   0
#define true    1
#define INLINE
#else
#include <sys/mman.h>
#include <sys/stat.h> /* For mode constants */
#include <fcntl.h> /* For O_* constants */
#define INLINE  inline
#endif

// standard headers.
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

#define ssmMalloc( n )    malloc( n )
#define ssmCalloc( n )    calloc( 1, n )
#define ssmFree( p )      { free( p ); (p) = NULL; }
#define ssmNew( type )    ssmCalloc( sizeof( type ) )
#define ssmDel( p )       ssmFree( p )
#define ssmMemCpy( dst, src, n )   memcpy( dst, src, n )

#if UINTPTR_MAX <= UINT_MAX
# define IS_32BIT    1
#else
# define IS_32BIT    0
#endif

#if ON_WINDOWS
typedef struct _SECTION_BASIC_INFORMATION {
  ULONG                   d000;
  ULONG                   SectionAttributes;
  LARGE_INTEGER           SectionSize;
} SECTION_BASIC_INFORMATION;
typedef DWORD ( CALLBACK* NtQuerySection_t )( HANDLE, DWORD, PVOID,DWORD,DWORD* );
#endif

static bool     ssmPlatformOpen( ssm_t* ssm );
static void     ssmPlatformClose( ssm_t* ssm );
static int64_t  ssmPlatformQuerySize( ssm_t* ssm );

static char*    strDup( const char* str );
static char*    strJoin( const char* s1, const char* s2 );

//==============================================================================
// struct ssm_s
//==============================================================================
struct ssm_s
{
  int64_t   size;
  char*     name;

  void*     mem;
#if ON_WINDOWS
  HANDLE    fd;
  HMODULE   hNtdll;
  NtQuerySection_t  NtQuerySection;
#else
  int       fd;
#endif
};

//------------------------------------------------------------------------------
SSM_API ssm_t*
such_shared_memory_open( int version, const char* name, int64_t size, int flags )
{
  if ( version != SSM_VERSION )
  {
    fprintf( stderr, "such_shared_memory_open(\"%s\"): version mismatch.  Expected %d, got %d\n", name, SSM_VERSION, version );
    return NULL;
  }

  if ( size < 0 )
  {
    fprintf( stderr, "such_shared_memory_open(\"%s\"): size < 0.\n", name );
    return NULL;
  }

  {
    ssm_t* ssm = ssmNew( ssm_t );
    ssm->size = size;
    ssm->name = strDup( name );
#if ON_WINDOWS
    ssm->fd = INVALID_HANDLE_VALUE;
#else
    ssm->fd = -1;
#endif

    if ( ssmPlatformOpen( ssm ) )
    {
      return ssm;
    }

    such_shared_memory_close( ssm );
    return NULL;
  }
}

//------------------------------------------------------------------------------
SSM_API void
such_shared_memory_close( ssm_t* ssm )
{
  if ( !ssm )
    return;

  ssmPlatformClose( ssm );
  ssmFree( ssm->name );
  ssmDel( ssm );
}

//==============================================================================
// platform-specific functionality.
//==============================================================================

//------------------------------------------------------------------------------
static size_t
ssmPlatformDetermineSize( ssm_t* ssm )
{
  int64_t size = ssmPlatformQuerySize( ssm );
  if ( size < 0 )
    return 0;

  /* do we want to map less than the whole file size? */
  if ( ( ssm->size > 0 ) && ( ssm->size < size ) )
    size = ssm->size;

#if IS_32BIT
  /* on 32-bit platforms, map only up to 1.5GB. */
  if ( size > ( ( 1024LL + 512LL ) * 1024 * 1024 ) )
    return ( ( 1024LL + 512LL ) * 1024 * 1024 );
#endif

#if IS_32BIT
  assert( size <= 0xFFFFFFFF );
#endif
  return (size_t)size;
}

//------------------------------------------------------------------------------
static bool
ssmPlatformOpen( ssm_t* ssm )
{
  ssmPlatformClose( ssm );

#if ON_WINDOWS
  {
    assert( ssm->fd == INVALID_HANDLE_VALUE );

    /* open ntdll.dll and get the NtQuerySection function. */
    {
      if ( !ssm->hNtdll )
      {
        ssm->hNtdll = LoadLibraryA( "Ntdll.dll" );
        if ( ssm->hNtdll == NULL )
        {
          fprintf( stderr, "such_shared_memory_open(\"%s\"): LoadLibraryA(\"Ntdll.dll\") failed with %d\n", ssm->name, GetLastError() );
          ssmPlatformClose( ssm );
          return false;
        }
      }

      if ( !ssm->NtQuerySection )
      {
        ssm->NtQuerySection = (NtQuerySection_t)GetProcAddress( ssm->hNtdll, "NtQuerySection" );
        if ( !ssm->NtQuerySection )
        {
          fprintf( stderr, "such_shared_memory_open(\"%s\"): GetProcAddress(\"NtQuerySection\") failed with %d\n", ssm->name, GetLastError() );
          ssmPlatformClose( ssm );
          return false;
        }
      }
    }

    /* create the file mapping. */
    {
      HANDLE h = NULL;
      char* name = strJoin( "Global\\", ssm->name );
      int64_t size = ssm->size;

      if ( false && ( size <= 0 ) )
      {
        h = OpenFileMappingA(
            FILE_MAP_WRITE,
            false,
            name );
      }
      else 
      {
        if ( size <= 0 )
          size = 1;

        h = CreateFileMappingA(
            INVALID_HANDLE_VALUE, /* backed by pagefile. */
            NULL, /* default security attributes. */
            PAGE_READWRITE,
            ( size >> 32 ), 
            ( size & 0xFFFFFFFF ),
            name );
      }

      ssmFree( name );

      if ( h == NULL )
      {
        if ( size > 0 )
        {
          fprintf( stderr, "such_shared_memory_open(\"%s\"): CreateFileMappingA() failed with %d\n", ssm->name, GetLastError() );
        }
        else
        {
          fprintf( stderr, "such_shared_memory_open(\"%s\"): OpenFileMappingA() failed with %d\n", ssm->name, GetLastError() );
        }
        ssmPlatformClose( ssm );
        return false;
      }

      assert( h != INVALID_HANDLE_VALUE );
      ssm->fd = h;
    }

    printf( "such_shared_memory_open(\"%s\"): Existing size is %lld\n", ssm->name, ssmPlatformQuerySize( ssm ) );
    printf( "such_shared_memory_open(\"%s\"): Determined size: %lld\n", ssm->name, (int64_t)ssmPlatformDetermineSize( ssm ) );

    /* map the memory. */
    {
      /* determine the size. */
      size_t size = ssmPlatformDetermineSize( ssm );
      if ( size == 0 )
      {
        fprintf( stderr, "such_shared_memory_open(\"%s\"): ssmPlatformDetermineSize() failed\n", ssm->name );
        ssmPlatformClose( ssm );
        return false;
      }

      /* map the memory. */
      {
        printf( "such_shared_memory_open(\"%s\"): Mapping size: %lld\n", ssm->name, (int64_t)size );

        ssm->mem = MapViewOfFile(
            ssm->fd,
            FILE_MAP_WRITE,
            0,
            0,
            size );

        if ( ssm->mem == NULL )
        {
          fprintf( stderr, "such_shared_memory_open(\"%s\"): MapViewOfFile() failed with %d\n", ssm->name, GetLastError() );
          ssmPlatformClose( ssm );
          return false;
        }

        ssm->size = size;
      }
    }

    return true;
  }
#else
  // TODO
#endif
}

//------------------------------------------------------------------------------
static void
ssmPlatformClose( ssm_t* ssm )
{
#if ON_WINDOWS
  {
    if ( ssm->hNtdll )
    {
      FreeLibrary( ssm->hNtdll );
      ssm->hNtdll = NULL;
      ssm->NtQuerySection = NULL;
    }

    if ( ssm->mem )
    {
      UnmapViewOfFile( ssm->mem );
      ssm->mem = NULL;
    }

    if ( ssm->fd != INVALID_HANDLE_VALUE )
    {
      CloseHandle( ssm->fd );
      ssm->fd = INVALID_HANDLE_VALUE;
    }
  }
#else
  // TODO
#endif
}

//------------------------------------------------------------------------------
static int64_t
ssmPlatformQuerySize( ssm_t* ssm )
{
  if ( ssm )
  {

#if ON_WINDOWS
    if ( ssm->NtQuerySection )
    {
      if ( ssm->fd != INVALID_HANDLE_VALUE )
      {
        SECTION_BASIC_INFORMATION sbi;

        if ( FAILED( ssm->NtQuerySection( ssm->fd, 0, &sbi, sizeof( SECTION_BASIC_INFORMATION ), 0 ) ) )
        {
          fprintf( stderr, "such_shared_memory_open(\"%s\"): NtQuerySection() failed with %d\n", ssm->name, GetLastError() );
        }
        else
        {
          return sbi.SectionSize.QuadPart;
        }
      }
    }
#else
  // TODO
#endif
  }
  return -1;
}

//==============================================================================
// private functionality.
//==============================================================================

//------------------------------------------------------------------------------
static char*
strDup( const char* str )
{
  size_t size = strlen( str );
  char* dup = ssmMalloc( size + 1 );
  ssmMemCpy( dup, str, size + 1 );
  return dup;
}

//------------------------------------------------------------------------------
static char*
strJoin( const char* sA, const char* sB )
{
  size_t sAsize = strlen( sA );
  size_t sBsize = strlen( sB );
  char* s = ssmMalloc( sAsize + sBsize + 1 );
  ssmMemCpy( s, sA, sAsize );
  ssmMemCpy( s + sAsize, sB, sBsize + 1 );
  return s;
}

