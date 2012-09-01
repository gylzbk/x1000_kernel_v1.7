/*
 *  Copyright (C) 2010 Ingenic Semiconductor Inc.
 *
 *   In this file, here are some macro/device/function to
 * to help the board special file to organize resources
 * on the chip.
 */

#ifndef __SOC_4770_H__
#define __SOC_4770_H__


#define I2S0_PORTE							\
	{ .name = "i2s0_sysclk",	.port = GPIO_PORT_E, .func = GPIO_FUNC_2, .pins = 0x1<<5, },	\
	{ .name = "i2s0_data",		.port = GPIO_PORT_E, .func = GPIO_FUNC_0, .pins = 0x3<<6, },	\
	{ .name = "i2s0_bitclk",	.port = GPIO_PORT_D, .func = GPIO_FUNC_1, .pins = 0x1<<12,},	\
	{ .name = "i2s0_sync",		.port = GPIO_PORT_D, .func = GPIO_FUNC_0, .pins = 0x1<<13,},	\
	{ .name = "i2s0_iclk",		.port = GPIO_PORT_E, .func = GPIO_FUNC_1, .pins = 0x3<<8, }	
/* devio define list */
#define UART0_PORTF							\
	{ .name = "uart0", .port = GPIO_PORT_F, .func = GPIO_FUNC_0, .pins = 0x0f, }
#define UART1_PORTD							\
	{ .name = "uart1", .port = GPIO_PORT_D, .func = GPIO_FUNC_0, .pins = 0xf<<26, }
#define UART2_PORTD							\
	{ .name = "uart2", .port = GPIO_PORT_D, .func = GPIO_FUNC_1, .pins = 0xf<<4, }	
#define UART3_PORTDE							\
	{ .name = "uart3-pd-f0", .port = GPIO_PORT_D, .func = GPIO_FUNC_0, .pins = 0x1<<12, },\
	{ .name = "uart3-pe-f1", .port = GPIO_PORT_E, .func = GPIO_FUNC_1, .pins = 0x1<<5, },	\
	{ .name = "uart3-pe-f0", .port = GPIO_PORT_E, .func = GPIO_FUNC_0, .pins = 0x3<<7, }
#define UART3_JTAG \
	{ .name = "uart3-jtag-31", .port = GPIO_PORT_A, .func = GPIO_FUNC_1, .pins = 1<<31, },\
	{ .name = "uart3-jtag-30", .port = GPIO_PORT_A, .func = GPIO_FUNC_0, .pins = 1<<30, }
#define UART4_PORTC							\
	{ .name = "uart4", .port = GPIO_PORT_C, .func = GPIO_FUNC_2, .pins = 1<<10 | 1<<20, }
/*******************************************************************************************************************/

#define MSC0_PORTA_4BIT							\
	{ .name = "msc0-pa-4bit",	.port = GPIO_PORT_A, .func = GPIO_FUNC_1, .pins = 0x00fc0000, }
#define MSC0_PORTA_8BIT							\
	{ .name = "msc0-pa-8bit",	.port = GPIO_PORT_A, .func = GPIO_FUNC_1, .pins = 0x00fc00f0, }
#define MSC0_PORTE							\
	{ .name = "msc0-pe",		.port = GPIO_PORT_E, .func = GPIO_FUNC_0, .pins = 0x30f00000, }
#define MSC0_PORTA_4BIT_RESET						\
	{ .name = "msc0-pa-4bit-reset",	.port = GPIO_PORT_A, .func = GPIO_FUNC_1, .pins = 0x01fc0000, }
#define MSC0_PORTA_8BIT_RESET						\
	{ .name = "msc0-pa-8bit-reset",	.port = GPIO_PORT_A, .func = GPIO_FUNC_1, .pins = 0x01fc00f0, }
#define MSC1_PORTD							\
	{ .name = "msc1-pd",		.port = GPIO_PORT_D, .func = GPIO_FUNC_0, .pins = 0x03f00000, }
#define MSC1_PORTE							\
	{ .name = "msc1-pe",		.port = GPIO_PORT_E, .func = GPIO_FUNC_1, .pins = 0x30f00000, }
#define MSC2_PORTB							\
	{ .name = "msc2-pb",		.port = GPIO_PORT_B, .func = GPIO_FUNC_0, .pins = 0xf0300000, }
#define MSC2_PORTE							\
	{ .name = "msc2-pe",		.port = GPIO_PORT_E, .func = GPIO_FUNC_2, .pins = 0x30f00000, }

/*******************************************************************************************************************/
/*****************************************************************************************************************/

#define SSI0_PORTB						       \
       { .name = "ssi0-pb",	       .port = GPIO_PORT_B, .func = GPIO_FUNC_1, .pins = 0xf0300020, }
#define SSI0_PORTD						       \
       { .name = "ssi0-pd",	       .port = GPIO_PORT_D, .func = GPIO_FUNC_1, .pins = 0x03f00000, }
#define SSI0_PORTE						       \
       { .name = "ssi0-pe",	       .port = GPIO_PORT_E, .func = GPIO_FUNC_0, .pins = 0x000fc000, }

#define SSI1_PORTB						       \
       { .name = "ssi1-pb",	       .port = GPIO_PORT_B, .func = GPIO_FUNC_2, .pins = 0xf0300000, }
#define SSI1_PORTD						       \
       { .name = "ssi1-pd",	       .port = GPIO_PORT_D, .func = GPIO_FUNC_2, .pins = 0x03f00000, }
#define SSI1_PORTE						       \
       { .name = "ssi1-pe",	       .port = GPIO_PORT_E, .func = GPIO_FUNC_1, .pins = 0x000fc000, }


/*****************************************************************************************************************/

#define I2C0_PORTD							\
	{ .name = "i2c0", .port = GPIO_PORT_D, .func = GPIO_FUNC_0, .pins = 0x3<<30, }
#define I2C1_PORTE							\
	{ .name = "i2c1", .port = GPIO_PORT_E, .func = GPIO_FUNC_0, .pins = 0x3<<30, }
#define I2C2_PORTF							\
	{ .name = "i2c2", .port = GPIO_PORT_F, .func = GPIO_FUNC_2, .pins = 0x3<<16, }
#define I2C3_PORTD							\
	{ .name = "i2c3", .port = GPIO_PORT_D, .func = GPIO_FUNC_1, .pins = 0x3<<10, }
#define I2C4_PORTE_OFF3							\
	{ .name = "i2c4-port-e-func1-off3", .port = GPIO_PORT_E, .func = GPIO_FUNC_1, .pins = 0x3<<3, }
#define I2C4_PORTE_OFF12						\
	{ .name = "i2c4-port-e-func1-off12", .port = GPIO_PORT_E, .func = GPIO_FUNC_1, .pins = 0x3<<12, }
#define I2C4_PORTF							\
	{ .name = "i2c4-port-f-func1", .port = GPIO_PORT_F, .func = GPIO_FUNC_1, .pins = 0x3<<24, }

/*******************************************************************************************************************/

#define NAND_PORTAB_COMMON                                                      \
        { .name = "nand-0", .port = GPIO_PORT_A, .func = GPIO_FUNC_0, .pins = 0x000c00ff, },     \
        { .name = "nand-1", .port = GPIO_PORT_B, .func = GPIO_FUNC_0, .pins = 0x00000003, }
#define NAND_PORTA_CS1                                                      \
        { .name = "nand-cs1", .port = GPIO_PORT_A, .func = GPIO_FUNC_0, .pins = 0x1<<21, }
#define NAND_PORTA_CS2                                                      \
        { .name = "nand-cs2", .port = GPIO_PORT_A, .func = GPIO_FUNC_0, .pins = 0x1<<22, }
#define NAND_PORTA_CS3                                                      \
        { .name = "nand-cs3", .port = GPIO_PORT_A, .func = GPIO_FUNC_0, .pins = 0x1<<23, }
#define NAND_PORTA_CS4                                                      \
        { .name = "nand-cs4", .port = GPIO_PORT_A, .func = GPIO_FUNC_0, .pins = 0x1<<24, }
#define NAND_PORTA_CS5                                                      \
        { .name = "nand-cs5", .port = GPIO_PORT_A, .func = GPIO_FUNC_0, .pins = 0x1<<25, }
#define NAND_PORTA_CS6                                                      \
        { .name = "nand-cs6", .port = GPIO_PORT_A, .func = GPIO_FUNC_0, .pins = 0x1<<26, }


/*******************************************************************************************************************/

#define LCD_PORTC							\
	{ .name = "lcd", .port = GPIO_PORT_C, .func = GPIO_FUNC_0, .pins = 0x0fffffff, }

#define PWM_PORTE							\
	{ .name = "pwm", .port = GPIO_PORT_E, .func = GPIO_FUNC_0, .pins = 0x7 << 0, }

#define MII_PORTBDF							\
	{ .name = "mii-0", .port = GPIO_PORT_B, .func = GPIO_FUNC_2, .pins = 0x10, },	\
	{ .name = "mii-1", .port = GPIO_PORT_D, .func = GPIO_FUNC_1, .pins = 0x3c000000, }, \
	{ .name = "mii-2", .port = GPIO_PORT_F, .func = GPIO_FUNC_0, .pins = 0xfff0, }

#define OTG_DRVVUS							\
	{ .name = "otg-drvvbus", .port = GPIO_PORT_E, .func = GPIO_FUNC_0, .pins = 1 << 10, }
	
#define HDMI_PORTF							\
	{ .name = "hdmi-ddc",    .port = GPIO_PORT_F, .func = GPIO_FUNC_0, .pins = 0x3800000, }	

#define CIM_PORTB							\
	{ .name = "cim",    .port = GPIO_PORT_B,  .func = GPIO_FUNC_0, .pins = 0xfff << 6, }	
/* JZ SoC on Chip devices list */
extern struct platform_device jz_msc0_device;
extern struct platform_device jz_msc1_device;
extern struct platform_device jz_msc2_device;

extern struct platform_device jz_i2c0_device;
extern struct platform_device jz_i2c1_device;
extern struct platform_device jz_i2c2_device;
extern struct platform_device jz_i2c3_device;
extern struct platform_device jz_i2c4_device;

extern struct platform_device jz_i2s0_device;
extern struct platform_device jz_i2s1_device;
extern struct platform_device jz_pcm0_device;
extern struct platform_device jz_pcm1_device;
extern struct platform_device jz_codec_device;

extern struct platform_device jz_fb0_device;
extern struct platform_device jz_fb1_device;

extern struct platform_device jz_ipu0_device;
extern struct platform_device jz_ipu1_device;

extern struct platform_device jz_uart0_device;
extern struct platform_device jz_uart1_device;
extern struct platform_device jz_uart2_device;
extern struct platform_device jz_uart3_device;
extern struct platform_device jz_uart4_device;

extern struct platform_device jz_ssi0_device;
extern struct platform_device jz_ssi1_device;

extern struct platform_device jz_pdma_device;

extern struct platform_device jz_cim_device;

extern struct platform_device jz_ohci_device;
extern struct platform_device jz_ehci_device;

extern struct platform_device jz_mac;

extern struct platform_device jz_nand_device;

extern struct platform_device jz_hdmi;
extern struct platform_device jz_rtc_device;
extern struct platform_device jz_tcsm_device;
extern struct platform_device jz_x2d_device;

int jz_device_register(struct platform_device *pdev,void *pdata);

#endif
