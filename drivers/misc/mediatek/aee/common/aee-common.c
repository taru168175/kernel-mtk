/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/bug.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <mt-plat/aee.h>
#include <linux/kgdb.h>
#include <linux/kdb.h>
#include <linux/utsname.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/clock.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#ifdef CONFIG_MTK_WATCHDOG
#include <mtk_wd_api.h>
#endif
#include "aee-common.h"
#include "aed.h"
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <mt-plat/mrdump.h>
#include <mrdump_private.h>

static struct aee_kernel_api *g_aee_api;
#define KERNEL_REPORT_LENGTH 344

struct aee_oops *aee_oops_create(enum AE_DEFECT_ATTR attr,
		enum AE_EXP_CLASS clazz, const char *module)
{
	struct aee_oops *oops = kzalloc(sizeof(struct aee_oops), GFP_ATOMIC);

	if (oops == NULL)
		return NULL;
	oops->attr = attr;
	oops->clazz = clazz;
	if (module != NULL)
		strlcpy(oops->module, module, sizeof(oops->module));
	else
		strlcpy(oops->module, "N/A", sizeof(oops->module));
	strlcpy(oops->backtrace, "N/A", sizeof(oops->backtrace));
	strlcpy(oops->process_path, "N/A", sizeof(oops->process_path));

	return oops;
}
EXPORT_SYMBOL(aee_oops_create);

void aee_oops_free(struct aee_oops *oops)
{
	kfree(oops->console);
	kfree(oops->android_main);
	kfree(oops->android_radio);
	kfree(oops->android_system);
	kfree(oops->userspace_info);
	kfree(oops->mmprofile);
	kfree(oops->mini_rdump);
	vfree(oops->userthread_stack.Userthread_Stack);
	vfree(oops->userthread_maps.Userthread_maps);
	kfree(oops);
	pr_notice("%s\n", __func__);
}
EXPORT_SYMBOL(aee_oops_free);

void aee_register_api(struct aee_kernel_api *aee_api)
{
	g_aee_api = aee_api;
}
EXPORT_SYMBOL(aee_register_api);

void aee_disable_api(void)
{
	if (g_aee_api) {
		pr_debug("disable aee kernel api");
		g_aee_api = NULL;
	}
}
EXPORT_SYMBOL(aee_disable_api);

void aee_kernel_exception_api_func(const char *file, const int line,
		const int db_opt, const char *module, const char *msg, ...)
{
	char msgbuf[KERNEL_REPORT_LENGTH];
	int offset = 0;
	va_list args;

	va_start(args, msg);
	offset += snprintf(msgbuf, KERNEL_REPORT_LENGTH, "<%s:%d> ", file,
				line);
	offset += vsnprintf(msgbuf + offset, KERNEL_REPORT_LENGTH - offset, msg,
				args);
	if (g_aee_api && g_aee_api->kernel_reportAPI)
		g_aee_api->kernel_reportAPI(AE_DEFECT_EXCEPTION, db_opt, module,
				msgbuf);
	else
		pr_notice("AEE kernel exception: %s", msgbuf);
	va_end(args);
}
EXPORT_SYMBOL(aee_kernel_exception_api_func);

void aee_kernel_warning_api_func(const char *file, const int line,
		const int db_opt, const char *module, const char *msg, ...)
{
	char msgbuf[KERNEL_REPORT_LENGTH];
	int offset = 0;
	va_list args;

	va_start(args, msg);
	offset += snprintf(msgbuf, KERNEL_REPORT_LENGTH, "<%s:%d> ", file,
			line);
	offset += vsnprintf(msgbuf + offset, KERNEL_REPORT_LENGTH - offset,
			msg, args);

	if (g_aee_api && g_aee_api->kernel_reportAPI) {
		if (module && strstr(module,
			"maybe have other hang_detect KE DB"))
			g_aee_api->kernel_reportAPI(AE_DEFECT_FATAL, db_opt,
				module, msgbuf);
		else
			g_aee_api->kernel_reportAPI(AE_DEFECT_WARNING, db_opt,
				module, msgbuf);
	} else {
		pr_notice("AEE kernel warning: %s", msgbuf);
	}
	va_end(args);
}
EXPORT_SYMBOL(aee_kernel_warning_api_func);

void aee_kernel_reminding_api(const char *file, const int line,
		const int db_opt, const char *module, const char *msg, ...)
{
	char msgbuf[KERNEL_REPORT_LENGTH];
	int offset = 0;
	va_list args;

	va_start(args, msg);
	offset += snprintf(msgbuf, KERNEL_REPORT_LENGTH, "<%s:%d> ",
				file, line);
	offset += vsnprintf(msgbuf + offset, KERNEL_REPORT_LENGTH - offset,
				msg, args);
	if (g_aee_api && g_aee_api->kernel_reportAPI)
		g_aee_api->kernel_reportAPI(AE_DEFECT_REMINDING, db_opt,
				module, msgbuf);
	else
		pr_notice("AEE kernel reminding: %s", msgbuf);
	va_end(args);
}
EXPORT_SYMBOL(aee_kernel_reminding_api);

void aed_md_exception_api(const int *log, int log_size, const int *phy,
			int phy_size, const char *detail, const int db_opt)
{
#ifdef CONFIG_MTK_AEE_AED
	pr_debug("%s\n", __func__);
	if (g_aee_api) {
		if (g_aee_api->md_exception) {
			g_aee_api->md_exception("modem", log, log_size, phy,
					phy_size, detail, db_opt);
		} else {
			pr_debug("g_aee_api->md_exception = 0x%p\n",
					g_aee_api->md_exception);
		}
	} else {
		pr_debug("g_aee_api is null\n");
	}
	pr_debug("%s out\n", __func__);
#endif
}
EXPORT_SYMBOL(aed_md_exception_api);

void aed_md32_exception_api(const int *log, int log_size, const int *phy,
			int phy_size, const char *detail, const int db_opt)
{
#ifdef CONFIG_MTK_AEE_AED
	pr_debug("%s\n", __func__);
	if (g_aee_api) {
		if (g_aee_api->md_exception) {
			g_aee_api->md_exception("md32", log, log_size, phy,
					phy_size, detail, db_opt);
		} else {
			pr_debug("g_aee_api->md32_exception = 0x%p\n",
					g_aee_api->md32_exception);
		}
	} else {
		pr_debug("g_aee_api is null\n");
	}
	pr_debug("%s out\n", __func__);
#endif
}
EXPORT_SYMBOL(aed_md32_exception_api);

void aed_scp_exception_api(const int *log, int log_size, const int *phy,
			int phy_size, const char *detail, const int db_opt)
{
#ifdef CONFIG_MTK_AEE_AED
	pr_debug("%s\n", __func__);
	if (g_aee_api) {
		if (g_aee_api->md_exception) {
			g_aee_api->md_exception("scp", log, log_size, phy,
					phy_size, detail, db_opt);
		} else {
			pr_debug("g_aee_api->scp_exception = 0x%p\n",
					g_aee_api->scp_exception);
		}
	} else {
		pr_debug("g_aee_api is null\n");
	}
	pr_debug("%s out\n", __func__);
#endif
}
EXPORT_SYMBOL(aed_scp_exception_api);


void aed_combo_exception_api(const int *log, int log_size, const int *phy,
			int phy_size, const char *detail, const int db_opt)
{
#ifdef CONFIG_MTK_AEE_AED
	pr_debug("aed_combo_exception\n");
	if (g_aee_api) {
		if (g_aee_api->combo_exception) {
			g_aee_api->combo_exception("combo", log, log_size, phy,
					phy_size, detail, db_opt);
		} else {
			pr_debug("g_aee_api->combo_exception = 0x%p\n",
					g_aee_api->combo_exception);
		}
	} else {
		pr_debug("g_aee_api is null\n");
	}
	pr_debug("aed_combo_exception out\n");
#endif
}
EXPORT_SYMBOL(aed_combo_exception_api);

void aed_common_exception_api(const char *assert_type, const int *log,
			int log_size, const int *phy, int phy_size,
			const char *detail, const int db_opt)
{
#ifdef CONFIG_MTK_AEE_AED
	pr_debug("%s\n", __func__);
	if (g_aee_api) {
		if (g_aee_api->common_exception) {
			g_aee_api->common_exception(assert_type, log, log_size,
					phy, phy_size, detail, db_opt);
		} else {
			pr_debug("g_aee_api->common_exception = 0x%p\n",
					g_aee_api->common_exception);
		}
	} else {
		pr_debug("g_aee_api is null\n");
	}
	pr_debug("%s out\n", __func__);
#endif
}
EXPORT_SYMBOL(aed_common_exception_api);

int aee_is_printk_too_much(const char *module)
{
#ifdef CONFIG_MTK_AEE_AED
	if (strstr(module, "intk too much"))
		return 1;
#endif
	return 0;
}
EXPORT_SYMBOL(aee_is_printk_too_much);

char sram_printk_buf[256];

void aee_sram_printk(const char *fmt, ...)
{
#ifdef CONFIG_MTK_RAM_CONSOLE
	unsigned long long t;
	unsigned long nanosec_rem;
	va_list args;
	int r, tlen;

	va_start(args, fmt);

	preempt_disable();
	t = cpu_clock(get_HW_cpuid());
	nanosec_rem = do_div(t, 1000000000);
	tlen = sprintf(sram_printk_buf, ">%5lu.%06lu< ", (unsigned long)t,
			nanosec_rem / 1000);

	if (tlen < 0) {
		preempt_enable();
		return;
	}

	r = vscnprintf(sram_printk_buf + tlen, sizeof(sram_printk_buf) - tlen,
			fmt, args);

	ram_console_write(NULL, sram_printk_buf, r + tlen);
	preempt_enable();
	va_end(args);
#endif
}
EXPORT_SYMBOL(aee_sram_printk);

int aee_is_enable(void)
{
	struct device_node *node;
	const char *aee_enable;
	int ret = 0;

	node = of_find_node_by_path("/chosen");
	if (node) {
		if (of_property_read_string(node, "aee,enable", &aee_enable) == 0) {
			if (strnstr(aee_enable, "mini", 4))
				ret = 1;
			else if (strnstr(aee_enable, "full", 4))
				ret = 2;
		}
		of_node_put(node);
	} else {
		pr_notice("%s: Can't find chosen node\n", __func__);
	}

	return ret;
}
EXPORT_SYMBOL(aee_is_enable);

int aed_get_status(void)
{
	int mode;

	mode = aee_get_mode();
	if ((mode >= AEE_MODE_MTK_ENG) && (mode <= AEE_MODE_CUSTOMER_USER))
		return 1;
	return 0;
}
EXPORT_SYMBOL(aed_get_status);

static int __init aee_common_init(void)
{
	int ret = 0;
	return ret;
}

static void __exit aee_common_exit(void)
{
}

module_init(aee_common_init);
module_exit(aee_common_exit);
