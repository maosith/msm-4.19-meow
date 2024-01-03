/*
 * Copyright (c) 2018 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Device Tree binding constants for Exynos9810 devfreq
 */

#ifndef _DT_BINDINGS_EXYNOS_9830_DEVFREQ_H
#define _DT_BINDINGS_EXYNOS_9830_DEVFREQ_H
/* DEVFREQ TYPE LIST */
#define DEVFREQ_MIF			0
#define DEVFREQ_INT			1
#define DEVFREQ_DISP		2
#define DEVFREQ_CAM			3
#define DEVFREQ_INTCAM		4
#define DEVFREQ_AUD			5
#define DEVFREQ_DSP			6
#define DEVFREQ_DNC			7
#define DEVFREQ_MFC			8
#define DEVFREQ_NPU			9
#define DEVFREQ_TNR			10
#define DEVFREQ_TYPE_END	11

/* ESS FLAG LIST */
#define ESS_FLAG_INT	3
#define ESS_FLAG_MIF	4
#define ESS_FLAG_ISP	5
#define ESS_FLAG_DISP	6
#define ESS_FLAG_INTCAM	7
#define ESS_FLAG_AUD	8
#define ESS_FLAG_DSP	9
#define ESS_FLAG_DNC	10
#define ESS_FLAG_MFC	11
#define ESS_FLAG_NPU	12
#define ESS_FLAG_TNR	13
#define ESS_FLAG_G3D	14  /* G3D doesn't use DEVFREQ, but this value is added here for the consistency */

/* DEVFREQ GOV TYPE */
#define SIMPLE_INTERACTIVE 0

#endif
