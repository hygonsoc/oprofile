/**
 * @file pe_profiling/operf_counter.cpp
 * C++ class implementation that abstracts the user-to-kernel interface
 * for using Linux Performance Events Subsystem.
 *
 * @remark Copyright 2011 OProfile authors
 * @remark Read the file COPYING
 *
 * Created on: Dec 7, 2011
 * @author Maynard Johnson
 * (C) Copyright IBM Corp. 2011
 *
 * Modified by Maynard Johnson <maynardj@us.ibm.com>
 * (C) Copyright IBM Corporation 2012
 *
*/

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <iostream>
#include <stdlib.h>
#include "op_events.h"
#include "operf_counter.h"
#include "op_abi.h"
#include "cverb.h"
#include "operf_process_info.h"
#include "op_libiberty.h"
#include "operf_stats.h"


using namespace std;
using namespace OP_perf_utils;


volatile bool quit;
volatile bool read_quit;
int sample_reads;
int num_mmap_pages;
unsigned int pagesize;
verbose vperf("perf_events");

extern bool first_time_processing;
extern bool throttled;

namespace {

vector<string> event_names;

static const char *__op_magic = "OPFILE";

#define OP_MAGIC	(*(u64 *)__op_magic)

}  // end anonymous namespace

operf_counter::operf_counter(operf_event_t evt,  bool enable_on_exec, bool do_cg,
                             bool separate_cpu)
{
	memset(&attr, 0, sizeof(attr));
	attr.size = sizeof(attr);
	attr.sample_type = OP_BASIC_SAMPLE_FORMAT;
	if (do_cg)
		attr.sample_type |= PERF_SAMPLE_CALLCHAIN;
	if (separate_cpu)
		attr.sample_type |= PERF_SAMPLE_CPU;
	attr.type = PERF_TYPE_RAW;
	attr.config = evt.evt_code;
	attr.sample_period = evt.count;
	attr.inherit = 1;
	attr.enable_on_exec = enable_on_exec ? 1 : 0;
	attr.disabled  = 1;
	attr.exclude_idle = 0;
	attr.exclude_kernel = evt.no_kernel;
	attr.exclude_hv = evt.no_hv;
	attr.read_format = PERF_FORMAT_ID;
	event_name = evt.name;
}

operf_counter::~operf_counter() {
}


int operf_counter::perf_event_open(pid_t ppid, int cpu, unsigned event, operf_record * rec)
{
	struct {
		u64 count;
		u64 id;
	} read_data;

	if (event == 0) {
		attr.mmap = 1;
		attr.comm = 1;
	}
	fd = op_perf_event_open(&attr, ppid, cpu, -1, 0);
	if (fd < 0) {
		int ret = -1;
		cverb << vperf << "perf_event_open failed: " << strerror(errno) << endl;
		if (errno == EBUSY) {
			cerr << "The performance monitoring hardware reports EBUSY. Is another profiling tool in use?" << endl
			     << "On some architectures, tools such as oprofile and perf being used in system-wide "
			     << "mode can cause this problem." << endl;
			ret = OP_PERF_HANDLED_ERROR;
		} else if (errno == ESRCH) {
			cerr << "!!!! No samples collected !!!" << endl;
			cerr << "The target program/command ended before profiling was started." << endl;
			ret = OP_PERF_HANDLED_ERROR;
		} else {
			cerr << "perf_event_open failed with " << strerror(errno) << endl;
		}
		return ret;
	}
	if (read(fd, &read_data, sizeof(read_data)) == -1) {
		perror("Error reading perf_event fd");
		return -1;
	}
	rec->register_perf_event_id(event, read_data.id, attr);

	cverb << vperf << "perf_event_open returning fd " << fd << endl;
	return fd;
}

operf_record::~operf_record()
{
	cverb << vperf << "operf_record::~operf_record()" << endl;
	if (poll_data)
		delete[] poll_data;
	close(output_fd);
	for (int i = 0; i < samples_array.size(); i++) {
		struct mmap_data *md = &samples_array[i];
		munmap(md->base, (num_mmap_pages + 1) * pagesize);
	}
	samples_array.clear();
	evts.clear();
	perfCounters.clear();
}

operf_record::operf_record(int out_fd, bool sys_wide, pid_t the_pid, bool pid_running,
                           vector<operf_event_t> & events, vmlinux_info_t vi, bool do_cg,
                           bool separate_by_cpu)
{
	int flags = O_CREAT|O_RDWR|O_TRUNC;
	struct sigaction sa;
	sigset_t ss;
	vmlinux_file = vi.image_name;
	kernel_start = vi.start;
	kernel_end = vi.end;
	pid = the_pid;
	pid_started = pid_running;
	system_wide = sys_wide;
	callgraph = do_cg;
	separate_cpu = separate_by_cpu;
	total_bytes_recorded = 0;
	poll_count = 0;
	evts = events;
	valid = false;
	poll_data = NULL;

	if (system_wide && (pid != -1 || pid_started))
		return;  // object is not valid

	output_fd = out_fd;
	cverb << vperf << "operf_record ctor using output fd " << output_fd << endl;

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_sigaction = op_perfrecord_sigusr1_handler;
	sigemptyset(&sa.sa_mask);
	sigemptyset(&ss);
	sigaddset(&ss, SIGUSR1);
	sigprocmask(SIG_UNBLOCK, &ss, NULL);
	sa.sa_mask = ss;
	sa.sa_flags = SA_NOCLDSTOP | SA_SIGINFO;
	cverb << vperf << "calling sigaction" << endl;
	if (sigaction(SIGUSR1, &sa, NULL) == -1) {
		cverb << vperf << "operf_record ctor: sigaction failed; errno is: "
		      << strerror(errno) << endl;
		_exit(EXIT_FAILURE);
	}
	cverb << vperf << "calling setup" << endl;
	setup();
}


void operf_record::register_perf_event_id(unsigned event, u64 id, perf_event_attr attr)
{
	// It's overkill to blindly do this assignment below every time, since this function
	// is invoked once for each event for each cpu; but it's not worth the bother of trying
	// to avoid it.
	opHeader.h_attrs[event].attr = attr;
	cverb << vperf << "Perf header: id = " << hex << (unsigned long long)id << " for event num "
			<< event << ", code " << attr.config <<  endl;
	opHeader.h_attrs[event].ids.push_back(id);
}

void operf_record::write_op_header_info()
{
	struct OP_file_header f_header;
	struct op_file_attr f_attr;

	f_header.magic = OP_MAGIC;
	f_header.size = sizeof(f_header);
	f_header.attr_size = sizeof(f_attr);
	f_header.attrs.size = evts.size() * sizeof(f_attr);
	f_header.data.size = 0;

	add_to_total(op_write_output(output_fd, &f_header, sizeof(f_header)));

	for (unsigned i = 0; i < evts.size(); i++) {
		struct op_header_evt_info attr = opHeader.h_attrs[i];
		f_attr.attr = attr.attr;
		f_attr.ids.size = attr.ids.size() * sizeof(u64);
		add_to_total(op_write_output(output_fd, &f_attr, sizeof(f_attr)));
	}

	for (unsigned i = 0; i < evts.size(); i++) {
		add_to_total(op_write_output(output_fd, &opHeader.h_attrs[i].ids[0],
		                             opHeader.h_attrs[i].ids.size() * sizeof(u64)));
	}
}

int operf_record::prepareToRecord(int cpu, int fd)
{
	struct mmap_data md;;
	md.prev = 0;
	md.mask = num_mmap_pages * pagesize - 1;

	fcntl(fd, F_SETFL, O_NONBLOCK);

	poll_data[cpu].fd = fd;
	poll_data[cpu].events = POLLIN;
	poll_count++;

	md.base = mmap(NULL, (num_mmap_pages + 1) * pagesize,
			PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (md.base == MAP_FAILED) {
		perror("failed to mmap");
		return -1;
	}
	samples_array.push_back(md);

	return 0;
}


void operf_record::setup()
{
	bool all_cpus_avail = true;
	int rc = 0;
	struct dirent *entry = NULL;
	DIR *dir = NULL;
	string err_msg;
	char cpus_online[129];
	bool need_IOC_enable = (system_wide || pid_started);


	if (system_wide)
		cverb << vperf << "operf_record::setup() for system-wide profiling" << endl;
	else
		cverb << vperf << "operf_record::setup() with pid_started = " << pid_started << endl;

	if (!system_wide && pid_started) {
		/* We need to verify the existence of the passed PID before trying
		 * perf_event_open or all hell will break loose.
		 */
		char fname[PATH_MAX];
		FILE *fp;
		snprintf(fname, sizeof(fname), "/proc/%d/status", pid);
		fp = fopen(fname, "r");
		if (fp == NULL) {
			// Process must have finished or invalid PID passed into us.
			// We'll bail out now.
			cerr << "Unable to find process information for PID " << pid << "." << endl;
			cverb << vperf << "couldn't open " << fname << endl;
			return;
		}

	}
	pagesize = sysconf(_SC_PAGE_SIZE);
	num_mmap_pages = (512 * 1024)/pagesize;
	num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	if (!num_cpus)
		throw runtime_error("Number of online CPUs is zero; cannot continue");;

	poll_data = new struct pollfd [num_cpus];

	cverb << vperf << "calling perf_event_open for pid " << pid << " on "
	      << num_cpus << " cpus" << endl;
	FILE * online_cpus = fopen("/sys/devices/system/cpu/online", "r");
	if (!online_cpus) {
		fclose(online_cpus);
		err_msg = "Internal Error: Number of online cpus cannot be determined.";
		rc = -1;
		goto error;
	}
	memset(cpus_online, 0, sizeof(cpus_online));
	fgets(cpus_online, sizeof(cpus_online), online_cpus);
	if (!cpus_online[0]) {
		fclose(online_cpus);
		err_msg = "Internal Error: Number of online cpus cannot be determined.";
		rc = -1;
		goto error;

	}
	if (index(cpus_online, ',')) {
		all_cpus_avail = false;
		dir = opendir("/sys/devices/system/cpu");
	}
	fclose(online_cpus);

	for (int cpu = 0; cpu < num_cpus; cpu++) {
		int real_cpu;
		int mmap_fd;
		bool mmap_done_for_cpu = false;
		if (all_cpus_avail) {
			real_cpu = cpu;
		} else {
			real_cpu = op_get_next_online_cpu(dir, entry);
			if (real_cpu < 0) {
				closedir(dir);
				err_msg = "Internal Error: Number of online cpus cannot be determined.";
				rc = -1;
				goto error;
			}
		}

		// Create new row to hold operf_counter objects since we need one
		// row for each cpu. Do the same for samples_array.
		vector<operf_counter> tmp_pcvec;

		perfCounters.push_back(tmp_pcvec);
		for (unsigned event = 0; event < evts.size(); event++) {
			evts[event].counter = event;
			perfCounters[cpu].push_back(operf_counter(evts[event],
			                                          (!pid_started && !system_wide),
			                                          callgraph, separate_cpu));
			if ((rc = perfCounters[cpu][event].perf_event_open(pid, real_cpu, event, this)) < 0) {
				err_msg = "Internal Error.  Perf event setup failed.";
				goto error;
			}
			if (!mmap_done_for_cpu) {
				if (((rc = prepareToRecord(cpu, perfCounters[cpu][event].get_fd()))) < 0) {
					err_msg = "Internal Error.  Perf event setup failed.";
					goto error;
				}
				mmap_fd = perfCounters[cpu][event].get_fd();
				mmap_done_for_cpu = true;
			} else {
				if (ioctl(perfCounters[cpu][event].get_fd(),
				          PERF_EVENT_IOC_SET_OUTPUT, mmap_fd) < 0)
					goto error;
			}
			if (need_IOC_enable)
				if (ioctl(perfCounters[cpu][event].get_fd(), PERF_EVENT_IOC_ENABLE) < 0)
					goto error;
		}
	}
	if (!all_cpus_avail)
		closedir(dir);
	write_op_header_info();

	// Set bit to indicate we're set to go.
	valid = true;
	return;

error:
	delete[] poll_data;
	for (int i = 0; i < samples_array.size(); i++) {
		struct mmap_data *md = &samples_array[i];
		munmap(md->base, (num_mmap_pages + 1) * pagesize);
	}
	samples_array.clear();
	close(output_fd);
	if (rc != OP_PERF_HANDLED_ERROR)
		throw runtime_error(err_msg);
}

void operf_record::recordPerfData(void)
{
	bool disabled = false;
	if (pid_started || system_wide) {
		if (op_record_process_info(system_wide, pid, this, output_fd) < 0) {
			for (int i = 0; i < num_cpus; i++) {
				for (unsigned int evt = 0; evt < evts.size(); evt++)
					ioctl(perfCounters[i][evt].get_fd(), PERF_EVENT_IOC_DISABLE);
			}
			throw runtime_error("operf_record: error recording process info");
		}
	}
	op_record_kernel_info(vmlinux_file, kernel_start, kernel_end, output_fd, this);

	while (1) {
		int prev = sample_reads;


		for (int i = 0; i < samples_array.size(); i++) {
			if (samples_array[i].base)
				op_get_kernel_event_data(&samples_array[i], this);
		}
		if (quit && disabled)
			break;

		if (prev == sample_reads) {
			poll(poll_data, poll_count, -1);
		}

		if (quit) {
			for (int i = 0; i < num_cpus; i++) {
				for (unsigned int evt = 0; evt < evts.size(); evt++)
					ioctl(perfCounters[i][evt].get_fd(), PERF_EVENT_IOC_DISABLE);
			}
			disabled = true;
			cverb << vperf << "operf_record::recordPerfData received signal to quit." << endl;
		}
	}
	close(output_fd);
	cverb << vdebug << "operf recording finished." << endl;
}

void operf_read::init(int sample_data_pipe_fd, string samples_loc,  op_cpu cputype, vector<operf_event_t> & events)
{
	struct sigaction sa;
	sigset_t ss;
	sample_data_fd = sample_data_pipe_fd;
	sampledir = samples_loc;
	evts = events;
	cpu_type = cputype;
	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_sigaction = op_perfread_sigusr1_handler;
	sigemptyset(&sa.sa_mask);
	sigemptyset(&ss);
	sigaddset(&ss, SIGUSR1);
	sigprocmask(SIG_UNBLOCK, &ss, NULL);
	sa.sa_mask = ss;
	sa.sa_flags = SA_NOCLDSTOP | SA_SIGINFO;
	cverb << vperf << "operf-read calling sigaction" << endl;
	if (sigaction(SIGUSR1, &sa, NULL) == -1) {
		cverb << vperf << "operf-read init: sigaction failed; errno is: "
		      << strerror(errno) << endl;
		_exit(EXIT_FAILURE);
	}

}

operf_read::~operf_read()
{
	evts.clear();
}

int operf_read::readPerfHeader(void)
{
	struct OP_file_header fheader;
	string errmsg;
	int num_fattrs;
	size_t fattr_size;
	vector<struct op_file_attr> f_attr_cache;

	errno = 0;
	if (read(sample_data_fd, &fheader, sizeof(fheader)) != sizeof(fheader)) {
		errmsg = "Error reading header on sample data pipe: " + string(strerror(errno));
		goto fail;
	}

	if (memcmp(&fheader.magic, __op_magic, sizeof(fheader.magic))) {
		errmsg = "Error: operf sample data does not have expected header data";
		goto fail;
	}

	cverb << vperf << "operf magic number " << (char *)&fheader.magic << " matches expected __op_magic " << __op_magic << endl;
	fattr_size = sizeof(struct op_file_attr);
	if (fattr_size != fheader.attr_size) {
		errmsg = "Error: perf_events binary incompatibility. Event data collection was apparently "
				"performed under a different kernel version than current.";
		goto fail;
	}
	num_fattrs = fheader.attrs.size/fheader.attr_size;
	cverb << vperf << "num_fattrs  is " << num_fattrs << endl;
	for (int i = 0; i < num_fattrs; i++) {
		struct op_file_attr f_attr;
		streamsize fattr_size = sizeof(f_attr);
		if (read(sample_data_fd, (char *)&f_attr, fattr_size) != fattr_size) {
			errmsg = "Error reading file attr on sample data pipe: " + string(strerror(errno));
			goto fail;
		}
		opHeader.h_attrs[i].attr = f_attr.attr;
		f_attr_cache.push_back(f_attr);
	}
	for (int i = 0; i < num_fattrs; i++) {
		vector<struct op_file_attr>::iterator it = f_attr_cache.begin();
		struct op_file_attr f_attr = *(it);
		int num_ids = f_attr.ids.size/sizeof(u64);

		for (int id = 0; id < num_ids; id++) {
			u64 perf_id;
			streamsize perfid_size = sizeof(perf_id);
			if (read(sample_data_fd, (char *)& perf_id, perfid_size) != perfid_size) {
				errmsg = "Error reading perf ID on sample data pipe: " + string(strerror(errno));
				goto fail;
			}
			cverb << vperf << "Perf header: id = " << hex << (unsigned long long)perf_id << endl;
			opHeader.h_attrs[i].ids.push_back(perf_id);
		}

	}
	valid = true;
	cverb << vperf << "Successfully read perf header" << endl;
	return 0;

fail:
	cerr << errmsg;
	return -1;
}

int operf_read::get_eventnum_by_perf_event_id(u64 id) const
{
	for (unsigned i = 0; i < evts.size(); i++) {
		struct op_header_evt_info attr = opHeader.h_attrs[i];
		for (unsigned j = 0; j < attr.ids.size(); j++) {
			if (attr.ids[j] == id)
				return i;
		}
	}
	return -1;
}

int operf_read::_get_one_perf_event(event_t * event)
{
	static size_t pe_header_size = sizeof(perf_event_header);
	char * evt = (char *)event;
	ssize_t num_read;
	perf_event_header * header = (perf_event_header *)event;

	/* A signal handler was setup for the operf_read process to handle interrupts
	 * (i.e., from ctrl-C), so the read syscalls below may get interrupted.  But the
	 * operf_read process should ignore the interrupt and continue processing
	 * until there's no more data to read or until the parent operf process
	 * forces us to stop.  So we must try the read operation again if it was
	 * interrupted.
	 */
again:
	errno = 0;
	if ((num_read = read(sample_data_fd, header, pe_header_size)) < 0) {
		cverb << vdebug << "Read 1 of sample data pipe returned with " << strerror(errno) << endl;
		if (errno == EINTR)
			goto again;
		else
			return -1;
	} else if (num_read == 0) {
		return -1;
	}
	evt += pe_header_size;
	if (!header->size)
		return -1;

again2:
	if ((num_read = read(sample_data_fd, evt, header->size - pe_header_size)) < 0) {
		cverb << vdebug << "Read 2 of sample data pipe returned with " << strerror(errno) << endl;
		if (errno == EINTR)
			goto again2;
		else
			return -1;
	} else if (num_read == 0) {
		return -1;
	}
	return 0;
}


int operf_read::convertPerfData(void)
{
	int num_bytes = 0;
	// Allocate way more than enough space for a really big event with a long callchain
	event_t * event = (event_t *)xmalloc(65536);

	for (int i = 0; i < OPERF_MAX_STATS; i++)
		operf_stats[i] = 0;

	cverb << vdebug << "Converting operf data to oprofile sample data format" << endl;
	cverb << vdebug << "sample type is " << hex <<  opHeader.h_attrs[0].attr.sample_type << endl;
	first_time_processing = true;
	memset(event, '\0', 65536);
	while (1) {
		streamsize rec_size = 0;
		if (_get_one_perf_event(event) < 0) {
			break;
		}
		rec_size = event->header.size;
		op_write_event(event, opHeader.h_attrs[0].attr.sample_type);
		num_bytes += rec_size;
	}
	first_time_processing = false;
	op_reprocess_unresolved_events(opHeader.h_attrs[0].attr.sample_type);

	op_release_resources();
	operf_print_stats(operf_options::session_dir, start_time_human_readable, throttled);

	char * cbuf;
	cbuf = (char *)xmalloc(operf_options::session_dir.length() + 5);
	strcpy(cbuf, operf_options::session_dir.c_str());
	strcat(cbuf, "/abi");
	op_write_abi_to_file(cbuf);
	free(cbuf);
	free(event);
	return num_bytes;
}
