/**
 * @file opd_stats.c
 * Management of daemon statistics
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <moz@compsoc.man.ac.uk>
 * @author Philippe Elie <phil_el@wanadoo.fr>
 */

#include "opd_stats.h"
#include "opd_proc.h"

#include "op_get_time.h"

#include <stdlib.h>
#include <stdio.h>

extern int nr_images;

unsigned long opd_stats[OPD_MAX_STATS];

/**
 * opd_print_stats - print out latest statistics
 */
void opd_print_stats(void)
{
	printf("%s\n", op_get_time());
	printf("Nr. proc struct: %d\n", opd_get_nr_procs());
	printf("Nr. image struct: %d\n", nr_images);
	printf("Nr. kernel samples: %lu\n", opd_stats[OPD_KERNEL]);
	printf("Nr. modules samples: %lu\n", opd_stats[OPD_MODULE]);
	printf("Nr. modules samples lost: %lu\n",
		opd_stats[OPD_LOST_MODULE]);
	printf("Nr. samples lost due to no process information: %lu\n",
		opd_stats[OPD_LOST_PROCESS]);
	printf("Nr. process samples in user-space: %lu\n", opd_stats[OPD_PROCESS]);
	printf("Nr. samples lost due to no map information: %lu\n",
		opd_stats[OPD_LOST_MAP_PROCESS]);
	if (opd_stats[OPD_PROC_QUEUE_ACCESS]) {
		printf("Average depth of search of proc queue: %f\n",
			(double)opd_stats[OPD_PROC_QUEUE_DEPTH]
			/ (double)opd_stats[OPD_PROC_QUEUE_ACCESS]);
	}
	if (opd_stats[OPD_MAP_ARRAY_ACCESS]) {
		printf("Average depth of iteration through mapping array: %f\n",
			(double)opd_stats[OPD_MAP_ARRAY_DEPTH]
			/ (double)opd_stats[OPD_MAP_ARRAY_ACCESS]);
	}
	printf("Nr. sample dumps: %lu\n", opd_stats[OPD_DUMP_COUNT]);
	printf("Nr. samples total: %lu\n", opd_stats[OPD_SAMPLES]);
	printf("Nr. notifications: %lu\n", opd_stats[OPD_NOTIFICATIONS]);
	fflush(stdout);
}