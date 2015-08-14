#ifndef __PMU_H__
#define __PMU_H__
#ifdef CONFIG_REGULATOR_RICOH619

#define PMU_I2C_BUSNUM  1

/* ****************************PMU DC/LDO NAME******************************* */
#define LDO1_NAME "cpu_2v5"
#define LDO2_NAME "wifi_vddio"
#define LDO3_NAME "vcc_gensor"
#define LDO4_NAME "vcc_dmic"
#define LDO5_NAME "lcd_3v3"
/* ****************************PMU DC/LDO NAME END*************************** */

/* ****************************PMU DC/LDO DEFAULT V************************** */
#define LDO1_INIT_UV    2500
#define LDO2_INIT_UV    3300
#define LDO3_INIT_UV    3300
#define LDO4_INIT_UV    3300
#define LDO5_INIT_UV    3300
/* ****************************PMU DC/LDO DEFAULT V END********************** */
/* ****************************PMU DC/LDO SLP DEFAULT V********************** */
#define LDO1_INIT_SLP_UV    1800
#define LDO2_INIT_SLP_UV    3300
#define LDO3_INIT_SLP_UV    2800
#define LDO4_INIT_SLP_UV    1800
#define LDO5_INIT_SLP_UV    2500
/* ****************************PMU DC/LDO SLP DEFAULT V END****************** */
/* ****************************PMU DC/LDO ALWAYS ON************************** */
#define LDO1_ALWAYS_ON    1
#define LDO2_ALWAYS_ON    0
#define LDO3_ALWAYS_ON    0
#define LDO4_ALWAYS_ON    0
#define LDO5_ALWAYS_ON    0
/* ****************************PMU DC/LDO ALWAYS ON END********************** */

/* ****************************PMU DC/LDO BOOT ON**************************** */
#define LDO1_BOOT_ON    1
#define LDO2_BOOT_ON    0
#define LDO3_BOOT_ON    1
#define LDO4_BOOT_ON    1
#define LDO5_BOOT_ON    1
/* ****************************PMU DC/LDO BOOT ON END************************ */

/* ****************************PMU DC/LDO INIT ENABLE************************ */
#define LDO1_INIT_ENABLE    LDO1_BOOT_ON
#define LDO2_INIT_ENABLE    LDO2_BOOT_ON
#define LDO3_INIT_ENABLE    LDO3_BOOT_ON
#define LDO4_INIT_ENABLE    LDO4_BOOT_ON
#define LDO5_INIT_ENABLE    LDO5_BOOT_ON

/* ****************************PMU DC/LDO INIT ENABLE END******************** */
#endif	/* CONFIG_REGULATOR_RICOH619 */
#endif
