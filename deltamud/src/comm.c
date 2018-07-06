/* ************************************************************************
   *   File: comm.c                                        Part of CircleMUD *
   *  Usage: Communication, socket handling, main(), central game loop       *
   *                                                                         *
   *  All rights reserved.  See license.doc for complete information.        *
   *                                                                         *
   *  Copyright (C) 1993, 94 by the Trustees of the Johns Hopkins University *
   *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
   ************************************************************************ */

#define __COMM_C__

#include "conf.h"
#include "sysdep.h"


#ifdef CIRCLE_WINDOWS           /* Includes for Win32 */
#include <direct.h>
#include <mmsystem.h>
#endif /* CIRCLE_WINDOWS */

#ifdef CIRCLE_AMIGA             /* Includes for the Amiga */
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <clib/socket_protos.h>
#endif /* CIRCLE_AMIGA */

#ifdef CIRCLE_OS2               /* Includes for OS/2 */
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <netdb.h>
#endif /* CIRCLE_OS2 */

/* Note, includes for UNIX are in sysdep.h */
/* This one is not heh */
#include <arpa/inet.h>
#include "structs.h"
#include "utils.h"
#include "comm.h"
#include "interpreter.h"
#include "handler.h"
#include "db.h"
#include "house.h"
#include "maputils.h"
#include "olc.h"
#include "screen.h"
#include "dg_scripts.h"
#include "clan.h"
#include "dbinterface.h"


#ifdef HAVE_ARPA_TELNET_H
#include <arpa/telnet.h>
#else
#include "telnet.h"
#endif

#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif

/* externs */

extern const char *DFLT_IP;
extern int www_who;
extern int reboot_hr;
extern int reboot_min;
extern int warn_hr;
extern int warn_min;
extern int circle_restrict;
extern int mini_mud;
extern int no_rent_check;
extern int autoreboot;
extern int DFLT_PORT;
extern char *DFLT_DIR;
extern char *LOGFILE;
extern char *class_abbrevs[];
extern char *race_abbrevs[];
extern char *prompt_diag (struct char_data *i, struct char_data *ch);
extern int MAX_PLAYERS;
extern int MAX_DESCRIPTORS_AVAILABLE;
extern int has_mail(long recipient); /* in mail.c */
extern int top_of_zone_table;
extern struct room_data *world; /* In db.c */
extern int top_of_world;        /* In db.c */
extern struct time_info_data time_info;         /* In db.c */
extern struct zone_data *zone_table;
extern char help[];
extern const char *save_info_msg[];     /* In olc.c */
void proc_color (char *inbuf, int color);
void auction_update (void);
void quest_update (void);
void blood_update (void);
int isignore(struct char_data *ch1, struct char_data *ch2, long type);
void weather_activity(void);

/* local globals */
MYSQL *SQLdb; /* Pointer to a mySQL database structure. */
struct descriptor_data *descriptor_list = NULL;         /* master desc list */
struct txt_block *bufpool = 0;  /* pool of large output buffers */
int buf_largecount = 0;         /* # of large buffers which exist */
int buf_overflows = 0;          /* # of overflows of output */
int buf_switches = 0;           /* # of switches from small to large buf */
int circle_shutdown = 0;        /* clean shutdown */
int circle_reboot = 0;          /* reboot the game after a shutdown */
int no_specials = 0;            /* Suppress ass. of special routines */
int max_players = 0;            /* max descriptors available */
int tics = 0;                   /* for extern checkpointing */
int scheck = 0;                 /* for syntax checking mode */
int dg_act_check;               /* toggle for act_trigger */
unsigned long dg_global_pulse = 0; /* number of pulses since game start */
extern int nameserver_is_slow;  /* see config.c */
extern int auto_save;           /* see config.c */
extern int autosave_time;       /* see config.c */
struct timeval null_time;       /* zero-valued time structure */
FILE *logfile;                  /* Where to log messages. */

static bool fCopyOver;          /* Are we booting in copyover mode? */
int  mother_desc;        /* Now a global */
int     port;

/* functions in this file */
int get_from_q (struct txt_q *queue, char *dest, int *aliased);
void init_game (int port);
void signal_setup (void);
void make_who2html (void);
void time_bells (void);
void write_mud_date_to_file(void);
void game_loop (int mother_desc);
int init_socket (int port);
int new_descriptor (int s);
int get_max_players (void);
int process_output (struct descriptor_data *t);
int process_input (struct descriptor_data *t);
void close_socket (struct descriptor_data *d);
struct timeval timediff (struct timeval a, struct timeval b);
struct timeval timeadd (struct timeval a, struct timeval b);
void flush_queues (struct descriptor_data *d);
void nonblock (socket_t s);
int perform_subst (struct descriptor_data *t, char *orig, char *subst);
int perform_alias (struct descriptor_data *d, char *orig);
void record_usage (void);
char *make_prompt (struct descriptor_data *point);
void check_idle_passwords (void);
void heartbeat (int pulse);
int set_sendbuf (socket_t s);
void    init_descriptor (struct descriptor_data *newd, int desc);

/* extern fcnts */
void boot_db (void);
void boot_world (void);
void zone_update (void);
void affect_update (void);      /* In spells.c */
void point_update (void);       /* In limits.c */
void mobile_activity (void);
void string_add (struct descriptor_data *d, char *str);
void perform_violence (void);
void show_string (struct descriptor_data *d, char *input);
int isbanned (char *hostname);
void another_hour(int mode);
void redit_save_to_disk (int zone_num);
void oedit_save_to_disk (int zone_num);
void medit_save_to_disk (int zone_num);
void sedit_save_to_disk (int zone_num);
void zedit_save_to_disk (int zone_num);
void clearobservers(struct char_data*);
void deobserve(struct char_data *);
int real_zone (int number);
void restore_bup_affects(struct char_data *ch);
ACMD (do_weather);


struct in_addr *get_bind_addr(void);
int parse_ip(const char *addr, struct in_addr *inaddr);


#ifdef __CXREF__
#undef FD_ZERO
#undef FD_SET
#undef FD_ISSET
#undef FD_CLR
#define FD_ZERO(x)
#define FD_SET(x, y) 0
#define FD_ISSET(x, y) 0
#define FD_CLR(x, y)
#endif


/***********************************************************************
*  main game loop and related stuff                                    *
***********************************************************************/

/* Windows doesn't have gettimeofday, so we'll simulate it. */
#ifdef CIRCLE_WINDOWS

void
gettimeofday (struct timeval *t, struct timezone *dummy)
{
  DWORD millisec = GetTickCount ();

  t->tv_sec = (int) (millisec / 1000);
  t->tv_usec = (millisec % 1000) * 1000;
}

#endif


int
main (int argc, char **argv)
{
//  int port;
  char buf[512];
  int pos = 1;
  char *dir;

  port = DFLT_PORT;
  dir = DFLT_DIR;

  /* Be nice to make this a command line option but the parser uses log() */
  if (*LOGFILE != '\0' && !(logfile = freopen (LOGFILE, "w", stderr)))
    {
      fprintf (stdout, "Error opening log file!\n");
      exit (1);
    }

  while ((pos < argc) && (*(argv[pos]) == '-'))
    {
      switch (*(argv[pos] + 1))
	{
	case 'C': /* -C<socket number> - recover from copyover, this is the control socket */
	    fCopyOver = TRUE;
	    mother_desc = atoi(argv[pos]+2);
	    break;

	case 'd':
	  if (*(argv[pos] + 2))
	    dir = argv[pos] + 2;
	  else if (++pos < argc)
	    dir = argv[pos];
	  else
	    {
	      log ("SYSERR: Directory arg expected after option -d.");
	      exit (1);
	    }
	  break;
	case 'm':
	  mini_mud = 1;
	  no_rent_check = 1;
	  log ("Running in minimized mode & with no rent check.");
	  break;
	case 'c':
	  scheck = 1;
	  log ("Syntax check mode enabled.");
	  break;
	case 'q':
	  no_rent_check = 1;
	  log ("Quick boot mode -- rent check supressed.");
	  break;
	case 'r':
	  circle_restrict = 1;
	  log ("Restricting game -- no new players allowed.");
	  break;
	case 's':
	  no_specials = 1;
	  log ("Suppressing assignment of special routines.");
	  break;
	default:
	  sprintf (buf, "SYSERR: Unknown option -%c in argument string.", *(argv[pos] + 1));
	  log (buf);
	  break;
	}
      pos++;
    }

  if (pos < argc)
    {
      if (!isdigit (*argv[pos]))
	{
	  fprintf (logfile, "Usage: %s [-c] [-m] [-q] [-r] [-s] [-d pathname] [port #]\n", argv[0]);
	  exit (1);
	}
      else if ((port = atoi (argv[pos])) <= 1024)
	{
	  fprintf (logfile, "Illegal port number.\n");
	  exit (1);
	}
    }

  if (chdir (dir) < 0)
    {
      perror ("SYSERR: Fatal error changing to data directory");
      exit (1);
    }
  sprintf (buf, "Using %s as data directory.", dir);
  log (buf);

  if (scheck)
    {
      boot_world ();
      log ("Done.");
      exit (0);
    }
  else
    {
      sprintf (buf, "Running game on port %d.", port);
      log (buf);
      init_game (port);
    }
log ("ERROR: Game loop not started, but no errors found.");
return 0;
}

 int enter_player_game(struct descriptor_data *d);
				       
 /* Reload players after a copyover */
 void copyover_recover()
 {     
       struct descriptor_data *d;
       FILE *fp, *tf;
       struct timeval time, time2, time3;
       char host[1024];
       int desc;
     bool fOld;
     char name[MAX_INPUT_LENGTH];
     extern void read_aliases (struct char_data *ch);
       log ("Copyover recovery initiated");
 
       fp = fopen (COPYOVER_FILE, "r");
 
       if (!fp) /* there are some descriptors open which will hang forever then ? */
       {
	       perror ("copyover_recover:fopen");
	       log ("Copyover file not found. Exitting.\n\r");
	       exit (1);
       }
       
       unlink (COPYOVER_FILE); /* In case something crashes - doesn't prevent reading  */
       gettimeofday (&time2, (struct timezone *) 0);
       if ((tf = fopen ("copyover.benchmark", "r"))) {
	 fread(&time, sizeof(time), 1, tf);
	 fclose(tf);
	 time3=timediff(time2, time);
       }
       else {
	 time3.tv_sec=0;
	 time3.tv_usec=0;
       }
     
       for (;;)
     {
	 fOld = TRUE;
	       fscanf (fp, "%d %s %s\n", &desc, name, host);
	       if (desc == -1)
		       break;
	
	       /* Write something, and check if it goes error-free */
	       if (write_to_descriptor (desc, "\n\rRestoring from copyover...\n\r") < 0)
	       {
		       close (desc); /* nope */
		       continue;
	       }
     
	 /* create a new descriptor */
	 CREATE (d, struct descriptor_data, 1);
	 memset ((char *) d, 0, sizeof (struct descriptor_data));
	       init_descriptor (d,desc); /* set up various stuff */
	       
	       strcpy(d->host, host);
	       d->next = descriptor_list;
	       descriptor_list = d;
	       
	 d->connected = CON_CLOSE;
		       
	       /* Now, find the pfile */
		
	 CREATE(d->character, struct char_data, 1);
	 clear_char(d->character);
	 CREATE(d->character->player_specials, struct player_special_data, 1);
	 d->character->desc = d;
	       
         if (retrieve_player_entry(name, d->character))
	 {
	     read_aliases(d->character);
	     if (!PLR_FLAGGED(d->character, PLR_DELETED))
		 REMOVE_BIT(PLR_FLAGS(d->character),PLR_WRITING | PLR_MAILING | PLR_CRYO);
	     else      
		 fOld = FALSE;
	 }      
	 else
	     fOld = FALSE;
	 
	       if (!fOld) /* Player file not found?! */
	       {
	       write_to_descriptor (desc, "\n\rSomehow, your character was lost in the copyover. Sorry.\n\r");
		       close_socket (d);
	       }
	       else /* ok! */
	       {
	     sprintf(buf, "\n\rThe reboot has been completed in %d.%d seconds. You may continue playing.\n\r", (int)time3.tv_sec, (int)time3.tv_usec);
	     write_to_descriptor (desc, buf);
	     enter_player_game(d);
	     d->connected = CON_PLAYING;
	     look_at_room(d->character, 0);
	       }
	     
       } 
	       
       fclose (fp);
 }
		      

/* Init sockets, run game, and cleanup sockets */
void
init_game (int port)
{
//  int mother_desc;

  circle_srandom (time (0));

  log ("Finding player limit.");
  max_players = get_max_players ();

       if (!fCopyOver) /* If copyover mother_desc is already set up */
       {
	       log ("Opening mother connection.");
	       mother_desc = init_socket (port);
       }

  boot_db ();

#ifdef CIRCLE_UNIX
  log ("Signal trapping.");
  signal_setup ();
#endif

     if (fCopyOver) /* reload players */
	       copyover_recover();
 
  log ("Entering game loop.");

  game_loop (mother_desc);

  log ("Closing all sockets.");
  while (descriptor_list)
    close_socket (descriptor_list);

  CLOSE_SOCKET (mother_desc);

  if (circle_reboot != 2 && olc_save_list)
    {                           /* Don't save zones. */
      struct olc_save_info *entry, *next_entry;
      int rznum;

      for (entry = olc_save_list; entry; entry = next_entry)
	{
	  next_entry = entry->next;
	  if (entry->type < 0 || entry->type > 4)
	    {
	      sprintf (buf, "OLC: Illegal save type %d!", entry->type);
	      log (buf);
	    }
	  else if ((rznum = real_zone (entry->zone * 100)) == -1)
	    {
	      sprintf (buf, "OLC: Illegal save zone %d!", entry->zone);
	      log (buf);
	    }
	  else if (rznum < 0 || rznum > top_of_zone_table)
	    {
	      sprintf (buf, "OLC: Invalid real zone number %d!", rznum);
	      log (buf);
	    }
	  else
	    {
	      sprintf (buf, "OLC: Reboot saving %s for zone %d.",
		save_info_msg[(int) entry->type], zone_table[rznum].number);
	      log (buf);
	      switch (entry->type)
		{
		case OLC_SAVE_ROOM:
		  redit_save_to_disk (rznum);
		  break;
		case OLC_SAVE_OBJ:
		  oedit_save_to_disk (rznum);
		  break;
		case OLC_SAVE_MOB:
		  medit_save_to_disk (rznum);
		  break;
		case OLC_SAVE_ZONE:
		  zedit_save_to_disk (rznum);
		  break;
		case OLC_SAVE_SHOP:
		  sedit_save_to_disk (rznum);
		  break;
		default:
		  log ("Unexpected olc_save_list->type");
		  break;
		}
	    }
	}
    }

  if (circle_reboot)
    {
      log ("Rebooting.");
      exit (52);                /* what's so great about HHGTTG, anyhow? */
    }
  log ("Normal termination of game.");
}

/*
 * init_socket sets up the mother descriptor - creates the socket, sets
 * its options up, binds it, and listens.
 */


int
init_socket (int port)
{
  int s, opt;
  struct sockaddr_in sa;

#ifdef CIRCLE_WINDOWS
  {
    WORD wVersionRequested;
    WSADATA wsaData;

    wVersionRequested = MAKEWORD (1, 1);

    if (WSAStartup (wVersionRequested, &wsaData) != 0)
      {
	log ("SYSERR: WinSock not available!\n");
	exit (1);
      }
    if ((wsaData.iMaxSockets - 4) < max_players)
      {
	max_players = wsaData.iMaxSockets - 4;
      }
    sprintf (buf, "Max players set to %d", max_players);
    log (buf);

    if ((s = socket (PF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
      {
	fprintf (logfile, "Error opening network connection: Winsock err #%d\n", WSAGetLastError ());
	exit (1);
      }
  }
#else
  /*
   * Should the first argument to socket() be AF_INET or PF_INET?  I don't
   * know, take your pick.  PF_INET seems to be more widely adopted, and
   * Comer (_Internetworking with TCP/IP_) even makes a point to say that
   * people erroneously use AF_INET with socket() when they should be using
   * PF_INET.  However, the man pages of some systems indicate that AF_INET
   * is correct; some such as ConvexOS even say that you can use either one.
   * All implementations I've seen define AF_INET and PF_INET to be the same
   * number anyway, so the point is (hopefully) moot.
   */

  if ((s = socket (PF_INET, SOCK_STREAM, 0)) < 0)
    {
      perror ("SYSERR: Error creating socket");
      exit (1);
    }
#endif /* CIRCLE_WINDOWS */

#if defined(SO_REUSEADDR)
  opt = 1;
  if (setsockopt (s, SOL_SOCKET, SO_REUSEADDR, (char *) &opt, sizeof (opt)) < 0)
    {
      perror ("SYSERR: setsockopt REUSEADDR");
      exit (1);
    }
#endif

  set_sendbuf (s);

#if defined(SO_LINGER)
  {
    struct linger ld;

    ld.l_onoff = 0;
    ld.l_linger = 0;
    if (setsockopt (s, SOL_SOCKET, SO_LINGER, (char *) &ld, sizeof (ld)) < 0)
      {
	perror ("SYSERR: setsockopt LINGER");
	exit (1);
      }
  }
#endif

  sa.sin_family = AF_INET;
  sa.sin_port = htons (port);
  sa.sin_addr = *(get_bind_addr());
  bzero(sa.sin_zero, 8);

  if (bind (s, (struct sockaddr *) &sa, sizeof (sa)) < 0)
    {
      perror ("SYSERR: bind");
      CLOSE_SOCKET (s);
      exit (1);
    }
  nonblock (s);
  listen (s, 5);
  return s;
}


int
get_max_players (void)
{
#ifndef CIRCLE_UNIX
  return MAX_PLAYERS;
#else

  int max_descs = 0;
  char *method;

/*
 * First, we'll try using getrlimit/setrlimit.  This will probably work
 * on most systems.
 */
#if defined (RLIMIT_NOFILE) || defined (RLIMIT_OFILE)
#if !defined(RLIMIT_NOFILE)
#define RLIMIT_NOFILE RLIMIT_OFILE
#endif
  {
    struct rlimit limit;

    /* find the limit of file descs */
    method = "rlimit";
    if (getrlimit (RLIMIT_NOFILE, &limit) < 0)
      {
	perror ("SYSERR: calling getrlimit");
	exit (1);
      }
    /* set the current to the maximum */
    limit.rlim_cur = limit.rlim_max;
    if (setrlimit (RLIMIT_NOFILE, &limit) < 0)
      {
	perror ("SYSERR: calling setrlimit");
	exit (1);
      }
#ifdef RLIM_INFINITY
    if (limit.rlim_max == RLIM_INFINITY)
      max_descs = MAX_PLAYERS + NUM_RESERVED_DESCS;
    else
      max_descs = MIN (MAX_PLAYERS + NUM_RESERVED_DESCS, limit.rlim_max);
#else
    max_descs = MIN (MAX_PLAYERS + NUM_RESERVED_DESCS, limit.rlim_max);
#endif
  }

#elif defined (OPEN_MAX) || defined(FOPEN_MAX)
#if !defined(OPEN_MAX)
#define OPEN_MAX FOPEN_MAX
#endif
  method = "OPEN_MAX";
  max_descs = OPEN_MAX;         /* Uh oh.. rlimit didn't work, but we have
				 * OPEN_MAX */
#elif defined (POSIX)
  /*
   * Okay, you don't have getrlimit() and you don't have OPEN_MAX.  Time to
   * use the POSIX sysconf() function.  (See Stevens' _Advanced Programming
   * in the UNIX Environment_).
   */
  method = "POSIX sysconf";
  errno = 0;
  if ((max_descs = sysconf (_SC_OPEN_MAX)) < 0)
    {
      if (errno == 0)
	max_descs = MAX_PLAYERS + NUM_RESERVED_DESCS;
      else
	{
	  perror ("SYSERR: Error calling sysconf");
	  exit (1);
	}
    }
#else
  /* if everything has failed, we'll just take a guess */
  max_descs = MAX_PLAYERS + NUM_RESERVED_DESCS;
#endif

  /* now calculate max _players_ based on max descs */
  max_descs = MIN (MAX_PLAYERS, max_descs - NUM_RESERVED_DESCS);

  if (max_descs <= 0)
    {
      sprintf (buf, "SYSERR: Non-positive max player limit!  (Set at %d using %s).",
	       max_descs, method);
      log (buf);
      exit (1);
    }
  sprintf (buf, "Setting player limit to %d using %s.", max_descs, method);
  log (buf);
  return max_descs;
#endif /* CIRCLE_UNIX */
}



/*
 * game_loop contains the main loop which drives the entire MUD.  It
 * cycles once every 0.10 seconds and is responsible for accepting new
 * new connections, polling existing connections for input, dequeueing
 * output and sending it out to players, and calling "heartbeat" functions
 * such as mobile_activity().
 */
void
game_loop (int mother_desc)
{
  fd_set input_set, output_set, exc_set, null_set;
  struct timeval last_time, before_sleep, opt_time, process_time, now,
    timeout;
  time_t timenow, timenext;
  struct tm *tmnow;
  char comm[MAX_STRING_LENGTH];  
  struct descriptor_data *d, *next_d;
  int pulse = 0, missed_pulses, maxdesc, aliased;

  /* initialize various time values */
  null_time.tv_sec = 0;
  null_time.tv_usec = 0;
  opt_time.tv_usec = OPT_USEC;
  opt_time.tv_sec = 0;
  timenow = timenext = 0;
  FD_ZERO (&null_set);
 
  gettimeofday (&last_time, (struct timezone *) 0);

  /* The Main Loop.  The Big Cheese.  The Top Dog.  The Head Honcho.  The.. */
  while (!circle_shutdown)
    {

      /* Sleep if we don't have any connections */
      if (descriptor_list == NULL)
	{
	  log ("No connections.  Going to sleep.");
	  FD_ZERO (&input_set);
	  FD_SET (mother_desc, &input_set);
	  if (select (mother_desc + 1, &input_set, (fd_set *) 0, (fd_set *) 0, NULL) < 0)
	    {
	      if (errno == EINTR)
		log ("Waking up to process signal.");
	      else
		perror ("Select coma");
	    }
	  else
	    log ("New connection.  Waking up.");
	  gettimeofday (&last_time, (struct timezone *) 0);
	}
      /* Timed reboot check */
      timenow = time(0);
      if (autoreboot == 1) {
      if ((timenow > timenext) && (reboot_hr >= 0)) {
	timenext = timenow + 30;
	tmnow = localtime (&timenow);
	if ((tmnow->tm_hour == warn_hr) && (tmnow->tm_min == warn_min)){
	  mudlog ("Reminder: System will AUTO reboot in 10 minutes time",
		  BRF, LVL_IMMORT, FALSE);
	  mudlog ("          Please save ALL your work to prevent loss.",
		  BRF, LVL_IMMORT, FALSE);
	  send_to_all ("&m[&YINFO&m]&n System will AUTO Reboot in ten minutes time.
\r\n");
	}
	if ((tmnow->tm_hour == reboot_hr) && (tmnow->tm_min == reboot_min)){
	  sprintf (buf, "(GC) AUTO Reboot by system.");
	  log (buf);
	  send_to_all ("&m[&YINFO&m]&n AUTO Rebooting.. come back in a minute or two.\r\n");
	  touch (FASTBOOT_FILE);
	  circle_shutdown = circle_reboot = 1;
	}
      }
     }
      /* Set up the input, output, and exception sets for select(). */
      FD_ZERO (&input_set);
      FD_ZERO (&output_set);
      FD_ZERO (&exc_set);
      FD_SET (mother_desc, &input_set);

      maxdesc = mother_desc;
      for (d = descriptor_list; d; d = d->next)
	{
#ifndef CIRCLE_WINDOWS
	  if (d->descriptor > maxdesc)
	    maxdesc = d->descriptor;
#endif
	  FD_SET (d->descriptor, &input_set);
	  FD_SET (d->descriptor, &output_set);
	  FD_SET (d->descriptor, &exc_set);
	}

      /*
       * At this point, we have completed all input, output and heartbeat
       * activity from the previous iteration, so we have to put ourselves
       * to sleep until the next 0.1 second tick.  The first step is to
       * calculate how long we took processing the previous iteration.
       */

      gettimeofday (&before_sleep, (struct timezone *) 0);      /* current time */
      process_time = timediff (before_sleep, last_time);

      /*
       * If we were asleep for more than one pass, count missed pulses and sleep
       * until we're resynchronized with the next upcoming pulse.
       */
      if (process_time.tv_sec == 0 && process_time.tv_usec < OPT_USEC)
	{
	  missed_pulses = 0;
	}
      else
	{
	  missed_pulses = process_time.tv_sec * PASSES_PER_SEC;
	  missed_pulses += process_time.tv_usec / OPT_USEC;
	  process_time.tv_sec = 0;
	  process_time.tv_usec = process_time.tv_usec % OPT_USEC;
	}

      /* Calculate the time we should wake up */
      last_time = timeadd (before_sleep, timediff (opt_time, process_time));

      /* Now keep sleeping until that time has come */

      gettimeofday (&now, (struct timezone *) 0);
      timeout = timediff (last_time, now);

      /* Go to sleep */
      do
	{
#ifdef CIRCLE_WINDOWS
	  Sleep (timeout.tv_sec * 1000 + timeout.tv_usec / 1000);
#else
	  if (select (0, (fd_set *) 0, (fd_set *) 0, (fd_set *) 0, &timeout) < 0)
	    {
	      if (errno != EINTR)
		{
		  perror ("SYSERR: Select sleep");
		  exit (1);
		}
	    }
#endif /* CIRCLE_WINDOWS */
	  gettimeofday (&now, (struct timezone *) 0);
	  timeout = timediff (last_time, now);
	}
      while (timeout.tv_usec || timeout.tv_sec);

      /* Poll (without blocking) for new input, output, and exceptions */
      if (select (maxdesc + 1, &input_set, &output_set, &exc_set, &null_time) < 0)
	{
	  perror ("Select poll");
	  return;
	}
      /* If there are new connections waiting, accept them. */
      if (FD_ISSET (mother_desc, &input_set))
	new_descriptor (mother_desc);

      /* Kick out the freaky folks in the exception set and marked for close */
      for (d = descriptor_list; d; d = next_d)
	{
	  next_d = d->next;
	  if (FD_ISSET (d->descriptor, &exc_set) || d->close_me)
	    {
	      FD_CLR (d->descriptor, &input_set);
	      FD_CLR (d->descriptor, &output_set);
	      close_socket (d);
	    }
	}

      /* Process descriptors with input pending */
      for (d = descriptor_list; d; d = next_d)
	{
	  next_d = d->next;
	  if (FD_ISSET (d->descriptor, &input_set))
	    if (process_input (d) < 0)
	      close_socket (d);
	}

      /* Process commands we just read from process_input */
      for (d = descriptor_list; d; d = next_d)
	{
	  next_d = d->next;

	  if ((--(d->wait) <= 0) && get_from_q (&d->input, comm, &aliased))
	    {
	      if (d->character)
		{
		  /* Reset the idle timer & pull char back from void if necessary */
		  d->character->char_specials.timer = 0;
		  if (!d->connected && GET_WAS_IN (d->character) != NOWHERE)
		    {
		      if (d->character->in_room != NOWHERE)
			char_from_room (d->character);
		      char_to_room (d->character, GET_WAS_IN (d->character));
		      GET_WAS_IN (d->character) = NOWHERE;
		      act ("$n has returned.", TRUE, d->character, 0, 0, TO_ROOM);
		    }
		}
	      d->wait = 1;
	      d->has_prompt = 0;

	      /*
	       * I reversed these top 2 if checks so that you can use the
	       * page_string function in the editor.
	       */
	      if (d->showstr_count)     /* Reading something w/ pager */
		show_string (d, comm);
	      else if (d->str)  /* Writing boards, mail, etc. */
		string_add (d, comm);
	      else if (d->connected != CON_PLAYING)     /* In menus, etc. */
		nanny (d, comm);
	      else
		{               /* else: we're playing normally. */
		  if (aliased)  /* To prevent recursive aliases. */
		    d->has_prompt = 1;  /* To get newline before next cmd output. */
		  else
		    {
		      if (perform_alias (d, comm))      /* Run it through aliasing system */
			get_from_q (&d->input, comm, &aliased);
		    }
		  command_interpreter (d->character, comm);     /* Send it to interpreter */
		}
	    }
	}

      /* Send queued output out to the operating system (ultimately to user) */
      for (d = descriptor_list; d; d = next_d)
	{
	  next_d = d->next;
	  if (*(d->output) && FD_ISSET (d->descriptor, &output_set))
	    {
	      /* Output for this player is ready */
	      if (process_output (d) < 0)
		close_socket (d);
	      else
		d->has_prompt = 1;
	    }
	}

      /* Print prompts for other descriptors who had no other output */
      for (d = descriptor_list; d; d = d->next)
	{
	  if (!d->has_prompt)
	    {
	      write_to_descriptor (d->descriptor, make_prompt (d));
	      d->has_prompt = 1;
	    }
	}

      /* Kick out folks in the CON_CLOSE state */
      for (d = descriptor_list; d; d = next_d)
	{
	  next_d = d->next;
	  if (STATE (d) == CON_CLOSE)
	    close_socket (d);
	}

      /*
       * Now, we execute as many pulses as necessary--just one if we haven't
       * missed any pulses, or make up for lost time if we missed a few
       * pulses by sleeping for too long.
       */
      missed_pulses++;

      if (missed_pulses <= 0)
	{
	  log ("SYSERR: **BAD** MISSED_PULSES NONPOSITIVE, TIME GOING BACKWARDS!!");
	  missed_pulses = 1;
	}

      /* If we missed more than 30 seconds worth of pulses, just do 30 secs */
      if (missed_pulses > (30 * PASSES_PER_SEC))
	{
	  log ("SYSERR: Missed more than 30 seconds worth of pulses");
	  missed_pulses = 30 * PASSES_PER_SEC;
	}

      /* Now execute the heartbeat functions */
      while (missed_pulses--)
	heartbeat (++pulse);

      /* Roll pulse over after 10 hours */
      if (pulse >= (600 * 60 * PASSES_PER_SEC))
	pulse = 0;

#ifdef CIRCLE_UNIX
      /* Update tics for deadlock protection (UNIX only) */
      tics++;
      write_mud_date_to_file();
#endif
    }
}


void heartbeat (int pulse)
{
  static int mins_since_crashsave = 0;

   void process_events(void);
   dg_global_pulse++;
   process_events();

   if (!(pulse % PULSE_DG_SCRIPT))
     script_trigger_check();

  if (!(pulse % PULSE_ZONE))
    zone_update ();


  if (!(pulse % (15 * PASSES_PER_SEC)))         /* 15 seconds */
    check_idle_passwords ();
  if (!(pulse % (15 * PASSES_PER_SEC)))
    auction_update ();

  if (!(pulse % PULSE_MOBILE))
    mobile_activity ();

  if (!(pulse % PULSE_VIOLENCE))
    perform_violence ();

  if (!(pulse % (30 * PASSES_PER_SEC))) /* every 30 secs */
    weather_activity();

  if (!(pulse % (60 * PASSES_PER_SEC))){ /* every minute */
    quest_update ();
    blood_update ();
  }

  if (!(pulse % (SECS_PER_MUD_HOUR * PASSES_PER_SEC)))
    {
      another_hour (1);
      affect_update ();
      point_update ();
    }

  if (auto_save && !(pulse % (60 * PASSES_PER_SEC)))
    {                           /* 1 minute */
      if (++mins_since_crashsave >= autosave_time)
	{
	  mins_since_crashsave = 0;
	  Crash_save_all ();
	  House_save_all ();
	  make_who2html ();
	}
    }
  if (!(pulse % (5 * 60 * PASSES_PER_SEC)))     /* 5 minutes */
    record_usage ();
}


/* ******************************************************************
   *  general utility stuff (for local use)                            *
   ****************************************************************** */

/*
 *  new code to calculate time differences, which works on systems
 *  for which tv_usec is unsigned (and thus comparisons for something
 *  being < 0 fail).  Based on code submitted by ss@sirocco.cup.hp.com.
 */

/*
 * code to return the time difference between a and b (a-b).
 * always returns a nonnegative value (floors at 0).
 */
struct timeval
timediff (struct timeval a, struct timeval b)
{
  struct timeval rslt;

  if (a.tv_sec < b.tv_sec)
    return null_time;
  else if (a.tv_sec == b.tv_sec)
    {
      if (a.tv_usec < b.tv_usec)
	return null_time;
      else
	{
	  rslt.tv_sec = 0;
	  rslt.tv_usec = a.tv_usec - b.tv_usec;
	  return rslt;
	}
    }
  else
    {                           /* a->tv_sec > b->tv_sec */
      rslt.tv_sec = a.tv_sec - b.tv_sec;
      if (a.tv_usec < b.tv_usec)
	{
	  rslt.tv_usec = a.tv_usec + 1000000 - b.tv_usec;
	  rslt.tv_sec--;
	}
      else
	rslt.tv_usec = a.tv_usec - b.tv_usec;
      return rslt;
    }
}

/* add 2 timevals */
struct timeval
timeadd (struct timeval a, struct timeval b)
{
  struct timeval rslt;

  rslt.tv_sec = a.tv_sec + b.tv_sec;
  rslt.tv_usec = a.tv_usec + b.tv_usec;

  while (rslt.tv_usec >= 1000000)
    {
      rslt.tv_usec -= 1000000;
      rslt.tv_sec++;
    }

  return rslt;
}


void
record_usage (void)
{
  int sockets_connected = 0, sockets_playing = 0;
  struct descriptor_data *d;
  char buf[256];

  for (d = descriptor_list; d; d = d->next)
    {
      sockets_connected++;
      if (!d->connected)
	sockets_playing++;
    }

  sprintf (buf, "nusage: %-3d sockets connected, %-3d sockets playing",
	   sockets_connected, sockets_playing);
  log (buf);

#ifdef RUSAGE
  {
    struct rusage ru;

    getrusage (0, &ru);
    sprintf (buf, "rusage: user time: %ld sec, system time: %ld sec, max res size: %ld",
	     ru.ru_utime.tv_sec, ru.ru_stime.tv_sec, ru.ru_maxrss);
    log (buf);
  }
#endif

}



/*
 * Turn off echoing (specific to telnet client)
 */
void
echo_off (struct descriptor_data *d)
{
  char off_string[] =
  {
    (char) IAC,
    (char) WILL,
    (char) TELOPT_ECHO,
    (char) 0,
  };

  SEND_TO_Q (off_string, d);
}


/*
 * Turn on echoing (specific to telnet client)
 */
void
echo_on (struct descriptor_data *d)
{
  char on_string[] =
  {
    (char) IAC,
    (char) WONT,
    (char) TELOPT_ECHO,
    (char) TELOPT_NAOFFD,
    (char) TELOPT_NAOCRD,
    (char) 0,
  };

  SEND_TO_Q (on_string, d);
}

/* Initialize a descriptor */
void init_descriptor (struct descriptor_data *newd, int desc)
{
    static int last_desc = 0;   /* last descriptor number */

  /* initialize descriptor data */
  newd->descriptor = desc;
  newd->connected = CON_QANSI;
  newd->idle_tics = 0;
  newd->wait = 1;
  newd->output = newd->small_outbuf;
  newd->bufspace = SMALL_BUFSIZE - 1;
  newd->login_time = time (0);
  *newd->output = '\0';
  newd->bufptr = 0;
  newd->has_prompt = 1;               /* prompt is part of greetings */
      
  if (++last_desc == 1000)
    last_desc = 1;
  newd->desc_num = last_desc;    
}

char *
make_prompt (struct descriptor_data *d)
{
  static char prompt[256];
  extern int exp_to_level();

  /* Note, prompt is truncated at MAX_PROMPT_LENGTH chars (structs.h ) */

  /*
   * These two checks were reversed to allow page_string() to work in the
   * online editor.
   */
  if (d->showstr_count)
    {
      sprintf (prompt,
	       "\r[ Return to continue, (q)uit, (r)efresh, (b)ack, or page number (%d/%d) ]",
	       d->showstr_page, d->showstr_count);
    }
  else if (d->str)
    strcpy (prompt, "] ");
  else if (!d->connected)
    {
/*    char prompt[MAX_INPUT_LENGTH]; */
      *prompt = '\0';

      if (GET_INVIS_LEV (d->character))
	sprintf (prompt, "&Ri&Y%d&n ", (int) GET_INVIS_LEV (d->character));

      if (PRF_FLAGGED (d->character, PRF_DISPHP))
	sprintf (prompt, "%s&G%d&ghp&w ", prompt, (int) GET_HIT (d->character));

      if (PRF_FLAGGED (d->character, PRF_DISPMANA))
	sprintf (prompt, "%s&C%d&cmp&w ", prompt, (int) GET_MANA (d->character));

      if (PRF_FLAGGED (d->character, PRF_DISPMOVE) && !RIDING(d->character)) {
	sprintf (prompt, "%s&M%d&mmv&w ", prompt, (int) GET_MOVE (d->character));
      } else {
	if (PRF_FLAGGED (d->character, PRF_DISPMOVE) && RIDING(d->character))
	  sprintf (prompt, "%s&M%d&m&ym&mmv&w ", prompt, 
		   (int) GET_MOVE(RIDING(d->character)));
      }

      if (PRF_FLAGGED (d->character, PRF_AFK))
	{
	  sprintf (prompt, "%s&W(&naway&W)&n ", prompt);

	}
      else
	{
	  if (PRF_FLAGGED (d->character, PRF_DISPEXP) && GET_LEVEL(d->character) < LVL_HERO)
	    sprintf (prompt, "%s&W(&n%d&W) ", prompt, exp_to_level(GET_LEVEL(d->character)) - GET_EXP (d->character));
	  if (PRF2_FLAGGED (d->character, PRF2_DISPMOB) && FIGHTING(d->character))
	 sprintf (prompt, "%s&R(&n%s&R) ", prompt, prompt_diag(FIGHTING(d->character), d->character));
	}
      if (has_mail (GET_IDNUM (d->character)))
     {
	    sprintf (prompt, "%s&B(&Ymail&B)&n ", prompt);
     }
if (GET_COND (d->character, DRUNK) > 4)
     {
	    sprintf (prompt, "%s&G(&ndrunk&G)&n ", prompt);
     }
      strcat (prompt, "&R>&w ");

/* Ok, now we need to cool the color interpreter before pasting to the
   socket */

      proc_color (prompt, GET_LEVEL(d->character) < LVL_IMMORT && world[d->character->in_room].weather == WEATHER_MAGICFOG ? -1 : (clr (d->character, C_NRM)));

      strcpy (prompt, prompt);
/*    write_to_descriptor(d->descriptor, prompt); */

    }
  else
    *prompt = '\0';

  return prompt;
}


void
write_to_q (char *txt, struct txt_q *queue, int aliased)
{
  struct txt_block *new;

  CREATE (new, struct txt_block, 1);
  CREATE (new->text, char, strlen (txt) + 1);
  strcpy (new->text, txt);
  new->aliased = aliased;

  /* queue empty? */
  if (!queue->head)
    {
      new->next = NULL;
      queue->head = queue->tail = new;
    }
  else
    {
      queue->tail->next = new;
      queue->tail = new;
      new->next = NULL;
    }
}



int
get_from_q (struct txt_q *queue, char *dest, int *aliased)
{
  struct txt_block *tmp;

  /* queue empty? */
  if (!queue->head)
    return 0;

  tmp = queue->head;
  strcpy (dest, queue->head->text);
  *aliased = queue->head->aliased;
  queue->head = queue->head->next;

  free (tmp->text);
  free (tmp);

  return 1;
}



/* Empty the queues before closing connection */
void
flush_queues (struct descriptor_data *d)
{
  int dummy;

  if (d->large_outbuf)
    {
      d->large_outbuf->next = bufpool;
      bufpool = d->large_outbuf;
    }

  while (get_from_q (&d->input, buf2, &dummy));
}

/* Add a new string to a player's output queue */
void
write_to_output (const char *txt, struct descriptor_data *t)
{
  int size;

  size = strlen (txt);

  /* if we're in the overflow state already, ignore this new output */
  if (t->bufptr < 0)
    return;

  /* if we have enough space, just write to buffer and that's it! */
  if (t->bufspace >= size)
    {
      strcpy (t->output + t->bufptr, txt);
      t->bufspace -= size;
      t->bufptr += size;
      return;
    }
  /*
   * If the text is too big to fit into even a large buffer, chuck the
   * new text and switch to the overflow state.
   */
  if (size + t->bufptr > LARGE_BUFSIZE - 1)
    {
      t->bufptr = -1;
      buf_overflows++;
      return;
    }
  buf_switches++;

  /* if the pool has a buffer in it, grab it */
  if (bufpool != NULL)
    {
      t->large_outbuf = bufpool;
      bufpool = bufpool->next;
    }
  else
    {                           /* else create a new one */
      CREATE (t->large_outbuf, struct txt_block, 1);
      CREATE (t->large_outbuf->text, char, LARGE_BUFSIZE);
      buf_largecount++;
    }

  strcpy (t->large_outbuf->text, t->output);    /* copy to big buffer */
  t->output = t->large_outbuf->text;    /* make big buffer primary */
  strcat (t->output, txt);      /* now add new text */

  /* set the pointer for the next write */
  t->bufptr = strlen (t->output);

  /* calculate how much space is left in the buffer */
  t->bufspace = LARGE_BUFSIZE - 1 - t->bufptr;
}



/* ******************************************************************
   *  socket handling                                                  *
   ****************************************************************** */

struct in_addr *get_bind_addr()
{
  static struct in_addr bind_addr;
  
  /* Clear the structure */
  memset((char *) &bind_addr, 0, sizeof(bind_addr));
      
  /* If DLFT_IP is unspecified, use INADDR_ANY */
  if (DFLT_IP == NULL) {
    bind_addr.s_addr = htonl(INADDR_ANY);
  } else {
    /* If the parsing fails, use INADDR_ANY */
    if (!parse_ip(DFLT_IP, &bind_addr)) { 
      log("SYSERR: DFLT_IP appears to be an invalid IP address");
      bind_addr.s_addr = htonl(INADDR_ANY);
    }
  }
 
  /* Put the address that we've finally decided on into the logs */
  if (bind_addr.s_addr == htonl(INADDR_ANY))
    log("Binding to all IP interfaces on this host.");
  else
    log("Binding to singular IP address.");
  
  return (&bind_addr);  
}

#ifdef HAVE_INET_ATON   
                  
int parse_ip(const char *addr, struct in_addr *inaddr)
{
  return (inet_aton(addr, inaddr));
}
     
#elif HAVE_INET_ADDR
  
/* inet_addr has a different interface, so we emulate inet_aton's */
int parse_ip(const char *addr, struct in_addr *inaddr)
{
  long ip;
  
  if ((ip = inet_addr(addr)) == -1) {
    return (0);
  } else {
    inaddr->s_addr = (unsigned long) ip;
    return (1);
  }   
}
 
#else

/* If you have neither function - sorry, you can't do specific binding. */
int parse_ip(const char *addr, struct in_addr *inaddr)
{
  log("SYSERR: warning: you're trying to set DFLT_IP but your system has no\n"
      "functions to parse IP addresses (how bizarre!)");
  return (0);
}
    
#endif /* INET_ATON and INET_ADDR */

#if defined(SO_SNDBUF)
/* Sets the kernel's send buffer size for the descriptor */
int
set_sendbuf (socket_t s)
{
  int opt = MAX_SOCK_BUF;

  if (setsockopt (s, SOL_SOCKET, SO_SNDBUF, (char *) &opt, sizeof (opt)) < 0)
    {
      perror ("SYSERR: setsockopt SNDBUF");
      return -1;
    }

#if 0
  if (setsockopt (s, SOL_SOCKET, SO_RCVBUF, (char *) &opt, sizeof (opt)) < 0)
    {
      perror ("SYSERR: setsockopt RCVBUF");
      return -1;
    }
#endif

  return 0;
}
#endif

int
new_descriptor (int s)
{
  socket_t desc;
  int sockets_connected = 0;
  unsigned long addr;
  int i;
//  static int last_desc = 0;   /* last descriptor number */
  struct descriptor_data *newd;
  struct sockaddr_in peer;
  struct hostent *from;
  extern char *ANSI;


  /* accept the new connection */
  i = sizeof (peer);
  if ((desc = accept (s, (struct sockaddr *) &peer, &i)) == INVALID_SOCKET)
    {
      perror ("accept");
      return -1;
    }
  /* keep it from blocking */
  nonblock (desc);

  /* set the send buffer size if available on the system */
#if defined (SO_SNDBUF)
  if (set_sendbuf (desc) < 0)
    {
      CLOSE_SOCKET (desc);
      return 0;
    }
#endif

  /* make sure we have room for it */
  for (newd = descriptor_list; newd; newd = newd->next)
    sockets_connected++;

  if (sockets_connected >= max_players)
    {
      write_to_descriptor (desc, "Sorry, DeltaMUD is full right now...please try again later!\r\n");
      CLOSE_SOCKET (desc);
      return 0;
    }
  /* create a new descriptor */
  CREATE (newd, struct descriptor_data, 1);
  memset ((char *) newd, 0, sizeof (struct descriptor_data));

  /* find the sitename */
  if (nameserver_is_slow || !(from = gethostbyaddr ((char *) &peer.sin_addr,
					  sizeof (peer.sin_addr), AF_INET)))
    {
      /* resolution failed */
      if (!nameserver_is_slow)
	perror ("gethostbyaddr");
      /* find the numeric site address */
      addr = ntohl (peer.sin_addr.s_addr);
      sprintf (newd->host, "%03u.%03u.%03u.%03u", (int) ((addr & 0xFF000000) >> 24),
	(int) ((addr & 0x00FF0000) >> 16), (int) ((addr & 0x0000FF00) >> 8),
	       (int) ((addr & 0x000000FF)));
    }
  else
    {
      strncpy (newd->host, from->h_name, HOST_LENGTH);
      *(newd->host + HOST_LENGTH) = '\0';
    }

  /* determine if the site is banned */
  if (isbanned (newd->host) == BAN_ALL)
    {
      write_to_descriptor (desc, "Your site is BANNED!\r\n");
      CLOSE_SOCKET (desc);
      sprintf (buf2, "Connection attempt denied from [%s]", newd->host);
      mudlog (buf2, PFT, LVL_GOD, TRUE);
      free (newd);
      return 0;
    }
#if 0
  /* Log new connections - probably unnecessary, but you may want it */
  sprintf (buf2, "New connection from [%s]", newd->host);
  mudlog (buf2, PFT, LVL_GOD, FALSE);
#endif

//  /* initialize descriptor data */
//  newd->descriptor = desc;
//  newd->connected = CON_QANSI;
//  newd->idle_tics = 0;
//  newd->wait = 1;
//  newd->output = newd->small_outbuf;
//  newd->bufspace = SMALL_BUFSIZE - 1;
//  newd->login_time = time (0);
//  *newd->output = '\0';
//  newd->bufptr = 0;
//  newd->has_prompt = 1;               /* prompt is part of greetings */
//
//  if (++last_desc == 1000)
//    last_desc = 1;
//  newd->desc_num = last_desc;

  init_descriptor(newd, desc);
  /* prepend to list */
  newd->next = descriptor_list;
  descriptor_list = newd;

  SEND_TO_Q (ANSI, newd);

  return 0;
}



/*
 * Send all of the output that we've accumulated for a player out to
 * the player's descriptor.
 * FIXME - This will be rewritten before 3.1, this code is dumb.
 */
int
process_output (struct descriptor_data *t)
{
  static char i[MAX_SOCK_BUF];
  static int result;

  /* we may need this \r\n for later -- see below */
  strcpy (i, "\r\n");

  /* now, append the 'real' output */
  strcpy (i + 2, t->output);

  /* if we're in the overflow state, notify the user */
  if (t->bufptr < 0)
    strcat (i, "**OVERFLOW**");

  /* add the extra CRLF if the person isn't in compact mode */
  if (!t->connected && t->character && !PRF_FLAGGED (t->character, PRF_COMPACT))
    strcat (i + 2, "\r\n");
  if (t->character)
    proc_color (i, GET_LEVEL(t->character) < LVL_IMMORT && world[t->character->in_room].weather == WEATHER_MAGICFOG ? -1 : (clr (t->character, C_NRM)));
  /* add a prompt */
  strncat (i + 2, make_prompt (t), MAX_PROMPT_LENGTH);

  /*
   * now, send the output.  If this is an 'interruption', use the prepended
   * CRLF, otherwise send the straight output sans CRLF.
   */
  if (t->has_prompt)            /* && !t->connected) */
    result = write_to_descriptor (t->descriptor, i);
  else
    result = write_to_descriptor (t->descriptor, i + 2);

  /* handle snooping: prepend "% " and send to snooper */
  if (t->snoop_by)
    {
      SEND_TO_Q ("% ", t->snoop_by);
      SEND_TO_Q (t->output, t->snoop_by);
      SEND_TO_Q ("%%", t->snoop_by);
    }
  /*
   * if we were using a large buffer, put the large buffer on the buffer pool
   * and switch back to the small one, or 25% of the time just free it.
   */
  if (t->large_outbuf)
    {
      if (number (0, 3))
	{                       /* Keep it. */
	  t->large_outbuf->next = bufpool;
	  bufpool = t->large_outbuf;
	}
      else
	{                       /* Free it. */
	  free (t->large_outbuf);
	  buf_largecount--;
	}
      t->large_outbuf = NULL;
      t->output = t->small_outbuf;
    }
  /* reset total bufspace back to that of a small buffer */
  t->bufspace = SMALL_BUFSIZE - 1;
  t->bufptr = 0;
  *(t->output) = '\0';

  return result;
}



int
write_to_descriptor (socket_t desc, char *txt)
{
  int total, bytes_written;

  total = strlen (txt);

  do
    {
#ifdef CIRCLE_WINDOWS
      if ((bytes_written = send (desc, txt, total, 0)) < 0)
	{
	  if (WSAGetLastError () == WSAEWOULDBLOCK)
#else
      if ((bytes_written = write (desc, txt, total)) < 0)
	{
#ifdef EWOULDBLOCK
	  if (errno == EWOULDBLOCK)
	    errno = EAGAIN;
#endif /* EWOULDBLOCK */
	  if (errno == EAGAIN)
#endif /* CIRCLE_WINDOWS */
	    log ("process_output: socket write would block, about to close");
	  else
	    perror ("Write to socket");
	  return -1;
	}
      else
	{
	  txt += bytes_written;
	  total -= bytes_written;
	}
    }
  while (total > 0);

  return 0;
}


/*
 * ASSUMPTION: There will be no newlines in the raw input buffer when this
 * function is called.  We must maintain that before returning.
 */
int
process_input (struct descriptor_data *t)
{
  int buf_length, bytes_read, space_left, failed_subst;
  char *ptr, *read_point, *write_point, *nl_pos = NULL;
  char tmp[MAX_INPUT_LENGTH + 8];

  /* first, find the point where we left off reading data */
  buf_length = strlen (t->inbuf);
  read_point = t->inbuf + buf_length;
  space_left = MAX_RAW_INPUT_LENGTH - buf_length - 1;

  do
    {
      if (space_left <= 0)
	{
	  log ("process_input: about to close connection: input overflow");
	  return -1;
	}
#ifdef CIRCLE_WINDOWS
      if ((bytes_read = recv (t->descriptor, read_point, space_left, 0)) < 0)
	{
	  if (WSAGetLastError () != WSAEWOULDBLOCK)
	    {
#else
      if ((bytes_read = read (t->descriptor, read_point, space_left)) < 0)
	{
#ifdef EWOULDBLOCK
	  if (errno == EWOULDBLOCK)
	    errno = EAGAIN;
#endif /* EWOULDBLOCK */
	  if (errno != EAGAIN && errno != EINTR)
	    {
#endif /* CIRCLE_WINDOWS */
	      perror ("process_input: about to lose connection");
	      return -1;        /* some error condition was encountered on
				 * read */
	    }
	  else
	    return 0;           /* the read would have blocked: just means no
				 * data there but everything's okay */
	}
      else if (bytes_read == 0)
	{
	  log ("EOF on socket read (connection broken by peer)");
	  return -1;
	}
      /* at this point, we know we got some data from the read */

      *(read_point + bytes_read) = '\0';        /* terminate the string */

      /* search for a newline in the data we just read */
      for (ptr = read_point; *ptr && !nl_pos; ptr++)
	if (ISNEWL (*ptr))
	  nl_pos = ptr;

      read_point += bytes_read;
      space_left -= bytes_read;

/*
 * on some systems such as AIX, POSIX-standard nonblocking I/O is broken,
 * causing the MUD to hang when it encounters input not terminated by a
 * newline.  This was causing hangs at the Password: prompt, for example.
 * I attempt to compensate by always returning after the _first_ read, instead
 * of looping forever until a read returns -1.  This simulates non-blocking
 * I/O because the result is we never call read unless we know from select()
 * that data is ready (process_input is only called if select indicates that
 * this descriptor is in the read set).  JE 2/23/95.
 */
#if !defined(POSIX_NONBLOCK_BROKEN)
    }
  while (nl_pos == NULL);
#else
    }
  while (0);

  if (nl_pos == NULL)
    return 0;
#endif /* POSIX_NONBLOCK_BROKEN */

  /*
   * okay, at this point we have at least one newline in the string; now we
   * can copy the formatted data to a new array for further processing.
   */

  read_point = t->inbuf;

  while (nl_pos != NULL)
    {
      write_point = tmp;
      space_left = MAX_INPUT_LENGTH - 1;

      for (ptr = read_point; (space_left > 0) && (ptr < nl_pos); ptr++)
	{
	  if (*ptr == '\b')
	    {                   /* handle backspacing */
	      if (write_point > tmp)
		{
		  if (*(--write_point) == '$')
		    {
		      write_point--;
		      space_left += 2;
		    }
		  else
		    space_left++;
		}
	    }
	  else if (isascii (*ptr) && isprint (*ptr))
	    {
	      if ((*(write_point++) = *ptr) == '$')
		{               /* copy one character */
		  *(write_point++) = '$';       /* if it's a $, double it */
		  space_left -= 2;
		}
	      else
		space_left--;
	    }
	}

      *write_point = '\0';

      if ((space_left <= 0) && (ptr < nl_pos))
	{
	  char buffer[MAX_INPUT_LENGTH + 64];

	  sprintf (buffer, "Line too long.  Truncated to:\r\n%s\r\n", tmp);
	  if (write_to_descriptor (t->descriptor, buffer) < 0)
	    return -1;
	}
      if (t->snoop_by)
	{
	  SEND_TO_Q ("% ", t->snoop_by);
	  SEND_TO_Q (tmp, t->snoop_by);
	  SEND_TO_Q ("\r\n", t->snoop_by);
	}
      failed_subst = 0;

      if (*tmp == '!')
	strcpy (tmp, t->last_input);
      else if (*tmp == '^')
	{
	  if (!(failed_subst = perform_subst (t, t->last_input, tmp)))
	    strcpy (t->last_input, tmp);
	}
      else
	strcpy (t->last_input, tmp);

      if (!failed_subst)
	write_to_q (tmp, &t->input, 0);

      /* find the end of this line */
      while (ISNEWL (*nl_pos))
	nl_pos++;

      /* see if there's another newline in the input buffer */
      read_point = ptr = nl_pos;
      for (nl_pos = NULL; *ptr && !nl_pos; ptr++)
	if (ISNEWL (*ptr))
	  nl_pos = ptr;
    }

  /* now move the rest of the buffer up to the beginning for the next pass */
  write_point = t->inbuf;
  while (*read_point)
    *(write_point++) = *(read_point++);
  *write_point = '\0';

  return 1;
}



/* perform substitution for the '^..^' csh-esque syntax orig is the
 * orig string, i.e. the one being modified.  subst contains the
 * substition string, i.e. "^telm^tell"
 */
int
perform_subst (struct descriptor_data *t, char *orig, char *subst)
{
  char new[MAX_INPUT_LENGTH + 5];

  char *first, *second, *strpos;

  /*
   * first is the position of the beginning of the first string (the one
   * to be replaced
   */
  first = subst + 1;

  /* now find the second '^' */
  if (!(second = strchr (first, '^')))
    {
      SEND_TO_Q ("Invalid substitution.\r\n", t);
      return 1;
    }
  /* terminate "first" at the position of the '^' and make 'second' point
   * to the beginning of the second string */
  *(second++) = '\0';

  /* now, see if the contents of the first string appear in the original */
  if (!(strpos = strstr (orig, first)))
    {
      SEND_TO_Q ("Invalid substitution.\r\n", t);
      return 1;
    }
  /* now, we construct the new string for output. */

  /* first, everything in the original, up to the string to be replaced */
  strncpy (new, orig, (strpos - orig));
  new[(strpos - orig)] = '\0';

  /* now, the replacement string */
  strncat (new, second, (MAX_INPUT_LENGTH - strlen (new) - 1));

  /* now, if there's anything left in the original after the string to
   * replaced, copy that too. */
  if (((strpos - orig) + strlen (first)) < strlen (orig))
    strncat (new, strpos + strlen (first), (MAX_INPUT_LENGTH - strlen (new) - 1));

  /* terminate the string in case of an overflow from strncat */
  new[MAX_INPUT_LENGTH - 1] = '\0';
  strcpy (subst, new);

  return 0;
}


void
close_socket (struct descriptor_data *d)
{
  char buf[128];
  struct descriptor_data *temp;
  long target_idnum = -1;

  CLOSE_SOCKET (d->descriptor);
  flush_queues (d);

  /* Forget snooping */
  if (d->snooping)
    d->snooping->snoop_by = NULL;

  if (d->snoop_by)
    {
      SEND_TO_Q ("Your victim is no longer among us.\r\n", d->snoop_by);
      d->snoop_by->snooping = NULL;
    }

  /*. Kill any OLC stuff . */
  switch (d->connected)
    {
    case CON_OEDIT:
    case CON_REDIT:
    case CON_ZEDIT:
    case CON_MEDIT:
    case CON_SEDIT:
    case CON_TRIGEDIT: 
     cleanup_olc (d, CLEANUP_ALL);
    default:
      break;
    }

  if (d->character)
    {
      target_idnum = GET_IDNUM (d->character);
      if (d->connected == CON_PLAYING)
	{
	  save_char (d->character, NOWHERE);

 if(GET_LEVEL(d->character) < LVL_IMMORT)
  {

// this was annoying, removed 8/23/98 --Mulder
//        sprintf (buf, "&m[&YINFO&m]&n %s has left Deltania. (linkless)\r\n", GET_NAME (d->character));
//        send_to_all (buf);

	  if (IS_ARENACOMBATANT(d->character))
	    restore_bup_affects(d->character);

	  REMOVE_BIT (PRF2_FLAGS (d->character), PRF2_LOCKOUT);      
	  deobserve(d->character);
	  clearobservers(d->character);
}
	  act("$n has lost $s link.", TRUE, d->character, 0, 0, TO_ROOM);
	  sprintf (buf, "Closing link to: %s.", GET_NAME (d->character));
	  mudlog (buf, NRM, MAX (LVL_IMMORT, GET_INVIS_LEV (d->character)), TRUE);
	  d->character->desc = NULL;
	}
      else
	{
	  sprintf (buf, "Losing player: %s.",
	      GET_NAME (d->character) ? GET_NAME (d->character) : "<null>");
	  mudlog (buf, PFT, LVL_IMMORT, TRUE);
	  free_char (d->character);
	}
    }
  else
    mudlog ("Losing descriptor without char.", PFT, LVL_IMMORT, TRUE);

  /* JE 2/22/95 -- part of my unending quest to make switch stable */
  if (d->original && d->original->desc)
    d->original->desc = NULL;

  REMOVE_FROM_LIST (d, descriptor_list, next);

  if (d->showstr_head)
    free (d->showstr_head);
  if (d->showstr_count)
    free (d->showstr_vector);

  free (d);
}



void
check_idle_passwords (void)
{
  struct descriptor_data *d, *next_d;

  for (d = descriptor_list; d; d = next_d)
    {
      next_d = d->next;
      if (STATE (d) != CON_PASSWORD && STATE (d) != CON_GET_NAME && STATE (d) != CON_QANSI)
	continue;
      if (!d->idle_tics)
	{
	  d->idle_tics++;
	  continue;
	}
      else
	{
	  echo_on (d);
	  SEND_TO_Q ("\r\nTimed out... goodbye.\r\n", d);
	  STATE (d) = CON_CLOSE;
	}
    }
}



/*
 * I tried to universally convert Circle over to POSIX compliance, but
 * alas, some systems are still straggling behind and don't have all the
 * appropriate defines.  In particular, NeXT 2.x defines O_NDELAY but not
 * O_NONBLOCK.  Krusty old NeXT machines!  (Thanks to Michael Jones for
 * this and various other NeXT fixes.)
 */

#if defined(CIRCLE_WINDOWS)

void
nonblock (socket_t s)
{
  long val;

  val = 1;
  ioctlsocket (s, FIONBIO, &val);
}

#elif defined(CIRCLE_AMIGA)

void
nonblock (socket_t s)
{
  long val;

  val = 1;
  IoctlSocket (s, FIONBIO, &val);
}

#elif defined(CIRCLE_UNIX) || defined(CIRCLE_OS2)

#ifndef O_NONBLOCK
#define O_NONBLOCK O_NDELAY
#endif

void
nonblock (socket_t s)
{
  int flags;

  flags = fcntl (s, F_GETFL, 0);
  flags |= O_NONBLOCK;
  if (fcntl (s, F_SETFL, flags) < 0)
    {
      perror ("SYSERR: Fatal error executing nonblock (comm.c)");
      exit (1);
    }
}

#endif /* CIRCLE_UNIX || CIRCLE_OS2 */


/* ******************************************************************
   *  signal-handling functions (formerly signals.c).  UNIX only.      *
   ****************************************************************** */


#ifdef CIRCLE_UNIX

RETSIGTYPE
checkpointing (int sig)
{
  if (!tics)
    {
      log ("SYSERR: CHECKPOINT shutdown: tics not updated");
      abort ();
    }
  else
    tics = 0;
}


RETSIGTYPE
reread_wizlists (int sig)
{
  void reboot_wizlists (void);

  mudlog ("Signal received - rereading wizlists.", PFT, LVL_IMMORT, TRUE);
  reboot_wizlists ();
}


RETSIGTYPE
unrestrict_game (int sig)
{
  extern struct ban_list_element *ban_list;
  extern int num_invalid;

  mudlog ("Received SIGUSR2 - completely unrestricting game (emergent)", BRF, LVL_IMMORT, TRUE);
  ban_list = NULL;
  circle_restrict = 0;
  num_invalid = 0;
}


RETSIGTYPE
hupsig (int sig)
{
  log ("SYSERR: Received SIGHUP, SIGINT, or SIGTERM.  Shutting down...");
  exit (0);                     /* perhaps something more elegant should
				 * substituted */
}


/*
 * This is an implementation of signal() using sigaction() for portability.
 * (sigaction() is POSIX; signal() is not.)  Taken from Stevens' _Advanced
 * Programming in the UNIX Environment_.  We are specifying that all system
 * calls _not_ be automatically restarted for uniformity, because BSD systems
 * do not restart select(), even if SA_RESTART is used.
 *
 * Note that NeXT 2.x is not POSIX and does not have sigaction; therefore,
 * I just define it to be the old signal.  If your system doesn't have
 * sigaction either, you can use the same fix.
 *
 * SunOS Release 4.0.2 (sun386) needs this too, according to Tim Aldric.
 */

#ifndef POSIX
#define my_signal(signo, func) signal(signo, func)
#else
sigfunc *
my_signal (int signo, sigfunc * func)
{
  struct sigaction act, oact;

  act.sa_handler = func;
  sigemptyset (&act.sa_mask);
  act.sa_flags = 0;
#ifdef SA_INTERRUPT
  act.sa_flags |= SA_INTERRUPT; /* SunOS */
#endif

  if (sigaction (signo, &act, &oact) < 0)
    return SIG_ERR;

  return oact.sa_handler;
}
#endif /* POSIX */


void
signal_setup (void)
{
  struct itimerval itime;
  struct timeval interval;

  /* user signal 1: reread wizlists.  Used by autowiz system. */
  my_signal (SIGUSR1, reread_wizlists);

  /*
   * user signal 2: unrestrict game.  Used for emergencies if you lock
   * yourself out of the MUD somehow.  (Duh...)
   */
  my_signal (SIGUSR2, unrestrict_game);

  /*
   * set up the deadlock-protection so that the MUD aborts itself if it gets
   * caught in an infinite loop for more than 3 minutes.
   */
  interval.tv_sec = 180;
  interval.tv_usec = 0;
  itime.it_interval = interval;
  itime.it_value = interval;
  setitimer (ITIMER_VIRTUAL, &itime, NULL);
  my_signal (SIGVTALRM, checkpointing);

  /* just to be on the safe side: */
  my_signal (SIGHUP, SIG_IGN);
  my_signal (SIGINT, hupsig);
  my_signal (SIGTERM, hupsig);
  my_signal (SIGPIPE, SIG_IGN);
  my_signal (SIGALRM, SIG_IGN);
  my_signal (SIGTRAP, SIG_IGN);
}
#endif /* CIRCLE_UNIX */

/* ****************************************************************
   *       Public routines for system-to-player-communication        *
   **************************************************************** */

void
send_to_char (char *messg, struct char_data *ch)
{
  if (ch->desc && messg)
    SEND_TO_Q (messg, ch->desc);
}


void
send_to_all (char *messg)
{
  struct descriptor_data *i;

  if (messg)
    for (i = descriptor_list; i; i = i->next)
      if (!i->connected)
	SEND_TO_Q (messg, i);
}


void
send_to_outdoor (char *messg)
{
  struct descriptor_data *i;

  if (!messg || !*messg)
    return;

  for (i = descriptor_list; i; i = i->next)
    if (!i->connected && i->character && AWAKE (i->character) &&
	OUTSIDE (i->character))
      SEND_TO_Q (messg, i);
}



void
send_to_room (char *messg, int room)
{
  struct char_data *i;

  if (messg)
    for (i = world[room].people; i; i = i->next_in_room)
      if (i->desc)
	SEND_TO_Q (messg, i->desc);
}



char *ACTNULL = "<NULL>";

#define CHECK_NULL(pointer, expression) \
  if ((pointer) == NULL) i = ACTNULL; else i = (expression);


/* higher-level communication: the act() function */
void
perform_act (char *orig, struct char_data *ch, struct obj_data *obj,
	     void *vict_obj, struct char_data *to)
{
  register char *i = NULL, *buf;
  static char lbuf[MAX_STRING_LENGTH];
  struct char_data *dg_victim = NULL;
  struct obj_data *dg_target = NULL; 
  char *dg_arg = NULL;


/*  if (PRF2_FLAGGED(ch, PRF2_INTANGIBLE) && !PRF2_FLAGGED(ch, PRF2_MBUILDING)
      && GET_LEVEL(to) < LVL_IMMORT) return;
*/
  buf = lbuf;

  for (;;)
    {
      if (*orig == '$')
	{
	  switch (*(++orig))
	    {
	    case 'n':
	      i = PERS (ch, to);
	      break;
	    case 'N':
	      CHECK_NULL (vict_obj, PERS ((struct char_data *) vict_obj, to));
	    dg_victim = (struct char_data *) vict_obj;
	      break;
	    case 'm':
	      i = HMHR (ch);
	      break;
	    case 'M':
	      CHECK_NULL (vict_obj, HMHR ((struct char_data *) vict_obj));
	  dg_victim = (struct char_data *) vict_obj;
	      break;
	    case 's':
	      i = HSHR (ch);
	      break;
	    case 'S':
	      CHECK_NULL (vict_obj, HSHR ((struct char_data *) vict_obj));
	 dg_victim = (struct char_data *) vict_obj;
	      break;
	    case 'e':
	      i = HSSH (ch);
	      break;
	    case 'E':
	      CHECK_NULL (vict_obj, HSSH ((struct char_data *) vict_obj));
	  dg_victim = (struct char_data *) vict_obj;
	      break;
	    case 'o':
	      CHECK_NULL (obj, OBJN (obj, to));
	      break;
	    case 'O':
	      CHECK_NULL (vict_obj, OBJN ((struct obj_data *) vict_obj, to));
	  dg_target = (struct obj_data *) vict_obj;
	      break;
	    case 'p':
	      CHECK_NULL (obj, OBJS (obj, to));
	      break;
	    case 'P':
	      CHECK_NULL (vict_obj, OBJS ((struct obj_data *) vict_obj, to));
	   dg_target = (struct obj_data *) vict_obj;
	      break;
	    case 'a':
	      CHECK_NULL (obj, SANA (obj));
	      break;
	    case 'A':
	      CHECK_NULL (vict_obj, SANA ((struct obj_data *) vict_obj));
	      dg_target = (struct obj_data *) vict_obj;
	      break;
	    case 'T':
	      CHECK_NULL (vict_obj, (char *) vict_obj);
	  dg_arg = (char *) vict_obj;
	      break;
	    case 'F':
	      CHECK_NULL (vict_obj, fname ((char *) vict_obj));
	      break;
	    case '$':
	      i = "$";
	      break;
	    default:
	      log ("SYSERR: Illegal $-code to act():");
	      strcpy (buf1, "SYSERR: ");
	      strcat (buf1, orig);
	      log (buf1);
	      break;
	    }
	  while ((*buf = *(i++)))
	    buf++;
	  orig++;
	}
      else if (!(*(buf++) = *(orig++)))
	break;
    }

  *(--buf) = '\r';
  *(++buf) = '\n';
  *(++buf) = '\0';

   if (to->desc)
     SEND_TO_Q(CAP(lbuf), to->desc);

   if ((IS_NPC(to) && dg_act_check) && (to != ch))
     act_mtrigger(to, lbuf, ch, dg_victim, obj, dg_target, dg_arg);
}

 /* moved this to utils.h --- mah */
 #ifndef SENDOK
#define SENDOK(ch) ((ch)->desc && (AWAKE(ch) || sleep) && \
		    !PLR_FLAGGED((ch), PLR_WRITING))
 #endif

void
act (char *str, int hide_invisible, struct char_data *ch,
     struct obj_data *obj, void *vict_obj, int type)
{
  struct char_data *to = NULL;
  struct char_data *vict = NULL;
  static int sleep;

  if (!str || !*str)
    return;

  if (!(dg_act_check = !(type & DG_NO_TRIG)))
    type &= ~DG_NO_TRIG;

  /*
   * Warning: the following TO_SLEEP code is a hack.
   * 
   * I wanted to be able to tell act to deliver a message regardless of sleep
   * without adding an additional argument.  TO_SLEEP is 128 (a single bit
   * high up).  It's ONLY legal to combine TO_SLEEP with one other TO_x
   * command.  It's not legal to combine TO_x's with each other otherwise.
   * TO_SLEEP only works because its value "happens to be" a single bit;
   * do not change it to something else.  In short, it is a hack.
   */

  /* check if TO_SLEEP is there, and remove it if it is. */
  if ((sleep = (type & TO_SLEEP)))
    type &= ~TO_SLEEP;

  if (type == TO_CHAR)
    {
      if (ch && SENDOK (ch))
	perform_act (str, ch, obj, vict_obj, ch);
      return;
    }

  if (type == TO_VICT)
    {
      if ((to = (struct char_data *) vict_obj) && SENDOK (to))
	perform_act (str, ch, obj, vict_obj, to);
      return;
    }
  /* ASSUMPTION: at this point we know type must be TO_NOTVICT or TO_ROOM */
  /* or TO_GMOTE */
 /* add from here to GMOTE_END */ 
 if (type == TO_GMOTE) {
struct descriptor_data *i;
    for (i = descriptor_list; i; i = i->next) {
      if (!i->connected && i->character &&
	!PRF_FLAGGED(i->character, PRF_NOGOSS) &&
	!PLR_FLAGGED(i->character, PLR_WRITING) &&
	!ROOM_FLAGGED(i->character->in_room, ROOM_SOUNDPROOF) &&
	!isignore(i->character, ch, IGNORE_EMOTE)) {
	send_to_char(CCYEL(i->character, C_NRM), i->character);
	perform_act(str, ch, obj, vict_obj, i->character);
	send_to_char(CCNRM(i->character, C_NRM), i->character);
      }
    }
    return;
  }
  if (ch && ch->in_room != NOWHERE)
    to = world[ch->in_room].people;
  else if (obj && obj->in_room != NOWHERE)
    to = world[obj->in_room].people;
  else
    {
      log ("SYSERR: no valid target to act()!");
      return;
    }

  for (; to; to = to->next_in_room){
    if (SENDOK (to) && !(hide_invisible && ch && !CAN_SEE (to, ch)) &&
	(to != ch) && (type == TO_ROOM || (to != vict_obj)))
      perform_act (str, ch, obj, vict_obj, to);
  }

  /* Arena mods - Thargor
   * This function is central to the communications module.
   * A player that has 1 or more observers will have a chain (linked-list)
   * of the observers. The single OBSERVE_BY() pointer is to the next observer
   * in the list. Note: A player who has an "OBSERVE_BY" pointer but has
   * GET_ARENASTAT != ARENA_COMBATANT means that that player is an observer,
   *  somewhere in the link list.
   */
  /* Why do we take the attacker's and the recipient points of view? 
   * Not their view of the events, we use a bystander's desc of the 
   * events, but report to the attacker's AND receipient's observers.
   * We cannot assume that there are other bystanders from which 
   * to look at things through.
   */
  /* Attacker's point of view */
  if (type == TO_NOTVICT
      && OBSERVE_BY(ch) != NULL 
      && IS_ARENACOMBATANT(ch)){
    to = OBSERVE_BY(ch);   
    while (to != NULL){     
      if (GET_ARENASTAT(to) == ARENA_OBSERVER)
	perform_act (str, ch, obj, vict_obj, to);
      to = OBSERVE_BY(to);
    }
  }
  /* Attackee's point of view */
  if (vict_obj != NULL){
    vict = (struct char_data *) vict_obj;
    if (vict != NULL)
      if (!IS_NPC(vict))
	if (type == TO_NOTVICT
	    && OBSERVE_BY(vict) != NULL 
	    && IS_ARENACOMBATANT(vict)){
	  to = OBSERVE_BY(vict);   
	  while (to != NULL){     
	    if (GET_ARENASTAT(to) == ARENA_OBSERVER)
	      perform_act (str, ch, obj, vict_obj, to);
	    to = OBSERVE_BY(to);
	  }
	}
  }
}

void
make_who2html (void)
{
  extern struct descriptor_data *descriptor_list;
  FILE *opf;
  struct descriptor_data *d, *x;
  struct char_data *tch, *bch=NULL;
  int num_can_see = 0, gnum=0, lstlev=0, docont=0, mort=0;
  int g;
  char i[500][500];
  extern char *class_name (struct char_data *ch);
  extern char *race_name (struct char_data *ch);

  const char *WizLevels[LVL_IMPL - (LVL_IMMORT - 1)] =
  {
    "    Immortal   ",
    "     Sage      ",
    "     Seer      ",
    "    Prophet    ",
    "  Implementor  "
  };

 if (!(www_who) > 0)
    return;

  if ((opf = fopen ("/home/mulder/www.deltamud.net/who.tmp", "w")) == 0)
    {
      log ("ERROR: Could not create who.tmp for who2html");
      return;                   /* or log it ? *shrug* */
    }
  fprintf (opf, "<html><head>\n");
  fprintf (opf, "<meta name=\"DESCRIPTION\" content=\"The official website of DeltaMUD, an online medieval roleplaying game.\">\n");
  fprintf (opf, "<meta name=\"KEYWORDS\" content=\"products; headlines; downloads; news; Web site; what's new; solutions; services; software; contests; events; MUD; games; circle; diku; adventure; rpg; virtual; world; quest; deltamud; telnet; fun; medieval; middle ages; dark ages; fantasy; merlin; myth; mythology; magic; dragons; witches; witchcraft; mages; wizards; knights; chivalry; royal; king\">\n");
  fprintf (opf, "<title>Online Who List</title>\n");
  fprintf (opf, "</head><font face=\"Arial\">\n");
  fprintf (opf, "<body bgcolor=\"#FFFFFF\" text=\"#000000\" topmargin=\"0\" leftmargin=\"0\">\n");
  fprintf (opf, "<table border=\"0\" cellpadding=\"0\" cellspacing=\"0\" width=\"100%%\"><tr><td valign=\"top\">\n");
  fprintf (opf, "<div align=\"left\"><table border=\"0\" width=\"100%%\" height=\"12\" bgcolor=\"#000000\" cellspacing=\"0\" cellpadding=\"0\">\n");
  fprintf (opf, "<tr> <td width=\"100%%\" height=\"12\" checked=\"false\" valign=\"middle\"><div align=\"center\">\n");
  fprintf (opf, "<center><p><img src=\"http://www.deltamud.net/images/banners/delt.jpg\" alt=\"DeltaMUD\" align=\"top\" width=\"171\" height=\"28\">\n");
  fprintf (opf, "<img src=\"http://www.deltamud.net/images/banners/onli.jpg\" alt=\"Online RPG\" align=\"top\" width=\"171\" height=\"28\"></td>\n");
  fprintf (opf, "</tr></table></div>\n");

  fprintf (opf, "<center><H3><strong>Online Who List</strong></H2></center>\n");

  fprintf (opf, "<p style=\"margin-left: 10px\">\n<CENTER><TABLE BORDER=\"0\" bgcolor=\"#000000\" width=\"90%%\"><TD><PRE>");
  fprintf (opf, "<font color=\"#C0C0C0\" FACE=\"fixedsys\">Immortals Currently Online<BR><font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<BR>");

  for (d = descriptor_list; d; d = d->next) {
    if (STATE(d) != CON_PLAYING)
      continue;
    if (d->original)
      tch = d->original;
    else if (!(tch = d->character))
      continue;
    if (GET_INVIS_LEV(tch) || AFF_FLAGGED(tch, AFF_INVISIBLE))
      continue;

    num_can_see=0;
    lstlev=0;
    strcpy(i[gnum],"NotARealPlayerName");
    for (x = descriptor_list; x; x = x->next) {
      if (STATE(x) != CON_PLAYING)
	continue;

      if (x->original)
	tch = x->original;
      else if (!(tch = x->character))
	continue;

      if (GET_INVIS_LEV(tch) || AFF_FLAGGED(tch, AFF_INVISIBLE))
	continue;

      num_can_see++;

      for (g=0;g<=gnum;g++)
	  if (strcmp(GET_NAME(tch),i[g])==0) {
	    docont=1;
	    break;
	  }

      if (docont) {
	docont=0;
	continue;
      }

      if (GET_LEVEL(tch)>lstlev) {
	lstlev=GET_LEVEL(tch);
	strcpy(i[gnum], GET_NAME(tch));
	if (x->original)
	  bch = x->original;
	else if (!(bch = x->character))
	  continue;
      }
    }
    buf2[0]='\0';
    if (GET_LEVEL(bch)<LVL_IMMORT && mort != 1) {
      if (gnum==0)
	strcat(buf2, "<font color=\"#C0C0C0\" FACE=\"fixedsys\">None at all!<BR>\nMortals Currently Online<BR><font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<BR>");
      else
	strcat(buf2, "\n<font color=\"#C0C0C0\" FACE=\"fixedsys\">Mortals Currently Online<BR><font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<font color=\"#00A000\">-<font color=\"#A0A000\">-<BR>");
      mort=1;
    }
    
    gnum++;
    if (GET_LEVEL (bch) >= LVL_IMMORT)
      sprintf (buf2+strlen(buf2), "<font color=\"#FFFF00\" FACE=\"fixedsys\">[%s] %s",
WizLevels[GET_LEVEL(bch) - LVL_IMMORT], GET_NAME(bch));
    else
      sprintf (buf2+strlen(buf2), "<font color=\"#0000FF\">[<font color=\"#C0C0C0\">%2d %s %s<font color=\"#0000FF\">]<font color=\"#C0C0C0\"> %s", GET_LEVEL (bch),
	    race_name(bch), class_name(bch), GET_NAME (bch));

    if (PLR_FLAGGED (bch, PLR_MAILING))
      strcat (buf2, " (mailing)");
    else if (PLR_FLAGGED (bch, PLR_WRITING))
      strcat (buf2, " (writing)");
    if (PRF_FLAGGED (bch, PRF_AFK))
      strcat (buf2, " (away)");
    if (PRF_FLAGGED (bch, PRF_DEAF))
      strcat (buf2, " (deaf)");
    if (PRF_FLAGGED (bch, PRF_NOTELL))
      strcat (buf2, " (notell)");
    if (PRF2_FLAGGED (bch, PRF2_QCHAN))
      strcat (buf2, " (qchan)");
    if (PLR_FLAGGED (bch, PLR_QUESTOR))
      strcat (buf2, " (autoquest)");
    if (PLR_FLAGGED (bch, PLR_THIEF))
      strcat (buf2, " (THIEF)");
    if (PLR_FLAGGED (bch, PLR_KILLER))
      strcat (buf2, " (KILLER)");
    if (PRF2_FLAGGED (bch, PRF2_MBUILDING))
      strcat (buf2, " (building)");
    strcat(buf2, "<BR>");

    fprintf (opf, buf2);
  }
  if (num_can_see == 0)
    fprintf (opf, "\n<font color=\"#C0C0C0\">No-one at all!<BR>");
  else if (num_can_see == 1)
    fprintf (opf, "\n<font color=\"#C0C0C0\">Just one lonely character displayed.<BR>");
  else
    fprintf (opf, "\n<font color=\"#C0C0C0\">%d characters displayed.<BR>", num_can_see);

fprintf (opf, "<font color=\"#FFFFFF\" FACE=\"fixedsys\">End of Who List</font></PRE></TD></TABLE></CENTER></p>\n");

fprintf (opf, "<p style=\"margin-left: 10px\"><small>Auto-updated every 60 seconds.<br>Invisible players are not displayed.<br></font></small></td></tr></table><table border=\"0\" cellpadding=\"0\" cellspacing=\"0\" width=\"100%%\"><tr><td><div align=\"center\"><center>\n");
fprintf (opf, "<table border=\"0\" width=\"100%%\" height=\"1\" cellspacing=\"0\" cellpadding=\"0\"><tr>\n");
fprintf (opf, "<td width=\"100%%\" height=\"1\" valign=\"middle\" align=\"center\"><p align=\"center\">\n");
fprintf (opf, "<a href=\"http://www21.valueclick.com/cgi-bin/redirect?host=h0031038&amp;b=1\" target=\"_blank\"\n");
fprintf (opf, "<img alt=\"Please visit our sponsors.\" border=\"0\" height=\"60\" src=\"http://www21.valueclick.com/cgi-bin/cycle?host=h0031038&amp;b=1\" width=\"468\"></a></td></tr></table></center></div>\n");
fprintf (opf, "<p align=\"center\" style=\"MARGIN-LEFT: 10px\"><small><small><font face=\"Arial\">[ <a href=\"index.html\">Home</a> | </font><a href=\"http://www.deltamud.net/main.html\"><font face=\"Arial\">Main</font></a><font face=\"Arial\"> | </font><a href=\"http://www.deltamud.net/story/index.html\"><font face=\"Arial\">Story</font></a><font face=\"Arial\"> | </font><a href=\"http://www.deltamud.net/policy/index.html\"><font face=\"Arial\">Rules</font></a><font face=\"Arial\"> | </font><a href=\"http://www.deltamud.net/download/index.html\"><font face=\"Arial\">Download</font></a><font face=\"Arial\"> | </font><a href=\"http://www.deltamud.net/news/index.html\"><font face=\"Arial\">News</font></a><font face=\"Arial\"> | </font><a href=\"http://www.deltamud.net/guide/index.html\"><font face=\"Arial\">Guide</font></a> <font face=\"Arial\"> | </font><a href=\"http://www.deltamud.net/events/index.html\"><font face=\"Arial\">Events</font></a><font face=\"Arial\"> | </font><a href=\"http://www.deltamud.net/admins/index.html\"><font face=\"Arial\">Admins</font></a><font face=\"Arial\"> ]</small></small><br> <small><small><small>DeltaMUD is � Copyright 1998 <a href=\"mailto:mulder@deltamud.net\">Michael J. Fara</a>. All Rights Reserved. The contents of this website may not be reproduced.</small></small></small></font></p></td></tr></table></body></html>\n");

//  sprintf (buf, "</BODY></HTML>\n");
//  fprintf (opf, buf);

  fclose (opf);
  system ("mv /home/mulder/www.deltamud.net/who.tmp /home/mulder/www.deltamud.net/who.html &");
}

void write_mud_date_to_file(void)
{
   FILE *f;
   struct time_write date;

   f = fopen("etc/date_record", "w");
   date.year = time_info.year;
   date.month = time_info.month;
   date.day   = time_info.day;
   fwrite(&date,sizeof(struct time_write),1,f);
   fclose(f);
}


/* Arena mod - Thargor */
struct char_data *findanyinarena()
{
  struct descriptor_data *i;

  for (i = descriptor_list; i; i = i->next)
    if (!i->connected)
      if (IS_ARENACOMBATANT(i->character))
	return (i->character);

  return NULL;

}

/* Arena mod - Thargor */
void send_to_observers(char *messg, struct char_data *who)
{
  struct char_data *tmp;
 
  if (GET_ARENASTAT(who) == ARENA_NOT || GET_ARENASTAT(who) == ARENA_OBSERVER)
    return;

  tmp = OBSERVE_BY(who);
  while (tmp != NULL){
    if (GET_ARENASTAT(tmp) == ARENA_OBSERVER && tmp->desc && messg)
      SEND_TO_Q (messg, tmp->desc);
    
    tmp = OBSERVE_BY(tmp);
  }

}

int check_multiplaying(char *hostname)
{
  struct descriptor_data *d;
  struct char_data *ch;
  int num_links = 0;
  char host[200];
  char *name=NULL;
  extern struct char_data *character_list;
  return 1; //While in development mode we wanna give our builders freedom...
  buf[0]='\0';
  if (!hostname || !*hostname)
    return 0;

  for (d = descriptor_list; d; d = d->next) {
    if (d->character || d->original) {
      if (GET_LEVEL(d->character ? d->character : d->original) >= LVL_IMPL)
        continue;
      else if (PLR_FLAGGED(d->character ? d->character : d->original, PLR_MULTIOK))
	continue;
    }
    if (!str_cmp(d->host, hostname)) {
      /* This code allows us to have a person relogin even if they're connected (takes 
	 care of ghosts and allows usurping).
	 -Storm
      */
      if (!d->character && !d->original) continue;
      if (!GET_NAME(d->character ? d->character : d->original)) continue;
      if (!name)
	name=GET_NAME(d->character);
      else if (!strcasecmp(name, GET_NAME(d->character ? d->character : d->original)))
	continue;
      num_links++;
    }
  }

  if (num_links >= 2) return 0;

  /* Check for linkdead players. */
  for (ch=character_list; ch; ch=ch->next) {
    if (!IS_NPC(ch) && !ch->desc) { // Linkdead char.
      if (!GET_NAME(ch)) continue;
      if (!name)
	name=GET_NAME(ch);
      else if (!strcasecmp(name, GET_NAME(ch)))
	continue; 
      if (PLR_FLAGGED(ch, PLR_MULTIOK)) continue;
      host[0]=0;
      pe_printf(GET_NAME(ch), "s", "host", &host);
      if (!str_cmp(host, hostname))
	num_links++; /* Ah! You left a linkdead char... bad boy. */
    }
  }
  if (num_links >= 2)
    return 0;

  return 1;
}
