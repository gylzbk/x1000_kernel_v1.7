#ifndef _CLIB_H_
#define _CLIB_H_

#ifdef  __KERNEL__
#include <linux/math64.h>
#include <linux/kernel.h>
#include <linux/string.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <string.h>
#endif

unsigned int nm_sleep(unsigned int seconds);
long long nd_getcurrentsec_ns(void);
unsigned int nd_get_timestamp(void);

#endif /*_CLIB_H_*/
