/*
 * LCD driver data for EK070TN93
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _EK070TN93_H
#define _EK070TN93_H

/**
 * @gpio_mode: de/sync mode select
 * @gpio_de: data input enable
 * @gpio_vs: vertical sync input
 * @gpio_hs: horizontal sync input
 * @gpio_lr: left right scan direction pin, 0: right to left, 1: left to right
 * @gpio_ud: stop bottom scan direction pin, 0: top to bottom, 1: bottom to top
 * @gpio_reset: global reset pin
 * @gpio_dithb: dithering function pin

 * @mode_select: 1: DE mode, 0: SYNC mode
 * @left_to_right_scan: scan direction, 0: right to left, 1: left to right
 * @bottom_to_top_scan: scan direction, 0: top to bottom, 1: bottom to top
 * @dither_enable: 1: dither enable
 */
struct platform_ek070tn93_data {
	unsigned int gpio_mode;
	unsigned int gpio_de;
	unsigned int gpio_vs;
	unsigned int gpio_hs;
	unsigned int gpio_lr;
	unsigned int gpio_ud;
	unsigned int gpio_reset;
	unsigned int gpio_dithb;

	unsigned int mode_select;
	unsigned int left_to_right_scan;
	unsigned int bottom_to_top_scan;
	unsigned int dither_enable;
};

#endif /* _EK070TN93_H */
