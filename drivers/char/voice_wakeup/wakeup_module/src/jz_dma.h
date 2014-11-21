#ifndef __JZ_PDMAC_H__
#define __JZ_PDMAC_H__

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

/* DMA BASE ADDRESS */
#define PDMAC_BASE	0xB3420000

/* DMA REGISTER */

#define DMA_DSAn	0x00
#define DMA_DTAn	0x04
#define DMA_DTCn	0x08
#define DMA_DRTn	0x0C
#define DMA_DCSn	0x10
#define DMA_DCMn	0x14
#define DMA_DDAn	0x18
#define DMA_DSDn	0x1C

#define DMADSA(chn)		(*(volatile u32*)((PDMAC_BASE)+(DMA_DSAn)+(chn)*0x20))
#define DMADTA(chn)		(*(volatile u32*)((PDMAC_BASE)+(DMA_DTAn)+(chn)*0x20))
#define DMADTC(chn)		(*(volatile u32*)((PDMAC_BASE)+(DMA_DTCn)+(chn)*0x20))
#define DMADRT(chn)		(*(volatile u32*)((PDMAC_BASE)+(DMA_DRTn)+(chn)*0x20))
#define DMADCS(chn)		(*(volatile u32*)((PDMAC_BASE)+(DMA_DCSn)+(chn)*0x20))
#define DMADCM(chn)		(*(volatile u32*)((PDMAC_BASE)+(DMA_DCMn)+(chn)*0x20))
#define DMADDA(chn)		(*(volatile u32*)((PDMAC_BASE)+(DMA_DDAn)+(chn)*0x20))
#define DMADSD(chn)		(*(volatile u32*)((PDMAC_BASE)+(DMA_DSDn)+(chn)*0x20))

#define DMA_DMAC	 0x1000
#define DMA_DIRQP	 0x1004
#define DMA_DDB		 0x1008
#define DMA_DDS		 0x100C
#define DMA_DCKE	 0x1010
#define DMA_DCKES	 0x1014
#define DMA_DCKEC	 0x1018
#define DMA_DMACP	 0x101C
#define DMA_DSIRQP	 0x1020
#define DMA_DSIRQM	 0x1024
#define DMA_DCIRQP	 0x1028
#define DMA_DCIRQM	 0x102C
#define DMA_DMCS	 0x1030
#define DMA_DMNMB	 0x1034
#define DMA_DMSMB	 0x1038
#define DMA_DMINT	 0x103C


#define DMADMAC		(*(volatile u32*)((PDMAC_BASE)+(DMA_DMAC)))
#define DMADIRQP	(*(volatile u32*)((PDMAC_BASE)+(DMA_DIRQP)))
#define DMADDB		(*(volatile u32*)((PDMAC_BASE)+(DMA_DDB)))
#define DMADDS		(*(volatile u32*)((PDMAC_BASE)+(DMA_DDS)))
#define DMADCKE		(*(volatile u32*)((PDMAC_BASE)+(DMA_DCKE)))
#define DMADCKES	(*(volatile u32*)((PDMAC_BASE)+(DMA_DCKES)))
#define DMADCKEC	(*(volatile u32*)((PDMAC_BASE)+(DMA_DCKEC)))
#define DMADMACP	(*(volatile u32*)((PDMAC_BASE)+(DMA_DMACP)))
#define DMADSIRQP	(*(volatile u32*)((PDMAC_BASE)+(DMA_DSIRQP)))
#define DMADSIRQM	(*(volatile u32*)((PDMAC_BASE)+(DMA_DSIRQM)))
#define DMADCIRQP	(*(volatile u32*)((PDMAC_BASE)+(DMA_DCIRQP)))
#define DMADCIRQM	(*(volatile u32*)((PDMAC_BASE)+(DMA_DCIRQM)))
#define DMADMCS		(*(volatile u32*)((PDMAC_BASE)+(DMA_DMCS)))
#define DMADMNMB	(*(volatile u32*)((PDMAC_BASE)+(DMA_DMNMB)))
#define DMADMSMB	(*(volatile u32*)((PDMAC_BASE)+(DMA_DMSMB)))
#define DMADMINT	(*(volatile u32*)((PDMAC_BASE)+(DMA_DMINT)))

#define DMA_READBIT(reg, bit, width)		(((reg)>>(bit))&(~((~0)<<(width))))
#define V_TO_P(n) (((unsigned long)(n))&(0x1fffffff))

struct dma_desc  {
	unsigned long dcm;
	unsigned long dsa;
	unsigned long dta;
	unsigned long dtc;
	unsigned long sd;
	unsigned long drt;
	unsigned long reserved[2];
};

struct dma_config{
	int type;
	int channel;
	int count;
	unsigned long src;
	unsigned long dst;
/* command */
	int increment;
	int sp_dp;
	int rdil;
	int tsz;
	int tie;
/* don't need careless this with nomal mode juse use 0*/
	int stde;
	int descriptor;
	int des8;
	int sd;
	int link;
	unsigned long desc;
};



#define DMA_MEM_TO_DEV	1
#define DMA_DEV_TO_MEM	2
#define DMA_MEM_TO_MEM	3

extern void build_one_desc(struct dma_config *, struct dma_desc *desc, struct dma_desc *next_desc);
extern void pdma_config(struct dma_config *dma);
extern void pdma_start(int channel);
extern void pdma_wait(int channel);
extern void pdma_end(int channel);
extern unsigned int pdma_trans_addr(int channel, int direction);

#endif /* __JZ_PDMAC_H__*/

