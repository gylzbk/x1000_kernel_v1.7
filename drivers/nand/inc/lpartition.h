#ifndef __LPARTITION_H__
#define __LPARTITION_H__

#define LPARTITION(OBJ) ((LPartition*)OBJ)

#ifndef String
#define String char*
#endif

#include "partcontext.h"
#include "singlelist.h"
typedef struct _LPartition LPartition;

struct _LPartition {
    int startSector;
    int sectorCount;
    const char* name;
    int mode;
	struct singlelist head;
    PartContext* pc;
	
	int hwsector;
	unsigned short segmentsize;
};

#endif
