/**
 * @file oprofpp_options.h
 * Command-line options
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <moz@compsoc.man.ac.uk>
 * @author Philippe Elie <phil_el@wanadoo.fr>
 */
 
#ifndef OPROFPP_OPTIONS_H
#define OPROFPP_OPTIONS_H

#include <string>
#include <vector>

#include "outsymbflag.h"

/// command line option values
namespace options {
	extern int counter_mask;
	extern int sort_by_counter;
	extern std::string gprof_file;
	extern std::string symbol;
	extern bool list_symbols;
	extern bool output_linenr_info;
	extern bool reverse_sort;
	extern bool show_shared_libs;
	extern OutSymbFlag output_format_flags;
	extern bool list_all_symbols_details;
	/** control the behavior of demangle_symbol() */
	extern bool demangle;
	/** a sample filename */
	extern std::string sample_file;
	/** an image filename */
	extern std::string image_file;
	/** the set of symbols to ignore */
	extern std::vector<std::string> exclude_symbols;
};

/**
 * get_options - process command line
 * @param argc program arg count
 * @param argv program arg array
 * @return an additional argument
 *
 * Process the arguments, fatally complaining on
 * error. This fills the values in the above options
 * namespace.
 */
std::string const get_options(int argc, char const **argv);
 
#endif // OPROFPP_OPTIONS_H
