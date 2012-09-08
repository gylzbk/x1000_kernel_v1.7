#ifndef _NANDDEBUG_H_
#define _NANDDEBUG_H_

#define DEBUG 1

#define FUNC_DEBUG(x)							\
	enum {										\
		x##_INFO = 1,							\
		x##_DEBUG,								\
		x##_ERROR,								\
	}

FUNC_DEBUG(VNAND);
FUNC_DEBUG(SIGBLOCK);

FUNC_DEBUG(L2PCONVERT);
FUNC_DEBUG(CACHEDATA);
FUNC_DEBUG(CACHEMANAGER);
FUNC_DEBUG(CACHELIST);

FUNC_DEBUG(ZONEMANAGER);
FUNC_DEBUG(HASH);
FUNC_DEBUG(HASHNODE);
FUNC_DEBUG(ZONE);
FUNC_DEBUG(TASKMANAGER);
FUNC_DEBUG(PARTITION);
FUNC_DEBUG(RECYCLE);
FUNC_DEBUG(TIMER);

#ifndef  LINUX_KERNEL
#define ndprint(level,...) printf(__VA_ARGS__);
#else
#define ndprint(level,...)						\
	do {										\
		printk(__VA_ARGS__);					\
		if (level == 3)							\
			dump_stack();						\
	} while (0)
#endif

#endif /* _NANDDEBUG_H_ */
