// -*- c-basic-offset: 4; related-file-name: "../include/click/glue.hh" -*-
/*
 * glue.{cc,hh} -- minimize portability headaches, and miscellany
 * Robert Morris, Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>

#include <click/glue.hh>
#include <click/error.hh>

#ifdef CLICK_USERLEVEL
# include <stdarg.h>
# include <unistd.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <fcntl.h>
#elif CLICK_LINUXMODULE
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
#  include <click/cxxprotect.h>
CLICK_CXX_PROTECT
#  include <linux/vmalloc.h>
CLICK_CXX_UNPROTECT
#  include <click/cxxunprotect.h>
# endif
#endif

// Include header structures so we can check their sizes with static_assert.
#include <clicknet/ether.h>
#include <clicknet/fddi.h>
#include <clicknet/ip.h>
#include <clicknet/ip6.h>
#include <clicknet/icmp.h>
#include <clicknet/tcp.h>
#include <clicknet/udp.h>
#include <clicknet/rfc1483.h>

void
click_check_header_sizes()
{
    // <clicknet/ether.h>
    static_assert(sizeof(click_ether) == 14);
    static_assert(sizeof(click_arp) == 8);
    static_assert(sizeof(click_ether_arp) == 28);
    static_assert(sizeof(click_nd_sol) == 32);
    static_assert(sizeof(click_nd_adv) == 32);
    static_assert(sizeof(click_nd_adv2) == 24);

    // <clicknet/ip.h>
    static_assert(sizeof(click_ip) == 20);

    // <clicknet/icmp.h>
    static_assert(sizeof(click_icmp) == 8);
    static_assert(sizeof(click_icmp_paramprob) == 8);
    static_assert(sizeof(click_icmp_redirect) == 8);
    static_assert(sizeof(click_icmp_sequenced) == 8);
    static_assert(sizeof(click_icmp_tstamp) == 20);

    // <clicknet/tcp.h>
    static_assert(sizeof(click_tcp) == 20);

    // <clicknet/udp.h>
    static_assert(sizeof(click_udp) == 8);

    // <clicknet/ip6.h>
    static_assert(sizeof(click_ip6) == 40);

    // <clicknet/fddi.h>
    static_assert(sizeof(click_fddi) == 13);
    static_assert(sizeof(click_fddi_8022_1) == 16);
    static_assert(sizeof(click_fddi_8022_2) == 17);
    static_assert(sizeof(click_fddi_snap) == 21);

    // <clicknet/rfc1483.h>
    static_assert(sizeof(click_rfc1483) == 8);
}


// DEBUGGING OUTPUT

CLICK_USING_DECLS

extern "C" {
void
click_chatter(const char *fmt, ...)
{
  va_list val;
  va_start(val, fmt);

  if (ErrorHandler::has_default_handler()) {
    ErrorHandler *errh = ErrorHandler::default_handler();
    errh->verror(ErrorHandler::ERR_MESSAGE, "", fmt, val);
  } else {
#if CLICK_LINUXMODULE
# if __MTCLICK__
    static char buf[NR_CPUS][512];	// XXX
    int i = vsprintf(buf[click_current_processor()], fmt, val);
    printk("<1>%s\n", buf[click_current_processor()]);
# else
    static char buf[512];		// XXX
    int i = vsprintf(buf, fmt, val);
    printk("<1>%s\n", buf);
# endif
#elif CLICK_BSDMODULE
    vprintf(fmt, val);
#else /* User-space */
    vfprintf(stderr, fmt, val);
    fprintf(stderr, "\n");
#endif
  }
  
  va_end(val);
}
}


// DEBUG MALLOC

uint32_t click_dmalloc_where = 0x3F3F3F3F;
size_t click_dmalloc_curnew = 0;
size_t click_dmalloc_totalnew = 0;
#if CLICK_DMALLOC
size_t click_dmalloc_curmem = 0;
size_t click_dmalloc_maxmem = 0;
#endif

#if CLICK_LINUXMODULE || CLICK_BSDMODULE

# if CLICK_LINUXMODULE
#  define CLICK_ALLOC(size)	kmalloc((size), GFP_ATOMIC)
#  define CLICK_FREE(ptr)	kfree((ptr))
# else
#  define CLICK_ALLOC(size)	malloc((size), M_TEMP, M_WAITOK)
#  define CLICK_FREE(ptr)	free(ptr, M_TEMP)
# endif

# if CLICK_DMALLOC
#  define CHUNK_MAGIC		0xffff3f7f	/* -49281 */
#  define CHUNK_MAGIC_FREED	0xc66b04f5
struct Chunk {
    uint32_t magic;
    uint32_t where;
    size_t size;
    Chunk *prev;
    Chunk *next;
};
static Chunk chunks = {
    CHUNK_MAGIC, 0, 0, &chunks, &chunks
};

static char *
printable_where(Chunk *c)
{
  static char wherebuf[13];
  const char *hexstr = "0123456789ABCDEF";
  char *s = wherebuf;
  for (int i = 24; i >= 0; i -= 8) {
    int ch = (c->where >> i) & 0xFF;
    if (ch >= 32 && ch < 127)
      *s++ = ch;
    else {
      *s++ = '%';
      *s++ = hexstr[(ch>>4) & 0xF];
      *s++ = hexstr[ch & 0xF];
    }
  }
  *s++ = 0;
  return wherebuf;
}
# endif

void *
operator new(size_t sz) throw ()
{
  click_dmalloc_curnew++;
  click_dmalloc_totalnew++;
# if CLICK_DMALLOC
  void *v = CLICK_ALLOC(sz + sizeof(Chunk));
  Chunk *c = (Chunk *)v;
  c->magic = CHUNK_MAGIC;
  c->size = sz;
  c->where = click_dmalloc_where;
  c->prev = &chunks;
  c->next = chunks.next;
  c->next->prev = chunks.next = c;
  click_dmalloc_curmem += sz;
  if (click_dmalloc_curmem > click_dmalloc_maxmem)
      click_dmalloc_maxmem = click_dmalloc_curmem;
  return (void *)((unsigned char *)v + sizeof(Chunk));
# else
  return CLICK_ALLOC(sz);
# endif
}

void *
operator new[](size_t sz) throw ()
{
  click_dmalloc_curnew++;
  click_dmalloc_totalnew++;
# if CLICK_DMALLOC
  void *v = CLICK_ALLOC(sz + sizeof(Chunk));
  Chunk *c = (Chunk *)v;
  c->magic = CHUNK_MAGIC;
  c->size = sz;
  c->where = click_dmalloc_where;
  c->prev = &chunks;
  c->next = chunks.next;
  c->next->prev = chunks.next = c;
  click_dmalloc_curmem += sz;
  if (click_dmalloc_curmem > click_dmalloc_maxmem)
      click_dmalloc_maxmem = click_dmalloc_curmem;
  return (void *)((unsigned char *)v + sizeof(Chunk));
# else
  return CLICK_ALLOC(sz);
# endif
}

void
operator delete(void *addr)
{
  if (addr) {
    click_dmalloc_curnew--;
# if CLICK_DMALLOC
    Chunk *c = (Chunk *)((unsigned char *)addr - sizeof(Chunk));
    if (c->magic == CHUNK_MAGIC_FREED) {
      click_chatter("click error: double-free of memory at %p (%u @ %s)\n",
		    addr, c->size, printable_where(c));
      return;
    }
    if (c->magic != CHUNK_MAGIC) {
      click_chatter("click error: memory corruption on delete %p\n", addr);
      return;
    }
    click_dmalloc_curmem -= c->size;
    c->magic = CHUNK_MAGIC_FREED;
    c->prev->next = c->next;
    c->next->prev = c->prev;
    CLICK_FREE((void *)c);
# else
    CLICK_FREE(addr);
# endif
  }
}

void
operator delete[](void *addr)
{
  if (addr) {
    click_dmalloc_curnew--;
# if CLICK_DMALLOC
    Chunk *c = (Chunk *)((unsigned char *)addr - sizeof(Chunk));
    if (c->magic == CHUNK_MAGIC_FREED) {
      click_chatter("click error: double-free of memory at %p (%u @ %s)\n",
		    addr, c->size, printable_where(c));
      return;
    }
    if (c->magic != CHUNK_MAGIC) {
      click_chatter("click error: memory corruption on delete[] %p\n", addr);
      return;
    }
    click_dmalloc_curmem -= c->size;
    c->magic = CHUNK_MAGIC_FREED;
    c->prev->next = c->next;
    c->next->prev = c->prev;
    CLICK_FREE((void *)c);
# else
    CLICK_FREE(addr);
# endif
  }
}

void
click_dmalloc_cleanup()
{
# if CLICK_DMALLOC
  while (chunks.next != &chunks) {
    Chunk *c = chunks.next;
    chunks.next = c->next;
    c->next->prev = &chunks;

    click_chatter("  chunk at %p size %d alloc[%s] data ",
		  (void *)(c + 1), c->size, printable_where(c));
    unsigned char *d = (unsigned char *)(c + 1);
    for (int i = 0; i < 20 && i < c->size; i++)
      click_chatter("%02x", d[i]);
    click_chatter("\n");
    CLICK_FREE((void *)c);
  }
# endif
}

#endif /* CLICK_LINUXMODULE || CLICK_BSDMODULE */


// LALLOC

#if CLICK_LINUXMODULE
extern "C" {

# define CLICK_LALLOC_MAX_SMALL	131072

void *
click_lalloc(size_t size)
{
    void *v;
    if ((v = ((size > CLICK_LALLOC_MAX_SMALL) ? vmalloc(size) : kmalloc(size, GFP_ATOMIC)))) {
	click_dmalloc_curnew++;
	click_dmalloc_totalnew++;
    }
    return v;
}

void
click_lfree(void *p, size_t size)
{
    if (p) {
	((size > CLICK_LALLOC_MAX_SMALL) ? vfree(p) : kfree(p));
	click_dmalloc_curnew--;
    }
}

}
#endif


// RANDOMNESS

CLICK_DECLS

void
click_random_srandom()
{
    static const int bufsiz = 16;
    uint32_t buf[bufsiz];
    int pos = 0;
    click_gettimeofday((struct timeval *)(buf + pos));
    pos += sizeof(struct timeval) / sizeof(uint32_t);
#ifdef CLICK_USERLEVEL
# ifdef O_NONBLOCK
    int fd = open("/dev/random", O_RDONLY | O_NONBLOCK);
# elif defined(O_NDELAY)
    int fd = open("/dev/random", O_RDONLY | O_NDELAY);
# else
    int fd = open("/dev/random", O_RDONLY);
# endif
    if (fd >= 0) {
	int amt = read(fd, buf + pos, sizeof(uint32_t) * (bufsiz - pos));
	close(fd);
	if (amt > 0)
	    pos += (amt / sizeof(uint32_t));
    }
    if (pos < bufsiz)
	buf[pos++] = getpid();
    if (pos < bufsiz)
	buf[pos++] = getuid();
#endif

    uint32_t result = 0;
    for (int i = 0; i < pos; i++) {
	result ^= buf[i];
	result = (result << 1) | (result >> 31);
    }
    srandom(result);
}

CLICK_ENDDECLS


#if CLICK_LINUXMODULE
extern "C" {
uint32_t click_random_seed = 152;

void
srandom(uint32_t seed)
{
    click_random_seed = seed;
}
}
#endif


// SORTING

static int
click_qsort_partition(void *base_v, size_t size, size_t *stack,
		      int (*compar)(const void *, const void *, void *),
		      void *thunk)
{
    if (size >= 64) {
#if CLICK_LINUXMODULE
	printk("<1>click_qsort_partition: elements too large!\n");
#elif CLICK_BSDMODULE
	printf("click_qsort_partition: elements too large!\n");
#endif
	stack[1] = stack[2] = 0;
	return -E2BIG;
    }
    
    uint8_t *base = reinterpret_cast<uint8_t *>(base_v);
    size_t left = stack[0];
    size_t right = stack[3] - 1;
    size_t middle;

    // optimize for already-sorted case
    while (left < right) {
	int cmp = compar(&base[size * left], &base[size * (left + 1)], thunk);
	if (cmp > 0)
	    goto true_qsort;
	left++;
    }

    stack[1] = stack[0];
    stack[2] = stack[3];
    return 0;

  true_qsort:
    // current invariant:
    // base[i] <= base[left] for all left_init <= i < left
    // base[left+1] < base[left]
    // => swap base[left] <=> base[left+1], make base[left] the pivot
    uint8_t pivot[64], tmp[64];
    memcpy(&pivot[0], &base[size * left], size);
    memcpy(&base[size * left], &base[size * (left + 1)], size);
    memcpy(&base[size * (left + 1)], &pivot[0], size);
    left = left + 1;
    middle = left + 1;
    
    // loop invariant:
    // base[i] <= pivot for all left_init <= i < left
    // base[i] > pivot for all right < i <= right_init
    // base[i] == pivot for all left <= i < middle
    while (middle <= right) {
	int cmp = compar(&base[size * middle], &pivot[0], thunk);
	size_t swapper = (cmp < 0 ? left : right);
	if (cmp != 0 && middle != swapper) {
	    memcpy(&tmp[0], &base[size * swapper], size);
	    memcpy(&base[size * swapper], &base[size * middle], size);
	    memcpy(&base[size * middle], &tmp[0], size);
	}
	if (cmp < 0) {
	    left++;
	    middle++;
	} else if (cmp > 0)
	    right--;
	else
	    middle++;
    }

    // afterwards, middle == right + 1
    // so base[i] == pivot for all left <= i <= right
    stack[1] = left;
    stack[2] = right + 1;
    return 0;
}

#define CLICK_QSORT_INITSTACK 20

int
click_qsort(void *base, size_t n, size_t size, int (*compar)(const void *, const void *, void *), void *thunk)
{
    size_t stackbuf[CLICK_QSORT_INITSTACK];
    size_t stacksiz = CLICK_QSORT_INITSTACK;
    size_t *stack = stackbuf;
    size_t stackpos = 0;
    int r = 0;

    stack[stackpos++] = 0;
    stack[stackpos++] = n;

    while (stackpos != 0 && r >= 0) {
	stackpos -= 2;
	if (stack[stackpos] + 1 >= stack[stackpos + 1])
	    continue;
	
	// grow stack if appropriate
	if (stackpos + 4 > stacksiz) {
	    size_t *nstack = (size_t *) CLICK_LALLOC(sizeof(size_t) * stacksiz * 2);
	    if (!nstack) {
		r = -ENOMEM;
		break;
	    }
	    memcpy(nstack, stack, sizeof(size_t) * stacksiz);
	    if (stack != stackbuf)
		CLICK_LFREE(stack, sizeof(size_t) * stacksiz);
	    stack = nstack;
	    stacksiz *= 2;
	}

	// partition
	stack[stackpos + 3] = stack[stackpos + 1];
	r = click_qsort_partition(base, size, stack + stackpos, compar, thunk);
	stackpos += 4;
    }

    if (stack != stackbuf)
	CLICK_LFREE(stack, sizeof(size_t) * stacksiz);
    return r;
}

int
click_qsort(void *base, size_t n, size_t size, int (*compar)(const void *, const void *))
{
    // XXX fix cast
    int (*compar2)(const void *, const void *, void *);
    compar2 = reinterpret_cast<int (*)(const void *, const void *, void *)>(compar);
    return click_qsort(base, n, size, compar2, 0);
}


// TIMEVALS AND JIFFIES

#if CLICK_USERLEVEL

# if CLICK_HZ != 100
#  error "CLICK_HZ must be 100"
# endif

unsigned
CLICK_NAME(click_jiffies)()
{
  struct timeval tv;
  click_gettimeofday(&tv);
  return (tv.tv_sec * 100) + (tv.tv_usec / 10000);
}

#endif


// OTHER

#if CLICK_LINUXMODULE || CLICK_BSDMODULE

#if CLICK_BSDMODULE

/*
 * Character types glue for isalnum() et al, from Linux.
 */

/*
 *  From linux/lib/ctype.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

unsigned char _ctype[] = {
_C,_C,_C,_C,_C,_C,_C,_C,			/* 0-7 */
_C,_C|_S,_C|_S,_C|_S,_C|_S,_C|_S,_C,_C,		/* 8-15 */
_C,_C,_C,_C,_C,_C,_C,_C,			/* 16-23 */
_C,_C,_C,_C,_C,_C,_C,_C,			/* 24-31 */
_S|_SP,_P,_P,_P,_P,_P,_P,_P,			/* 32-39 */
_P,_P,_P,_P,_P,_P,_P,_P,			/* 40-47 */
_D,_D,_D,_D,_D,_D,_D,_D,			/* 48-55 */
_D,_D,_P,_P,_P,_P,_P,_P,			/* 56-63 */
_P,_U|_X,_U|_X,_U|_X,_U|_X,_U|_X,_U|_X,_U,	/* 64-71 */
_U,_U,_U,_U,_U,_U,_U,_U,			/* 72-79 */
_U,_U,_U,_U,_U,_U,_U,_U,			/* 80-87 */
_U,_U,_U,_P,_P,_P,_P,_P,			/* 88-95 */
_P,_L|_X,_L|_X,_L|_X,_L|_X,_L|_X,_L|_X,_L,	/* 96-103 */
_L,_L,_L,_L,_L,_L,_L,_L,			/* 104-111 */
_L,_L,_L,_L,_L,_L,_L,_L,			/* 112-119 */
_L,_L,_L,_P,_P,_P,_P,_C,			/* 120-127 */
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,		/* 128-143 */
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,		/* 144-159 */
_S|_SP,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,   /* 160-175 */
_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,       /* 176-191 */
_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,       /* 192-207 */
_U,_U,_U,_U,_U,_U,_U,_P,_U,_U,_U,_U,_U,_U,_U,_L,       /* 208-223 */
_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,       /* 224-239 */
_L,_L,_L,_L,_L,_L,_L,_P,_L,_L,_L,_L,_L,_L,_L,_L};      /* 240-255 */

extern "C" void __assert(const char *file, int line, const char *cond) {
    printf("Failed assertion at %s:%d: %s\n", file, line, cond);
}

#endif

extern "C" {

void
__assert_fail(const char *__assertion,
	      const char *__file,
	      unsigned int __line,
	      const char *__function)
{
  click_chatter("assertion failed %s %s %d %s\n",
	 __assertion,
	 __file,
	 __line,
	 __function);
  panic("Click assertion failed");
}

/*
 * GCC generates calls to these run-time library routines.
 */

#if __GNUC__ >= 3
void
__cxa_pure_virtual()
{
  click_chatter("pure virtual method called\n");
}
#else
void
__pure_virtual()
{
  click_chatter("pure virtual method called\n");
}
#endif

void *
__rtti_si()
{
  click_chatter("rtti_si\n");
  return(0);
}

void *
__rtti_user()
{
  click_chatter("rtti_user\n");
  return(0);
}

#ifdef CLICK_LINUXMODULE

/*
 * Convert a string to a long integer. use simple_strtoul, which is in the
 * kernel
 */

long
strtol(const char *nptr, char **endptr, int base)
{
  // XXX should check for overflow and underflow, but strtoul doesn't, so why
  // bother?
  if (*nptr == '-')
    return -simple_strtoul(nptr + 1, endptr, base);
  else if (*nptr == '+')
    return simple_strtoul(nptr + 1, endptr, base);
  else
    return simple_strtoul(nptr, endptr, base);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 0) && __GNUC__ == 2 && __GNUC_MINOR__ == 96
int
click_strcmp(const char *a, const char *b)
{
int d0, d1;
register int __res;
__asm__ __volatile__(
	"1:\tlodsb\n\t"
	"scasb\n\t"
	"jne 2f\n\t"
	"testb %%al,%%al\n\t"
	"jne 1b\n\t"
	"xorl %%eax,%%eax\n\t"
	"jmp 3f\n"
	"2:\tsbbl %%eax,%%eax\n\t"
	"orb $1,%%al\n"
	"3:"
	:"=a" (__res), "=&S" (d0), "=&D" (d1)
		     :"1" (a),"2" (b));
return __res;
}
#endif
#endif

}

#endif /* !__KERNEL__ */

#if CLICK_LINUXMODULE && !defined(__HAVE_ARCH_STRLEN) && !defined(HAVE_LINUX_STRLEN_EXPOSED)
// Need to provide a definition of 'strlen'. This one is taken from Linux.
extern "C" {
size_t strlen(const char * s)
{
    const char *sc;
    for (sc = s; *sc != '\0'; ++sc)
	/* nothing */;
    return sc - s;
}
}

#endif
