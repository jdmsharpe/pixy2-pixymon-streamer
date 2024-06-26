//
// begin license header
//
// This file is part of Pixy CMUcam5 or "Pixy" for short
//
// All Pixy source code is provided under the terms of the
// GNU General Public License v2 (http://www.gnu.org/licenses/gpl-2.0.html).
// Those wishing to use Pixy source code, software and/or
// technologies under different licensing terms should contact us at
// cmucam@cs.cmu.edu. Such licensing terms are available for
// all portions of the Pixy codebase presented here.
//
// end license header
//

#include <string.h>
#include <stdio.h>
#include <debug.h>
#include <pixy_init.h>
#include <pixyvals.h>
#include <pixy_init.h>
#include <misc.h>
#include "camera.h"
#include "led.h"
#include "conncomp.h"
#include "line.h"
#include "exec.h"
#include "camera.h"
#include "param.h"
#include "serial.h"
#include "rcservo.h"
 

#include <new>

// M0 code 
#ifdef KEIL
const // so m0 program goes into RO memory
#include "../main_m0/m0_image.c"
#endif

extern "C" 
{
// For some strange reason, putting this routine in libpixy messes with the debugger
// or the execution--- not sure which. 
// this is called if we allocate memory (new) and don't catch exception
// it may be called for other reasons too... 
void __default_signal_handler(int signal, int type)
{										   
	char message[48];

	asprintf(message, "received signal: %d %d\n", signal, type);
	showError(signal, 0xff0000, message);
}
		    
#if 0
unsigned __check_heap_overflow (void * new_end_of_heap)
{

	if ((uint32_t)new_end_of_heap >= 0x20008000-0x600)
		return 1; // Heap has overflowed
	return 0; // Heap still OK
}
#endif
}


int main(void)	 
{
	uint16_t major, minor, build;
	uint16_t hwVer[3];
	char *type;
	int i, res, count, count2;
	volatile uint32_t d;
	
    // insert a small delay so power supply can stabilize
	for (d=0; d<2500000; d++);

#ifdef KEIL
 	pixyInit(SRAM3_LOC, &LR0[0], sizeof(LR0));
#else
	pixyInit();
#endif

	exec_getHardwareVersion(hwVer);
	
	// main init of hardware plus a version-dependent number for the parameters that will
	// force a format of parameter between version numbers.  

	exec_init(g_chirpUsb);
	cam_init(hwVer);
	
#ifndef LEGO
	rcs_init();
#endif
	cc_init(g_chirpUsb);
	line_init(g_chirpUsb);
	ser_init(g_chirpUsb);


#if 1

#if 1 
   	// check version
	prm_add("fwver", PRM_FLAG_INTERNAL, PRM_PRIORITY_DEFAULT, "", UINT16(FW_MAJOR_VER), UINT16(FW_MINOR_VER), UINT16(FW_BUILD_VER), END);
	prm_add("fwtype", PRM_FLAG_INTERNAL, PRM_PRIORITY_DEFAULT, "", STRING(FW_TYPE), END);
	
	// this code formats if the version has changed
	for (i=0, count=0, count2=0; i<25; i++)
	{
		res = prm_get("fwver", &major, &minor, &build, END);
		if (res>=0 && major==FW_MAJOR_VER && minor==FW_MINOR_VER && build==FW_BUILD_VER)
			count++;
		res = prm_get("fwtype", &type, END);
		if (res>=0 && strcmp(type, FW_TYPE)==0)
			count2++;
	}
	if (count==0 || count2==0)
		prm_format();
#endif

	exec_mainLoop();
#endif
#if 0
	#define DELAY 500000
	rcs_setFreq(60);
	rcs_setLimits(0, -200, 200);
	rcs_setLimits(1, -200, 200);
	while(1)
	{
		rcs_setPos(0, 0);
		rcs_setPos(1, 0);
		delayus(DELAY);
		rcs_setPos(0, 1000);
		rcs_setPos(1, 1000);
		delayus(DELAY);
		rcs_setPos(0, 0);
		rcs_setPos(1, 0);
		delayus(DELAY);
		rcs_setPos(0, 500);
		rcs_setPos(1, 500);
		delayus(DELAY);
		rcs_setPos(0, 1000);
		rcs_setPos(1, 1000);
		delayus(DELAY);
	}

#endif
#if 0
	while(1)
	{
		g_chirpUsb->service();
		handleButton();
	}
#endif
}

