/* $Id: op_init.c,v 1.6 2001/08/19 20:09:17 movement Exp $ */
/* COPYRIGHT (C) 2000 THE VICTORIA UNIVERSITY OF MANCHESTER and John Levon
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "oprofile.h"

EXPORT_NO_SYMBOLS;

extern int cpu_type;

static int __init hw_ok(void)
{
	/* we want to include all P6 processors (i.e. > Pentium Classic,
	 * < Pentium IV
	 */
	if ((current_cpu_data.x86_vendor != X86_VENDOR_INTEL &&
	    current_cpu_data.x86 != 6) ||
		(current_cpu_data.x86_vendor != X86_VENDOR_AMD &&
		 current_cpu_data.x86 != 6)) {
		printk(KERN_ERR "oprofile: not an Intel P6 or AMD Athlon processor. Sorry.\n");
		return CPU_NO_GOOD;
	}

	/* 0 if PPro, 1 if PII, 2 if PIII, 3 if Athlon */
	if (current_cpu_data.x86_vendor == X86_VENDOR_AMD)
		cpu_type = CPU_ATHLON;
	else
		cpu_type = (current_cpu_data.x86_model > 5) ? CPU_PIII :
			(current_cpu_data.x86_model > 2);
	return cpu_type;
}

int __init stub_init(void)
{
	if (hw_ok() == CPU_NO_GOOD)
		return -EINVAL;

	return oprof_init();
}

void __exit stub_exit(void)
{
	oprof_exit();
	return;
}

module_init(stub_init);
module_exit(stub_exit);
