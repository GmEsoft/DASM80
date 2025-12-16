#ifndef __DISASZ80_H__
#define __DISASZ80_H__

typedef unsigned int uint;
typedef unsigned char uchar;
typedef unsigned long ulong;
typedef unsigned int ushort; //TODO: unsigned short causes 10000H to be never reached

typedef int (*compfptr_t)(const void*, const void*);
typedef unsigned char (*readfptr_t)( unsigned short );


typedef struct textline_t { 
	struct textline_t* next;
	char* text;
} textline_t;

typedef struct {
	char		name[41];
	uint		val;
	uint		lval;
	char		seg;
	char  		label;			// 1 if label, 0 if equate
	char		ref;			// 1 if ref, 0 if not
	char		newsym;			// 1 if new symbol, 0 if not
	char		ds;				// 1 if DS label
	char		gen;			// 1 if already generated
	char		comment[61];
	textline_t* pTextLine;
} symbol_t;

enum { DS_NO = 0, DS_YES = 1 }; // Allow labels for DS

// get symbol by value
symbol_t* getSymbol(uint val);

// get label of given code address
char* getLabel( uint val, char ds );

// set label generated (DS labels)
void setLabelGen( uint val );

// get label of given code address
char* getXAddr( uint val );

// get comment associated to label of given code address from last getXAddr()/getLabel() call
char* getLastComment();

// fetch long external address and return it as hex string or as label
char* getLAddr();

// get single instruction source
char* source();

extern uint		pc;

extern char		noNewEqu;
extern char		labelColon;
extern char		usesvc;

extern int		pcOffset;
extern ushort	pcOffsetBeg, pcOffsetEnd;
extern char		pcOffsetSeg;

//  Forward declaration of array of instruction-processing functions for simulator
typedef struct
{
	int mnemon, opn1, opn2, arg1, arg2;
} instr_t;

// get macro line by number
char* getMacroLine( uint line );

// Attach disassembler to external symbol table
void setSymbols(symbol_t* pSymbols, int pNSymbols, int pSymbolsSize);

// update symbols table
void updateSymbols();

// reset symbols table
void resetSymbols();

// get number of symbols
uint getNumSymbols();

// Attach Z80 to memory and I/O ports
void setGetData( readfptr_t getData );

// Sort symbols
int  compareSymbolValues(symbol_t *a, symbol_t *b);

// Compare symbols by name
int  compareSymbolNames(symbol_t *a, symbol_t *b);

#endif
