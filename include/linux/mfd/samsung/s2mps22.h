/*
 * s2mps22.h - Driver for the s2mps22
 *
 *  Copyright (C) 2019 Samsung Electrnoics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef __S2MPS22_MFD_H__
#define __S2MPS22_MFD_H__
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define MFD_DEV_NAME "s2mps22"
/**
 * sec_regulator_data - regulator data
 * @id: regulator id
 * @initdata: regulator init data (contraints, supplies, ...)
 */
struct s2mps22_regulator_data {
	int id;
	struct regulator_init_data *initdata;
	struct device_node *reg_node;
};

/*
 * sec_opmode_data - regulator operation mode data
 * @id: regulator id
 * @mode: regulator operation mode
 */
struct sec_opmode_data {
	int id;
	unsigned int mode;
};

/*
 * samsung regulator operation mode
 * SEC_OPMODE_OFF	Regulator always OFF
 * SEC_OPMODE_ON	Regulator always ON
 * SEC_OPMODE_LOWPOWER  Regulator is on in low-power mode
 * SEC_OPMODE_SUSPEND   Regulator is changed by PWREN pin
 *			If PWREN is high, regulator is on
 *			If PWREN is low, regulator is off
 */
enum sec_opmode {
	SEC_OPMODE_OFF,
	SEC_OPMODE_SUSPEND,
	SEC_OPMODE_LOWPOWER,
	SEC_OPMODE_ON,
	SEC_OPMODE_TCXO = 0x2,
	SEC_OPMODE_MIF = 0x2,
};

struct s2mps22_platform_data {

	bool use_i2c_speedy;
	bool wakeup;
	bool g3d_en;

	/* IRQ */
	int irq_base;
	int irq_gpio;

	int (*cfg_pmic_irq)(void);
	int device_type;
	int ono;
	int buck_ramp_delay;
	/* adc_mode
	 * 0 : not use
	 * 1 : current meter
	 * 2 : power meter
	*/
	int adc_mode;
	/* 1 : sync mode, 2 : async mode  */
	int adc_sync_mode;
	/* wtsr */
	int wtsr_en;

	int num_regulators;
	int num_subdevs;
	struct s2mps22_regulator_data *regulators;
	struct sec_opmode_data *opmode;
	struct mfd_cell *sub_devices;
};

struct s2mps22 {
	struct regmap *regmap;
};
#endif /* __S2MPS22_MFD_H__ */
