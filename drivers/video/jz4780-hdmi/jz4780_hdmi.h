#ifndef __JZ4780_HDMI_H__
#define __JZ4780_HDMI_H__

#include "api/api.h"
#include "edid/edid.h"
#include "util/log.h"
#include "bsp/mutex.h"
#include "bsp/board.h"

#include "core/control.h"
#include "core/video.h"
#include "core/audio.h"
#include "core/packets.h"
#include "hdcp/hdcp.h"
#include "edid/edid.h"
#include "phy/halSourcePhy.h"
#include "util/error.h"

#define GPA_ENABLE 0

#define PHY_BASE_ADDR 0x3000

#define HDMI_VIDEO_MODE_NUM 3 /* for test */
#define MODE_NAME_LEN 32

#if 0
typedef enum HMDI_STATUS{
	HDMI_WORK_STAT_STANDBY = 0, /* */
	HDMI_WORK_STAT_WORKING = 1, /* */
	HDMI_WORK_STAT_DISABLED = 2, /* */
} HMDI_STATUS_T;

typedef enum HMDI_POWER{
	POWER_OFF = 0,
	POWER_ON,
	POWER_INVAILD,
} HMDI_POWER_T;

typedef enum HMDI_WORK{
	HMDI_NULL_WORK = 0,
	HMDI_DETECT_WORK,
	HMDI_DEVPROC_WORK,
	HMDI_DEVCHECK_WORK,
} HMDI_WORK_T;
#endif

struct hdmi_video_mode {
	char *name;
	unsigned int refresh;
};

struct hdmi_video_mode mode_index[] = {
	{"640x480-p-60hz-4:3", 60},
	{"720x480-p-60hz-4:3", 60},
	{"720x480-p-60hz-16:9", 60},
};

struct hdmi_device_params{
	videoParams_t *pVideo;
	audioParams_t *pAudio;
	hdcpParams_t *pHdcp;
	productParams_t *pProduct;
	//dtd_t           *dtd /* video mode */
};

enum HMDI_STATUS {
	HDMI_HOTPLUG_DISCONNECTED,
	HDMI_HOTPLUG_CONNECTED,
	HDMI_HOTPLUG_EDID_DONE,
};

struct hdmi_info{
	enum HMDI_STATUS  hdmi_status;
	unsigned int out_type;
	//unsigned int hdmi_status;
};

struct jzhdmi{
	struct device *dev;
	void __iomem *base;
	struct resource *mem;
	struct clk *hdmi_clk;
	unsigned int init;

	atomic_t opened;
	struct miscdevice hdmi_miscdev;
	struct switch_dev hdmi_switch;
	wait_queue_head_t wait;


	struct delayed_work detect_work;
	struct workqueue_struct *workqueue;


	struct hdmi_device_params hdmi_params;
	struct early_suspend  early_suspend;
	struct hdmi_info hdmi_info;

	unsigned int hpd_connected;
	unsigned int edid_done;
	unsigned int is_suspended;
};

/* ioctl commands */
#define HDMI_POWER_OFF			_IO('F', 0x301)
#define	HDMI_VIDEOMODE_CHANGE		_IOW('F', 0x302, char *)
#define	HDMI_POWER_ON			_IO('F', 0x303)

#endif
