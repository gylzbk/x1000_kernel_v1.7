/**********************************************************************
 *
 * Copyright (C) Imagination Technologies Ltd. All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope it will be useful but, except 
 * as otherwise stated in writing, without any warranty; without even the 
 * implied warranty of merchantability or fitness for a particular purpose. 
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Imagination Technologies Ltd. <gpl-support@imgtec.com>
 * Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK 
 *
 ******************************************************************************/

#include <linux/version.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/hardirq.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include "sgxdefs.h"
#include "services_headers.h"
#include "sysinfo.h"
#include "sgxapi_km.h"
#include "sysconfig.h"
#include "sgxinfokm.h"
#include "syslocal.h"

#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#if defined(SYS_XB47_HAS_DVFS_FRAMEWORK)
#include <linux/opp.h>
#endif

#if defined(SUPPORT_DRI_DRM_PLUGIN)
#include <drm/drmP.h>
#include <drm/drm.h>

#include <linux/xb_gpu.h>

#include "pvr_drm.h"
#endif

#define	ONE_MHZ	1000000
#define	HZ_TO_MHZ(m) ((m) / ONE_MHZ)

#define SGX_PARENT_CLOCK "core_ck"

#if defined(LDM_PLATFORM) && !defined(PVR_DRI_DRM_NOT_PCI)
extern struct platform_device *gpsPVRLDMDev;
#endif

static PVRSRV_ERROR PowerLockWrap(SYS_SPECIFIC_DATA *psSysSpecData, IMG_BOOL bTryLock)
{
	if (!in_interrupt())
	{
		if (bTryLock)
		{
			int locked = mutex_trylock(&psSysSpecData->sPowerLock);
			if (locked == 0)
			{
				return PVRSRV_ERROR_RETRY;
			}
		}
		else
		{
			mutex_lock(&psSysSpecData->sPowerLock);
		}
	}

	return PVRSRV_OK;
}

static IMG_VOID PowerLockUnwrap(SYS_SPECIFIC_DATA *psSysSpecData)
{
	if (!in_interrupt())
	{
		mutex_unlock(&psSysSpecData->sPowerLock);
	}
}

PVRSRV_ERROR SysPowerLockWrap(IMG_BOOL bTryLock)
{
	SYS_DATA	*psSysData;

	SysAcquireData(&psSysData);

	return PowerLockWrap(psSysData->pvSysSpecificData, bTryLock);
}

IMG_VOID SysPowerLockUnwrap(IMG_VOID)
{
	SYS_DATA	*psSysData;

	SysAcquireData(&psSysData);

	PowerLockUnwrap(psSysData->pvSysSpecificData);
}

IMG_BOOL WrapSystemPowerChange(SYS_SPECIFIC_DATA *psSysSpecData)
{
	return IMG_TRUE;
}

IMG_VOID UnwrapSystemPowerChange(SYS_SPECIFIC_DATA *psSysSpecData)
{
}

IMG_VOID SysGetSGXTimingInformation(SGX_TIMING_INFORMATION *psTimingInfo)
{
#if !defined(NO_HARDWARE)
	PVR_ASSERT(atomic_read(&gpsSysSpecificData->sSGXClocksEnabled) != 0);
#endif
#if defined(SYS_XB47_HAS_DVFS_FRAMEWORK)
	psTimingInfo->ui32CoreClockSpeed =
		gpsSysSpecificData->pui32SGXFreqList[gpsSysSpecificData->ui32SGXFreqListIndex];
#else 
	psTimingInfo->ui32CoreClockSpeed = SYS_SGX_CLOCK_SPEED;
#endif
	psTimingInfo->ui32HWRecoveryFreq = SYS_SGX_HWRECOVERY_TIMEOUT_FREQ;
	psTimingInfo->ui32uKernelFreq = SYS_SGX_PDS_TIMER_FREQ;
#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
ddsddsd
	psTimingInfo->bEnableActivePM = IMG_TRUE;
#else
	psTimingInfo->bEnableActivePM = IMG_FALSE;
#endif 
	psTimingInfo->ui32ActivePowManLatencyms = SYS_SGX_ACTIVE_POWER_LATENCY_MS;
}

PVRSRV_ERROR EnableSGXClocks(SYS_DATA *psSysData)
{
#if !defined(NO_HARDWARE)
	SYS_SPECIFIC_DATA *psSysSpecData = (SYS_SPECIFIC_DATA *) psSysData->pvSysSpecificData;

	
	if (atomic_read(&psSysSpecData->sSGXClocksEnabled) != 0)
	{
		return PVRSRV_OK;
	}

	PVR_DPF((PVR_DBG_MESSAGE, "EnableSGXClocks: Enabling SGX Clocks"));

#if defined(LDM_PLATFORM) && !defined(PVR_DRI_DRM_NOT_PCI)
#if defined(SYS_XB47_HAS_DVFS_FRAMEWORK)
	{
		struct gpu_platform_data *pdata;
		IMG_UINT32 max_freq_index;
		int res;

		pdata = (struct gpu_platform_data *)gpsPVRLDMDev->dev.platform_data;
		max_freq_index = psSysSpecData->ui32SGXFreqListSize - 2;

		
		if (psSysSpecData->ui32SGXFreqListIndex != max_freq_index)
		{
			PVR_ASSERT(pdata->device_scale != IMG_NULL);
			res = pdata->device_scale(&gpsPVRLDMDev->dev,
									  &gpsPVRLDMDev->dev,
									  psSysSpecData->pui32SGXFreqList[max_freq_index]);
			if (res == 0)
			{
				psSysSpecData->ui32SGXFreqListIndex = max_freq_index;
			}
			else if (res == -EBUSY)
			{
				PVR_DPF((PVR_DBG_WARNING, "EnableSGXClocks: Unable to scale SGX frequency (EBUSY)"));
				psSysSpecData->ui32SGXFreqListIndex = psSysSpecData->ui32SGXFreqListSize - 1;
			}
			else if (res < 0)
			{
				PVR_DPF((PVR_DBG_ERROR, "EnableSGXClocks: Unable to scale SGX frequency (%d)", res));
				psSysSpecData->ui32SGXFreqListIndex = psSysSpecData->ui32SGXFreqListSize - 1;
			}
		}
	}
#endif 
	{
		int res = pm_runtime_get_sync(&gpsPVRLDMDev->dev);
		if (res < 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "EnableSGXClocks: pm_runtime_get_sync failed (%d)", -res));
			return PVRSRV_ERROR_UNABLE_TO_ENABLE_CLOCK;
		}
	}
#endif 

	SysEnableSGXInterrupts(psSysData);

	
	atomic_set(&psSysSpecData->sSGXClocksEnabled, 1);

#else	
	PVR_UNREFERENCED_PARAMETER(psSysData);
#endif	
	return PVRSRV_OK;
}


IMG_VOID DisableSGXClocks(SYS_DATA *psSysData)
{
#if !defined(NO_HARDWARE)
	SYS_SPECIFIC_DATA *psSysSpecData = (SYS_SPECIFIC_DATA *) psSysData->pvSysSpecificData;

	
	if (atomic_read(&psSysSpecData->sSGXClocksEnabled) == 0)
	{
		return;
	}

	PVR_DPF((PVR_DBG_MESSAGE, "DisableSGXClocks: Disabling SGX Clocks"));

	SysDisableSGXInterrupts(psSysData);

#if defined(LDM_PLATFORM) && !defined(PVR_DRI_DRM_NOT_PCI)
	{
		int res = pm_runtime_put_sync(&gpsPVRLDMDev->dev);
		if (res < 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "DisableSGXClocks: pm_runtime_put_sync failed (%d)", -res));
		}
	}
#if defined(SYS_XB47_HAS_DVFS_FRAMEWORK)
	{
		struct gpu_platform_data *pdata;
		int res;

		pdata = (struct gpu_platform_data *)gpsPVRLDMDev->dev.platform_data;

		
		if (psSysSpecData->ui32SGXFreqListIndex != 0)
		{
			PVR_ASSERT(pdata->device_scale != IMG_NULL);
			res = pdata->device_scale(&gpsPVRLDMDev->dev,
									  &gpsPVRLDMDev->dev,
									  psSysSpecData->pui32SGXFreqList[0]);
			if (res == 0)
			{
				psSysSpecData->ui32SGXFreqListIndex = 0;
			}
			else if (res == -EBUSY)
			{
				PVR_DPF((PVR_DBG_WARNING, "DisableSGXClocks: Unable to scale SGX frequency (EBUSY)"));
				psSysSpecData->ui32SGXFreqListIndex = psSysSpecData->ui32SGXFreqListSize - 1;
			}
			else if (res < 0)
			{
				PVR_DPF((PVR_DBG_ERROR, "DisableSGXClocks: Unable to scale SGX frequency (%d)", res));
				psSysSpecData->ui32SGXFreqListIndex = psSysSpecData->ui32SGXFreqListSize - 1;
			}
		}
	}
#endif 
#endif 

	
	atomic_set(&psSysSpecData->sSGXClocksEnabled, 0);

#else	
	PVR_UNREFERENCED_PARAMETER(psSysData);
#endif	
}

static PVRSRV_ERROR AcquireGPTimer(SYS_SPECIFIC_DATA *psSysSpecData)
{
#if defined(PVR_XB47_TIMING_CPM)
	PVRSRV_ERROR eError;

	IMG_CPU_PHYADDR sTimerRegPhysBase;
	IMG_HANDLE hTimerEnable;
	IMG_UINT32 *pui32TimerEnable;

	PVR_ASSERT(psSysSpecData->sTimerRegPhysBase.uiAddr == 0);
	
	sTimerRegPhysBase.uiAddr = SYS_XB4780_GPU_TIMER_ENABLE_SYS_PHYS_BASE;
	pui32TimerEnable = OSMapPhysToLin(sTimerRegPhysBase,
                  4,
                  PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
                  &hTimerEnable);

	if (pui32TimerEnable == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "EnableSystemClocks: OSMapPhysToLin failed"));
		goto ExitDisableGPT11ICK;
	}

        PVR_DPF((PVR_DBG_ERROR, "EnableSystemClocks: pui32TimerEnable = %p", pui32TimerEnable));
	
        *pui32TimerEnable &= (~(1 << 4));

        // Turn on the light
        outl((inl(0x10000004) & ~(1 << 29)), 0x10000004);
        do {
            printk("Waiting for GPU power\n");
        } while ((inl(0x10000004) & (1 << 25)));

        // Open the gate
        *pui32TimerEnable &= (~(1 << 4));
        // Set clock
        outl(0xA0000002, 0x10000088);

	OSUnMapPhysToLin(pui32TimerEnable,
		    4,
		    PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
		    hTimerEnable);

	psSysSpecData->sTimerRegPhysBase = sTimerRegPhysBase;

	eError = PVRSRV_OK;

	goto Exit;

ExitDisableGPT11ICK:
	eError = PVRSRV_ERROR_CLOCK_REQUEST_FAILED;
Exit:
	return eError;

#else //#if defined(PVR_XB47_TIMING_CPM)

	PVR_UNREFERENCED_PARAMETER(psSysSpecData);

	return PVRSRV_OK;
#endif //#if defined(PVR_XB47_TIMING_CPM)
}

static void ReleaseGPTimer(SYS_SPECIFIC_DATA *psSysSpecData)
{
#if defined(PVR_XB47_TIMING_CPM)
	IMG_HANDLE hTimerDisable;
	IMG_UINT32 *pui32TimerDisable;

	if (psSysSpecData->sTimerRegPhysBase.uiAddr == 0)
	{
		return;
	}

	
	pui32TimerDisable = OSMapPhysToLin(psSysSpecData->sTimerRegPhysBase,
				4,
				PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
				&hTimerDisable);

	if (pui32TimerDisable == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "DisableSystemClocks: OSMapPhysToLin failed"));
	}
	else
	{
		*pui32TimerDisable |= (1 << 4);

		OSUnMapPhysToLin(pui32TimerDisable,
				4,
				PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
				hTimerDisable);
	}

	psSysSpecData->sTimerRegPhysBase.uiAddr = 0;

#else //#if defined(PVR_XB47_TIMING_CPM)
	PVR_UNREFERENCED_PARAMETER(psSysSpecData);
#endif
}

PVRSRV_ERROR EnableSystemClocks(SYS_DATA *psSysData)
{
	SYS_SPECIFIC_DATA *psSysSpecData = (SYS_SPECIFIC_DATA *) psSysData->pvSysSpecificData;

	PVR_TRACE(("EnableSystemClocks: Enabling System Clocks"));

	if (!psSysSpecData->bSysClocksOneTimeInit)
	{
		mutex_init(&psSysSpecData->sPowerLock);

		atomic_set(&psSysSpecData->sSGXClocksEnabled, 0);

		psSysSpecData->bSysClocksOneTimeInit = IMG_TRUE;
	}

	return AcquireGPTimer(psSysSpecData);
}

IMG_VOID DisableSystemClocks(SYS_DATA *psSysData)
{
	SYS_SPECIFIC_DATA *psSysSpecData = (SYS_SPECIFIC_DATA *) psSysData->pvSysSpecificData;

	PVR_TRACE(("DisableSystemClocks: Disabling System Clocks"));

	
	DisableSGXClocks(psSysData);

	ReleaseGPTimer(psSysSpecData);
}

PVRSRV_ERROR SysPMRuntimeRegister(void)
{
#if defined(LDM_PLATFORM) && !defined(PVR_DRI_DRM_NOT_PCI)
	pm_runtime_enable(&gpsPVRLDMDev->dev);
#endif
	return PVRSRV_OK;
}

PVRSRV_ERROR SysPMRuntimeUnregister(void)
{
#if defined(LDM_PLATFORM) && !defined(PVR_DRI_DRM_NOT_PCI)
	pm_runtime_disable(&gpsPVRLDMDev->dev);
#endif
	return PVRSRV_OK;
}

PVRSRV_ERROR SysDvfsInitialize(SYS_SPECIFIC_DATA *psSysSpecificData)
{
#if !defined(SYS_XB47_HAS_DVFS_FRAMEWORK)
	PVR_UNREFERENCED_PARAMETER(psSysSpecificData);

#else 
	IMG_UINT32 i, *freq_list;
	IMG_INT32 opp_count;
	unsigned long freq;
	struct opp *opp;

	
	rcu_read_lock();
	opp_count = opp_get_opp_count(&gpsPVRLDMDev->dev);
	if (opp_count < 1)
	{
		rcu_read_unlock();
		PVR_DPF((PVR_DBG_ERROR, "SysDvfsInitialize: Could not retrieve opp count"));
		return PVRSRV_ERROR_NOT_SUPPORTED;
	}

	
	freq_list = kmalloc((opp_count + 1) * sizeof(IMG_UINT32), GFP_ATOMIC);
	if (!freq_list)
	{
		rcu_read_unlock();
		PVR_DPF((PVR_DBG_ERROR, "SysDvfsInitialize: Could not allocate frequency list"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	
	freq = 0;
	for (i = 0; i < opp_count; i++)
	{
		opp = opp_find_freq_ceil(&gpsPVRLDMDev->dev, &freq);
		if (IS_ERR_OR_NULL(opp))
		{
			rcu_read_unlock();
			PVR_DPF((PVR_DBG_ERROR, "SysDvfsInitialize: Could not retrieve opp level %d", i));
			kfree(freq_list);
			return PVRSRV_ERROR_NOT_SUPPORTED;
		}
		freq_list[i] = (IMG_UINT32)freq;
		freq++;
	}
	rcu_read_unlock();
	freq_list[opp_count] = freq_list[opp_count - 1];

	psSysSpecificData->ui32SGXFreqListSize = opp_count + 1;
	psSysSpecificData->pui32SGXFreqList = freq_list;

	
	psSysSpecificData->ui32SGXFreqListIndex = opp_count;
#endif 

	return PVRSRV_OK;
}

PVRSRV_ERROR SysDvfsDeinitialize(SYS_SPECIFIC_DATA *psSysSpecificData)
{
#if !defined(SYS_XB47_HAS_DVFS_FRAMEWORK)
	PVR_UNREFERENCED_PARAMETER(psSysSpecificData);
#else 
	
	if (psSysSpecificData->ui32SGXFreqListIndex != 0)
	{
		struct gpu_platform_data *pdata;
		IMG_INT32 res;

		pdata = (struct gpu_platform_data *)gpsPVRLDMDev->dev.platform_data;

		PVR_ASSERT(pdata->device_scale != IMG_NULL);
		res = pdata->device_scale(&gpsPVRLDMDev->dev,
								  &gpsPVRLDMDev->dev,
								  psSysSpecificData->pui32SGXFreqList[0]);
		if (res == -EBUSY)
		{
			PVR_DPF((PVR_DBG_WARNING, "SysDvfsDeinitialize: Unable to scale SGX frequency (EBUSY)"));
		}
		else if (res < 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "SysDvfsDeinitialize: Unable to scale SGX frequency (%d)", res));
		}

		psSysSpecificData->ui32SGXFreqListIndex = 0;
	}

	kfree(psSysSpecificData->pui32SGXFreqList);
	psSysSpecificData->pui32SGXFreqList = 0;
	psSysSpecificData->ui32SGXFreqListSize = 0;
#endif 

	return PVRSRV_OK;
}
