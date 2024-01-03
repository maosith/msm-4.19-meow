/*
 *  sec_step_charging.c
 *  Samsung Mobile Battery Driver
 *
 *  Copyright (C) 2018 Samsung Electronics
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "include/sec_battery.h"

#define STEP_CHARGING_CONDITION_VOLTAGE			0x01
#define STEP_CHARGING_CONDITION_SOC				0x02
#define STEP_CHARGING_CONDITION_CHARGE_POWER 	0x04
#define STEP_CHARGING_CONDITION_ONLINE 			0x08
#define STEP_CHARGING_CONDITION_CURRENT_NOW		0x10
#define STEP_CHARGING_CONDITION_FLOAT_VOLTAGE	0x20
#define STEP_CHARGING_CONDITION_INPUT_CURRENT		0x40
#define STEP_CHARGING_CONDITION_SOC_INIT_ONLY		0x80 /* use this to consider SOC to decide starting step only */

#define STEP_CHARGING_CONDITION_DC_INIT		(STEP_CHARGING_CONDITION_VOLTAGE | STEP_CHARGING_CONDITION_SOC | STEP_CHARGING_CONDITION_SOC_INIT_ONLY)

#define DIRECT_CHARGING_FLOAT_VOLTAGE_MARGIN		20
#define DIRECT_CHARGING_FORCE_SOC_MARGIN			10

void sec_bat_reset_step_charging(struct sec_battery_info *battery)
{
	pr_info("%s\n", __func__);

	if ((battery->step_charging_type) &&
		(battery->step_charging_status >= 0) &&
		(battery->step_charging_status < battery->step_charging_step)) {

		pr_info("%s: reset state about step charging(not dc step)\n", __func__);
#if defined(CONFIG_DIRECT_CHARGING)
		if (!is_pd_apdo_wire_type(battery->cable_type))
			battery->pdata->charging_current[battery->cable_type].fast_charging_current =
				battery->pdata->step_charging_current[battery->pdata->age_step][battery->step_charging_step-1];
#endif
		if ((battery->step_charging_type & STEP_CHARGING_CONDITION_FLOAT_VOLTAGE) &&
			(battery->swelling_mode == SWELLING_MODE_NONE)) {
			int new_fv = battery->pdata->step_charging_float_voltage[battery->pdata->age_step][battery->step_charging_step-1];
			union power_supply_propval val;

			psy_do_property(battery->pdata->charger_name, get,
				POWER_SUPPLY_PROP_VOLTAGE_MAX, val);
			pr_info("%s : float voltage = %d <--> %d\n", __func__, val.intval, new_fv);

			if (val.intval != new_fv) {
				val.intval = new_fv;
				psy_do_property(battery->pdata->charger_name, set,
					POWER_SUPPLY_PROP_VOLTAGE_MAX, val);
			}
		}
	}

	battery->step_charging_status = -1;
#if defined(CONFIG_DIRECT_CHARGING)
	battery->dc_float_voltage_set = false;
#endif
}

/*
 * true: step is changed
 * false: not changed
 */
bool sec_bat_check_step_charging(struct sec_battery_info *battery)
{
	int i = 0, value = 0, step_condition = 0, lcd_status = 0;
#if defined(CONFIG_DUAL_BATTERY)
	int value_vsub = 0, step_condition_vsub = 0;
#endif
	static int curr_cnt = 0;
	static bool skip_lcd_on_changed;

#if defined(CONFIG_SEC_FACTORY)
	return false;
#endif

#if defined(CONFIG_ENG_BATTERY_CONCEPT)
	if (battery->test_charge_current)
		return false;
	if (battery->test_step_condition <= 4500)
		battery->pdata->step_charging_condition[0][0] = battery->test_step_condition;
#endif

	if (!battery->step_charging_type)
		return false;

	if (battery->siop_level < 100 || battery->lcd_status)
		lcd_status = 1;
	else
		lcd_status = 0;

	if (battery->step_charging_type & STEP_CHARGING_CONDITION_ONLINE) {
#if defined(CONFIG_DIRECT_CHARGING)
		if (is_pd_apdo_wire_type(battery->cable_type) &&
			!((battery->current_event & SEC_BAT_CURRENT_EVENT_DC_ERR) &&
			(battery->ta_alert_mode == OCP_NONE)))
			return false;

		if ((is_pd_apdo_wire_type(battery->cable_type) || is_pd_apdo_wire_type(battery->wire_status)) &&
			(battery->pdic_info.sink_status.rp_currentlvl == RP_CURRENT_LEVEL3)) {
			pr_info("%s: This cable type should be checked in dc step check\n", __func__);
			return false;
		}
#endif
		if (!is_hv_wire_type(battery->cable_type) && !(battery->cable_type == SEC_BATTERY_CABLE_PDIC) &&
			!(battery->pdic_info.sink_status.rp_currentlvl == RP_CURRENT_LEVEL3))
			return false;
	}

	pr_info("%s\n", __func__);

	if (battery->step_charging_type & STEP_CHARGING_CONDITION_CHARGE_POWER) {
		if (battery->max_charge_power < battery->step_charging_charge_power) {
			/* In case of max_charge_power falling by AICL during step-charging ongoing */
			sec_bat_reset_step_charging(battery);
			return false;
		}
	}

	if (battery->step_charging_skip_lcd_on && lcd_status) {
		if (!skip_lcd_on_changed) {
			if (battery->step_charging_status != (battery->step_charging_step - 1)) {
				battery->pdata->charging_current[battery->cable_type].fast_charging_current
					= battery->pdata->step_charging_current[battery->pdata->age_step][battery->step_charging_step - 1];

				if ((battery->step_charging_type & STEP_CHARGING_CONDITION_FLOAT_VOLTAGE) &&
					(battery->swelling_mode == SWELLING_MODE_NONE)) {
					union power_supply_propval val;

					val.intval = battery->pdata->step_charging_float_voltage[battery->pdata->age_step][battery->step_charging_step - 1];
					pr_info("%s : float voltage = %d\n", __func__, val.intval);
					psy_do_property(battery->pdata->charger_name, set,
						POWER_SUPPLY_PROP_VOLTAGE_MAX, val);
				}
				pr_info("%s : skip step charging because lcd on\n", __func__);
				skip_lcd_on_changed = true;
				return true;
			}
		}
		return false;
	}

	if (battery->step_charging_status < 0)
		i = 0;
	else
		i = battery->step_charging_status;

	step_condition = battery->pdata->step_charging_condition[battery->pdata->age_step][i];

	if (battery->step_charging_type & STEP_CHARGING_CONDITION_VOLTAGE) {
#if defined(CONFIG_DUAL_BATTERY)
		step_condition_vsub = battery->pdata->step_charging_condition_vsub[battery->pdata->age_step][i];
		value = battery->voltage_avg_main;
		value_vsub = battery->voltage_avg_sub;
#else
		value = battery->voltage_avg;
#endif
	} else if (battery->step_charging_type & STEP_CHARGING_CONDITION_SOC) {
		value = battery->capacity;
		if (lcd_status) {
			step_condition = battery->pdata->step_charging_condition[battery->pdata->age_step][i] + 15;
			curr_cnt = 0;
		}
	} else {
		return false;
	}

	while (i < battery->step_charging_step - 1) {
#if defined(CONFIG_DUAL_BATTERY)
		if (battery->step_charging_type & STEP_CHARGING_CONDITION_VOLTAGE) {
			if ((value < step_condition) && (value_vsub < step_condition_vsub))
				break;
		} else {
			if (value < step_condition)
				break;
		}
#else
		if (value < step_condition)
			break;
#endif
		i++;

		if ((battery->step_charging_type & STEP_CHARGING_CONDITION_SOC) &&
			lcd_status) {
			step_condition = battery->pdata->step_charging_condition[battery->pdata->age_step][i] + 15;
		} else {
			step_condition = battery->pdata->step_charging_condition[battery->pdata->age_step][i];
#if defined(CONFIG_DUAL_BATTERY)
			if (battery->step_charging_type & STEP_CHARGING_CONDITION_VOLTAGE)
				step_condition_vsub = battery->pdata->step_charging_condition_vsub[battery->pdata->age_step][i];
#endif

		}
		if (battery->step_charging_status != -1)
			break;
	}

	if ((i != battery->step_charging_status) || skip_lcd_on_changed) {
		/* this is only for no consuming current */
		if ((battery->step_charging_type & STEP_CHARGING_CONDITION_CURRENT_NOW) &&
			!lcd_status &&
			battery->step_charging_status >= 0) {
			int condition_curr;
			condition_curr = max(battery->current_avg, battery->current_now);
			if (condition_curr < battery->pdata->step_charging_condition_curr[battery->step_charging_status]) {
				curr_cnt++;
				pr_info("%s : cnt = %d, current avg(%d)mA < current condition(%d)mA\n", __func__,
					curr_cnt, condition_curr, battery->pdata->step_charging_condition_curr[battery->step_charging_status]);
				if (curr_cnt < 3)
					return false;
			} else {
				pr_info("%s : clear count, current avg(%d)mA >= current condition(%d)mA or"
					" < 0mA this log is for debug\n", __func__,
					condition_curr, battery->pdata->step_charging_condition_curr[battery->step_charging_status]);
				curr_cnt = 0;
				return false;
			}
		}

		pr_info("%s : prev=%d, new=%d, value=%d, current=%d, curr_cnt=%d\n", __func__,
			battery->step_charging_status, i, value, battery->pdata->step_charging_current[battery->pdata->age_step][i], curr_cnt);
		battery->pdata->charging_current[battery->cable_type].fast_charging_current = battery->pdata->step_charging_current[battery->pdata->age_step][i];
		battery->step_charging_status = i;
		skip_lcd_on_changed = false;

		if ((battery->step_charging_type & STEP_CHARGING_CONDITION_FLOAT_VOLTAGE) &&
			(battery->swelling_mode == SWELLING_MODE_NONE)) {
			union power_supply_propval val;

			pr_info("%s : float voltage = %d\n", __func__, battery->pdata->step_charging_float_voltage[battery->pdata->age_step][i]);
			val.intval = battery->pdata->step_charging_float_voltage[battery->pdata->age_step][i];
			psy_do_property(battery->pdata->charger_name, set,
				POWER_SUPPLY_PROP_VOLTAGE_MAX, val);
		}
		return true;
	}
	return false;
}

#if defined(CONFIG_DIRECT_CHARGING)
bool sec_bat_check_dc_step_charging(struct sec_battery_info *battery)
{
	int i, value;
	int step = -1, step_vol = -1, step_input = -1, step_soc = -1, soc_condition = 0;
	bool force_change_step = false;
	union power_supply_propval val;

	if (!battery->dc_step_chg_type)
		return false;

	if (battery->dc_step_chg_type & STEP_CHARGING_CONDITION_CHARGE_POWER)
		if (battery->charge_power < battery->dc_step_chg_charge_power)
			return false;

	if (battery->dc_step_chg_type & STEP_CHARGING_CONDITION_ONLINE) {
		if (!is_pd_apdo_wire_type(battery->cable_type))
			return false;
	}

    if (battery->current_event & SEC_BAT_CURRENT_EVENT_SWELLING_MODE ||
		battery->current_event & SEC_BAT_CURRENT_EVENT_HV_DISABLE ||
		((battery->current_event & SEC_BAT_CURRENT_EVENT_DC_ERR) &&
		(battery->ta_alert_mode == OCP_NONE)) ||
		battery->current_event & SEC_BAT_CURRENT_EVENT_SIOP_LIMIT ||
		battery->wc_tx_enable) {
		if (battery->step_charging_status >= 0)
			sec_bat_reset_step_charging(battery);
		return false;
	}

	if (battery->step_charging_status < 0)
		i = 0;
	else
		i = battery->step_charging_status;

	if (!(battery->dc_step_chg_type & STEP_CHARGING_CONDITION_DC_INIT)) {
		pr_info("%s : cond_vol and cond_soc are both empty\n", __func__);
		return false;
	}

	/* this is only for step enter condition and do not use STEP_CHARGING_CONDITION_SOC at the same time */
	if (battery->dc_step_chg_type & STEP_CHARGING_CONDITION_SOC_INIT_ONLY) {
		if (battery->step_charging_status < 0) {
			step_soc = i;
			value = battery->capacity;
			while (step_soc < battery->dc_step_chg_step - 1) {
				soc_condition = battery->pdata->dc_step_chg_cond_soc[battery->pdata->age_step][step_soc];
				if (value < soc_condition)
					break;
				step_soc++;
			}

			if ((step_soc < step) || (step < 0))
				step = step_soc;

			pr_info("%s : set initial step(%d) by soc\n", __func__, step_soc);
			goto check_dc_step_change;
		} else
			step_soc = battery->dc_step_chg_step - 1;
	}

	if (battery->dc_step_chg_type & STEP_CHARGING_CONDITION_SOC) {
		step_soc = i;
		value = battery->capacity;
		while (step_soc < battery->dc_step_chg_step - 1) {
			soc_condition = battery->pdata->dc_step_chg_cond_soc[battery->pdata->age_step][step_soc];
			if (battery->step_charging_status >= 0 &&
				(battery->siop_level < 100 || battery->lcd_status)) {
				soc_condition += DIRECT_CHARGING_FORCE_SOC_MARGIN;
				force_change_step = true;
			}
			if (value < soc_condition)
				break;
			step_soc++;
			if (battery->step_charging_status >= 0)
				break;
		}

		if ((step_soc < step) || (step < 0))
			step = step_soc;

		if (battery->step_charging_status < 0) {
			pr_info("%s : set initial step(%d) by soc\n", __func__, step_soc);
			goto check_dc_step_change;
		}
		if (force_change_step) {
			pr_info("%s : force check step(%d) by soc\n", __func__, step_soc);
			step_vol = step_input = step_soc;
			battery->dc_step_chg_iin_cnt = battery->pdata->dc_step_chg_iin_check_cnt;
			goto check_dc_step_change;
		}
	} else
		step_soc = battery->dc_step_chg_step - 1;

	if (battery->dc_step_chg_type & STEP_CHARGING_CONDITION_VOLTAGE) {
		step_vol = i;

#if defined(CONFIG_DUAL_BATTERY_CELL_SENSING)
		if (battery->voltage_cell_main < battery->voltage_cell_sub)
			value = battery->voltage_cell_sub;
		else
			value = battery->voltage_cell_main - battery->pdata->main_cell_margin_cc;
		pr_info("%s : mc:%dmV, sc:%dmV, val=%dmV\n", __func__, battery->voltage_cell_main, battery->voltage_cell_sub, value);
#else
		if (battery->dc_step_chg_type & STEP_CHARGING_CONDITION_FLOAT_VOLTAGE)
			value = battery->voltage_now + DIRECT_CHARGING_FLOAT_VOLTAGE_MARGIN;
		else
			value = battery->voltage_avg;
#endif
		while (step_vol < battery->dc_step_chg_step - 1) {
			if (value < battery->pdata->dc_step_chg_cond_vol[step_vol])
				break;
			step_vol++;
			if (battery->step_charging_status >= 0)
				break;
		}
		if ((step_vol < step) || (step < 0))
			step = step_vol;

		if (battery->step_charging_status < 0) {
			pr_info("%s : set initial step(%d) by vol\n", __func__, step_vol);
			goto check_dc_step_change;
		}
	} else
		step_vol = battery->dc_step_chg_step - 1;

	if (battery->dc_step_chg_type & STEP_CHARGING_CONDITION_INPUT_CURRENT) {
		step_input = i;
		psy_do_property(battery->pdata->charger_name, get,
			POWER_SUPPLY_EXT_PROP_DIRECT_CHARGER_MODE, val);
		if (val.intval != SEC_DIRECT_CHG_MODE_DIRECT_ON) {	
			pr_info("%s : dc no charging status = %d\n", __func__, val.intval);
			battery->dc_step_chg_iin_cnt = 0;
			return false;
		} else if (battery->siop_level >= 100 && !battery->lcd_status) {
			val.intval = SEC_BATTERY_IIN_MA;
			psy_do_property(battery->pdata->charger_name, get,
					POWER_SUPPLY_EXT_PROP_MEASURE_INPUT, val);
			value = val.intval;

			while (step_input < battery->dc_step_chg_step - 1) {
				if (value > battery->pdata->dc_step_chg_cond_iin[step_input])
					break;
				step_input++;

				if (battery->step_charging_status >= 0) {
					battery->dc_step_chg_iin_cnt++;
					break;
				} else {
					battery->dc_step_chg_iin_cnt = 0;
				}
			}
		} else {
			/* Do not check input current when lcd is on or siop is not 100 since there might be quite big system current */
			step_input = battery->dc_step_chg_step - 1;
		}

		if ((step_input < step) || (step < 0))
			step = step_input;
	} else
		step_input = battery->dc_step_chg_step - 1;

check_dc_step_change:
	pr_info("%s : curr_step(%d), step_vol(%d), step_soc(%d), step_input(%d), curr_cnt(%d/%d)\n",
		__func__, step, step_vol, step_soc, step_input,
		battery->dc_step_chg_iin_cnt, battery->pdata->dc_step_chg_iin_check_cnt);

	if (battery->step_charging_status < 0 ||
		(step != battery->step_charging_status && step == min(min(step_vol, step_soc), step_input))) {
		if ((battery->dc_step_chg_type & STEP_CHARGING_CONDITION_INPUT_CURRENT) &&
			(battery->step_charging_status >= 0)) {
			if (battery->dc_step_chg_iin_cnt < battery->pdata->dc_step_chg_iin_check_cnt &&
				(battery->siop_level >= 100 && !battery->lcd_status)) {
				pr_info("%s : keep step(%d), curr_cnt(%d/%d)\n",
					__func__, battery->step_charging_status,
					battery->dc_step_chg_iin_cnt, battery->pdata->dc_step_chg_iin_check_cnt);
				return false;
			}
		}

		pr_info("%s : cable(%d), soc(%d), step changed(%d->%d), current(%dmA)\n",
			__func__, battery->cable_type, battery->capacity,
			battery->step_charging_status, step, battery->pdata->dc_step_chg_val_iout[battery->pdata->age_step][step]);

		/* set charging current */
		battery->pdata->charging_current[battery->cable_type].fast_charging_current = battery->pdata->dc_step_chg_val_iout[battery->pdata->age_step][step];

		if ((battery->dc_step_chg_type & STEP_CHARGING_CONDITION_FLOAT_VOLTAGE) &&
			(battery->swelling_mode == SWELLING_MODE_NONE)) {
			if (battery->step_charging_status < 0) {
				pr_info("%s : step float voltage = %d\n", __func__, battery->pdata->dc_step_chg_val_vfloat[battery->pdata->age_step][step]);
				val.intval = battery->pdata->dc_step_chg_val_vfloat[battery->pdata->age_step][step];
				psy_do_property(battery->pdata->charger_name, set,
					POWER_SUPPLY_EXT_PROP_DIRECT_VOLTAGE_MAX, val);
			}
			battery->dc_float_voltage_set = true;
		}

		if (battery->step_charging_status < 0) {
			pr_info("%s : step input current = %d\n", __func__, battery->pdata->dc_step_chg_val_iout[battery->pdata->age_step][step] / 2);
			/* updated charging current */
			sec_bat_refresh_charging_current(battery);
		}

		battery->step_charging_status = step;
		battery->dc_step_chg_iin_cnt = 0;

		return true;
	} else {
		battery->dc_step_chg_iin_cnt = 0;
	}

	return false;
}

int sec_dc_step_charging_dt(struct sec_battery_info *battery, struct device *dev)
{
	struct device_node *np = dev->of_node;
	int ret = 0, len = 0;
	sec_battery_platform_data_t *pdata = battery->pdata;
	unsigned int i = 0, j = 0, dc_step_chg_type = 0;
	const u32 *p;
	char str[128] = {0,};
	u32 *soc_cond_temp, *dc_step_chg_val_vfloat_temp, *dc_step_chg_val_iout_temp;

	ret = of_property_read_u32(np, "battery,dc_step_chg_type",
			&battery->dc_step_chg_type);
	pr_err("%s: dc_step_chg_type 0x%x\n", __func__, battery->dc_step_chg_type);
	if (ret) {
		pr_err("%s: dc_step_chg_type is Empty\n", __func__);
		battery->dc_step_chg_type = 0;
		return -1;
	}

	ret = of_property_read_u32(np, "battery,dc_step_chg_charge_power",
			&battery->dc_step_chg_charge_power);
	if (ret) {
		pr_err("%s: dc_step_chg_charge_power is Empty\n", __func__);
		battery->dc_step_chg_charge_power = 20000;
	}	

	ret = of_property_read_u32(np, "battery,dc_step_chg_step",
			&battery->dc_step_chg_step);
	if (ret) {
		pr_err("%s: dc_step_chg_step is Empty\n", __func__);
		battery->dc_step_chg_step = 0;
		goto dc_step_charging_dt_error;
	} else {
		pr_err("%s: dc_step_chg_step is %d\n",
			__func__, battery->dc_step_chg_step);
	}

	dc_step_chg_type = battery->dc_step_chg_type;

	if (battery->dc_step_chg_type & STEP_CHARGING_CONDITION_VOLTAGE) {
		p = of_get_property(np, "battery,dc_step_chg_cond_vol", &len);
		if (!p) {
			pr_err("%s: dc_step_chg_cond_vol is Empty, type(0x%X->0x%X)\n",
				__func__, battery->dc_step_chg_type,
				battery->dc_step_chg_type & ~STEP_CHARGING_CONDITION_VOLTAGE);
			battery->dc_step_chg_type &= ~STEP_CHARGING_CONDITION_VOLTAGE;
		} else {
			len = len / sizeof(u32);

			if (len != battery->dc_step_chg_step) {
/* [dchg] TODO: do some error handling */
				pr_err("%s: len of dc_step_chg_cond_vol is not matched, len(%d/%d)\n",
					__func__, len, battery->dc_step_chg_step);
			}

			pdata->dc_step_chg_cond_vol = kzalloc(sizeof(u32) * len, GFP_KERNEL);
			ret = of_property_read_u32_array(np, "battery,dc_step_chg_cond_vol",
					pdata->dc_step_chg_cond_vol, len);
			if (ret) {
				pr_info("%s : dc_step_chg_cond_vol read fail\n", __func__);
				battery->dc_step_chg_type &= ~STEP_CHARGING_CONDITION_VOLTAGE;
			}
		}
	}

	if (battery->dc_step_chg_type & STEP_CHARGING_CONDITION_SOC ||
		battery->dc_step_chg_type & STEP_CHARGING_CONDITION_SOC_INIT_ONLY) {
		p = of_get_property(np, "battery,dc_step_chg_cond_soc", &len);
		if (!p) {
			pr_err("%s: dc_step_chg_cond_soc is Empty, type(0x%X->0x%x)\n",
				__func__, battery->dc_step_chg_type,
				battery->dc_step_chg_type & ~(STEP_CHARGING_CONDITION_SOC | STEP_CHARGING_CONDITION_SOC_INIT_ONLY));
			battery->dc_step_chg_type &= ~(STEP_CHARGING_CONDITION_SOC | STEP_CHARGING_CONDITION_SOC_INIT_ONLY);
		} else {
			len = len / sizeof(u32);

			if (len/battery->pdata->num_age_step != battery->dc_step_chg_step) {
/* [dchg] TODO: do some error handling */
				pr_err("%s: len of dc_step_charging_cond_soc is not matched, len(%d/%d)\n",
					__func__, len, battery->dc_step_chg_step);
			}
			pr_info("%s step(%d) * age_step(%d) = %d\n", __func__, battery->dc_step_chg_step, battery->pdata->num_age_step, len);

			/* get dt to buff */
			soc_cond_temp = kzalloc(sizeof(u32) * battery->dc_step_chg_step * battery->pdata->num_age_step, GFP_KERNEL);
			ret = of_property_read_u32_array(np, "battery,dc_step_chg_cond_soc",
					soc_cond_temp, battery->dc_step_chg_step * battery->pdata->num_age_step);

			/* copy buff to 2d arr */
			pdata->dc_step_chg_cond_soc = kzalloc(sizeof(u32 *) * battery->pdata->num_age_step, GFP_KERNEL);
			for (i = 0; i < battery->pdata->num_age_step; i++) {
				pdata->dc_step_chg_cond_soc[i] = kzalloc(sizeof(u32) * battery->dc_step_chg_step, GFP_KERNEL);
				for (j = 0; j < battery->dc_step_chg_step; j++) {
					pdata->dc_step_chg_cond_soc[i][j] = soc_cond_temp[i*battery->dc_step_chg_step + j];
					pr_info("%s arr = %d ", __func__, pdata->dc_step_chg_cond_soc[i][j]);
				}
				pr_info("\n");
			}

			/* if there are only 1 dimentional array of value, get the same value */
			if (battery->dc_step_chg_step * battery->pdata->num_age_step != len) {
				ret = of_property_read_u32_array(np, "battery,dc_step_chg_cond_soc",
						*pdata->dc_step_chg_cond_soc, battery->dc_step_chg_step);

				for (i = 1; i < battery->pdata->num_age_step; i++) {
					for (j = 0; j < battery->dc_step_chg_step; j++)
						pdata->dc_step_chg_cond_soc[i][j] = pdata->dc_step_chg_cond_soc[0][j];
				}
			}

			if (ret) {
				pr_info("%s : dc_step_chg_cond_soc read fail\n", __func__);
				battery->dc_step_chg_type &= ~STEP_CHARGING_CONDITION_SOC;
			}

			kfree(soc_cond_temp);

			if (battery->dc_step_chg_type & STEP_CHARGING_CONDITION_SOC &&
				battery->dc_step_chg_type & STEP_CHARGING_CONDITION_SOC_INIT_ONLY) {
				pr_info("%s : do not set SOC and SOC_INIT_ONLY at the same time\n", __func__);
				battery->dc_step_chg_type &= ~STEP_CHARGING_CONDITION_SOC;
			}
		}
	}

	if (battery->dc_step_chg_type & STEP_CHARGING_CONDITION_INPUT_CURRENT) {
		p = of_get_property(np, "battery,dc_step_chg_cond_iin", &len);
		if (!p) {
			pr_err("%s: dc_step_chg_cond_iin is Empty, type(0x%X->0x%x)\n",
				__func__, battery->dc_step_chg_type,
				battery->dc_step_chg_type & ~STEP_CHARGING_CONDITION_INPUT_CURRENT);
			battery->dc_step_chg_type &= ~STEP_CHARGING_CONDITION_INPUT_CURRENT;
		} else {
			len = len / sizeof(u32);

			if (len != battery->dc_step_chg_step) {
/* [dchg] TODO: do some error handling */
				pr_err("%s: len of dc_step_chg_cond_iin is not matched, len(%d/%d)\n",
					__func__, len, battery->dc_step_chg_step);
			}

			pdata->dc_step_chg_cond_iin = kzalloc(sizeof(u32) * len, GFP_KERNEL);
			ret = of_property_read_u32_array(np, "battery,dc_step_chg_cond_iin",
					pdata->dc_step_chg_cond_iin, len);
			if (ret) {
				pr_info("%s : dc_step_chg_cond_iin read fail\n", __func__);
				battery->dc_step_chg_type &= ~STEP_CHARGING_CONDITION_INPUT_CURRENT;
			}

			ret = of_property_read_u32(np, "battery,dc_step_chg_iin_check_cnt",
					&battery->pdata->dc_step_chg_iin_check_cnt);
			if (ret) {
				pr_err("%s: dc_step_chg_iin_check_cnt is Empty\n", __func__);
				battery->pdata->dc_step_chg_iin_check_cnt = 2;
			} else {
				pr_err("%s: dc_step_chg_iin_check_cnt is %d\n",
					__func__, battery->pdata->dc_step_chg_iin_check_cnt);
			}			
		}
	}

	if (battery->dc_step_chg_type & STEP_CHARGING_CONDITION_FLOAT_VOLTAGE) {
		p = of_get_property(np, "battery,dc_step_chg_val_vfloat", &len);
		if (!p) {
			pr_err("%s: dc_step_chg_val_vfloat is Empty, type(0x%X->0x%x)\n",
				__func__, battery->dc_step_chg_type,
				battery->dc_step_chg_type & ~STEP_CHARGING_CONDITION_FLOAT_VOLTAGE);
			battery->dc_step_chg_type &= ~STEP_CHARGING_CONDITION_FLOAT_VOLTAGE;
		} else {
			len = len / sizeof(u32);

			if (len/battery->pdata->num_age_step != battery->dc_step_chg_step) {
/* [dchg] TODO: do some error handling */
				pr_err("%s: len of dc_step_chg_val_vfloat is not matched, len(%d/%d)\n",
					__func__, len, battery->dc_step_chg_step);
			}
			pr_info("%s step(%d) * age_step(%d) = %d\n", __func__, battery->dc_step_chg_step, battery->pdata->num_age_step, len);

			dc_step_chg_val_vfloat_temp = kzalloc(sizeof(u32) * battery->dc_step_chg_step * battery->pdata->num_age_step, GFP_KERNEL);
			ret = of_property_read_u32_array(np, "battery,dc_step_chg_val_vfloat",
						dc_step_chg_val_vfloat_temp, battery->dc_step_chg_step * battery->pdata->num_age_step);

			/* copy buff to 2d arr */
			pdata->dc_step_chg_val_vfloat = kzalloc(sizeof(u32 *) * battery->pdata->num_age_step, GFP_KERNEL);
			for (i = 0; i < battery->pdata->num_age_step; i++) {
				pdata->dc_step_chg_val_vfloat[i] = kzalloc(sizeof(u32) * battery->dc_step_chg_step, GFP_KERNEL);
				for (j = 0; j < battery->dc_step_chg_step; j++) {
					pdata->dc_step_chg_val_vfloat[i][j] = dc_step_chg_val_vfloat_temp[i*battery->dc_step_chg_step + j];
					pr_info("%s arr = %d ", __func__, pdata->dc_step_chg_val_vfloat[i][j]);
				}
				pr_info("\n");
			}

			/* if there are only 1 dimentional array of value, get the same value */
			if (battery->dc_step_chg_step * battery->pdata->num_age_step != len) {
				ret = of_property_read_u32_array(np, "battery,dc_step_chg_val_vfloat",
						*pdata->dc_step_chg_val_vfloat, battery->dc_step_chg_step);

				for (i = 1; i < battery->pdata->num_age_step; i++) {
					for (j = 0; j < battery->dc_step_chg_step; j++)
						pdata->dc_step_chg_val_vfloat[i][j] = pdata->dc_step_chg_val_vfloat[0][j];
				}
			}

			if (ret) {
				pr_info("%s : dc_step_chg_val_vfloat read fail\n", __func__);
				battery->dc_step_chg_type &= ~STEP_CHARGING_CONDITION_FLOAT_VOLTAGE;
			}
			kfree(dc_step_chg_val_vfloat_temp);
		}
	}

	p = of_get_property(np, "battery,dc_step_chg_val_iout", &len);
	if (!p) {
		pr_err("%s: dc_step_chg_val_iout is Empty\n", __func__);
		battery->dc_step_chg_type = 0;
		return -1;
	} else {
		len = len / sizeof(u32);

		if (len/battery->pdata->num_age_step != battery->dc_step_chg_step) {
/* [dchg] TODO: do some error handling */
			pr_err("%s: len of dc_step_chg_val_iout is not matched, len(%d/%d)\n",
				__func__, len, battery->dc_step_chg_step);
		}
		pr_info("%s step(%d) * age_step(%d) = %d\n", __func__, battery->dc_step_chg_step, battery->pdata->num_age_step, len);

		dc_step_chg_val_iout_temp = kzalloc(sizeof(u32) * battery->dc_step_chg_step * battery->pdata->num_age_step, GFP_KERNEL);
		ret = of_property_read_u32_array(np, "battery,dc_step_chg_val_iout",
					dc_step_chg_val_iout_temp, battery->dc_step_chg_step * battery->pdata->num_age_step);

		/* copy buff to 2d arr */
		pdata->dc_step_chg_val_iout = kzalloc(sizeof(u32 *) * battery->pdata->num_age_step, GFP_KERNEL);
		for (i = 0; i < battery->pdata->num_age_step; i++) {
			pdata->dc_step_chg_val_iout[i] = kzalloc(sizeof(u32) * battery->dc_step_chg_step, GFP_KERNEL);
			for (j = 0; j < battery->dc_step_chg_step; j++) {
				pdata->dc_step_chg_val_iout[i][j] = dc_step_chg_val_iout_temp[i*battery->dc_step_chg_step + j];
				pr_info("%s arr = %d ", __func__, pdata->dc_step_chg_val_iout[i][j]);
			}
			pr_info("\n");
		}

		/* if there are only 1 dimentional array of value, get the same value */
		if (battery->dc_step_chg_step * battery->pdata->num_age_step != len) {
			ret = of_property_read_u32_array(np, "battery,dc_step_chg_val_iout",
					*pdata->dc_step_chg_val_iout, battery->dc_step_chg_step);

			for (i = 1; i < battery->pdata->num_age_step; i++) {
				for (j = 0; j < battery->dc_step_chg_step; j++)
					pdata->dc_step_chg_val_iout[i][j] = pdata->dc_step_chg_val_iout[0][j];
			}
		}

		if (ret) {
			pr_info("%s : dc_step_chg_val_iout read fail\n", __func__); 
		}
		kfree(dc_step_chg_val_iout_temp);
	}

	if (battery->dc_step_chg_type != dc_step_chg_type)	
		pr_err("%s : dc_step_chg_type is changed, type(0x%X->0x%x)\n",
			__func__, dc_step_chg_type, battery->dc_step_chg_type);

	// print dc step charging information
	for (i = 0; i < battery->dc_step_chg_step; i++) {
		memset(str, 0x0, sizeof(str));
		if (battery->dc_step_chg_type & STEP_CHARGING_CONDITION_VOLTAGE)
			sprintf(str + strlen(str), "cond_vol: %dmV, ", pdata->dc_step_chg_cond_vol[i]);
		if (battery->dc_step_chg_type & STEP_CHARGING_CONDITION_SOC)
			sprintf(str + strlen(str), "cond_soc: %d%%, ", pdata->dc_step_chg_cond_soc[battery->pdata->age_step][i]);
		if (battery->dc_step_chg_type & STEP_CHARGING_CONDITION_INPUT_CURRENT)
			sprintf(str + strlen(str), "cond_iin: %dmA, ", pdata->dc_step_chg_cond_iin[i]);
		if (battery->dc_step_chg_type & STEP_CHARGING_CONDITION_FLOAT_VOLTAGE)
			sprintf(str + strlen(str), "vfloat: %dmV, ", pdata->dc_step_chg_val_vfloat[battery->pdata->age_step][i]);

		sprintf(str + strlen(str), "iout: %dmA,", pdata->dc_step_chg_val_iout[battery->pdata->age_step][i]);
		pr_info("%s : step [%d] %s\n", __func__, i, str);
	}

#if defined(CONFIG_DUAL_BATTERY_CELL_SENSING)
	ret = of_property_read_u32(np, "battery,main_cell_margin_cc",
			&battery->pdata->main_cell_margin_cc);
	if (ret) {
		pr_err("%s: main_cell_margin_cc is Empty\n", __func__);
		battery->pdata->main_cell_margin_cc = 100;
	}
#endif

	return 0;

dc_step_charging_dt_error:
	return -1;
}
#endif

#if defined(CONFIG_BATTERY_AGE_FORECAST)
void sec_bat_set_aging_info_step_charging(struct sec_battery_info *battery)
{
#if defined(CONFIG_DIRECT_CHARGING)
	union power_supply_propval val;
	int i = 0;
#endif

	if (battery->step_charging_type) {
		if (battery->step_charging_type & STEP_CHARGING_CONDITION_FLOAT_VOLTAGE)
			battery->pdata->step_charging_float_voltage[battery->pdata->age_step][battery->step_charging_step-1] = battery->pdata->chg_float_voltage;

		dev_info(battery->dev, "%s: float_v(%d)\n",
			__func__, battery->pdata->step_charging_float_voltage[battery->pdata->age_step][battery->step_charging_step-1]);
	}
#if defined(CONFIG_DIRECT_CHARGING)
	for (i = 0; i < battery->dc_step_chg_step; i++) {
		if (battery->dc_step_chg_type & STEP_CHARGING_CONDITION_FLOAT_VOLTAGE)
			if (battery->pdata->dc_step_chg_val_vfloat[battery->pdata->age_step][i] > battery->pdata->chg_float_voltage)
				battery->pdata->dc_step_chg_val_vfloat[battery->pdata->age_step][i] = battery->pdata->chg_float_voltage;
		if (battery->dc_step_chg_type & STEP_CHARGING_CONDITION_VOLTAGE)
			if (battery->pdata->dc_step_chg_cond_vol[i] > battery->pdata->chg_float_voltage)
				battery->pdata->dc_step_chg_cond_vol[i] = battery->pdata->chg_float_voltage;
		if ((battery->dc_step_chg_type & STEP_CHARGING_CONDITION_INPUT_CURRENT) && (i < battery->dc_step_chg_step - 1))
			battery->pdata->dc_step_chg_cond_iin[i] = battery->pdata->dc_step_chg_val_iout[battery->pdata->age_step][i+1] / 2;
	}

	for (i = 0; i < battery->dc_step_chg_step; i++) {
		dev_info(battery->dev, "%s: cond_vol: %dmV, vfloat: %dmV, cond_iin: %dmA, iout: %dmA\n", __func__,
			battery->dc_step_chg_type & STEP_CHARGING_CONDITION_VOLTAGE ? battery->pdata->dc_step_chg_cond_vol[i] : 0,
			battery->dc_step_chg_type & STEP_CHARGING_CONDITION_FLOAT_VOLTAGE ? battery->pdata->dc_step_chg_val_vfloat[battery->pdata->age_step][i] : 0,
			battery->dc_step_chg_type & STEP_CHARGING_CONDITION_INPUT_CURRENT ? battery->pdata->dc_step_chg_cond_iin[i] : 0,
			battery->pdata->dc_step_chg_val_iout[battery->pdata->age_step][i]);
	}

	if (battery->dc_step_chg_type & STEP_CHARGING_CONDITION_FLOAT_VOLTAGE) {
		val.intval = battery->pdata->dc_step_chg_val_vfloat[battery->pdata->age_step][battery->dc_step_chg_step-1];
		psy_do_property(battery->pdata->charger_name, set,
			POWER_SUPPLY_EXT_PROP_DIRECT_FLOAT_MAX, val);
	}

	sec_bat_reset_step_charging(battery);
	sec_bat_check_dc_step_charging(battery);
#endif
}
#endif

void sec_step_charging_init(struct sec_battery_info *battery, struct device *dev)
{
	struct device_node *np = dev->of_node;
	int ret, len;
	sec_battery_platform_data_t *pdata = battery->pdata;
	unsigned int i = 0, j = 0;
	const u32 *p;
	u32 *soc_cond_temp, *step_charging_float_voltage_temp, *step_charging_current_temp;

	ret = of_property_read_u32(np, "battery,step_charging_type",
			&battery->step_charging_type);
	pr_err("%s: step_charging_type 0x%x\n", __func__, battery->step_charging_type);
	if (ret) {
		pr_err("%s: step_charging_type is Empty\n", __func__);
		battery->step_charging_type = 0;
	}

	battery->step_charging_skip_lcd_on = of_property_read_bool(np,
						     "battery,step_charging_skip_lcd_on");

	if (battery->step_charging_type) {
		ret = of_property_read_u32(np, "battery,step_charging_step",
				&battery->step_charging_step);
		if (ret) {
			pr_err("%s: step_charging_step is Empty\n", __func__);
			battery->step_charging_step = 0;
		} else {
			pr_err("%s: step_charging_step is %d\n",
				__func__, battery->step_charging_step);
		}

		ret = of_property_read_u32(np, "battery,step_charging_charge_power",
				&battery->step_charging_charge_power);
		if (ret) {
			pr_err("%s: step_charging_charge_power is Empty\n", __func__);
			battery->step_charging_charge_power = 20000;
		}

		p = of_get_property(np, "battery,step_charging_condition", &len);
		if (!p) {
			battery->step_charging_step = 0;
		} else {
			len = len / sizeof(u32);

			pr_info("%s step(%d) * age_step(%d) = %d\n", __func__, battery->step_charging_step, battery->pdata->num_age_step, len);

			/* get dt to buff */
			soc_cond_temp = kzalloc(sizeof(u32) * battery->step_charging_step * battery->pdata->num_age_step, GFP_KERNEL);
			ret = of_property_read_u32_array(np, "battery,step_charging_condition",
					soc_cond_temp, battery->step_charging_step * battery->pdata->num_age_step);

			/* copy buff to 2d arr */
			pdata->step_charging_condition = kzalloc(sizeof(u32 *) * battery->pdata->num_age_step, GFP_KERNEL);
			for (i = 0; i < battery->pdata->num_age_step; i++) {
				pdata->step_charging_condition[i] = kzalloc(sizeof(u32) * battery->step_charging_step, GFP_KERNEL);
				for (j = 0; j < battery->step_charging_step; j++) {
					pdata->step_charging_condition[i][j] = soc_cond_temp[i*battery->step_charging_step + j];
					pr_info("%s arr = %d ", __func__, pdata->step_charging_condition[i][j]);
				}
				pr_info("\n");
			}

			/* if there are only 1 dimentional array of value, get the same value */
			if (battery->step_charging_step * battery->pdata->num_age_step != len) {
				ret = of_property_read_u32_array(np, "battery,step_charging_condition",
					*pdata->step_charging_condition, battery->step_charging_step);
				for (i = 0; i < battery->pdata->num_age_step; i++) {
					for (j = 0; j < battery->step_charging_step; j++)
						pdata->step_charging_condition[i][j] = pdata->step_charging_condition[0][j];
				}
			}
			if (ret) {
				pr_info("%s : step_charging_condition read fail\n", __func__);
				battery->step_charging_step = 0;
			}
#if defined(CONFIG_DUAL_BATTERY)
			if (battery->step_charging_type & STEP_CHARGING_CONDITION_VOLTAGE) {
				/* get dt to buff */
				ret = of_property_read_u32_array(np, "battery,step_charging_condition_vsub",
						soc_cond_temp, battery->step_charging_step * battery->pdata->num_age_step);

				/* copy buff to 2d arr */
				pdata->step_charging_condition_vsub = kzalloc(sizeof(u32 *) * battery->pdata->num_age_step, GFP_KERNEL);
				for (i = 0; i < battery->pdata->num_age_step; i++) {
					pdata->step_charging_condition_vsub[i] = kzalloc(sizeof(u32) * battery->step_charging_step, GFP_KERNEL);
					for (j = 0; j < battery->step_charging_step; j++) {
						pdata->step_charging_condition_vsub[i][j] = soc_cond_temp[i*battery->step_charging_step + j];
						pr_info("%s arr = %d ", __func__, pdata->step_charging_condition_vsub[i][j]);
					}
					pr_info("\n");
				}

				/* if there are only 1 dimentional array of value, get the same value */
				if (battery->step_charging_step * battery->pdata->num_age_step != len) {
					ret = of_property_read_u32_array(np, "battery,step_charging_condition_vsub",
						*pdata->step_charging_condition_vsub, battery->step_charging_step);
					for (i = 0; i < battery->pdata->num_age_step; i++) {
						for (j = 0; j < battery->step_charging_step; j++)
							pdata->step_charging_condition_vsub[i][j] = pdata->step_charging_condition_vsub[0][j];
					}
				}
				if (ret)
					pr_info("%s : step_charging_condition_vsub read fail\n", __func__);
			}
#endif
			kfree(soc_cond_temp);

			p = of_get_property(np, "battery,step_charging_condition_curr", &len);
			if (!p) {
				pr_err("%s: step_charging_condition_curr is Empty\n", __func__);
			} else {
				len = len / sizeof(u32);
				pdata->step_charging_condition_curr = kzalloc(sizeof(u32) * len, GFP_KERNEL);
				ret = of_property_read_u32_array(np, "battery,step_charging_condition_curr",
						pdata->step_charging_condition_curr, len);
				if (ret) {
					pr_info("%s : step_charging_condition_curr read fail\n", __func__);
					battery->step_charging_step = 0;
				}
			}

			p = of_get_property(np, "battery,step_charging_float_voltage", &len);
			if (!p) {
				pr_err("%s: step_charging_float_voltage is Empty\n", __func__);
			} else {
				len = len / sizeof(u32);

				pr_info("%s step(%d) * age_step(%d) = %d\n", __func__, battery->step_charging_step, battery->pdata->num_age_step, len);

				step_charging_float_voltage_temp = kzalloc(sizeof(u32) * battery->step_charging_step * battery->pdata->num_age_step, GFP_KERNEL);
				ret = of_property_read_u32_array(np, "battery,step_charging_float_voltage",
					step_charging_float_voltage_temp, battery->step_charging_step * battery->pdata->num_age_step);

				/* copy buff to 2d arr */
				pdata->step_charging_float_voltage = kzalloc(sizeof(u32 *) * battery->pdata->num_age_step, GFP_KERNEL);
				for (i = 0; i < battery->pdata->num_age_step; i++) {
					pdata->step_charging_float_voltage[i] = kzalloc(sizeof(u32) * battery->step_charging_step, GFP_KERNEL);
					for (j = 0; j < battery->step_charging_step; j++) {
						pdata->step_charging_float_voltage[i][j] = step_charging_float_voltage_temp[i*battery->step_charging_step + j];
						pr_info("%s arr = %d ", __func__, pdata->step_charging_float_voltage[i][j]);
					}
					pr_info("\n");
				}

				/* if there are only 1 dimentional array of value, get the same value */
				if (battery->step_charging_step * battery->pdata->num_age_step != len) {
					ret = of_property_read_u32_array(np, "battery,step_charging_float_voltage",
						*pdata->step_charging_float_voltage, battery->step_charging_step);

					for (i = 1; i < battery->pdata->num_age_step; i++) {
						for (j = 0; j < battery->step_charging_step; j++) {
							pdata->step_charging_float_voltage[i][j] = pdata->step_charging_float_voltage[0][j];
							pr_info("%s arr = %d ", __func__, pdata->step_charging_float_voltage[i][j]);
						}
					}
				}

				if (ret)
					pr_info("%s : step_charging_float_voltage read fail\n", __func__);

				kfree(step_charging_float_voltage_temp);
			}

			p = of_get_property(np, "battery,step_charging_current", &len);
			if (!p) {
				pr_err("%s: step_charging_current is Empty\n", __func__);
			} else {
				len = len / sizeof(u32);
				step_charging_current_temp = kzalloc(sizeof(u32) * battery->step_charging_step * battery->pdata->num_age_step, GFP_KERNEL);
				ret = of_property_read_u32_array(np, "battery,step_charging_current",
					step_charging_current_temp, battery->step_charging_step * battery->pdata->num_age_step);

				/* copy buff to 2d arr */
				pdata->step_charging_current = kzalloc(sizeof(u32 *) * battery->pdata->num_age_step, GFP_KERNEL);
				for (i = 0; i < battery->pdata->num_age_step; i++) {
					pdata->step_charging_current[i] = kzalloc(sizeof(u32) * battery->step_charging_step, GFP_KERNEL);
					for (j = 0; j < battery->step_charging_step; j++) {
						pdata->step_charging_current[i][j] = step_charging_current_temp[i*battery->step_charging_step + j];
						pr_info("%s arr = %d ", __func__, pdata->step_charging_current[i][j]);
					}
					pr_info("\n");
				}

				/* if there are only 1 dimentional array of value, get the same value */
				if (battery->step_charging_step * battery->pdata->num_age_step != len) {
					ret = of_property_read_u32_array(np, "battery,step_charging_current",
						*pdata->step_charging_current, battery->step_charging_step);

					for (i = 1; i < battery->pdata->num_age_step; i++) {
						for (j = 0; j < battery->step_charging_step; j++) {
							pdata->step_charging_current[i][j] = pdata->step_charging_current[0][j];
							pr_info("%s arr = %d ", __func__, pdata->step_charging_current[i][j]);
						}
					}
				}

				if (ret)
					pr_info("%s : step_charging_current read fail\n", __func__);

				kfree(step_charging_current_temp);
			}
		}
	}
#if defined(CONFIG_DIRECT_CHARGING)
	sec_dc_step_charging_dt(battery, dev);
#endif
}
