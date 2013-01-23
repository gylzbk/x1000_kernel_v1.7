/**
 * jzsnd.h
 *
 **/
#ifndef __MACH_JZSND_H__
#define __MACH_JZSND_H__

#include <linux/list.h>
#include <linux/sound.h>
#include <linux/platform_device.h>

enum snd_codec_route_t {
	SND_ROUTE_NONE = 0,
	/*
	 * clear route
	 */
	SND_ROUTE_ALL_CLEAR,
	SND_ROUTE_REPLAY_CLEAR,
	SND_ROUTE_RECORD_CLEAR,
	/*
	 * replay route
	 */
	SND_ROUTE_REPLAY_ROUTE_START,
	/*internal codec: dacrl to lineout*/
	SND_ROUTE_REPLAY_DACRL_TO_LO = SND_ROUTE_REPLAY_ROUTE_START,
	/*internal codec: dacrl to hprl*/
	SND_ROUTE_REPLAY_DACRL_TO_HPRL,
	/*internal codec: ..*/
	SND_ROUTE_REPLAY_DACRL_TO_ALL,
	/*internal codec replay route end*/
	SND_ROUTE_REPLAY_ROUTE_END = SND_ROUTE_REPLAY_DACRL_TO_ALL,
	/*
	 * record route
	 */
	SND_ROUTE_RECORD_ROUTE_START,
	/*internal codec: mic 1 to adcrl*/
	SND_ROUTE_RECORD_MIC1_AN1 = SND_ROUTE_RECORD_ROUTE_START,
	/*internal codec: mic 2 to adcrl*/
	SND_ROUTE_RECORD_MIC1_SIN_AN2,
	/*internal codec: mic 2 to adcrl*/
	SND_ROUTE_RECORD_MIC2_SIN_AN3,
	/*internal codec: linein 1 to adcrl*/
	SND_ROUTE_RECORD_LINEIN1_DIFF_AN1,
	/*internal codec: call record mic bypass to hprl*/
	SND_ROUTE_CALL_RECORD,
	/*internal codec record route end*/
	//SND_ROUTE_RECORD_ROUTE_END = SND_ROUTE_RECORD_LINEIN1_DIFF_AN1,

	SND_ROUTE_RECORD_ROUTE_END = SND_ROUTE_CALL_RECORD,
	/*
	 * loop route and bypass rount
	 */
	SND_ROUTE_LOOP_ROUTE_START,
	/*internal codec: linein2 bypass to lineout */
	SND_ROUTE_REPLAY_LINEIN2_BYPASS_TO_LINEOUT = SND_ROUTE_LOOP_ROUTE_START,
	/*internal codec: linein2 bypass to hprl */
	SND_ROUTE_REPLAY_LINEIN2_BYPASS_TO_HPRL,
	/*internal codec: mic bypass to hprl*/
	SND_ROUTE_CALL_MIC_BYPASS_TO_HPRL,
	/*internal codec: record mic1 an1 to adclr replay hp stereo*/
	SND_ROUTE_LOOP_MIC1_AN1_LOOP_TO_HP,
	SND_ROUTE_MIC2_AN3_TO_AD_AND_DA_TO_LO,

	SND_ROUTE_LOOP_ROUTE_END = SND_ROUTE_LOOP_MIC1_AN1_LOOP_TO_HP,
	/*
	 *route count
	 */
	SND_ROUTE_COUNT = SND_ROUTE_RECORD_LINEIN1_DIFF_AN1,
};

struct snd_board_route {
	enum snd_codec_route_t route;
	//val : 0 is mean not set use default volume base
	int replay_volume_base;				//val: +6 ~ +1, 32(0dB), -1 ~ -25 (dB);
	int record_volume_base;				//val: 32(0dB), +4, +8, +16, +20 (dB);
	int record_digital_volume_base;		//val: 32(0dB), +1 ~ +23 (dB);
	int replay_digital_volume_base;		//val: 32(0dB), -1 ~ -31 (dB);
	int bypass_l_volume_base;			//val: +6 ~ +1, 32(0dB), -1 ~ -25 (dB);
	int bypass_r_volume_base;			//val: +6 ~ +1, 32(0dB), -1 ~ -25 (dB);
	int gpio_hp_mute_stat;	/* -1: keep , 0: disable, 1: enable */
	int gpio_spk_en_stat;	/* -1: keep , 0: disable, 1: enable */
	int gpio_handset_en_stat;  /* -1: keep , 0: disable, 1: enable */
	int gpio_buildin_mic_en_stat; /* -1: keep , 0:disable, 1: enable */
};

struct snd_board_gpio {
	int gpio;			/* -1: gpio is unavailable*/
	int active_level;	/* 0: is low available other: is high available*/
};

struct snd_codec_data {
	/* clk */
	int codec_sys_clk;
	int codec_dmic_clk;

	/* volume */
	int replay_volume_base;						/*hp*/
	int record_volume_base;
	int record_digital_volume_base;
	int replay_digital_volume_base;
	int bypass_l_volume_base;					/*call uplink or other use*/
	int bypass_r_volume_base;					/*call downlink or other use*/

	/* default route */
	struct snd_board_route replay_def_route;
	struct snd_board_route record_def_route;
	//struct snd_board_route call_record_def_route;

	/* device <-> route map record*/
	struct snd_board_route record_headset_mic_route;
	struct snd_board_route record_buildin_mic_route;

	/* device <-> route map replay*/
	struct snd_board_route replay_headset_route;
	struct snd_board_route replay_speaker_route;
	struct snd_board_route replay_headset_and_speaker_route;
	struct snd_board_route fm_speaker_route;
	struct snd_board_route fm_headset_route;
	struct snd_board_route bt_route;
	struct snd_board_route downlink_handset_route;
	struct snd_board_route downlink_headset_route;
	struct snd_board_route downlink_speaker_route;
	struct snd_board_route uplink_buildin_mic_route;
	struct snd_board_route uplink_headset_mic_route;
	struct snd_board_route record_incall_route;
	struct snd_board_route headset_record_incall_route;
	struct snd_board_route replay_loop_route;

	/* gpio */
	struct snd_board_gpio gpio_hp_mute;
	struct snd_board_gpio gpio_spk_en;
	struct snd_board_gpio gpio_handset_en;
	struct snd_board_gpio gpio_hp_detect;
	struct snd_board_gpio gpio_mic_detect;
	struct snd_board_gpio gpio_mic_detect_en;
	struct snd_board_gpio gpio_buildin_mic_select;
	/* other */
	int hpsense_active_level;
    int hook_active_level;   /* -1: no hook detect,
								 0: hook low level available,
                                 1: hook high level available,
                                 2: hook adc enable
								 */
};


/*####################################################*\
* common, used for sound devices
\*####################################################*/
/**
 * device mame and minor
 **/
#define MAX_SND_MINOR  	128
#define SOUND_STEP		16

#define DEV_DSP_NAME	"dsp"

#define SND_DEV_DSP0  	(SND_DEV_DSP + SOUND_STEP * 0)
#define SND_DEV_DSP1  	(SND_DEV_DSP + SOUND_STEP * 1)

#define DEV_MIXER_NAME	"mixer"

#define SND_DEV_MIXER0	(SND_DEV_CTL + SOUND_STEP * 0)
#define SND_DEV_MIXER1 	(SND_DEV_CTL + SOUND_STEP * 1)

#define minor2index(x)	((x) / SOUND_STEP)

/**
 * struct snd_dev_data: sound device platform_data
 * @list: the list is usd to point to next or pre snd_dev_data.
 * @ext_data: used for dsp device, point to
 * (struct dsp_endpoints *) of dsp device.
 * @minor: the minor of the specific device. value will be
 * SND_DEV_DSP(0...), SND_DEV_MIXER(0...) and so on.
 * @is_suspend: indicate if the device is suspend or not.
 * @dev_ioctl: privide by the specific device, used
 * to control the device by command.
 * @init: specific device init.
 * @shutdown: specific device shutdown.
 * @suspend: specific device suspend.
 * @resume: specific device resume.
 **/
struct snd_dev_data {
	struct list_head list;
	struct device *dev;
	void *ext_data;
	int minor;
	bool is_suspend;
	long (*dev_ioctl) (unsigned int cmd, unsigned long arg);
	int (*init)(struct platform_device *pdev);
	void (*shutdown)(struct platform_device *pdev);
	int (*suspend)(struct platform_device *pdev, pm_message_t state);
	int (*resume)(struct platform_device *pdev);
};

extern struct snd_dev_data i2s_data;
extern struct snd_dev_data pcm_data;
extern struct snd_dev_data snd_mixer0_data;
extern struct snd_dev_data snd_mixer1_data;
/*call state*/
extern bool i2s_is_incall(void);

#endif //__MACH_JZSND_H__
