#include "clib.h"
#include "FileDesc.h"
#include "nandmanagerinterface.h"
#include "vNand.h"

struct argoption arg_option={0};

void help()
{
	printf("help");
}
static void start(int handle){
	LPartition* lp,*lpentry;
	struct singlelist *it;
	int pHandle;
	FILE *fp;
	unsigned char *buf;
	int rlen;
	int bl;
	SectorList *sl;
	int sectorid;
	NandManger_getPartition(handle,&lp);
	singlelist_for_each(it,&lp->head){
		lpentry = singlelist_entry(it,LPartition,head);
		pHandle = NandManger_open(handle,lpentry->name,lpentry->mode);
		if(pHandle)
			break;
		else {
			printf("NandManger open failed!\n");
			exit(1);
		}
			
	}
	buf = (unsigned char*)malloc(256*512);
	if(buf == NULL){
		printf("malloc failed!\n");
		exit(1);
	}
	bl = BuffListManager_BuffList_Init();
	if(bl == 0){
		printf("BuffListManager Init failed!\n");
		exit(1);
	}
	fp = fopen(arg_option.file_desc.inname,"rb");
	if(fp == NULL){
		printf("read infile failed!\n");
		exit(1);
	}
	sectorid = 0;
	while(!feof(fp)){
		rlen = fread(buf,1,256*512,fp);
		if(rlen){
			sl = BuffListManager_getTopNode(bl,sizeof(SectorList));
			if(sl == 0){
				printf("Bufferlist request sectorlist failed!\n");
				exit(1);
			}
			sl->startSector = sectorid;
			sl->pData = buf;
			sl->sectorCount = (rlen + 511)/ 512;
			sectorid += (rlen + 511)/ 512;
			
			printf("******************** sl = %p ****************\n",sl);
			if(NandManger_write(pHandle,sl) < 0){
				printf("NandManger write failed!\n");
				exit(1);
			}
			BuffListManager_freeAllList(bl,(void **)&sl,sizeof(SectorList));
		}
	}
	printf("%s(%s) %d\n",__FUNCTION__,__FILE__,__LINE__);

	NandManger_close(pHandle);
	free(buf);
	fclose(fp);
	printf("ssssssssssssssssssssssssssssssss\n");
	BuffListManager_BuffList_DeInit(bl);
}

/*
   1--64 info
   3--pageperblock
   8 block -- 1 zone
 */
static int caloutfilelen(int len){
	int blocklen = arg_option.file_desc.bytesperpage * arg_option.file_desc.pageperblock;
	int zonelen = blocklen * 8;
	int trimzoneinfolen = zonelen - 8 * 3 * arg_option.file_desc.bytesperpage;
	int realzonelen = trimzoneinfolen * 63 / 64; 
	int l;
   	l = (len + realzonelen - 1) / realzonelen * blocklen * 8;
	printf("l = %d\n",l);
	return l;
} 
int main(int argc, char *argv[])
{
	int pHandle;
	int preblocks;
	get_option(argc,argv);
	dumpconfig();
	if(arg_option.file_desc.length <= 0){
		printf("the len of inputfile shouldn't be zero!\n");
		return -1;
	}
	preblocks = (
		caloutfilelen(arg_option.file_desc.length) + arg_option.file_desc.bytesperpage * arg_option.file_desc.pageperblock - 1) 
		/ (arg_option.file_desc.bytesperpage * 
		   arg_option.file_desc.pageperblock);
	if(preblocks >= arg_option.file_desc.blocks){
		printf("There isn't enough block for it!");
		exit(1);
	}	
	if(arg_option.file_desc.blocks == 0){
		printf("Block count shouldn't be zero!");
		exit(1);
	}
	printf("%s(%s) %d arg_option.file_desc.blocks = %d\n",__FUNCTION__,__FILE__,__LINE__,arg_option.file_desc.blocks);
	
	pHandle = NandManger_Init();
	NandManger_startNotify(pHandle,start,pHandle);
	
	ND_probe(&arg_option.file_desc);
	NandManger_Deinit(pHandle);
    return 0;
}
