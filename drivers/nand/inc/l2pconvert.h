#ifndef __L2PCONVERT_H__
#define __L2PCONVERT_H__

#define MAXDIFFTIME 20

#include "context.h"
#include "partitioninterface.h"
#include "bufflistmanager.h"
#include "pmanager.h"
#include "vnandinfo.h"
#include "ppartition.h"
#include "sectorlist.h"

typedef struct _L2pConvert L2pConvert;
struct _L2pConvert {
	SectorList *follow_node;
	int break_type;
	int node_left_sector_count;
	int page_left_sector_count;
	int L4_startsectorid;
	int *sectorid;
	int l4count;
	int l4_is_new;
	int zone_is_new;
	int pagecount;
	int alloced_new_zone;
};

enum cmd {
	SUSPEND,
	RESUME,
};

#define INIT_L2P(x) do{					\
		(x)->follow_node = NULL;		\
		(x)->break_type = 0;			\
		(x)->node_left_sector_count = 0;	\
		(x)->page_left_sector_count = 0;	\
		(x)->L4_startsectorid = -1;		\
		(x)->sectorid = NULL;			\
		(x)->l4count = 0;			\
		(x)->l4_is_new = 0;			\
		(x)->zone_is_new = 0;			\
		(x)->pagecount = 0;			\
		(x)->alloced_new_zone = 0;			\
	}while(0)

#define NOT_SAME_L4(x)     ((x) |= 1)
#define NO_ENOUGH_PAGES(x) ((x) |= (1 << 1))
#define END_WRITE(x) ((x) |= (1 << 2))

#define IS_BREAK(x) ((x) > 0)
#define IS_NOT_SAME_L4(x)     (((x) & 1) == 1)
#define IS_NO_ENOUGH_PAGES(x) ((((x) >> 1) & 1) == 1)
#define IS_END_WRITE(x) ((((x) >> 2) & 1) == 1)

int L2PConvert_Init(PManager *pm);
void L2PConvert_Deinit(int handle);
int L2PConvert_ZMOpen(VNandInfo *vnand, PPartition *pt);
int L2PConvert_ZMClose(int handle);
int L2PConvert_ReadSector ( int handle, SectorList *sl );
int L2PConvert_WriteSector ( int handle, SectorList *sl );
int L2PConvert_Ioctrl(int handle, int cmd, int argv);

#endif
