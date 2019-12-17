#ifndef __MEMZ80_H__
#define __MEMZ80_H__

#define SYMSIZE 10240        // symbol table size

typedef struct
{
	unsigned short beg, end;
} range_t;

extern range_t ranges[];
extern unsigned nranges;

// TRS-80 Data Write Routine (memory address space)
unsigned char putdata( unsigned short addr, unsigned char byte );

// TRS-80 Data Read Routine (memory address space)
unsigned char getdata( unsigned short addr );

#endif
