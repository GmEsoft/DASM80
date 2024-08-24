//#define NDEBUG
#include <stdio.h>
#include <assert.h>
#include <time.h>

#include "memz80.h"
#include "disasZ80.h"

#ifdef trace
#undef trace
#endif

#ifdef _TRACE_ACTIVE_
#	define trace(trace) trace
#	define tgetch() getch()
#else
#	define trace(trace)
#	define tgetch()
#endif




FILE *readFile(const char *prompt, const char *mode);

char data[0x10000];

range_t ranges[1000];
unsigned nRanges = 0;

// Data Write Routine (memory address space)
unsigned char putData(unsigned short addr, unsigned char byte)
{
	// System RAM
	data[addr] = byte;

	if ( !nRanges || ranges[nRanges-1].end != addr )
	{
		ranges[nRanges].beg = addr;
		++nRanges;
	}
	ranges[nRanges-1].end = addr + 1;

    return byte;
}

// TRS-80 Data Read Routine (memory address space)
unsigned char getData(unsigned short addr)
{
	unsigned char byte;

    byte = data[addr];
    return (unsigned char)byte;
}

