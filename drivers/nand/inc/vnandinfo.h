#ifndef __VNANDINFO_H__
#define __VNANDINFO_H__

#include "nandinterface.h"
#include "ppartition.h"
#include "timerdebug.h"

typedef struct _VNandInfo VNandInfo;
typedef struct _VNandManager VNandManager;

struct _VNandInfo {
    int startBlockID; 
    int PagePerBlock;
    int BytePerPage;
    int TotalBlocks;
    int* prData;
    int MaxBadBlockCount;
    unsigned short hwSector;
    unsigned int *pt_badblock_info;
#ifdef TIMER_DEBUG
	TimeByte *timebyte;
#endif
};

struct _VNandManager {
	VNandInfo info;
    PPartArray* pt;
};

#define CONV_PT_VN(pt,vn)							\
	do{									\
		(vn)->startBlockID = 0;;						\
		(vn)->PagePerBlock = (pt)->pageperblock;			\
		(vn)->BytePerPage = (pt)->byteperpage;				\
		(vn)->TotalBlocks = (pt)->totalblocks;				\
		(vn)->MaxBadBlockCount = (pt)->badblockcount;			\
		(vn)->hwSector = (pt)->hwsector;				\
		(vn)->pt_badblock_info = ((pt)->pt_badblock_info + 1);		\
		(vn)->prData = (void*)(pt);										\
	}while(0)

#endif

