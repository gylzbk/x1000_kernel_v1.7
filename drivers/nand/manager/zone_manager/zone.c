/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 by sftan (sftan@ingenic.cn)
 */

#include "clib.h"
#include "zone.h"
#include "pageinfo.h"
#include "pagelist.h"
#include "sigzoneinfo.h"
#include "context.h"
#include "vnandinfo.h"
#include "nmbitops.h"
#include "nandpageinfo.h"
#include "NandAlloc.h"
#include "nanddebug.h"
#include "vNand.h"
#include "l2vNand.h"
#include "singlelist.h"
#include "bufflistmanager.h"
#include "nandzoneinfo.h"
//#include "badblockinfo.h"

/*block per zone 8 block */
#define FIRSTPAGEINFO(vnand)	   (((vnand)->v2pp->_2kPerPage > 1)?(vnand)->v2pp->_2kPerPage * 2:3)
#define SIGZONEINFO(vnand)        ((vnand)->v2pp->_2kPerPage)

/*test define */
#define MEMORY_PAGE_NUM      3
/*define bad block bit = 1 */
/*define max l2info l3info size 5K*/
/*read page info form nand flash */

static int get_invalidpagecount(unsigned int startpage,
		unsigned short pagecnt, ZoneValidInfo * zonevalidinfo)
{
	Wpages *wpages = zonevalidinfo->wpages;
	int current_count = zonevalidinfo->current_count;
	int i = 0, invalidpage = 0;

	for (i = 0; i < current_count; i++) {
		if (startpage >= (wpages + i)->startpage + (wpages + i)->pagecnt ||
				startpage + pagecnt <= (wpages + i)->startpage) {
			continue;
		}

		if (startpage < (wpages + i)->startpage + (wpages + i)->pagecnt) {
			invalidpage += (wpages + i)->startpage + (wpages + i)->pagecnt - startpage;
		} else {
			invalidpage += (wpages + i)->startpage -  (startpage + pagecnt);
		}
	}
	return invalidpage;
}

static void check_invalidpage(Zone *zone, unsigned int startpage, unsigned short pagecnt)
{
	int invalidpage = 0;
	ZoneValidInfo * zonevalidinfo = &(((Context *)(zone->context))->zonep->zonevalidinfo);
	Wpages *wpages = zonevalidinfo->wpages;

	if (zonevalidinfo->zoneid == -1) {
		zonevalidinfo->zoneid = zone->ZoneID;
		zonevalidinfo->current_count = 0;
		(wpages + zonevalidinfo->current_count)->startpage = startpage;
		(wpages + zonevalidinfo->current_count)->pagecnt = pagecnt;
		zonevalidinfo->current_count++;

		return;
	}

	if (zone->ZoneID != zonevalidinfo->zoneid) {
		zonevalidinfo->zoneid = zone->ZoneID;
		zonevalidinfo->current_count = 0;

		(wpages + zonevalidinfo->current_count)->startpage = startpage;
		(wpages + zonevalidinfo->current_count)->pagecnt = pagecnt;
		zonevalidinfo->current_count++;
	} else {
		invalidpage = get_invalidpagecount(startpage, pagecnt, zonevalidinfo);
		if (invalidpage > 0) {
			zone->sigzoneinfo->validpage -= invalidpage;
			zone->validpage -= invalidpage;
		}

		(wpages + zonevalidinfo->current_count)->startpage = startpage;
		(wpages + zonevalidinfo->current_count)->pagecnt = pagecnt;
		zonevalidinfo->current_count++;
	}
}

/**
 *	read_info_l2l3l4info - read and unpackage info page
 *
 *	@zone: operate object
 *	@pageid: id of page
 *	@pi: pageinfo
 */
static int read_info_l2l3l4info(Zone *zone ,unsigned int pageid , PageInfo *pi)
{
	int ret = -1;
	unsigned char *buf = NULL;
	NandPageInfo *nandpageinfo;
	PageList *pagelist = NULL;
	BuffListManager *blm = NULL;

	buf = zone->mem0;
	nandpageinfo = (NandPageInfo *)buf;
	blm = ((Context *)(zone->context))->blm;

	pagelist = (PageList *)BuffListManager_getTopNode((int)blm, sizeof(PageList));
	pagelist->startPageID = pageid;
	pagelist->OffsetBytes = 0;
	pagelist->Bytes = zone->vnand->BytePerPage;
	pagelist->pData = (void *)buf;
	pagelist->retVal = 0;
	(pagelist->head).next = NULL;

	ret = vNand_MultiPageRead(zone->vnand,pagelist);
	if(ret != 0)
	{
		if (ISNOWRITE(pagelist->retVal)) {
			ndprint(ZONE_INFO,"WARNING: no write to read func %s line %d \n",
				__FUNCTION__,__LINE__);
		}
		else {
			ndprint(ZONE_ERROR,"vNand read pageinfo error ret = %d pageid = %d func %s line %d \n",
				pagelist->retVal, pagelist->startPageID, __FUNCTION__,__LINE__);
		}
		goto err;
	}
	if(nandpageinfo->MagicID != 0xaaaa) {
		ndprint(ZONE_ERROR,"read_info_l2l3l4info read nandpageinfo error MageicID = 0x%04X\n",nandpageinfo->MagicID);
	}

	zone->NextPageInfo = nandpageinfo->NextPageInfo;

	pi->L1InfoLen = zone->L1InfoLen;
	pi->L2InfoLen = zone->L2InfoLen;
	pi->L3InfoLen = zone->L3InfoLen;
	pi->L4InfoLen = zone->L4InfoLen;

	pi->L1Info = zone->L1Info;
	pi->PageID = pageid;
	pi->zoneID = nandpageinfo->ZoneID;

	pi->L1Index = nandpageinfo->L1Index;
	nandpageinfo->L4Info = buf + sizeof(NandPageInfo);
	memcpy(pi->L4Info,nandpageinfo->L4Info,pi->L4InfoLen);

	if(pi->L3InfoLen != 0)
	{
		if(pi->L2InfoLen == 0)
		{
			pi->L3Index = nandpageinfo->L3Index;
			nandpageinfo->L3Info = buf + sizeof(NandPageInfo) + pi->L4InfoLen;
			memcpy(pi->L3Info,nandpageinfo->L3Info,pi->L3InfoLen);
		}
		else
		{
			pi->L2Index = nandpageinfo->L2Index;
			nandpageinfo->L2Info = buf + sizeof(NandPageInfo) + pi->L4InfoLen;
			memcpy(pi->L2Info,nandpageinfo->L2Info,pi->L2InfoLen);

			pi->L3Index = nandpageinfo->L3Index;
			nandpageinfo->L3Info = buf + sizeof(NandPageInfo) + pi->L2InfoLen + pi->L4InfoLen;
			memcpy(pi->L3Info,nandpageinfo->L3Info,pi->L3InfoLen);
		}
	}

err:
	ret = pagelist->retVal;
	BuffListManager_freeList((int)blm, (void **)&pagelist,(void *)pagelist, sizeof(PageList));
	return ret;
}

static int release_l2l3l4info(Zone *zone,PageInfo *pi)
{
	return 0;
}

/**
 *	package_pageinfo - package info page
 *
 *	@zone: operate object
 *	@buf: info of page
 *	@pi: pageinfo
 */
static unsigned short package_pageinfo(Zone *zone,unsigned char *buf,PageInfo *pi)
{
	NandPageInfo *nandpageinfo = (NandPageInfo *)buf;
	unsigned short len = 0;

	nandpageinfo->L1Index = pi->L1Index;
	nandpageinfo->L4Info = buf + sizeof(NandPageInfo);
	memcpy(nandpageinfo->L4Info,pi->L4Info,pi->L4InfoLen);
	len = pi->L4InfoLen;

	if (zone->L3InfoLen != 0)
	{
		if(zone->L2InfoLen == 0)
		{
			nandpageinfo->L3Index = pi->L3Index;
			nandpageinfo->L3Info = buf + sizeof(NandPageInfo) + pi->L4InfoLen;
			memcpy(nandpageinfo->L3Info,pi->L3Info,pi->L3InfoLen);

			len += pi->L3InfoLen;
		}
		else
		{
			nandpageinfo->L2Index = pi->L2Index;
			nandpageinfo->L2Info = buf + sizeof(NandPageInfo) + pi->L4InfoLen;
			memcpy(nandpageinfo->L2Info,pi->L2Info,pi->L2InfoLen);

			nandpageinfo->L3Index = pi->L3Index;
			nandpageinfo->L3Info = buf + sizeof(NandPageInfo) + pi->L2InfoLen + pi->L4InfoLen;
			memcpy(nandpageinfo->L3Info,pi->L3Info,pi->L3InfoLen);

			len += pi->L2InfoLen + pi->L3InfoLen;
		}
	}
	nandpageinfo->MagicID = 0xaaaa;
	nandpageinfo->crc = 0;
	return len;
}

/**
 *	check_pagelist_error - check return value of pagelist
 *
 *	@pl: which need to check
 */
static int check_pagelist_error(PageList *pl)
{
	struct singlelist *sg = NULL;
	do{
		if(pl->retVal < 0){
			ndprint(ZONE_ERROR,"ERROR: FUNCTION: %s   LINE: %d pl->retVal= %d\n",__func__,__LINE__,pl->retVal);
			return pl->retVal;
		}

		sg = (pl->head).next;
		if(sg == NULL)/*if exec then buf happen*/
			break;
		pl = singlelist_entry(sg,PageList,head);
	}while(pl);

	return -1;
}

/**
 *	zone_page1_pageid - get pageid of zone'page1
 *
 *	@zone: operate object
 */
static inline unsigned short zone_page1_pageid(Zone *zone)
{
	int blockno = 0;

	while(nm_test_bit(blockno,&zone->badblock) && (++blockno));

	return ( blockno * zone->vnand->PagePerBlock + SIGZONEINFO(zone->vnand));
}

/**
 *	zone_L1Info_addr - get pageid of zone'L1Info
 *
 *	@zone: operate object
 */
static inline unsigned int zone_L1Info_addr(Zone *zone)
{
	return (zone_page1_pageid(zone) +
			zone->startblockID * zone->vnand->PagePerBlock + 1);
}

/**
 *	calc_zone_page - calc page count of zone
 *
 *	@zone: operate object
 */
static unsigned short calc_zone_page(Zone *zone)
{
	unsigned int blockno = 0;
	unsigned short page = 0;

	for(blockno = 0 ; blockno < BLOCKPERZONE(zone->vnand); blockno++)
	{
		if(!nm_test_bit(blockno,&zone->badblock))
		{
			page += zone->vnand->PagePerBlock ;
		}
	}

	return page;
}

/**
 *	Zone_FindFirstPageInfo - find first pageinfo of zone
 *
 *	@zone: operate object
 *	@pi: return pageinfo to caller
 */
int Zone_FindFirstPageInfo ( Zone *zone, PageInfo* pi )
{
	int blockno = 0;
	unsigned int pageid = 0;

	while(nm_test_bit(blockno,&zone->badblock) && (++blockno));

	pageid = (zone->vnand->PagePerBlock) * (zone->startblockID + blockno)
			+ FIRSTPAGEINFO(zone->vnand);

	return read_info_l2l3l4info(zone,pageid,pi);
}

/**
 *	Zone_FindNextPageInfo - find next pageinfo of zone
 *
 *	@zone: operate object
 *	@pi: return pageinfo to caller
 */
int Zone_FindNextPageInfo ( Zone *zone, PageInfo* pi )
{
	unsigned int pageid = 0;

	if(zone->NextPageInfo == 0)
		return -1;

	pageid = (zone->vnand->PagePerBlock) * zone->startblockID
			+ zone->NextPageInfo;

	return read_info_l2l3l4info(zone,pageid,pi);
}

/**
 *	Zone_ReleasePageInfo - release pageinfo
 *
 *	@zone: operate object
 *	@pi: which to release
 */
int Zone_ReleasePageInfo ( Zone *zone, PageInfo* pi )
{
	return release_l2l3l4info(zone,pi);
}

/**
 *	Zone_ReadPageInfo - read pageinfo
 *
 *	@zone: operate object
 *	@pageID: id of page
 *	@pi: pageinfo
 */
int Zone_ReadPageInfo ( Zone *zone, unsigned int pageID, PageInfo* pi )
{
	return read_info_l2l3l4info(zone,pageID,pi);
}

/**
 *	Zone_MultiWritePage - MultiWritePage operation
 *
 *	@zone: operate object
 *	@pagecount: count of page to write
 *	@pl: pagelist which to write
 *	@pi: pageinfo
 */
int Zone_MultiWritePage ( Zone *zone, unsigned int pagecount, PageList* pl, PageInfo* pi)
{
	unsigned char *buf = NULL;
	NandPageInfo *nandpageinfo = NULL;
	unsigned short len = 0;
	int ret = -1;
	PageList *pagelist = NULL;
	BuffListManager *blm = ((Context *)(zone->context))->blm;
	int sectorperpage = zone->vnand->BytePerPage / SECTOR_SIZE;

	buf = zone->mem0;
	nandpageinfo = (NandPageInfo *)buf;

	memset(buf,0xff,zone->vnand->BytePerPage);
	nandpageinfo->NextPageInfo = (zone->allocPageCursor + zone->vnand->v2pp->_2kPerPage)
		/ zone->vnand->v2pp->_2kPerPage * zone->vnand->v2pp->_2kPerPage;
	nandpageinfo->ZoneID = pi->zoneID;
	len = package_pageinfo(zone,buf,pi);
	if( (len+sizeof(NandPageInfo)) > zone->vnand->BytePerPage )
	{
		ndprint(ZONE_ERROR,"package page info error func %s line %d \n",
			__FUNCTION__,__LINE__);
		return -1;
	}

	nandpageinfo->len = len + sizeof(NandPageInfo);

	zone->sigzoneinfo->validpage--;

	if(zone->allocedpage > zone->sumpage - zone->vnand->v2pp->_2kPerPage)
		nandpageinfo->NextPageInfo = 0;

	pagelist = (PageList *)BuffListManager_getTopNode((int)blm, sizeof(PageList));
	pagelist->startPageID = pi->PageID;
	pagelist->OffsetBytes = 0;
	pagelist->Bytes = zone->vnand->BytePerPage;
	pagelist->pData = buf;
	pagelist->retVal = 0;

	BuffListManager_mergerList((int)blm, (void *)pagelist,(void *)pl);

	ret = vNand_MultiPageWrite(zone->vnand,pagelist);
	if(ret != 0)
	{
		ndprint(ZONE_ERROR,"vNand MultiPage Write error func %s line %d \n",__FUNCTION__,__LINE__);
		goto err;
	}

	zone->pageCursor = zone->allocPageCursor;

	BuffListManager_freeList((int)blm, (void **)&pagelist,(void *)pagelist, sizeof(PageList));

	if (pagecount > 0)
		check_invalidpage(zone, zone->currentLsector / sectorperpage, pagecount);
	return ret;

err:
	 ret = check_pagelist_error(pagelist);
	 BuffListManager_freeList((int)blm, (void **)&pagelist,(void*)pagelist, sizeof(PageList));

	return ret;
}

/**
 *	Zone_AllocNextPage - alloc next page of zone to write
 *
 *	@zone: operate object
 */
int Zone_AllocNextPage ( Zone *zone )
{
	int i;
	int j = 0;
	int end_blockid;
	int badblocknum = 0;
	unsigned int pageperblock = zone->vnand->PagePerBlock;
	ZoneManager *zonep = ((Context *)(zone->context))->zonep;

	if(zone->allocedpage == zone->sumpage){
		ndprint(ZONE_ERROR,"Function: %s LINE: %d zoneid = %d have alloced all the page in zone, allocdpage = %d\n",
			__func__,__LINE__, zone->ZoneID, zone->allocedpage);
		return -1;
	}

	zone->allocPageCursor++;
	if (zone->allocPageCursor > 0 && zone->allocPageCursor % pageperblock == 0) {
		if (zone->ZoneID == zonep->pt_zonenum - 1)
			end_blockid = zonep->vnand->TotalBlocks - 1;
		else {
			//end_blockid = BadBlockInfo_Get_blockID(zonep->badblockinfo,zone->ZoneID,BLOCKPERZONE(zone->vnand) - 1);
			end_blockid = ((zone->ZoneID + 1) * BLOCKPERZONE(zonep->vnand)) - 1;
		}
		j = zone->allocPageCursor / pageperblock;
		for (i = zone->startblockID + zone->allocPageCursor / pageperblock; i < end_blockid; i++) {
			//if (vNand_IsBadBlock(zone->vnand,i) || nm_test_bit(j++,&(zone->badblock)))
			if (nm_test_bit(j++,&(zone->badblock)))
				badblocknum++;
			else
				break;
		}
	}

	zone->allocPageCursor = zone->allocPageCursor % pageperblock + (zone->allocPageCursor / pageperblock + badblocknum) * pageperblock;
	zone->allocedpage++;
	if(zone->allocPageCursor >= pageperblock * BLOCKPERZONE(zonep->vnand)){
		ndprint(ZONE_ERROR,"ERROR: allocPageCursor have too large = %d !!\n",zone->allocPageCursor);
		return -1;
	}

	return zone->allocPageCursor + zone->startblockID * zone->vnand->PagePerBlock;
}

/**
 *	Zone_GetFreePageCount - get free page count of zone
 *
 *	@zone: operate object
 */
unsigned short Zone_GetFreePageCount(Zone *zone)
{
	return zone->sumpage - (zone->allocedpage + zone->vnand->v2pp->_2kPerPage - 1) / zone->vnand->v2pp->_2kPerPage * zone->vnand->v2pp->_2kPerPage;
}

/**
 *	Zone_MarkEraseBlock - mark erase blcok
 *
 *	@zone: operate object
 *	@PageID: id of page
 *	@Mode: mode ???
 */
int Zone_MarkEraseBlock ( Zone *zone, unsigned int PageID, int Mode )
{
	BlockList *blocklist = NULL;
	int ret = -1;
	BuffListManager *blm = ((Context *)(zone->context))->blm;

	blocklist = (BlockList*)BuffListManager_getTopNode((int)blm, sizeof(BlockList));
	blocklist->startBlock = PageID / zone->vnand->PagePerBlock;
	blocklist->BlockCount = 1;
	blocklist->retVal = 0;
	blocklist->head.next = NULL;

	ret = vNand_MultiBlockErase(zone->vnand,blocklist);
	if(ret < 0)
	{
		ndprint(ZONE_ERROR,"vNand MultiBlockErase error func %s line %d \n",__FUNCTION__,__LINE__);
		return ret;
	}
	BuffListManager_freeList((int)blm, (void **)&blocklist,(void *)blocklist, sizeof(BlockList));
	return 0;
}

/**
 *	Zone_Init - Initialize operation
 *
 *	@zone: operate object
 *	@prev: previous sigzoneinfo
 *	@next: next sigzoneinfo
 *
 *	fill Zone struct information and write zone Page1 information to nand flash
*/
int Zone_Init (Zone *zone, SigZoneInfo* prev, SigZoneInfo* next )
{
	NandZoneInfo *nandzoneinfo;
	PageList *pagelist = NULL;
	PageList *pagelist1 = NULL;
	int ret = -1;
	unsigned int blockno = 0;
	BuffListManager *blm = ((Context *)(zone->context))->blm;
	ZoneManager *zonep = ((Context *)(zone->context))->zonep;

	nandzoneinfo = (NandZoneInfo *)(zone->mem0);
	zone->badblock = zone->sigzoneinfo->badblock;
	zone->ZoneID = zone->sigzoneinfo - zone->top;

	zone->sumpage = calc_zone_page(zone);
	if (zone->sumpage <= 1)
		return -1;

	memset(nandzoneinfo,0xff,zone->vnand->BytePerPage);
	/*file local zone information to page1 buf*/
	nandzoneinfo->localZone.ZoneID = zone->sigzoneinfo - zone->top;
	if (zone->sigzoneinfo->lifetime == -1)
		zone->sigzoneinfo->lifetime = 0;
	CONV_SZ_ZI(zone->sigzoneinfo, &nandzoneinfo->localZone);

	if(prev != NULL)
	{
		/*file prev zone information to page1 buf*/
		nandzoneinfo->preZone.ZoneID = prev - zone->top;
		zone->sigzoneinfo->pre_zoneid = prev - zone->top;
		CONV_SZ_ZI(prev, &nandzoneinfo->preZone);
	}
	else
		nandzoneinfo->preZone.ZoneID = 0xffff;

	if(next != NULL)
	{
		/*file next zone information to page1 buf*/
		nandzoneinfo->nextZone.ZoneID = next - zone->top;
		zone->sigzoneinfo->next_zoneid = next - zone->top;
		CONV_SZ_ZI(next, &nandzoneinfo->nextZone);
	}
	else
		nandzoneinfo->nextZone.ZoneID = 0xffff;

	zonep->maxserial++;
	nandzoneinfo->serialnumber = zonep->maxserial;

	while(nm_test_bit(blockno,&(zone->badblock)) && (++blockno));

	pagelist = (PageList *)BuffListManager_getTopNode((int)blm, sizeof(PageList));

#ifdef TEST_ZONE //for test
	pagelist->startPageID = (zone->ZoneID * BLOCKPERZONE(zone->vnand) + blockno)*
								(zone->vnand->PagePerBlock)
								+ SIGZONEINFO(zone->vnand);
#else
	pagelist->startPageID = (zone->startblockID + blockno)*
							(zone->vnand->PagePerBlock)
							+ SIGZONEINFO(zone->vnand);

#endif

	pagelist->OffsetBytes = 0;
	pagelist->Bytes = zone->vnand->BytePerPage;
	pagelist->pData = (void*)nandzoneinfo;
	pagelist->retVal = 0;

	pagelist1 = (PageList *)BuffListManager_getNextNode((int)blm,(void *)pagelist,sizeof(PageList));
	pagelist1->startPageID = zone_L1Info_addr(zone);
	pagelist1->OffsetBytes = 0;
	pagelist1->Bytes = zone->vnand->BytePerPage;
	pagelist1->pData = (void *)zonep->L1->page;
	pagelist1->retVal = 0;

	ret = vNand_MultiPageWrite(zone->vnand,pagelist);
	if(ret != 0)
	{
		ndprint(ZONE_ERROR,"vNand_MultiPageWrite error func %s line %d \n",__FUNCTION__,__LINE__);
		Nand_ContinueFree(nandzoneinfo);
		return check_pagelist_error(pagelist);
	}

	zone->prevzone = prev;
	zone->nextzone = next;

	if (zone->vnand->v2pp->_2kPerPage == 1){
		zone->pageCursor = blockno * zone->vnand->PagePerBlock + SIGZONEINFO(zone->vnand);
		zone->allocPageCursor = zone->pageCursor + 1;
		zone->allocedpage = 3;
	}
	else if (zone->vnand->v2pp->_2kPerPage > 1) {
		zone->pageCursor = blockno * zone->vnand->PagePerBlock + SIGZONEINFO(zone->vnand);
		zone->allocPageCursor = zone->vnand->v2pp->_2kPerPage * 2 - 1;
		zone->allocedpage = zone->vnand->v2pp->_2kPerPage * 2;
	}
	zone->validpage = zone->vnand->PagePerBlock * BLOCKPERZONE(zone->vnand) - zone->allocedpage;
	BuffListManager_freeAllList((int)blm, (void **)&pagelist, sizeof(PageList));

	return 0;
}

/**
 *	Zone_DeInit - Deinit operation
 *
 *	@zone: operate object
*/
int Zone_DeInit ( Zone *zone )
{
	return 0;
}

/**
 *	Zone_RawMultiWritePage - MultiPageWrite without pageinfo
 *
 *	@zone: operate object
 *	@pl: pagelist which to write
*/
int Zone_RawMultiWritePage ( Zone *zone, PageList *pl )
{
	return vNand_MultiPageWrite(zone->vnand,pl);
}

/**
 *	Zone_RawMultiReadPage - MultiPageRead without pageinfo
 *
 *	@zone: operate object
 *	@pl: pagelist which to read
*/
int Zone_RawMultiReadPage ( Zone *zone, PageList *pl )
{
	return vNand_MultiPageRead(zone->vnand,pl);
}

