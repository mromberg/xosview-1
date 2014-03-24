//  
//  Initial port performed by Greg Onufer (exodus@cheers.bungi.com)
//
#include "loadmeter.h"
#include "cpumeter.h"
#include "xosview.h"
#include <stdlib.h>
#include <string.h>
#include <kstat.h>
#include <iostream>
#ifdef NO_GETLOADAVG
#ifndef FSCALE
#define FSCALE (1<<8)
#endif
#else
#include <sys/loadavg.h>
#endif


LoadMeter::LoadMeter(XOSView *parent, kstat_ctl_t *_kc)
	: FieldMeterGraph(parent, 2, "LOAD", "PROCS/MIN", 1, 1, 0)
{
	kc = _kc;
#ifdef NO_GETLOADAVG
	ksp = kstat_lookup(kc, "unix", 0, "system_misc");
	if (ksp == NULL) {
		parent_->done(1);
		return;
	}
#endif
	total_ = -1;
	lastalarmstate = -1;
	old_cpu_speed = cur_cpu_speed = 0;
}

LoadMeter::~LoadMeter(void)
{
}

void LoadMeter::checkResources(void)
{
	FieldMeterGraph::checkResources();

	warnloadcol = parent_->allocColor(parent_->getResource("loadWarnColor"));
	procloadcol = parent_->allocColor(parent_->getResource("loadProcColor"));
	critloadcol = parent_->allocColor(parent_->getResource("loadCritColor"));

	setfieldcolor(0, procloadcol);
	setfieldcolor(1, parent_->getResource("loadIdleColor"));
	priority_ = atoi (parent_->getResource("loadPriority"));
	dodecay_ = parent_->isResourceTrue("loadDecay");
	useGraph_ = parent_->isResourceTrue("loadGraph");
	SetUsedFormat(parent_->getResource("loadUsedFormat"));
	do_cpu_speed = parent_->isResourceTrue("loadCpuSpeed");

	const char *warn = parent_->getResource("loadWarnThreshold");
	if (strncmp(warn, "auto", 2) == 0)
		warnThreshold = CPUMeter::countCPUs(kc);
	else
		warnThreshold = atoi(warn);

	const char *crit = parent_->getResource("loadCritThreshold");
	if (strncmp(crit, "auto", 2) == 0)
		critThreshold = warnThreshold * 4;
	else
		critThreshold = atoi(crit);

	if (dodecay_){
		/*
		 * Warning: Since the loadmeter changes scale
		 * occasionally, old decay values need to be rescaled.
		 * However, if they are rescaled, they could go off the
		 * edge of the screen.  Thus, for now, to prevent this
		 * whole problem, the load meter can not be a decay
		 * meter.  The load is a decaying average kind of thing
		 * anyway, so having a decaying load average is
		 * redundant.
		 */
		std::cerr << "Warning:  The loadmeter can not be configured as a decay\n"
		     << "  meter.  See the source code (" << __FILE__ << ") for further\n"
		     << "  details.\n";
		dodecay_ = 0;
	}
}

void LoadMeter::checkevent(void)
{
	getloadinfo();
	if (do_cpu_speed) {
		getspeedinfo();
		if (old_cpu_speed != cur_cpu_speed) {
			// update the legend:
			char l[32];
			snprintf(l, 32, "PROCS/MIN %d MHz", cur_cpu_speed);
			legend(l);
			drawlegend();
		}
	}
	drawfields();
}

void LoadMeter::getloadinfo(void)
{
	int alarmstate;
#ifdef NO_GETLOADAVG
	// This code is mainly for Solaris 6 and earlier, but should work on
	// any version.
	kstat_named_t *k;

	if (kstat_read(kc, ksp, NULL) == -1) {
		parent_->done(1);
		return;
	}
	k = (kstat_named_t *)kstat_data_lookup(ksp, "avenrun_1min");
	if (k == NULL) {
		parent_->done(1);
		return;
	}
	fields_[0] = (double)k->value.l / FSCALE;
#else
	// getloadavg() if found on Solaris 7 and newer.
	getloadavg(&fields_[0], 1);
#endif
	
	if (fields_[0] <  warnThreshold)
		alarmstate = 0;
	else if (fields_[0] >= critThreshold)
		alarmstate = 2;
	else /* if fields_[0] >= warnThreshold */
		alarmstate = 1;

	if (alarmstate != lastalarmstate) {
		if (alarmstate == 0)
			setfieldcolor(0, procloadcol);
		else if (alarmstate == 1)
			setfieldcolor(0, warnloadcol);
		else /* if alarmstate == 2 */
			setfieldcolor(0, critloadcol);
		drawlegend();
		lastalarmstate = alarmstate;
	}

	// Adjust total to next power-of-two of the current load.
	if ( (fields_[0]*5.0 < total_ && total_ > 1.0) || fields_[0] > total_ ) {
		unsigned int i = fields_[0];
		i |= i >> 1; i |= i >> 2; i |= i >> 4; i |= i >> 8; i |= i >> 16;  // i = 2^n - 1
		total_ = i + 1;
	}

	fields_[1] = total_ - fields_[0];
	setUsed(fields_[0], total_);
}

void LoadMeter::getspeedinfo(void)
{
	unsigned int total_mhz = 0, cpu = 0;
	kstat_named_t *k;
	kstat_t *ksp_speed;

	for (ksp_speed = kc->kc_chain; ksp_speed != NULL; ksp_speed = ksp_speed->ks_next) {
		if (strncmp(ksp_speed->ks_name, "cpu_info", 8))
			continue;
		if (kstat_read(kc, ksp_speed, NULL) == -1) {
			parent_->done(1);
			return;
		}
		// Try current_clock_Hz first (needs frequency scaling support),
		// then clock_MHz.
		k = (kstat_named_t *)kstat_data_lookup(ksp_speed, "current_clock_Hz");
		if (k == NULL) {
			k = (kstat_named_t *)kstat_data_lookup(ksp_speed, "clock_MHz");
			if (k == NULL) {
				std::cerr << "CPU speed is not available." << std::endl;
				parent_->done(1);
				return;
			}
			XOSDEBUG("Speed of cpu %d is %d MHz\n", cpu, k->value.ui32);
			total_mhz += k->value.ui32;
		}
		else {
			XOSDEBUG("Speed of cpu %d is %lld Hz\n", cpu, k->value.ui64);
			total_mhz += ( k->value.ui64 / 1000000 );
		}
		cpu++;
	}
	old_cpu_speed = cur_cpu_speed;
	cur_cpu_speed = ( cpu > 0 ? total_mhz / cpu : 0 );
}

