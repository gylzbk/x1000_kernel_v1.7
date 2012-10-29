
#include <mach/jzsnd.h>
#include "npm801.h"

struct snd_codec_data codec_data = {
	.codec_sys_clk = 0 ,
	.codec_dmic_clk = 0,
	/* volume */
	.replay_volume_base = 0,
	.record_volume_base = 0,
	.record_digital_volume_base = 0,
	.replay_digital_volume_base = 0,
	.replay_hp_output_gain_base = 0,
	/* default route */
	.replay_def_route = {.route = SND_ROUTE_REPLAY_SPEAKER,
						 .gpio_hp_mute_stat = 0,
						 .gpio_spk_en_stat = 1},
	.record_def_route = {.route = SND_ROUTE_RECORD_MIC,
						 .gpio_hp_mute_stat = 0,
						 .gpio_spk_en_stat = 1},
	/* device <-> route map */
	.handset_route = {.route = SND_ROUTE_REPLAY_INCALL_WITH_HANDSET,
					  .gpio_hp_mute_stat = 0,
					  .gpio_spk_en_stat = 1},
	.headset_route = {.route = SND_ROUTE_REPLAY_HEADPHONE,
					  .gpio_hp_mute_stat = 0,
					  .gpio_spk_en_stat = 0},
	.speaker_route = {.route = SND_ROUTE_REPLAY_SPEAKER,
					  .gpio_hp_mute_stat = 0,
					  .gpio_spk_en_stat = 1},
	.headset_and_speaker_route = {.route = SND_ROUTE_REPLAY_SPEAKER_AND_HEADPHONE,
								  .gpio_hp_mute_stat = 0,
								  .gpio_spk_en_stat = 1},
	.fm_speaker_route = {.route = SND_ROUTE_REPLAY_FM_SPEAKER,
						 .gpio_hp_mute_stat = 0,
						 .gpio_spk_en_stat = 1},
	.fm_headset_route = {.route = SND_ROUTE_REPLAY_FM_HEADSET,
						 .gpio_hp_mute_stat = 0,
						 .gpio_spk_en_stat = 0},
	/* gpio */
	.gpio_hp_mute = {.gpio = GPIO_HP_MUTE, .active_level = 1},
	.gpio_spk_en = {.gpio = GPIO_SPEAKER_EN, .active_level = 1},
	.gpio_head_det = {.gpio = -1, .active_level = 0},

	.hpsense_active_level = 1,
};
