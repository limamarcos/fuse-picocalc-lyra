/* logger.c: Diagnostic logging, crash reporter, and freeze watchdog for Fuse on PicoCalc
   Designed for Luckfox Lyra / PicoCalc platform
*/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/resource.h>

#include "logger.h"

/* External hook to release keyboard grab and restore fb if crashed */
extern int fbkeyboard_end( void );
extern int fbdisplay_end( void );

#define ACTION_HISTORY_SIZE 64
#define ACTION_STR_LEN      160

static FILE *log_file = NULL;
static char log_file_path[256] = "/home/pico/fuse.log";
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t action_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Circular action history */
static char action_history[ACTION_HISTORY_SIZE][ACTION_STR_LEN];
static int action_head = 0;
static int action_count = 0;

/* Watchdog variables */
static volatile time_t last_ping_time = 0;
static volatile int watchdog_running = 0;
static pthread_t watchdog_thread;
static char current_action[ACTION_STR_LEN] = "Initialization";
static volatile int watchdog_paused = 0;

/* Helper to get formatted timestamp: [YYYY-MM-DD HH:MM:SS.mmm] */
static void get_timestamp_str( char *buffer, size_t maxlen )
{
  struct timeval tv;
  struct tm tm_info;

  gettimeofday( &tv, NULL );
  localtime_r( &tv.tv_sec, &tm_info );

  snprintf( buffer, maxlen, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
            tm_info.tm_year + 1900,
            tm_info.tm_mon + 1,
            tm_info.tm_mday,
            tm_info.tm_hour,
            tm_info.tm_min,
            tm_info.tm_sec,
            (int)( tv.tv_usec / 1000 ) );
}

/* Helper to get memory RSS in KB */
static long get_memory_rss_kb( void )
{
  long rss_pages = 0;
  FILE *f = fopen( "/proc/self/statm", "r" );
  if( f ) {
    if( fscanf( f, "%*s %ld", &rss_pages ) == 1 ) {
      fclose( f );
      return rss_pages * ( sysconf( _SC_PAGESIZE ) / 1024 );
    }
    fclose( f );
  }
  return -1;
}

/* Main logging function */
void fuse_log( const char *module, const char *fmt, ... )
{
  char ts[32];
  va_list args;

  get_timestamp_str( ts, sizeof( ts ) );

  pthread_mutex_lock( &log_mutex );

  if( log_file ) {
    fprintf( log_file, "[%s] [%-8s] ", ts, module ? module : "INFO" );
    va_start( args, fmt );
    vfprintf( log_file, fmt, args );
    va_end( args );
    fprintf( log_file, "\n" );
    fflush( log_file );
  }

  pthread_mutex_unlock( &log_mutex );
}

/* Record a high-level user/system action into file and circular history buffer */
void fuse_log_action( const char *action, const char *details )
{
  char entry[ACTION_STR_LEN];
  char ts[32];

  get_timestamp_str( ts, sizeof( ts ) );

  if( details && details[0] ) {
    snprintf( entry, sizeof( entry ), "%s (%s)", action, details );
  } else {
    snprintf( entry, sizeof( entry ), "%s", action );
  }

  pthread_mutex_lock( &action_mutex );

  snprintf( current_action, sizeof( current_action ), "%s", entry );

  snprintf( action_history[action_head], sizeof( action_history[action_head] ),
            "[%s] %s", ts, entry );
  action_head = ( action_head + 1 ) % ACTION_HISTORY_SIZE;
  if( action_count < ACTION_HISTORY_SIZE ) action_count++;

  pthread_mutex_unlock( &action_mutex );

  fuse_log( "ACTION", "%s", entry );
}

/* Ping watchdog from main loop */
void fuse_watchdog_ping( void )
{
  last_ping_time = time( NULL );
}

/* Update ongoing action for watchdog */
void fuse_watchdog_set_action( const char *action )
{
  pthread_mutex_lock( &action_mutex );
  if( action ) {
    snprintf( current_action, sizeof( current_action ), "%s", action );
  }
  pthread_mutex_unlock( &action_mutex );
}

/* Update pause state for watchdog */
void fuse_watchdog_set_paused( int paused )
{
  watchdog_paused = paused;
  fuse_watchdog_ping();
}

/* Watchdog thread function */
static void *watchdog_worker( void *arg )
{
  int warn_level = 0;
  (void)arg;

  fuse_log( "WATCHDOG", "Freeze watchdog thread active (sampling interval: 1s)" );

  while( watchdog_running ) {
    sleep( 1 );
    if( !watchdog_running ) break;

    time_t now = time( NULL );
    time_t ping = last_ping_time;
    time_t diff = now - ping;

    if( ping > 0 && diff >= 3 && warn_level == 0 ) {
      warn_level = 1;
      fuse_log( "WATCHDOG", "[WARNING] Main thread has not pinged watchdog for %ld seconds! (Paused=%d, RSS=%ld KB, Last Action: '%s')",
                (long)diff, watchdog_paused, get_memory_rss_kb(), current_action );
    } else if( ping > 0 && diff >= 8 && warn_level == 1 ) {
      warn_level = 2;
      fuse_log( "WATCHDOG", "[CRITICAL FREEZE] Main thread FROZEN for %ld seconds! Stuck in action: '%s'. Dumping recent action trace:",
                (long)diff, current_action );

      pthread_mutex_lock( &action_mutex );
      int start = ( action_count < ACTION_HISTORY_SIZE ) ? 0 : action_head;
      for( int i = 0; i < action_count; i++ ) {
        int idx = ( start + i ) % ACTION_HISTORY_SIZE;
        fuse_log( "WATCHDOG", "  Trace [%02d]: %s", i + 1, action_history[idx] );
      }
      pthread_mutex_unlock( &action_mutex );
    } else if( diff < 3 && warn_level > 0 ) {
      fuse_log( "WATCHDOG", "[RECOVERED] Main thread resumed execution after %ld seconds lag.", (long)diff );
      warn_level = 0;
    }
  }

  fuse_log( "WATCHDOG", "Freeze watchdog thread terminated" );
  return NULL;
}

/* Signal crash handler */
static void crash_signal_handler( int sig, siginfo_t *info, void *ucontext )
{
  (void)ucontext;
  const char *sig_name = "UNKNOWN";

  switch( sig ) {
    case SIGSEGV: sig_name = "SIGSEGV (Segmentation fault)"; break;
    case SIGBUS:  sig_name = "SIGBUS (Bus error / alignment fault)"; break;
    case SIGABRT: sig_name = "SIGABRT (Aborted)"; break;
    case SIGFPE:  sig_name = "SIGFPE (Floating point exception)"; break;
    case SIGILL:  sig_name = "SIGILL (Illegal instruction)"; break;
    case SIGPIPE: sig_name = "SIGPIPE (Broken pipe)"; break;
    case SIGTERM: sig_name = "SIGTERM (Terminated)"; break;
    case SIGINT:  sig_name = "SIGINT (Interrupted)"; break;
    case SIGHUP:  sig_name = "SIGHUP (Hangup)"; break;
  }

  fuse_log( "CRASH", "=================================================================" );
  fuse_log( "CRASH", "FATAL SIGNAL CAUGHT: %d - %s", sig, sig_name );
  if( info ) {
    fuse_log( "CRASH", "Faulting Address / Sender: %p (errno=%d, code=%d)",
              info->si_addr, info->si_errno, info->si_code );
  }
  fuse_log( "CRASH", "Memory RSS at crash: %ld KB", get_memory_rss_kb() );
  fuse_log( "CRASH", "Last registered action: '%s'", current_action );
  fuse_log( "CRASH", "--- Post-Mortem Action History (Last %d events) ---", action_count );

  int start = ( action_count < ACTION_HISTORY_SIZE ) ? 0 : action_head;
  for( int i = 0; i < action_count; i++ ) {
    int idx = ( start + i ) % ACTION_HISTORY_SIZE;
    fuse_log( "CRASH", "  [%02d] %s", i + 1, action_history[idx] );
  }
  fuse_log( "CRASH", "=================================================================" );

  /* Ensure keyboard grab is released so user is not locked out of console */
  fbkeyboard_end();
  fbdisplay_end();

  if( log_file ) {
    fflush( log_file );
    fsync( fileno( log_file ) );
    fclose( log_file );
    log_file = NULL;
  }

  /* Re-raise signal with default handler */
  struct sigaction sa;
  memset( &sa, 0, sizeof( sa ) );
  sa.sa_handler = SIG_DFL;
  sigaction( sig, &sa, NULL );
  raise( sig );
}

/* Initialize logger subsystem */
int fuse_logger_init( const char *log_path )
{
  char old_log[280];

  if( log_path && log_path[0] ) {
    snprintf( log_file_path, sizeof( log_file_path ), "%s", log_path );
  }

  /* Rotate previous log file */
  snprintf( old_log, sizeof( old_log ), "%s.old", log_file_path );
  rename( log_file_path, old_log );

  log_file = fopen( log_file_path, "w" );
  if( !log_file ) {
    fprintf( stderr, "fuse_logger: Warning: could not open log file '%s': %s\n",
             log_file_path, strerror( errno ) );
    return 1;
  }

  fuse_log( "INIT", "==========================================================" );
  fuse_log( "INIT", " Fuse ZX Spectrum Emulator on PicoCalc (Luckfox Lyra)" );
  fuse_log( "INIT", " Diagnostic Self-Logging and Freeze Watchdog Initialized" );
  fuse_log( "INIT", " Log file: %s (Rotated old log to %s)", log_file_path, old_log );
  fuse_log( "INIT", " Initial RSS memory: %ld KB", get_memory_rss_kb() );
  fuse_log( "INIT", "==========================================================" );

  /* Install crash handlers */
  struct sigaction sa;
  memset( &sa, 0, sizeof( sa ) );
  sa.sa_sigaction = crash_signal_handler;
  sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_RESETHAND;

  sigaction( SIGSEGV, &sa, NULL );
  sigaction( SIGBUS,  &sa, NULL );
  sigaction( SIGABRT, &sa, NULL );
  sigaction( SIGFPE,  &sa, NULL );
  sigaction( SIGILL,  &sa, NULL );
  sigaction( SIGPIPE, &sa, NULL );
  sigaction( SIGTERM, &sa, NULL );
  sigaction( SIGHUP,  &sa, NULL );

  /* Start watchdog thread */
  last_ping_time = time( NULL );
  watchdog_running = 1;
  if( pthread_create( &watchdog_thread, NULL, watchdog_worker, NULL ) != 0 ) {
    fuse_log( "INIT", "[WARN] Could not create watchdog thread: %s", strerror( errno ) );
  }

  return 0;
}

/* Shutdown logger subsystem */
void fuse_logger_shutdown( void )
{
  fuse_log( "SHUTDOWN", "Fuse emulator exiting normally. Final RSS memory: %ld KB", get_memory_rss_kb() );

  if( watchdog_running ) {
    watchdog_running = 0;
    pthread_join( watchdog_thread, NULL );
  }

  pthread_mutex_lock( &log_mutex );
  if( log_file ) {
    fflush( log_file );
    fsync( fileno( log_file ) );
    fclose( log_file );
    log_file = NULL;
  }
  pthread_mutex_unlock( &log_mutex );
}
