/******************************************************************************
  Copyright (c) 1995, 1996 Xerox Corporation.  All rights reserved.
  Portions of this code were written by Stephen White, aka ghond.
  Use and copying of this software and preparation of derivative works based
  upon this software are permitted.  Any distribution of this software or
  derivative works must comply with all applicable United States export
  control laws.  This software is made available AS IS, and Xerox Corporation
  makes no warranty about the software, its performance or its conformity to
  any specification.  Any person obtaining a copy of this software is requested
  to send their name and post office or electronic mail address to:
    Pavel Curtis
    Xerox PARC
    3333 Coyote Hill Rd.
    Palo Alto, CA 94304
    Pavel@Xerox.Com
 *****************************************************************************/

/* Extensions to the MOO server

 * This module contains some examples of how to extend the MOO server using
 * some of the interfaces exported by various other modules.  The examples are
 * all commented out, since they're really not all that useful in general; they
 * were written primarily to test out the interfaces they use.
 *
 * The uncommented parts of this module provide a skeleton for any module that
 * implements new MOO built-in functions.  Feel free to replace the
 * commented-out bits with your own extensions; in future releases, you can
 * just replace the distributed version of this file (which will never contain
 * any actually useful code) with your own edited version as a simple way to
 * link in your extensions.
 */

#define EXAMPLE 0

#include "bf_register.h"
#include "functions.h"
#include "db_tune.h"

#if EXAMPLE

#include "my-unistd.h"

#include "log.h"
#include "net_multi.h"
#include "storage.h"
#include "tasks.h"

typedef struct stdin_waiter {
    struct stdin_waiter *next;
    vm the_vm;
} stdin_waiter;

static stdin_waiter *waiters = 0;

static task_enum_action
stdin_enumerator(task_closure closure, void *data)
{
    stdin_waiter **ww;

    for (ww = &waiters; *ww; ww = &((*ww)->next)) {
	stdin_waiter *w = *ww;
	const char *status = (w->the_vm->task_id & 1
			      ? "stdin-waiting"
			      : "stdin-weighting");
	task_enum_action tea = (*closure) (w->the_vm, status, data);

	if (tea == TEA_KILL) {
	    *ww = w->next;
	    myfree(w, M_TASK);
	    if (!waiters)
		network_unregister_fd(0);
	}
	if (tea != TEA_CONTINUE)
	    return tea;
    }

    return TEA_CONTINUE;
}

static void
stdin_readable(int fd, void *data)
{
    char buffer[1000];
    int n;
    Var v;
    stdin_waiter *w;

    if (data != &waiters)
	panic("STDIN_READABLE: Bad data!");

    if (!waiters) {
	errlog("STDIN_READABLE: Nobody cares!\n");
	return;
    }
    n = read(0, buffer, sizeof(buffer));
    buffer[n] = '\0';
    while (n)
	if (buffer[--n] == '\n')
	    buffer[n] = 'X';

    if (buffer[0] == 'a') {
	v.type = TYPE_ERR;
	v.v.err = E_NACC;
    } else {
	v.type = TYPE_STR;
	v.v.str = str_dup(buffer);
    }

    resume_task(waiters->the_vm, v);
    w = waiters->next;
    myfree(waiters, M_TASK);
    waiters = w;
    if (!waiters)
	network_unregister_fd(0);
}

static enum error
stdin_suspender(vm the_vm, void *data)
{
    stdin_waiter *w = data;

    if (!waiters)
	network_register_fd(0, stdin_readable, 0, &waiters);

    w->the_vm = the_vm;
    w->next = waiters;
    waiters = w;

    return E_NONE;
}

static package
bf_read_stdin(Var arglist, Byte next, void *vdata, Objid progr)
{
    stdin_waiter *w = mymalloc(sizeof(stdin_waiter), M_TASK);

    return make_suspend_pack(stdin_suspender, w);
}
#endif				/* EXAMPLE */

#define STUPID_VERB_CACHE 1
#ifdef STUPID_VERB_CACHE
#include "utils.h"

static package
bf_verb_cache_stats(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;

    free_var(arglist);

    if (!is_wizard(progr)) {
	return make_error_pack(E_PERM);
    }
    r = db_verb_cache_stats();

    return make_var_pack(r);
}

static package
bf_log_cache_stats(Var arglist, Byte next, void *vdata, Objid progr)
{
    free_var(arglist);

    if (!is_wizard(progr)) {
	return make_error_pack(E_PERM);
    }
    db_log_cache_stats();

    return no_var_pack();
}
#endif

// Perlin Noise code [[

  static int p[512];
  static int permutation[] = { 151,160,137,91,90,15,
  131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,
  21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
  35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
  74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,
  230,220,105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,216,
  80,73,209,76,132,187,208,89,18,169,200,196,135,130,116,188,159,86,
  164,100,109,198,173,186,3,64,52,217,226,250,124,123,5,202,38,147,
  118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,223,
  183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,
  172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
  218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,
  145,235,249,14,239,107,49,192,214,31,181,199,106,157,184,84,204,176,
  115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,
  141,128,195,78,66,215,61,156,180
  };
  /* Function declarations */
  double fade(double t);
  double lerp(double t, double a, double b);
  double grad(int hash, double x, double y, double z);
  void init_noise();
  double pnoise(double x, double y, double z);
  void init_noise() {
	int i;
	for(i = 0; i < 256 ; i++)
	p[256+i] = p[i] = permutation[i];
  }
  double pnoise(double x, double y, double z)  {
	  int X = (int)floor(x) & 255, /* FIND UNIT CUBE THAT */
	  Y = (int)floor(y) & 255, /* CONTAINS POINT. */
	  Z = (int)floor(z) & 255;
	  x -= floor(x); /* FIND RELATIVE X,Y,Z */
	  y -= floor(y); /* OF POINT IN CUBE. */
	  z -= floor(z);
	  double u = fade(x), /* COMPUTE FADE CURVES */
	  v = fade(y), /* FOR EACH OF X,Y,Z. */
	  w = fade(z);
	  int A = p[X]+Y,
	  AA = p[A]+Z,
	  AB = p[A+1]+Z, /* HASH COORDINATES OF */
	  B = p[X+1]+Y,
	  BA = p[B]+Z,
	  BB = p[B+1]+Z; /* THE 8 CUBE CORNERS, */
	  return lerp(w,lerp(v,lerp(u, grad(p[AA ], x, y, z), /* AND ADD */
	  grad(p[BA ], x-1, y, z)), /* BLENDED */
	  lerp(u, grad(p[AB ], x, y-1, z), /* RESULTS */
	  grad(p[BB ], x-1, y-1, z))), /* FROM 8 */
	  lerp(v, lerp(u, grad(p[AA+1], x, y, z-1 ),/* CORNERS */
	  grad(p[BA+1], x-1, y, z-1)), /* OF CUBE */
	  lerp(u, grad(p[AB+1], x, y-1, z-1),
	  grad(p[BB+1], x-1, y-1, z-1))));
  }
  double fade(double t){ return t * t * t * (t * (t * 6 - 15) + 10); }
  double lerp(double t, double a, double b){ return a + t * (b - a); }
  double grad(int hash, double x, double y, double z) {
	  int h = hash & 15; /* CONVERT LO 4 BITS OF HASH CODE */
	  double u = h < 8 ? x : y, /* INTO 12 GRADIENT DIRECTIONS. */
	  v = h < 4 ? y : h==12||h==14 ? x : z;
	  return ((h&1) == 0 ? u : -u) + ((h&2) == 0 ? v : -v);
  }
  int noise2d(int x, int y, double scalex, double scaley, int size) {
	  double xf = (double)x;
	  double yf = (double)y;
	  double sizef = (double)size;
	  double noiseval = pnoise(xf/(sizef * scalex), yf/(sizef * scaley), 0.5);
	  noiseval = sizef * ((noiseval + 1.0)/2.0);
	  return (int)noiseval;
  }
  int fbm2d(int x, int y, double scalex, double scaley, int size, int octaves) {
	double xf = (double)x;
	double yf = (double)y;
	double sizef = (double)size;
	double noiseval = 0.0;
  }
  int i;
  for(i = 1; i <= octaves; i++) {
	  double n = pnoise(i * xf/(sizef * scalex), i * yf/(sizef * scaley), 0.5);
	  n = sizef * ((n/2.0) + 0.5);
	  noiseval = noiseval + n / (double)i;
  }
  return (int)noiseval;
//  } // commenting out to see if this fixes the error
  static package bf_perlin_2d(Var arglist, Byte next, void *vdata, Objid progr) {
	  Var r;
	  int x = (int)arglist.v.list[1].v.num;
	  int y = (int)arglist.v.list[2].v.num;
	  double alpha = *arglist.v.list[3].v.fnum;
	  double beta = *arglist.v.list[4].v.fnum;
	  int n = (int)arglist.v.list[5].v.num;
	  int octaves = (int)arglist.v.list[6].v.num;
	  init_noise();
	  r.v.num = (int)fbm2d(x, y, alpha, beta, n, octaves);
	  r.type = TYPE_INT;
	  free_var(arglist);
	  return make_var_pack(r);
  }
  }

// ]] Perlin Noise code  
  
void
register_extensions()
{
#if EXAMPLE
    register_task_queue(stdin_enumerator);
    register_function("read_stdin", 0, 0, bf_read_stdin);
#endif
#ifdef STUPID_VERB_CACHE
    register_function("log_cache_stats", 0, 0, bf_log_cache_stats);
    register_function("verb_cache_stats", 0, 0, bf_verb_cache_stats);
#endif
// Perlin Noise code [[
  register_function("perlin_2d", 6, 6, bf_perlin_2d, TYPE_INT,
  TYPE_INT, TYPE_FLOAT, TYPE_FLOAT, TYPE_INT, TYPE_INT);
// ]] Perlin Noise code
}