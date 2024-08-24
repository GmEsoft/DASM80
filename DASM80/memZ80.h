#ifndef __MEMZ80_H__
#define __MEMZ80_H__

#define SYMSIZE 10240        // symbol table size

typedef struct
{
	unsigned int beg, end;
} range_t;

extern range_t ranges[];
extern unsigned nRanges;

// Data Write Routine (memory address space)
unsigned char putData( unsigned short addr, unsigned char byte );

// Data Read Routine (memory address space)
unsigned char getData( unsigned short addr );

#endif
