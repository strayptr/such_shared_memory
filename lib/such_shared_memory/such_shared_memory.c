/*
 * Note: On Windows, it's not possible to resize shared memory after
 * it's created.
 */

#if defined( __WIN32__ ) || defined( _WIN32 )
# define ON_WINDOWS 1
# define _CRT_SECURE_NO_WARNINGS /* allow old-style C functions. */
#else
# define ON_WINDOWS 0
#endif

// module header.
#include "such_shared_memory.h"

// standard headers.
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <stdarg.h>

// platform-specific headers.
#if ON_WINDOWS
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h> /* For mode constants */
#include <fcntl.h> /* For O_* constants */
#endif

// platform-specific definitions.
#if ON_WINDOWS
# define INLINE   static
# define va_copy( dst, src )  ( (dst) = (src) )
#else
# define INLINE   static inline
#endif

typedef int     bool;
#define false   0
#define true    1

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
static char*    strVFmt( const char* fmt, va_list va );
static char*    strFmt( const char* fmt, ... );

#define LOG_FATAL   1
#define LOG_ERROR   2
#define LOG_WARN    3
#define LOG_INFO    4

static bool     ssmShouldLog( int verbosity, int level );
static void     ssmVLog( ssm_t* ssm, int level, const char* fmt, va_list va );
static void     ssmFatal( ssm_t* ssm, const char* fmt, ... )  { va_list va; va_start( va, fmt ); ssmVLog( ssm, LOG_FATAL, fmt, va ); va_end( va ); }
static void     ssmError( ssm_t* ssm, const char* fmt, ... )  { va_list va; va_start( va, fmt ); ssmVLog( ssm, LOG_ERROR, fmt, va ); va_end( va ); }
static void     ssmWarn( ssm_t* ssm, const char* fmt, ... )   { va_list va; va_start( va, fmt ); ssmVLog( ssm, LOG_WARN, fmt, va ); va_end( va ); }
static void     ssmInfo( ssm_t* ssm, const char* fmt, ... )   { va_list va; va_start( va, fmt ); ssmVLog( ssm, LOG_INFO, fmt, va ); va_end( va ); }

#define ssmAssert( cond )                                       \
  if ( !( cond ) )                                              \
  {                                                             \
    ssmFatal( ssm, "assertion failed:  %s", #cond );            \
    exit( 1 );                                                  \
  }                         

//==============================================================================
// cross-platform atomics.
//==============================================================================
#if ON_WINDOWS
// Windows atomics.
INLINE int32_t xinc32 ( volatile int32_t* x )                                   { return InterlockedIncrement( x ); }
INLINE int32_t xdec32 ( volatile int32_t* x )                                   { return InterlockedDecrement( x ); }
INLINE void*   xcasp  ( volatile void** dst, void* oldval, void* newval )       { return InterlockedCompareExchangePointer( dst, newval, oldval ); }
INLINE int32_t xcas32 ( volatile int32_t* dst, int32_t oldval, int32_t newval ) { return InterlockedCompareExchange( dst, newval, oldval ); }
INLINE void*   xchgp  ( volatile void** dst, void* newval )                     { return InterlockedExchangePointer( dst, newval ); }
INLINE int32_t xchg32 ( volatile int32_t* dst, int32_t newval )                 { return InterlockedExchange( dst, newval ); }
INLINE void    xyield()       { SwitchToThread(); }
INLINE void    xfence()       { MemoryBarrier(); }
INLINE void    xfenceread()   { MemoryBarrier(); }
INLINE void    xfencewrite()  { MemoryBarrier(); }
INLINE int32_t xthreadid()    { DWORD id = GetCurrentThreadId(); return *(int32_t*)&id; }
#define XInvalidThreadID      0 
#else
// non-Windows atomics.
#include <sys/types.h> /* for gettid() */
INLINE int32_t xthreadid()    { return gettid(); }
#define XInvalidThreadID      0 
#error TODO
#endif

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// common atomics.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#define xread( val )        ( xfenceread(), (val) )
#define xwrite( dst, src )  { (dst) = (src); xfencewrite(); }

typedef volatile int32_t  xlock_t;
typedef int32_t           xlockstate;

#define XLock_None     0
#define XLock_Locked   1
#define XLock_Unlocked 2

//------------------------------------------------------------------------------
INLINE xlockstate xlock( xlock_t* lock )
{
  int32_t tid = xthreadid();

  while ( true )
  {
    int32_t oldval = xcas32( lock, XInvalidThreadID, tid );
    if ( oldval == XInvalidThreadID )
      return XLock_Locked;

    /* allow the current thread to lock multiple times without deadlocking. */
    if ( oldval == tid )
      return XLock_Unlocked;

    xyield();
  }
}

//------------------------------------------------------------------------------
INLINE void xunlock( xlock_t* lock, xlockstate locked )
{
  if ( locked == XLock_Locked )
  {
    int32_t oldval = xchg32( lock, 0 );
    assert( oldval == xthreadid() );
  }
}

//------------------------------------------------------------------------------
INLINE xlockstate xbeginp( volatile void** dst, void* cmp, xlock_t* lock )
{
  if ( xread( *dst ) == cmp )
  {
    xlockstate locked = xlock( lock );
    {
      if ( xread( *dst ) == cmp )
        return locked;
    }
    xunlock( lock, locked );
  }
  return XLock_None;
}

//------------------------------------------------------------------------------
INLINE xlockstate xgetp( volatile void** dst, xlock_t* lock )
{
  while ( true )
  {
    xlockstate locked = xbeginp( dst, xread( *dst ), lock );
    if ( locked != XLock_None )
      return locked;
    xyield();
  }
}

//------------------------------------------------------------------------------
INLINE void xendp( volatile void** dst, void* newval, xlock_t* lock, xlockstate locked )
{
  xwrite( *dst, newval );
  xunlock( lock, locked );
}

//------------------------------------------------------------------------------
INLINE xlockstate xbegin32( volatile int32_t* dst, const int32_t cmp, xlock_t* lock )
{
  if ( xread( *dst ) == cmp )
  {
    xlockstate locked = xlock( lock );
    {
      if ( xread( *dst ) == cmp )
        return locked;
    }
    xunlock( lock, locked );
  }
  return XLock_None;
}

//------------------------------------------------------------------------------
INLINE xlockstate xget32( volatile int32_t* dst, xlock_t* lock )
{
  while ( true )
  {
    xlockstate locked = xbegin32( dst, xread( *dst ), lock );
    if ( locked != XLock_None )
      return locked;
    xyield();
  }
}

//------------------------------------------------------------------------------
INLINE void xend32( volatile int32_t* dst, const int32_t newval, xlock_t* lock, xlockstate locked )
{
  xwrite( *dst, newval );
  xunlock( lock, locked );
}

//------------------------------------------------------------------------------
typedef struct xrefstate_s
{
  volatile int32_t* refcount;
  xlock_t*          lock;
  xlockstate        locked;
  int32_t           newcount;
} xrefstate_t;

//------------------------------------------------------------------------------
INLINE void xrefend( xrefstate_t* xr )
{
  if ( xr->locked != XLock_None )
  {
    xend32( xr->refcount, xr->newcount, xr->lock, xr->locked );
    xr->locked = XLock_None;
  }
}

//------------------------------------------------------------------------------
INLINE bool xrefacquire( xrefstate_t* xr, volatile int32_t* refcount, xlock_t* lock )
{
  xr->locked = xget32( refcount, lock );
  xr->refcount = refcount;
  xr->lock = lock;
  xr->newcount = ( xread( *refcount ) + 1 );
  assert( xr->newcount >= 1 );
  return ( xr->newcount == 1 );
}

//------------------------------------------------------------------------------
INLINE bool xrefrelease( volatile int32_t* refcount, xlock_t* lock )
{
  xr->locked = xget32( refcount, lock );
  xr->refcount = refcount;
  xr->lock = lock;
  xr->newcount = ( xread( *refcount ) - 1 );
  assert( xr->newcount >= 0 );
  return ( xr->newcount == 0 );
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// CPU cache line helpers.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#define CacheLineSize 64    /* bytes */

#define CacheLineBegin( name )    \
  union                           \
  {                               \
    volatile int32_t  cacheline_##name[ CacheLineSize / sizeof( int32_t ) ]; \
    struct
#define CacheLineEnd()            \
    ;                             \
  };

//==============================================================================
// cross-platform synchronization primitives.
//==============================================================================

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// xsection
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
typedef struct xsection_s
{
  volatile int32_t            refcount;
  volatile int32_t            lock;
#if ON_WINDOWS
  CRITICAL_SECTION            cs;
#else
#error TODO
#endif
} xsection_t;

#define XSECTION_SPIN_COUNT   4000

//------------------------------------------------------------------------------
INLINE void
xsection_enter( xsection_t* sec )
{
  assert( sec );

  /* need initialization? */
  {
    if ( xbegin32( &sec->refcount, 0, &sec->lock ) )
    {
#if ON_WINDOWS
      if ( !InitializeCriticalSectionAndSpinCount( &sec->cs, spincount ) )
      {
        assert( !"InitializeCriticalSectionAndSpinCount failed" );
      }
#else
#error TODO
#endif
    }
    xend32( &sec->refcount, 1, sec->lock );
  }

  /* platform-specific enter critical section. */
  {
    assert( xread( sec->refcount ) > 0 );

#if ON_WINDOWS
    EnterCriticalSection( &sec->cs );
#else
#error TODO
#endif
  }
}

//------------------------------------------------------------------------------
INLINE void
xsection_leave( xsection_t* sec )
{
  assert( sec );

  /* platform-specific leave critical section. */
  {
    assert( xread( sec->refcount ) > 0 );

#if ON_WINDOWS
    LeaveCriticalSection( &sec->cs );
#else
#error TODO
#endif
  }
}

//------------------------------------------------------------------------------
INLINE void
xsection_addref( xsection_t* sec )
{
  assert( sec );
  {
    xrefstate_t xr;
    if ( xrefacquire( &xr, sec->refcount, sec->lock ) )
    {
#if ON_WINODWS
      if ( !InitializeCriticalSectionAndSpinCount( &sec->cs, XSECTION_SPIN_COUNT ) )
      {
        assert( !"InitializeCriticalSectionAndSpinCount failed" );
      }
#else
#error TODO
#endif
    }
    xrefend( &xr );
  }
}

//------------------------------------------------------------------------------
INLINE void
xsection_release( xsection_t* sec )
{
  assert( sec );
  {
    xrefstate_t xr;
    if ( xrefrelease( &xr, sec->refcount, sec->lock ) )
    {
#if ON_WINODWS
      DeleteCriticalSection( &sec->cs );
#else
#error TODO
#endif
    }
    xrefend( &xr );
  }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// xref
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
typedef struct xref_s 
{
  CacheLineBegin( line )
  {
    volatile int32_t     refcount;
    volatile xsection_t  sec;
  }
  CacheLineEnd();

  volatile void*      instance;
} xref_t;

INLINE bool     xref_acquire( xref_t* xref )
{
  if ( inc32( &xref->refcount ) > 1 )
    return false;

  return true;
}
INLINE void     xref_acquired( xref_t* xref, void* obj );
INLINE void*    xref_release( xref_t* xref );
INLINE void     xref_released( xref_t* xref );

//==============================================================================
// per-process state.
//==============================================================================
typedef struct ssm_singleton_s
{
#if ON_WINDOWS
  HMODULE           hNtdll;
  NtQuerySection_t  NtQuerySection;
#else
  int               foo;
#endif
} ssm_singleton_t;

static void*


static ssm_singleton_t   g_ssm;

//------------------------------------------------------------------------------
SSM_API int
such_shared_memory_startup( int version )
{
  if ( version != SSM_VERSION )
  {
    ssmError( NULL, "startup(): DLL verison mismatch.  Expected %d, got %d", SSM_VERSION, version );
    return 0;
  }

  assert( g_ssm.refcount >= 0 );

  if ( xinc32( &g_ssm.refcount ) == 1 )
  {
#if ON_WINDOWS
    /* open ntdll.dll and get the NtQuerySection function. */
    {
      if ( !g_ssm.hNtdll )
      {
        g_ssm.hNtdll = LoadLibraryA( "Ntdll.dll" );
        if ( g_ssm.hNtdll == NULL )
        {
          ssmError( NULL, "startup(): LoadLibraryA(\"Ntdll.dll\") failed with %d\n", GetLastError() );
          such_shared_memory_shutdown();
          return -1;
        }
      }

      if ( !g_ssm.NtQuerySection )
      {
        assert( g_ssm.hNtdll != NULL );
        g_ssm.NtQuerySection = (NtQuerySection_t)GetProcAddress( g_ssm.hNtdll, "NtQuerySection" );
        if ( !g_ssm.NtQuerySection )
        {
          ssmError( NULL, "startup(): GetProcAddress(\"NtQuerySection\") failed with %d\n", GetLastError() );
          such_shared_memory_shutdown();
          return -1;
        }
      }
    }
#endif
  }
  return g_ssm.refcount;
}

//------------------------------------------------------------------------------
SSM_API int
such_shared_memory_shutdown()
{
  assert( g_ssm.refcount > 0 );
  --g_ssm.refcount;
  if ( g_ssm.refcount == 0 )
  {
#if ON_WINDOWS
    if ( g_ssm.hNtdll )
    {
      FreeLibrary( g_ssm.hNtdll );
      g_ssm.hNtdll = NULL;
      g_ssm.NtQuerySection = NULL;
    }
#endif
  }
  return g_ssm.refcount;
}

//==============================================================================
// struct ssm_s
//==============================================================================
struct ssm_s
{
  int64_t   size;
  int64_t   memSize;
  char*     name;
  char*     nameInternal;

  void*     mem;
#if ON_WINDOWS
  HANDLE    fd;
#else
  int       fd;
#endif

  int       verbosity;
  int       flags;
  bool      created;
};

//------------------------------------------------------------------------------
SSM_API ssm_t*
such_shared_memory_open( int version, int verbosity, const char* name, int64_t size, int flags )
{
  if ( version != SSM_VERSION )
  {
    ssmError( NULL, "verison mismatch.  Expected %d, got %d", SSM_VERSION, version );
    return NULL;
  }

  {
    ssm_t* ssm = ssmNew( ssm_t );
#if ON_WINDOWS
    ssm->fd = INVALID_HANDLE_VALUE;
#else
    ssm->fd = -1;
#endif

    ssm->verbosity = verbosity;
    ssm->name = strDup( name );
    ssm->size = size;
    ssm->flags = flags;

    ssmInfo( ssm, "open()" );


    if ( size < 0 )
      ssmError( ssm, "size is %lld", (int64_t)size );
    else
    {
      if ( ssmPlatformOpen( ssm ) )
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
  ssmInfo( ssm, "close()" );

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
    ssmAssert( ssm->fd == INVALID_HANDLE_VALUE );

    /* create the file mapping. */
    {
      HANDLE h = NULL;
      HANDLE hOpened = NULL;

      ssmAssert( ssm->nameInternal == NULL );
      ssm->nameInternal = strJoin( "Global\\", ssm->name );

      if ( ssm->size <= 0 )
      {
        hOpened = OpenFileMappingA(
            FILE_MAP_WRITE,
            false,
            ssm->nameInternal );

        if ( hOpened == NULL )
        {
          ssmWarn( ssm, "OpenFileMappingA() failed with %d", GetLastError() );
        }

        /*
        if ( hOpened == NULL )
        {
          ssmError( ssm, "OpenFileMappingA() failed with %d", GetLastError() );
          ssmPlatformClose( ssm );
          return false;
        }
        */
      }

      {
        if ( ( hOpened != NULL ) || ( ssm->size > 0 ) )
        {
          int64_t size = ssm->size;
          if ( size <= 0 )
            size = 1;

          h = CreateFileMappingA(
              INVALID_HANDLE_VALUE, /* backed by pagefile. */
              NULL, /* default security attributes. */
              PAGE_READWRITE,
              ( size >> 32 ), 
              ( size & 0xFFFFFFFF ),
              ssm->nameInternal );
        }

        /* if we opened the shared memory, then release it, since we
         * just acquired it via CreateFileMapping. */
        if ( hOpened != NULL )
        {
          CloseHandle( hOpened );
          hOpened = NULL;
        }

        if ( h == NULL )
        {
          if ( ssm->size > 0 )
          {
            ssmError( ssm, "CreateFileMappingA() failed with %d", GetLastError() );
          }
          ssmPlatformClose( ssm );
          return false;
        }
      }

      ssmAssert( h != INVALID_HANDLE_VALUE );
      ssm->fd = h;
    }

    ssmInfo( ssm, "Existing size: %lld", ssmPlatformQuerySize( ssm ) );
    ssmInfo( ssm, "Determined size: %lld", (int64_t)ssmPlatformDetermineSize( ssm ) );

    /* map the memory. */
    {
      /* determine the size. */
      size_t size = ssmPlatformDetermineSize( ssm );
      if ( size == 0 )
      {
        ssmError( ssm, "ssmPlatformDetermineSize() failed\n" );
        ssmPlatformClose( ssm );
        return false;
      }

      /* map the memory. */
      {
        ssmInfo( ssm, "Mapping size: %lld", (int64_t)size );

        ssm->mem = MapViewOfFile(
            ssm->fd,
            FILE_MAP_WRITE,
            0,
            0,
            size );

        if ( ssm->mem == NULL )
        {
          ssmError( ssm, "MapViewOfFile() failed with %d\n", GetLastError() );
          ssmPlatformClose( ssm );
          return false;
        }

        ssm->memSize = size;
      }
    }

    return true;
  }
#else
  // TODO
#endif
  return false;
}

//------------------------------------------------------------------------------
static void
ssmPlatformClose( ssm_t* ssm )
{
#if ON_WINDOWS
  {
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

  ssmFree( ssm->nameInternal );
}

//------------------------------------------------------------------------------
static int64_t
ssmPlatformQuerySize( ssm_t* ssm )
{
  if ( ssm )
  {
#if ON_WINDOWS
    ssmAssert( g_ssm.NtQuerySection != NULL );

    if ( ssm->fd != INVALID_HANDLE_VALUE )
    {
      SECTION_BASIC_INFORMATION sbi;

      if ( FAILED( g_ssm.NtQuerySection( ssm->fd, 0, &sbi, sizeof( SECTION_BASIC_INFORMATION ), 0 ) ) )
      {
        ssmError( ssm, "NtQuerySection() failed with %d\n", GetLastError() );
      }
      else
      {
        return sbi.SectionSize.QuadPart;
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

//------------------------------------------------------------------------------
static char*
strVFmt( const char* fmt, va_list va )
{
  char* ret = NULL;
  size_t size = 0;
  va_list va1;
  va_list va2;
  va_copy( va1, va );
  va_copy( va2, va );
  size = vsnprintf( NULL, 0, fmt, va1 );
  assert( size > 0 );
  if ( size > 0 )
  {
    ret = ssmMalloc( size + 1 );
    vsnprintf( ret, size + 1, fmt, va2 );
  }
  va_end( va2 );
  va_end( va1 );
  return ret;
}

//------------------------------------------------------------------------------
static char*
strFmt( const char* fmt, ... )
{
  char* ret = NULL;
  va_list va;
  va_start( va, fmt );
  ret = strVFmt( fmt, va );
  va_end( va );
  return ret;
}

//------------------------------------------------------------------------------
static bool
ssmShouldLog( int verbosity, int level )
{
  if ( level == LOG_FATAL )
    return true;

  if ( verbosity == SSMV_QUIET )
    return false;

  if ( verbosity == SSMV_ALL )
    return true;

  switch ( level )
  {
    case LOG_ERROR: return verbosity >= SSMV_ERRORS;
    case LOG_WARN: return verbosity >= SSMV_WARNINGS;
    case LOG_INFO: return verbosity >= SSMV_INFO;
  }
  assert( false );
  return false;
}

//------------------------------------------------------------------------------
static const char*
ssmGetLevelStr( int level )
{
  switch ( level )
  {
    case LOG_FATAL:
      return "[FATAL]";
    case LOG_ERROR:
      return "[ERR]";
    case LOG_WARN:
      return "[WARN]";
    case LOG_INFO:
      return "[INFO]";
    default:
      return "[UNK]";
  }
}

//------------------------------------------------------------------------------
static void
ssmVLog( ssm_t* ssm, int level, const char* fmt, va_list va )
{
  int verbosity = SSMV_ALL;
  if ( ssm )
    verbosity = ssm->verbosity;

  if ( ssmShouldLog( verbosity, level ) )
  {
    char* msg = strVFmt( fmt, va );

    {
      char* body = msg;
      char* header = NULL;
      if ( ssm )
        header = strFmt( "such_shared_memory( \"%s\", %lld, %lld ):", ssm->name, (int64_t)ssm->size, (int64_t)ssm->flags );
      else
        header = strFmt( "such_shared_memory:" );
      msg = strFmt( "%s %s \t%s\n", ssmGetLevelStr( level ), header, msg );
      ssmFree( body );
      ssmFree( header );
    }

    switch ( level )
    {
      case LOG_INFO:
        fprintf( stdout, "%s", msg );
        break;
      case LOG_WARN:
      case LOG_ERROR:
      case LOG_FATAL:
      default:
        fprintf( stderr, "%s", msg );
        break;
    }

    ssmFree( msg );
  }

  if ( level == LOG_FATAL )
    exit( 1 );
}

