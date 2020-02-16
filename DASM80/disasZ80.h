#ifndef __DISASZ80_H__
#define __DISASZ80_H__

typedef unsigned int uint;
typedef unsigned char uchar;
typedef unsigned long ulong;
typedef unsigned long ushort; //TODO: check

typedef int (*compfptr_t)(const void*, const void*);
typedef uchar (*readfptr_t)( ushort );
typedef uchar (*writefptr_t)( ushort, uchar );

typedef struct symbol_t { char    name[41];
                        uint    val;
						uint	lval;
                        char    seg;
                        char  	label;			// 1 if label, 0 if equate
						char	ref;			// 1 if ref, 0 if not
						char	newsym;			// 1 if new symbol, 0 if not
						char	ds;				// 1 if DS label
						char	gen;			// 1 if already generated
						char	comment[61];
                      } symbol_t;

// get label of given code address
char* getlabel( uint val, char ds );

// set label generated (DS labels)
void setlabelgen( uint val );

// get label of given code address
char* getxaddr( uint val );

// get comment associated to label of given code address from last getXAddr()/getLabel() call
char* getLastComment();

// fetch long external address and return it as hex string or as label
char* getladdr();

// get single instruction source
char* source();

// Z-80 simulator
extern uint		pc;

extern char		nonewequ;

extern int		nZ80symbols;
extern symbol_t	*Z80symbols;
extern int		pcoffset;
extern ushort	pcoffsetbeg, pcoffsetend;
extern char		pcoffsetseg;

//  Forward declaration of array of instruction-processing functions for simulator
typedef struct
{
	int mnemon, opn1, opn2, arg1, arg2;
} instr_t;

extern instr_t instr[];

char* getmacroline( uint line );

// Attach Z80 to external symbol table
void setZ80Symbols( symbol_t *pSymbols, int pNSymbols, int pSymbolsSize );

void updateZ80Symbols();

void resetZ80Symbols();

int getNumZ80Symbols();

// Attach Z80 to memory and I/O ports
void setZ80MemIO( /*writefptr_t outdata, 
				  readfptr_t indata, 
				  writefptr_t putdata, */
				 readfptr_t getdata );

// Sort symbols
int  symsort(symbol_t *a, symbol_t *b);

// Compare symbols by name
int  symcompname(symbol_t *a, symbol_t *b);

#endif
