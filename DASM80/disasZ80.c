#define _CRT_SECURE_NO_WARNINGS 1

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <dos.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys\stat.h>

//#include "borl2ms.h"

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

// Z-80 DISASSEMBLER-SIMULATOR ////////////////////////////////////////////////

const char false = 0;
const char true  = -1;



//  Enumerated constants for instructions, also array subscripts
enum {  NOP=0, LD, INC, DEC, ADD, SUB, ADC, SBC, AND, OR, XOR, RLCA,
        RRCA, RLA, RRA, EX, EXX, DJNZ, JR, JP, CALL, RET, RST, CPL, NEG, SCF, CCF,
        CP, IN, OUT, PUSH, POP, HALT, DI, EI, DAA, RLD, RRD,
        RLC, RRC, RL, RR, SLA, SRA, SLL, SRL, BIT, RES, SET,
        LDI, LDD, LDIR, LDDR, CPI, CPIR, CPD, CPDR,
        INI, INIR, IND, INDR, OUTI, OTIR, OUTD, OTDR, IM, RETI, RETN,
        BREAK, EXIT, CSINB, CSOUTC, CSEND, KBINC, KBWAIT, PUTCH,
        DIV16, DIV32, IDIV32,
        OPEN, CLOSE, READ, WRITE, SEEK,
        GSPD, SSPD, ERROR, FINDFIRST, FINDNEXT, LOAD,
        CHDIR, GETDIR, TRUNCATE, PATHOPEN, CLOSEALL,
        DEFB};

//  Mnemonics for disassembler
char mnemo[][9] = {
        "NOP", "LD", "INC", "DEC", "ADD", "SUB", "ADC", "SBC", "AND", "OR", "XOR", "RLCA",
        "RRCA", "RLA", "RRA", "EX", "EXX", "DJNZ", "JR", "JP", "CALL", "RET", "RST", "CPL", "NEG", "SCF", "CCF",
        "CP", "IN", "OUT", "PUSH", "POP", "HALT", "DI", "EI", "DAA", "RLD", "RRD",
        "RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL", "BIT", "RES", "SET",
        "LDI", "LDD", "LDIR", "LDDR", "CPI", "CPIR", "CPD", "CPDR",
        "INI", "INIR", "IND", "INDR", "OUTI", "OTIR", "OUTD", "OTDR", "IM", "RETI", "RETN",
        "$BREAK", "$EXIT", "$CSINB", "$CSOUTC", "$CSEND", "$KBINC", "$KBWAIT", "$PUTCH",
        "$DIV16", "$DIV32","$IDIV32",
		"$OPEN", "$CLOSE", "$READ", "$WRITE", "$SEEK",
		"$GSPD", "$SSPD", "$ERROR", "$FFST", "$FNXT", "$LOAD",
        "$CHDIR", "$GDIR", "$TRUNC", "$PTHOPN", "$CLSALL",
        "DEFB"
        };

//  Enumerated constants for operands, also array subscripts
enum {R=1, RX, BYTE, WORD, OFFSET, ATR, ATRX, ATPTR, AFP,
      Z, C, NZ, NC, PE, PO, P, M, ATBYTE, ATWORD, DIRECT, BITPOS };


enum {simA=1, simB, simC, simD, simE, simH, simL, simI, simR, simBC, simDE, simHL, simAF, simSP, simIR};

char regnames[][3] = { "??",
    "A", "B", "C", "D", "E", "H", "L", "I", "R", "BC", "DE", "HL", "AF", "SP", "IR"
    } ;

char			nonewequ = 0;
char			labelcolon = 0;

int				useix, useiy;
signed char		offset;
uint			pc;

int				pcoffset = 0;
ushort			pcoffsetbeg, pcoffsetend;
char			pcoffsetseg = 'R';

// Symbols table
symbol_t		*Z80symbols = NULL;
uint			Z80symbolsSize = 0;
uint			nZ80symbols = 0;
uint			nNewZ80symbols = 0;

static char		*comment;

int				usedextopcodes[32] = { 0 };
uint			nusedextopcodes = 0;
char			macrolines[260][40] = { 0 };
uint			nmacrolines = 0;
int				svcmacro = 0;

char* getmacroline( uint line )
{
	if ( line < nmacrolines )
		return macrolines[line];
	return 0;
}

void setZ80Symbols( symbol_t *pSymbols, int pNSymbols, int pSymbolsSize )
{
	Z80symbols = pSymbols;
	nZ80symbols = pNSymbols;
	nNewZ80symbols = nZ80symbols;
	Z80symbolsSize = pSymbolsSize;
	qsort(Z80symbols, nZ80symbols, sizeof(symbol_t), (compfptr_t)symsort);
}

void updateZ80Symbols()
{
	setZ80Symbols( Z80symbols, nNewZ80symbols, Z80symbolsSize );
}

void resetZ80Symbols()
{
	int i;

	for ( i=0; i<nZ80symbols; ++i )
	{
		Z80symbols[i].gen = 0;
	}
}

int getNumZ80Symbols()
{
	return nZ80symbols;
}

// comparison function for qsort() and bsearch()

int  symsort(symbol_t *a, symbol_t *b)
{
    if (a->val < b->val) return -1;
    if (a->val > b->val) return 1;
    if (a->seg < b->seg) return -1;
    if (a->seg > b->seg) return 1;
    return 0;
}

int  symcompname(symbol_t *a, symbol_t *b)
{
    return strcmp (a->name, b->name);
}

static char getcodeseg()
{
	return pcoffset ? pcoffsetseg : 'C';
}


// get label of given code address
char* getlabel(uint val, char ds)
{
    static char name[40] ;

    symbol_t symtofind[1];
    symbol_t *sym;

	comment = NULL;

	name[0] = 0;

	symtofind->val = val - pcoffset;
    symtofind->seg = getcodeseg();

	if ( val == 0x4C09 )
		printf( "break" );

	//printf( "%04X %c\t", symtofind->val, symtofind->seg );

    sym = bsearch(symtofind, Z80symbols, nZ80symbols, sizeof(symbol_t), (compfptr_t)symsort);
    if (sym == NULL)
	{
		return name;
	}

	if ( *sym->comment )
		comment = sym->comment;

	if ( ds && ( sym->gen || !sym->ds ) )
	{
		// label is used by code or already generated => no DS label
		return name;
	}

	if ( !ds )
	{
		sym->ds = 0;
	}

	if ( sym->newsym )
	{
		*sym->name = pcoffset ? getcodeseg() : 'L';
	}
    strcpy (name, sym->name);
	if ( labelcolon )
		strcat (name, ":");
	sym->label = 1;
	sym->newsym = 0;

    return name;
}

// set label generated (DS labels)
void setlabelgen( uint val )
{
    symbol_t symtofind[1];
    symbol_t *sym;

	symtofind->val = val - pcoffset;
    symtofind->seg = getcodeseg();

    sym = bsearch(symtofind, Z80symbols, nZ80symbols, sizeof(symbol_t), (compfptr_t)symsort);
    if (sym != NULL)
	{
		sym->gen = 1;
	}
}

// get label and offset of given code address
char* getlabeloffset(uint val)
{
    static char name[40] ;
	int i;

    symbol_t symtofind[1];
    symbol_t *sym;

	symtofind->val = val - pcoffset;
    symtofind->seg = getcodeseg();

    sym = Z80symbols;

	for ( i=0; i<nZ80symbols; ++i )
	{
		if ( symsort( symtofind, Z80symbols+i ) < 0 )
			break;
	}

    name[0] = 0;

	if (i>0)
	{
		if ( Z80symbols[i-1].val && val-Z80symbols[i-1].val < 0x0400 )
			sprintf( name, "%s + %Xh", Z80symbols[i-1].name, val-Z80symbols[i-1].val );
    }

    return name;
}

// TRS-80 Data Read Routine (memory address space)
uchar getdata_null(ushort addr)
{
	return 0xFF;
}

readfptr_t vgetdata = getdata_null;

void setZ80MemIO( /*writefptr_t outdata, readfptr_t indata, writefptr_t putdata, */
				 readfptr_t getdata )
{
	//voutdata = outdata;
	//vindata = indata;
	//vputdata = putdata;
	vgetdata = getdata;
}

//#define outdata (*voutdata)
//#define indata  (*vindata)
//#define putdata (*vputdata)
#define getdata (*vgetdata)

//  get next instruction byte (sim)
#define fetch() (getdata(pc++))
/*__inline uchar fetch() {
    return (code[pc++]);
}
*/
//  return hex-string or label for double-byte x (dasm)
char* getxaddr( uint x )
{
	static char addr[41];
	symbol_t symtofind;
	symbol_t *sym;

	comment = NULL;

	symtofind.val = x;
	symtofind.seg = getcodeseg();

	sym = bsearch(&symtofind, Z80symbols, nZ80symbols, sizeof(symbol_t), (compfptr_t)symsort);

	if ( sym && ( !nonewequ || !sym->newsym ) )
	{
		strcpy(addr, sym->name);
		if ( *sym->comment )
			comment = sym->comment;
	}
	else 
	{
		uint xorg = x;
		//if ( pcoffsetseg != 'C' )
		//	xorg -= pcoffset;

		if ( !sym )
		{
			if ( nNewZ80symbols == Z80symbolsSize )
			{
				fprintf( stderr, "*** Symbols table (size %d) overflow [M%04X]\n", Z80symbolsSize, x );
				exit( 1 );
			}
			sym = &Z80symbols[nNewZ80symbols++];
			sprintf( sym->name, "%c%04X", pcoffset ? getcodeseg() : 'D' , xorg );
			sym->seg = getcodeseg();
			sym->val = x;
			sym->lval = xorg;
			sym->label = 0;
			sym->newsym = 1;
			sym->ds = 1;
			*sym->comment = 0;
			updateZ80Symbols();
		}

		if ( xorg > 0x9FFF )
			sprintf( addr, "%05XH", xorg );
		else
			sprintf( addr, "%04XH", xorg );
		//printf( "%5d:%s\t", nNewZ80symbols, sym->name );
	}
	sym->ref = 1;
	return addr;
}

// get comment associated to label of given code address from last getXAddr()/getLabel() call
char* getLastComment()
{
	return comment;
}

//  return hex-string or label for double-byte x (dasm)
char* getsvc( uint x )
{
	static char addr[41];
	symbol_t symtofind;
	symbol_t *sym;

	symtofind.val = x;
	symtofind.seg = 'S';

	sym = bsearch(&symtofind, Z80symbols, nZ80symbols, sizeof(symbol_t), (compfptr_t)symsort);

	if ( sym && ( !nonewequ || !sym->newsym ) )
		strcpy( addr, sym->name );
	else 
	{
		if ( !sym )
		{
			if ( nNewZ80symbols == Z80symbolsSize )
			{
				fprintf( stderr, "*** Symbols table (size %d) overflow [@SVC%02X]\n", Z80symbolsSize, x );
				exit( 1 );
			}
			sym = &Z80symbols[nNewZ80symbols++];
			sprintf( sym->name, "@SVC%02X", x );
			sym->seg = 'S';
			sym->val = x;
			sym->lval = x;
			sym->label = 0;
			sym->newsym = 1;
			sym->ds = 1;
			updateZ80Symbols();
		}

		if ( x > 0x9F )
			sprintf (addr, "%03XH", x);
		else
			sprintf (addr, "%02XH", x);
	}
	sym->ref = 1;
	return addr;
}


// fetch long external address and return it as hex string or as label
char* getladdr()
{
	uint x;
	char oldseg = pcoffsetseg;
	char *ret;

	x = fetch ();
	x += fetch () << 8;
	if ( pcoffset && ( x + pcoffset >= pcoffsetbeg ) && ( x + pcoffset < pcoffsetend ) )
	{
		//x += pcoffset;
	}
	else
		pcoffsetseg = 'C';
	ret = getxaddr( x );
	pcoffsetseg = oldseg;
	return ret;
}

// fetch short relative external address and return it as hex string or as label
char* getsaddr()
{
	uint x;
	char oldseg = pcoffsetseg;
	char *ret;
	signed char d;

	d = (signed char) fetch ();
	x = pc + d;
	if ( pcoffset && ( x + pcoffset >= pcoffsetbeg ) && ( x + pcoffset < pcoffsetend ) )
	{
		x -= pcoffset;
	}
	else
		pcoffsetseg = 'C';
	ret = getxaddr( x );
	pcoffsetseg = oldseg;
	return ret;
}

// Get nth opcode (1st or 2nd)
__inline int getopn(int opcode, int pos) { return pos==1? instr[opcode].opn1 : instr[opcode].opn2 ; }

// Get nth argument (1st or 2nd)
__inline int getarg(int opcode, int pos) { return pos==1? instr[opcode].arg1 : instr[opcode].arg2 ; }

// return operand name or value
char* getoperand (int opcode, int pos)
{
	static char op[41];
	uint x;

	strcpy (op, "??");

	switch (getopn(opcode, pos))
	{
	case 0:
		return NULL;
	case R:
	case RX:
		return regnames[getarg(opcode, pos)] ;
	case WORD:
		return getladdr ();
	case BYTE:
		x = fetch ();
		if ( opcode == 0x3E && getdata( pc ) == 0xEF )
		{
			// if	LD		A, @svc
			//		RST		28H
			// then get SVC label
			return getsvc( x );
		}
		if (x>0x9F)
			sprintf (op, "%03XH", x);
		else
			sprintf (op, "%02XH", x);
		break;
	case ATR:
	case ATPTR:
	case ATRX:
		strcpy (op,"(");
		strcat (op, regnames[getarg(opcode, pos)]) ;
		strcat (op, ")");
		break;
	case ATWORD:
		strcpy (op,"(");
		strcat (op, getladdr()) ;
		strcat (op, ")");
		break;
	case ATBYTE:
		x = fetch();
		if ( x > 0x9F )
			sprintf( op, "(%03XH)", x );
		else
			sprintf( op, "(%02XH)", x );
		break;
	case OFFSET:
		return getsaddr();
	case DIRECT:
		x = getarg( opcode, pos );
		if ( x > 0x9F )
			sprintf( op, "%03XH", x );
		else
			sprintf( op, "%02XH", x );
		break;
	case BITPOS:
		x = getarg( opcode, pos );
		sprintf( op, "%X", x );
		break;
	case Z:
		return "Z";
	case NZ:
		return "NZ";
	case C:
		return "C";
	case NC:
		return "NC";
	case PE:
		return "PE";
	case PO:
		return "PO";
	case P:
		return "P";
	case M:
		return "M";
	case AFP:
		return "AF'";
	}
	return op;
}

// get 1st operand name or value
char* getoperand1 (int opcode)
{
	return getoperand( opcode, 1 );
}

// get 2nd operand name or value
char* getoperand2 (int opcode)
{
	return getoperand( opcode, 2 );
}

// add comment if any
static void addComment( char *src, int size, char *comment )
{
	int n;

	if ( comment )
	{
		for ( n = strlen( src ); n < 24; ++n )
			src[n] = ' ';

		src[n] = 0;
		strncat_s( src, size, comment, size - n - 1 );
	}
}

// get single instruction source
char* source ()
{
	ushort opcode;
	static char src[80];
	char substr[41];
	int i;
	char* op;
	signed char offset;
	int x;

	useix = useiy = 0;
	opcode = fetch ();
	if (opcode == 0xCB)
		opcode = 0x100 | fetch();
	else if (opcode == 0xED)
		opcode = 0x200 | fetch();
	else if (opcode == 0xDD) {
		useix = 1;
		opcode = fetch();
	}
	else if (opcode == 0xFD) {
		useiy = 1;
		opcode = fetch();
	}

	if ( useix || useiy ) 
	{
		if ( opcode==0xE3 || opcode==0xE9 ) // ex (SP),IX ; jp (ix)
		{
			offset = 0;
		} 
		else if ( instr[opcode].opn1 == ATPTR || instr[opcode].opn2 == ATPTR ) 
		{
			offset = (signed char) fetch();
		} 
		else if ( opcode == 0xCB ) 
		{
			offset = (signed char) fetch();
			opcode = 0x100 | fetch();
		}
	}

	if ( opcode == 0x3E && getdata( pc + 1 ) == 0xEF )
	{
		if ( !svcmacro )
		{
			svcmacro = 1;
			sprintf( macrolines[nmacrolines++], "$SVC    MACRO   #N" );
			sprintf( macrolines[nmacrolines++], "        LD      A,#N" );
			sprintf( macrolines[nmacrolines++], "        RST     %s", getxaddr( 0x28 ) );
			sprintf( macrolines[nmacrolines++], "        ENDM" );
			*macrolines[nmacrolines++] = 0;
		}
		x = fetch();
		sprintf( src, "$SVC    %-24s", getsvc( x ) );
		fetch();
		return src;
	}


	strcpy( src, mnemo[instr[opcode].mnemon] );

	if ( *mnemo[instr[opcode].mnemon] == '$' )
	{
		for ( i = 0; i < nusedextopcodes; ++i )
		{
			if ( usedextopcodes[i] == opcode )
				break;
		}

		if ( i == nusedextopcodes )
		{
			usedextopcodes[nusedextopcodes++] = opcode;
			sprintf( macrolines[nmacrolines++], "%-7s MACRO", mnemo[instr[opcode].mnemon] );
			sprintf( macrolines[nmacrolines++], "        DB      0EDH,0%02XH", opcode & 0xFF );
			sprintf( macrolines[nmacrolines++], "        ENDM" );
			*macrolines[nmacrolines++] = 0;
		}
	}

	/*
	for (i=0;src[i]!=0;i++)
	src[i] = tolower(src[i]);
	*/
	for (i=strlen(src);i<8;i++) {
		src[i] = ' ';
	} /* endfor */

	src[i] = '\0';

	comment = 0;

	op = getoperand1(opcode);
	if (op != NULL) {
		if ((useix || useiy) && instr[opcode].arg1 == simHL) {
			if (instr[opcode].opn1 == RX) {
				op = useix ? "IX" : "IY" ;
			} else if (instr[opcode].opn1 == ATPTR) {
				sprintf (substr, "(%s%+d)", useix ? "IX" : "IY", offset);
				op = substr ;
			} else if (instr[opcode].opn1 == ATRX) {
				sprintf (substr, "(%s%)", useix ? "IX" : "IY");
				op = substr ;
			}
		}
		strcat(src, op);
		op = getoperand2(opcode);
		if (op != NULL) {
			strcat(src, ",");
			if ((useix || useiy) && instr[opcode].arg2 == simHL) {
				if (instr[opcode].opn2 == RX) {
					op = useix ? "IX" : "IY" ;
				} else if (instr[opcode].opn2 == ATPTR) {
					sprintf (substr, "(%s%+d)", useix ? "IX" : "IY", offset);
					op = substr ;
				} else if (instr[opcode].opn2 == ATRX) {
					sprintf (substr, "(%s%)", useix ? "IX" : "IY");
					op = substr ;
				}
			}
			strcat(src, op);
		}
	}

	switch ( instr[opcode].mnemon )
	{
	case LD:
	case JP:
	case JR:
	case CALL:
	case RST:
		addComment( src, sizeof(src), comment );
		break;
	}

	for (i=strlen(src);i<48;i++) {
		src[i] = ' ';
	}
	src[i] = '\0';
	return src;
}


// PROCESSOR INSTRUCTIONS TABLE ///////////////////////////////////////////////

//  Processor's instruction set
instr_t instr[] = {
// 00-0F
        NOP,            0,              0,              0,              0,
        LD,             RX,             WORD,           simBC,          0,
        LD,             ATPTR,          R,              simBC,          simA,
        INC,            RX,             0,              simBC,          0,
        INC,            R,              0,              simB,           0,
        DEC,            R,              0,              simB,           0,
        LD,             R,              BYTE,           simB,           0,
        RLCA,           0,              0,              0,              0,
        EX,             RX,             AFP,            simAF,          0,
        ADD,            RX,             RX,             simHL,          simBC,
        LD,             R,              ATPTR,          simA,           simBC,
        DEC,            RX,             0,              simBC,          0,
        INC,            R,              0,              simC,           0,
        DEC,            R,              0,              simC,           0,
        LD,             R,              BYTE,           simC,           0,
        RRCA,           0,              0,              0,              0,
// 10-1F
        DJNZ,           OFFSET,         0,              0,              0,
        LD,             RX,             WORD,           simDE,          0,
        LD,             ATPTR,          R,              simDE,          simA,
        INC,            RX,             0,              simDE,          0,
        INC,            R,              0,              simD,           0,
        DEC,            R,              0,              simD,           0,
        LD,             R,              BYTE,           simD,           0,
        RLA,            0,              0,              0,              0,
        JR,             OFFSET,         0,              0,              0,
        ADD,            RX,             RX,             simHL,          simDE,
        LD,             R,              ATPTR,          simA,           simDE,
        DEC,            RX,             0,              simDE,          0,
        INC,            R,              0,              simE,           0,
        DEC,            R,              0,              simE,           0,
        LD,             R,              BYTE,           simE,           0,
        RRA,            0,              0,              0,              0,
// 20-2F
        JR,             NZ,             OFFSET,         0,              0,
        LD,             RX,             WORD,           simHL,          0,
        LD,             ATWORD,         RX,             0,              simHL,
        INC,            RX,             0,              simHL,          0,
        INC,            R,              0,              simH,           0,
        DEC,            R,              0,              simH,           0,
        LD,             R,              BYTE,           simH,           0,
        DAA,            0,              0,              0,              0,
        JR,             Z,              OFFSET,         0,              0,
        ADD,            RX,             RX,             simHL,          simHL,
        LD,             RX,             ATWORD,         simHL,          0,
        DEC,            RX,             0,              simHL,          0,
        INC,            R,              0,              simL,           0,
        DEC,            R,              0,              simL,           0,
        LD,             R,              BYTE,           simL,           0,
        CPL,            0,              0,              0,              0,
// 30-3F
        JR,             NC,             OFFSET,         0,              0,
        LD,             RX,             WORD,           simSP,          0,
        LD,             ATWORD,         R,              0,              simA,
        INC,            RX,             0,              simSP,          0,
        INC,            ATPTR,          0,              simHL,          0,
        DEC,            ATPTR,          0,              simHL,          0,
        LD,             ATPTR,          BYTE,           simHL,          0,
        SCF,            0,              0,              0,              0,
        JR,             C,              OFFSET,         0,              0,
        ADD,            RX,             RX,             simHL,          simSP,
        LD,             R,              ATWORD,         simA,           0,
        DEC,            RX,             0,              simSP,          0,
        INC,            R,              0,              simA,           0,
        DEC,            R,              0,              simA,           0,
        LD,             R,              BYTE,           simA,           0,
        CCF,            0,              0,              0,              0,
// 40-4F
        LD,             R,              R,              simB,           simB,
        LD,             R,              R,              simB,           simC,
        LD,             R,              R,              simB,           simD,
        LD,             R,              R,              simB,           simE,
        LD,             R,              R,              simB,           simH,
        LD,             R,              R,              simB,           simL,
        LD,             R,              ATPTR,          simB,           simHL,
        LD,             R,              R,              simB,           simA,
        LD,             R,              R,              simC,           simB,
        LD,             R,              R,              simC,           simC,
        LD,             R,              R,              simC,           simD,
        LD,             R,              R,              simC,           simE,
        LD,             R,              R,              simC,           simH,
        LD,             R,              R,              simC,           simL,
        LD,             R,              ATPTR,          simC,           simHL,
        LD,             R,              R,              simC,           simA,
// 50-5F
        LD,             R,              R,              simD,           simB,
        LD,             R,              R,              simD,           simC,
        LD,             R,              R,              simD,           simD,
        LD,             R,              R,              simD,           simE,
        LD,             R,              R,              simD,           simH,
        LD,             R,              R,              simD,           simL,
        LD,             R,              ATPTR,          simD,           simHL,
        LD,             R,              R,              simD,           simA,
        LD,             R,              R,              simE,           simB,
        LD,             R,              R,              simE,           simC,
        LD,             R,              R,              simE,           simD,
        LD,             R,              R,              simE,           simE,
        LD,             R,              R,              simE,           simH,
        LD,             R,              R,              simE,           simL,
        LD,             R,              ATPTR,          simE,           simHL,
        LD,             R,              R,              simE,           simA,
// 60-6F
        LD,             R,              R,              simH,           simB,
        LD,             R,              R,              simH,           simC,
        LD,             R,              R,              simH,           simD,
        LD,             R,              R,              simH,           simE,
        LD,             R,              R,              simH,           simH,
        LD,             R,              R,              simH,           simL,
        LD,             R,              ATPTR,          simH,           simHL,
        LD,             R,              R,              simH,           simA,
        LD,             R,              R,              simL,           simB,
        LD,             R,              R,              simL,           simC,
        LD,             R,              R,              simL,           simD,
        LD,             R,              R,              simL,           simE,
        LD,             R,              R,              simL,           simH,
        LD,             R,              R,              simL,           simL,
        LD,             R,              ATPTR,          simL,           simHL,
        LD,             R,              R,              simL,           simA,
// 70-7F
        LD,             ATPTR,          R,              simHL,          simB,
        LD,             ATPTR,          R,              simHL,          simC,
        LD,             ATPTR,          R,              simHL,          simD,
        LD,             ATPTR,          R,              simHL,          simE,
        LD,             ATPTR,          R,              simHL,          simH,
        LD,             ATPTR,          R,              simHL,          simL,
        HALT,           0,              0,              0,              0,
        LD,             ATPTR,          R,              simHL,          simA,
        LD,             R,              R,              simA,           simB,
        LD,             R,              R,              simA,           simC,
        LD,             R,              R,              simA,           simD,
        LD,             R,              R,              simA,           simE,
        LD,             R,              R,              simA,           simH,
        LD,             R,              R,              simA,           simL,
        LD,             R,              ATPTR,          simA,           simHL,
        LD,             R,              R,              simA,           simA,
// 80-8F
        ADD,            R,              R,              simA,           simB,
        ADD,            R,              R,              simA,           simC,
        ADD,            R,              R,              simA,           simD,
        ADD,            R,              R,              simA,           simE,
        ADD,            R,              R,              simA,           simH,
        ADD,            R,              R,              simA,           simL,
        ADD,            R,              ATPTR,          simA,           simHL,
        ADD,            R,              R,              simA,           simA,
        ADC,            R,              R,              simA,           simB,
        ADC,            R,              R,              simA,           simC,
        ADC,            R,              R,              simA,           simD,
        ADC,            R,              R,              simA,           simE,
        ADC,            R,              R,              simA,           simH,
        ADC,            R,              R,              simA,           simL,
        ADC,            R,              ATPTR,          simA,           simHL,
        ADC,            R,              R,              simA,           simA,
// 90-9F
        SUB,            R,              0,              simB,           0,
        SUB,            R,              0,              simC,           0,
        SUB,            R,              0,              simD,           0,
        SUB,            R,              0,              simE,           0,
        SUB,            R,              0,              simH,           0,
        SUB,            R,              0,              simL,           0,
        SUB,            ATPTR,          0,              simHL,          0,
        SUB,            R,              0,              simA,           0,
        SBC,            R,              R,              simA,           simB,
        SBC,            R,              R,              simA,           simC,
        SBC,            R,              R,              simA,           simD,
        SBC,            R,              R,              simA,           simE,
        SBC,            R,              R,              simA,           simH,
        SBC,            R,              R,              simA,           simL,
        SBC,            R,              ATPTR,          simA,           simHL,
        SBC,            R,              R,              simA,           simA,
// A0-AF
        AND,            R,              0,              simB,           0,
        AND,            R,              0,              simC,           0,
        AND,            R,              0,              simD,           0,
        AND,            R,              0,              simE,           0,
        AND,            R,              0,              simH,           0,
        AND,            R,              0,              simL,           0,
        AND,            ATPTR,          0,              simHL,          0,
        AND,            R,              0,              simA,           0,
        XOR,            R,              0,              simB,           0,
        XOR,            R,              0,              simC,           0,
        XOR,            R,              0,              simD,           0,
        XOR,            R,              0,              simE,           0,
        XOR,            R,              0,              simH,           0,
        XOR,            R,              0,              simL,           0,
        XOR,            ATPTR,          0,              simHL,          0,
        XOR,            R,              0,              simA,           0,
// B0-BF
        OR,             R,              0,              simB,           0,
        OR,             R,              0,              simC,           0,
        OR,             R,              0,              simD,           0,
        OR,             R,              0,              simE,           0,
        OR,             R,              0,              simH,           0,
        OR,             R,              0,              simL,           0,
        OR,             ATPTR,          0,              simHL,          0,
        OR,             R,              0,              simA,           0,
        CP,             R,              0,              simB,           0,
        CP,             R,              0,              simC,           0,
        CP,             R,              0,              simD,           0,
        CP,             R,              0,              simE,           0,
        CP,             R,              0,              simH,           0,
        CP,             R,              0,              simL,           0,
        CP,             ATPTR,          0,              simHL,          0,
        CP,             R,              0,              simA,           0,
// C0-CF
        RET,            NZ,             0,              0,              0,
        POP,            RX,             0,              simBC,          0,
        JP,             NZ,             WORD,           0,              0,
        JP,             WORD,           0,              0,              0,
        CALL,           NZ,             WORD,           0,              0,
        PUSH,           RX,             0,              simBC,          0,
        ADD,            R,              BYTE,           simA,           0,
        RST,            DIRECT,         0,              0x00,           0,
        RET,            Z,              0,              0,              0,
        RET,            0,              0,              0,              0,
        JP,             Z,              WORD,           0,              0,
        DEFB,           DIRECT,         BYTE,           0xCB,           0,
        CALL,           Z,              WORD,           0,              0,
        CALL,           WORD,           0,              0,              0,
        ADC,            R,              BYTE,           simA,           0,
        RST,            DIRECT,         0,              0x08,           0,
// D0-DF
        RET,            NC,             0,              0,              0,
        POP,            RX,             0,              simDE,          0,
        JP,             NC,             WORD,           0,              0,
        OUT,            ATBYTE,         R,              0,              simA,
        CALL,           NC,             WORD,           0,              0,
        PUSH,           RX,             0,              simDE,          0,
        SUB,            BYTE,           0,              0,              0,
        RST,            DIRECT,         0,              0x10,           0,
        RET,            C,              0,              0,              0,
        EXX,            0,              0,              0,              0,
        JP,             C,              WORD,           0,              0,
        IN,             R,              ATBYTE,         simA,           0,
        CALL,           C,              WORD,           0,              0,
        DEFB,           DIRECT,         0,              0xDD,           0,
        SBC,            R,              BYTE,           simA,           0,
        RST,            DIRECT,         0,              0x18,           0,
// E0-EF
        RET,            PO,             0,              0,              0,
        POP,            RX,             0,              simHL,          0,
        JP,             PO,             WORD,           0,              0,
        EX,             ATRX,           RX,             simSP,          simHL,
        CALL,           PO,             WORD,           0,              0,
        PUSH,           RX,             0,              simHL,          0,
        AND,            BYTE,           0,              0,              0,
        RST,            DIRECT,         0,              0x20,           0,
        RET,            PE,             0,              0,              0,
        JP,             ATRX,           0,              simHL,          0,
        JP,             PE,             WORD,           0,              0,
        EX,             RX,             RX,             simDE,          simHL,
        CALL,           PE,             WORD,           0,              0,
        DEFB,           DIRECT,         BYTE,           0xED,           0,
        XOR,            BYTE,           0,              0,              0,
        RST,            DIRECT,         0,              0x28,           0,
// F0-FF
        RET,            P,              0,              0,              0,
        POP,            RX,             0,              simAF,          0,
        JP,             P,              WORD,           0,              0,
        DI,             0,              0,              0,              0,
        CALL,           P,              WORD,           0,              0,
        PUSH,           RX,             0,              simAF,          0,
        OR,             BYTE,           0,              0,              0,
        RST,            DIRECT,         0,              0x30,           0,
        RET,            M,              0,              0,              0,
        LD,             RX,             RX,             simSP,          simHL,
        JP,             M,              WORD,           0,              0,
        EI,             0,              0,              0,              0,
        CALL,           M,              WORD,           0,              0,
        DEFB,           DIRECT,         0,              0xFD,           0,
        CP,             BYTE,           0,              0,              0,
        RST,            DIRECT,         0,              0x38,           0,
// CB 00-0F
        RLC,            R,              0,              simB,           0,
        RLC,            R,              0,              simC,           0,
        RLC,            R,              0,              simD,           0,
        RLC,            R,              0,              simE,           0,
        RLC,            R,              0,              simH,           0,
        RLC,            R,              0,              simL,           0,
        RLC,            ATPTR,          0,              simHL,          0,
        RLC,            R,              0,              simA,           0,
        RRC,            R,              0,              simB,           0,
        RRC,            R,              0,              simC,           0,
        RRC,            R,              0,              simD,           0,
        RRC,            R,              0,              simE,           0,
        RRC,            R,              0,              simH,           0,
        RRC,            R,              0,              simL,           0,
        RRC,            ATPTR,          0,              simHL,          0,
        RRC,            R,              0,              simA,           0,
// CB 10-1F
        RL,             R,              0,              simB,           0,
        RL,             R,              0,              simC,           0,
        RL,             R,              0,              simD,           0,
        RL,             R,              0,              simE,           0,
        RL,             R,              0,              simH,           0,
        RL,             R,              0,              simL,           0,
        RL,             ATPTR,          0,              simHL,          0,
        RL,             R,              0,              simA,           0,
        RR,             R,              0,              simB,           0,
        RR,             R,              0,              simC,           0,
        RR,             R,              0,              simD,           0,
        RR,             R,              0,              simE,           0,
        RR,             R,              0,              simH,           0,
        RR,             R,              0,              simL,           0,
        RR,             ATPTR,          0,              simHL,          0,
        RR,             R,              0,              simA,           0,
// CB 20-2F
        SLA,            R,              0,              simB,           0,
        SLA,            R,              0,              simC,           0,
        SLA,            R,              0,              simD,           0,
        SLA,            R,              0,              simE,           0,
        SLA,            R,              0,              simH,           0,
        SLA,            R,              0,              simL,           0,
        SLA,            ATPTR,          0,              simHL,          0,
        SLA,            R,              0,              simA,           0,
        SRA,            R,              0,              simB,           0,
        SRA,            R,              0,              simC,           0,
        SRA,            R,              0,              simD,           0,
        SRA,            R,              0,              simE,           0,
        SRA,            R,              0,              simH,           0,
        SRA,            R,              0,              simL,           0,
        SRA,            ATPTR,          0,              simHL,          0,
        SRA,            R,              0,              simA,           0,
// CB 30-3F
        SLL,            R,              0,              simB,           0,
        SLL,            R,              0,              simC,           0,
        SLL,            R,              0,              simD,           0,
        SLL,            R,              0,              simE,           0,
        SLL,            R,              0,              simH,           0,
        SLL,            R,              0,              simL,           0,
        SLL,            ATPTR,          0,              simHL,          0,
        SLL,            R,              0,              simA,           0,
        SRL,            R,              0,              simB,           0,
        SRL,            R,              0,              simC,           0,
        SRL,            R,              0,              simD,           0,
        SRL,            R,              0,              simE,           0,
        SRL,            R,              0,              simH,           0,
        SRL,            R,              0,              simL,           0,
        SRL,            ATPTR,          0,              simHL,          0,
        SRL,            R,              0,              simA,           0,
// CB 40-4F
        BIT,            BITPOS,         R,              0,              simB,
        BIT,            BITPOS,         R,              0,              simC,
        BIT,            BITPOS,         R,              0,              simD,
        BIT,            BITPOS,         R,              0,              simE,
        BIT,            BITPOS,         R,              0,              simH,
        BIT,            BITPOS,         R,              0,              simL,
        BIT,            BITPOS,         ATPTR,          0,              simHL,
        BIT,            BITPOS,         R,              0,              simA,
        BIT,            BITPOS,         R,              1,              simB,
        BIT,            BITPOS,         R,              1,              simC,
        BIT,            BITPOS,         R,              1,              simD,
        BIT,            BITPOS,         R,              1,              simE,
        BIT,            BITPOS,         R,              1,              simH,
        BIT,            BITPOS,         R,              1,              simL,
        BIT,            BITPOS,         ATPTR,          1,              simHL,
        BIT,            BITPOS,         R,              1,              simA,
// CB 50-5F
        BIT,            BITPOS,         R,              2,              simB,
        BIT,            BITPOS,         R,              2,              simC,
        BIT,            BITPOS,         R,              2,              simD,
        BIT,            BITPOS,         R,              2,              simE,
        BIT,            BITPOS,         R,              2,              simH,
        BIT,            BITPOS,         R,              2,              simL,
        BIT,            BITPOS,         ATPTR,          2,              simHL,
        BIT,            BITPOS,         R,              2,              simA,
        BIT,            BITPOS,         R,              3,              simB,
        BIT,            BITPOS,         R,              3,              simC,
        BIT,            BITPOS,         R,              3,              simD,
        BIT,            BITPOS,         R,              3,              simE,
        BIT,            BITPOS,         R,              3,              simH,
        BIT,            BITPOS,         R,              3,              simL,
        BIT,            BITPOS,         ATPTR,          3,              simHL,
        BIT,            BITPOS,         R,              3,              simA,
// CB 60-6F
        BIT,            BITPOS,         R,              4,              simB,
        BIT,            BITPOS,         R,              4,              simC,
        BIT,            BITPOS,         R,              4,              simD,
        BIT,            BITPOS,         R,              4,              simE,
        BIT,            BITPOS,         R,              4,              simH,
        BIT,            BITPOS,         R,              4,              simL,
        BIT,            BITPOS,         ATPTR,          4,              simHL,
        BIT,            BITPOS,         R,              4,              simA,
        BIT,            BITPOS,         R,              5,              simB,
        BIT,            BITPOS,         R,              5,              simC,
        BIT,            BITPOS,         R,              5,              simD,
        BIT,            BITPOS,         R,              5,              simE,
        BIT,            BITPOS,         R,              5,              simH,
        BIT,            BITPOS,         R,              5,              simL,
        BIT,            BITPOS,         ATPTR,          5,              simHL,
        BIT,            BITPOS,         R,              5,              simA,
// CB 70-7F
        BIT,            BITPOS,         R,              6,              simB,
        BIT,            BITPOS,         R,              6,              simC,
        BIT,            BITPOS,         R,              6,              simD,
        BIT,            BITPOS,         R,              6,              simE,
        BIT,            BITPOS,         R,              6,              simH,
        BIT,            BITPOS,         R,              6,              simL,
        BIT,            BITPOS,         ATPTR,          6,              simHL,
        BIT,            BITPOS,         R,              6,              simA,
        BIT,            BITPOS,         R,              7,              simB,
        BIT,            BITPOS,         R,              7,              simC,
        BIT,            BITPOS,         R,              7,              simD,
        BIT,            BITPOS,         R,              7,              simE,
        BIT,            BITPOS,         R,              7,              simH,
        BIT,            BITPOS,         R,              7,              simL,
        BIT,            BITPOS,         ATPTR,          7,              simHL,
        BIT,            BITPOS,         R,              7,              simA,
// CB 80-8F
        RES,            BITPOS,         R,              0,              simB,
        RES,            BITPOS,         R,              0,              simC,
        RES,            BITPOS,         R,              0,              simD,
        RES,            BITPOS,         R,              0,              simE,
        RES,            BITPOS,         R,              0,              simH,
        RES,            BITPOS,         R,              0,              simL,
        RES,            BITPOS,         ATPTR,          0,              simHL,
        RES,            BITPOS,         R,              0,              simA,
        RES,            BITPOS,         R,              1,              simB,
        RES,            BITPOS,         R,              1,              simC,
        RES,            BITPOS,         R,              1,              simD,
        RES,            BITPOS,         R,              1,              simE,
        RES,            BITPOS,         R,              1,              simH,
        RES,            BITPOS,         R,              1,              simL,
        RES,            BITPOS,         ATPTR,          1,              simHL,
        RES,            BITPOS,         R,              1,              simA,
// CB 90-9F
        RES,            BITPOS,         R,              2,              simB,
        RES,            BITPOS,         R,              2,              simC,
        RES,            BITPOS,         R,              2,              simD,
        RES,            BITPOS,         R,              2,              simE,
        RES,            BITPOS,         R,              2,              simH,
        RES,            BITPOS,         R,              2,              simL,
        RES,            BITPOS,         ATPTR,          2,              simHL,
        RES,            BITPOS,         R,              2,              simA,
        RES,            BITPOS,         R,              3,              simB,
        RES,            BITPOS,         R,              3,              simC,
        RES,            BITPOS,         R,              3,              simD,
        RES,            BITPOS,         R,              3,              simE,
        RES,            BITPOS,         R,              3,              simH,
        RES,            BITPOS,         R,              3,              simL,
        RES,            BITPOS,         ATPTR,          3,              simHL,
        RES,            BITPOS,         R,              3,              simA,
// CB A0-AF
        RES,            BITPOS,         R,              4,              simB,
        RES,            BITPOS,         R,              4,              simC,
        RES,            BITPOS,         R,              4,              simD,
        RES,            BITPOS,         R,              4,              simE,
        RES,            BITPOS,         R,              4,              simH,
        RES,            BITPOS,         R,              4,              simL,
        RES,            BITPOS,         ATPTR,          4,              simHL,
        RES,            BITPOS,         R,              4,              simA,
        RES,            BITPOS,         R,              5,              simB,
        RES,            BITPOS,         R,              5,              simC,
        RES,            BITPOS,         R,              5,              simD,
        RES,            BITPOS,         R,              5,              simE,
        RES,            BITPOS,         R,              5,              simH,
        RES,            BITPOS,         R,              5,              simL,
        RES,            BITPOS,         ATPTR,          5,              simHL,
        RES,            BITPOS,         R,              5,              simA,
// CB B0-BF
        RES,            BITPOS,         R,              6,              simB,
        RES,            BITPOS,         R,              6,              simC,
        RES,            BITPOS,         R,              6,              simD,
        RES,            BITPOS,         R,              6,              simE,
        RES,            BITPOS,         R,              6,              simH,
        RES,            BITPOS,         R,              6,              simL,
        RES,            BITPOS,         ATPTR,          6,              simHL,
        RES,            BITPOS,         R,              6,              simA,
        RES,            BITPOS,         R,              7,              simB,
        RES,            BITPOS,         R,              7,              simC,
        RES,            BITPOS,         R,              7,              simD,
        RES,            BITPOS,         R,              7,              simE,
        RES,            BITPOS,         R,              7,              simH,
        RES,            BITPOS,         R,              7,              simL,
        RES,            BITPOS,         ATPTR,          7,              simHL,
        RES,            BITPOS,         R,              7,              simA,
// CB C0-CF
        SET,            BITPOS,         R,              0,              simB,
        SET,            BITPOS,         R,              0,              simC,
        SET,            BITPOS,         R,              0,              simD,
        SET,            BITPOS,         R,              0,              simE,
        SET,            BITPOS,         R,              0,              simH,
        SET,            BITPOS,         R,              0,              simL,
        SET,            BITPOS,         ATPTR,          0,              simHL,
        SET,            BITPOS,         R,              0,              simA,
        SET,            BITPOS,         R,              1,              simB,
        SET,            BITPOS,         R,              1,              simC,
        SET,            BITPOS,         R,              1,              simD,
        SET,            BITPOS,         R,              1,              simE,
        SET,            BITPOS,         R,              1,              simH,
        SET,            BITPOS,         R,              1,              simL,
        SET,            BITPOS,         ATPTR,          1,              simHL,
        SET,            BITPOS,         R,              1,              simA,
// CB D0-DF
        SET,            BITPOS,         R,              2,              simB,
        SET,            BITPOS,         R,              2,              simC,
        SET,            BITPOS,         R,              2,              simD,
        SET,            BITPOS,         R,              2,              simE,
        SET,            BITPOS,         R,              2,              simH,
        SET,            BITPOS,         R,              2,              simL,
        SET,            BITPOS,         ATPTR,          2,              simHL,
        SET,            BITPOS,         R,              2,              simA,
        SET,            BITPOS,         R,              3,              simB,
        SET,            BITPOS,         R,              3,              simC,
        SET,            BITPOS,         R,              3,              simD,
        SET,            BITPOS,         R,              3,              simE,
        SET,            BITPOS,         R,              3,              simH,
        SET,            BITPOS,         R,              3,              simL,
        SET,            BITPOS,         ATPTR,          3,              simHL,
        SET,            BITPOS,         R,              3,              simA,
// CB E0-EF
        SET,            BITPOS,         R,              4,              simB,
        SET,            BITPOS,         R,              4,              simC,
        SET,            BITPOS,         R,              4,              simD,
        SET,            BITPOS,         R,              4,              simE,
        SET,            BITPOS,         R,              4,              simH,
        SET,            BITPOS,         R,              4,              simL,
        SET,            BITPOS,         ATPTR,          4,              simHL,
        SET,            BITPOS,         R,              4,              simA,
        SET,            BITPOS,         R,              5,              simB,
        SET,            BITPOS,         R,              5,              simC,
        SET,            BITPOS,         R,              5,              simD,
        SET,            BITPOS,         R,              5,              simE,
        SET,            BITPOS,         R,              5,              simH,
        SET,            BITPOS,         R,              5,              simL,
        SET,            BITPOS,         ATPTR,          5,              simHL,
        SET,            BITPOS,         R,              5,              simA,
// CB F0-FF
        SET,            BITPOS,         R,              6,              simB,
        SET,            BITPOS,         R,              6,              simC,
        SET,            BITPOS,         R,              6,              simD,
        SET,            BITPOS,         R,              6,              simE,
        SET,            BITPOS,         R,              6,              simH,
        SET,            BITPOS,         R,              6,              simL,
        SET,            BITPOS,         ATPTR,          6,              simHL,
        SET,            BITPOS,         R,              6,              simA,
        SET,            BITPOS,         R,              7,              simB,
        SET,            BITPOS,         R,              7,              simC,
        SET,            BITPOS,         R,              7,              simD,
        SET,            BITPOS,         R,              7,              simE,
        SET,            BITPOS,         R,              7,              simH,
        SET,            BITPOS,         R,              7,              simL,
        SET,            BITPOS,         ATPTR,          7,              simHL,
        SET,            BITPOS,         R,              7,              simA,
// ED 00-0F
        DEFB,           DIRECT,         DIRECT,         0xED,           0x00,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x01,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x02,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x03,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x04,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x05,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x06,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x07,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x08,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x09,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x0a,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x0b,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x0c,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x0d,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x0e,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x0f,
// ED 10-1F
        DEFB,           DIRECT,         DIRECT,         0xED,           0x10,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x11,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x12,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x13,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x14,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x15,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x16,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x17,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x18,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x19,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x1a,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x1b,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x1c,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x1d,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x1e,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x1f,
// ED 20-2F
        DEFB,           DIRECT,         DIRECT,         0xED,           0x20,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x21,
        DIV16,          0,         		0,         		0,           	0,
        DIV32,          0,         		0,         		0,           	0,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x24,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x25,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x26,
        IDIV32,         0,         		0,         		0,           	0,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x28,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x29,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x2a,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x2b,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x2c,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x2d,
        SSPD,           0,         		0,         		0,              0,
        GSPD,           0,         		0,         		0,              0,
// ED 30-3F
        OPEN,           0,         		0,         		0,           	0,
        CLOSE,          0,         		0,         		0,           	0,
        READ,           0,         		0,         		0,           	0,
        WRITE,          0,         		0,         		0,           	0,
		SEEK,           0,         		0,         		0,           	0,
        ERROR,          0,				0,				0,				0,
        FINDFIRST,      0,         		0,         		0,           	0,
        FINDNEXT,       0,         		0,         		0,           	0,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x38,
        LOAD,           0,         		0,         		0,           	0,
        CHDIR,      	0,         		0,         		0,           	0,
        GETDIR,      	0,         		0,         		0,           	0,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x3c,
        TRUNCATE,       0,         		0,         		0,           	0,
        PATHOPEN,       0,         		0,         		0,           	0,
        CLOSEALL,       0,         		0,         		0,           	0,
// ED 40-4F
        IN,				R,				ATR,			simB,           simC,
		OUT,           	ATR,         	R,         		simC,           simB,
        SBC,            RX,             RX,             simHL,          simBC,
        LD,             ATWORD,         RX,             0,              simBC,
        NEG,            0,              0,              0,              0,
        RETN,           0,         		0,         		0,           	0,
        IM,				0,				0,				0,				0,
        LD,           	R,         		R,         		simI,           simA,
        IN,				R,				ATR,			simC,           simC,
		OUT,			ATR,			R,				simC,           simC,
        ADC,           	RX,         	RX,         	simHL,          simBC,
        LD,             RX,             ATWORD,         simBC,          0,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x4c,
		RETI,           0,				0,				0,				0,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x4e,
		LD,				R,				R,				simR,           simA,	// TODO: special version of LD
// ED 50-5F
		IN,				R,				ATR,			simD,           simC,
        OUT,           	ATR,         	R,         		simC,           simD,
        SBC,            RX,             RX,             simHL,          simDE,
        LD,             ATWORD,         RX,             0,              simDE,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x54,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x55,
        IM,             DIRECT,         0,              1,              0,
        LD,           	R,         		R,         		simA,           simI,	// TODO: use a special version of LD
        IN,				R,				ATR,			simE,           simC,
		OUT,			ATR,			R,				simC,           simE,
        ADC,            RX,             RX,             simHL,          simDE,
        LD,             RX,             ATWORD,         simDE,          0,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x5c,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x5d,
        IM,				DIRECT,         0,				2,				0,
        LD,             R,              R,              simA,           simR,
// ED 60-6F
		IN,				R,				ATR,			simH,           simC,
        OUT,            ATR,            R,              simC,           simH,
        SBC,            RX,             RX,             simHL,          simHL,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x63,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x64,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x65,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x66,
        RRD,           	0,         		0,         		0,           	0,
		IN,				R,				ATR,			simL,           simC,
        OUT,            ATR,            R,              simC,           simL,
        ADC,           	RX,         	RX,         	simHL,          simHL,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x6b,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x6c,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x6d,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x6e,
        RLD,           	0,         		0,         		0,           	0,
// ED 70-7F
        DEFB,           DIRECT,         DIRECT,         0xED,           0x70,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x71,
		SBC,			RX,				RX,				simHL,          simSP,
        LD,             ATWORD,         RX,             0,              simSP,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x74,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x75,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x76,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x77,
		IN,				R,				ATR,			simA,           simC,
        OUT,           	ATR,         	R,         		simC,           simA,
        ADC,			RX,				RX,				simHL,          simSP,
        LD,             RX,             ATWORD,         simSP,          0,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x7c,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x7d,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x7e,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x7f,
// ED 80-8F
        DEFB,           DIRECT,         DIRECT,         0xED,           0x80,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x81,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x82,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x83,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x84,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x85,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x86,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x87,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x88,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x89,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x8a,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x8b,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x8c,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x8d,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x8e,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x8f,
// ED 90-9F
        DEFB,           DIRECT,         DIRECT,         0xED,           0x90,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x91,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x92,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x93,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x94,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x95,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x96,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x97,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x98,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x99,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x9a,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x9b,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x9c,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x9d,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x9e,
        DEFB,           DIRECT,         DIRECT,         0xED,           0x9f,
// ED A0-AF
        LDI,           	0,         		0,         		0,           	0,
        CPI,           	0,         		0,         		0,           	0,
        INI,           	0,         		0,         		0,           	0,
        OUTI,           0,         		0,         		0,           	0,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xa4,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xa5,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xa6,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xa7,
        LDD,            0,              0,              0,              0,
        CPD,           	0,         		0,         		0,           	0,
        IND,           	0,         		0,         		0,           	0,
        OUTD,           0,         		0,         		0,           	0,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xac,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xad,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xae,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xaf,
// ED B0-BF
        LDIR,           0,              0,              0,              0,
        CPIR,           0,         		0,         		0,           	0,
        INIR,           0,				0,				0,				0,
        OTIR,           0,				0,				0,				0,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xb4,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xb5,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xb6,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xb7,
        LDDR,           0,              0,              0,              0,
        CPDR,           0,         		0,         		0,           	0,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xba,		//INDR
        DEFB,           DIRECT,         DIRECT,         0xED,           0xbb,		//OTDR
        DEFB,           DIRECT,         DIRECT,         0xED,           0xbc,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xbd,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xbe,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xbf,
// ED C0-CF
        DEFB,           DIRECT,         DIRECT,         0xED,           0xc0,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xc1,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xc2,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xc3,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xc4,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xc5,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xc6,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xc7,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xc8,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xc9,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xca,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xcb,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xcc,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xcd,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xce,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xcf,
// ED D0-DF
        DEFB,           DIRECT,         DIRECT,         0xED,           0xd0,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xd1,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xd2,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xd3,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xd4,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xd5,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xd6,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xd7,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xd8,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xd9,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xda,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xdb,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xdc,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xdd,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xde,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xdf,
// ED E0-EF
        DEFB,           DIRECT,         DIRECT,         0xED,           0xe0,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xe1,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xe2,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xe3,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xe4,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xe5,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xe6,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xe7,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xe8,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xe9,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xea,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xeb,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xec,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xed,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xee,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xef,
// ED F0-FF
        CSINB,          0,              0,              0,              0,
        CSOUTC,         0,              0,              0,              0,
        CSEND,          0,              0,              0,              0,
        KBINC,          0,              0,              0,              0,
        KBWAIT,         0,              0,              0,              0,
        BREAK,          0,         		0,         		0,           	0,
        EXIT,           0,         		0,         		0,           	0,
        PUTCH,          0,				0,				0,				0,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xf8,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xf9,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xfa,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xfb,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xfc,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xfd,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xfe,
        DEFB,           DIRECT,         DIRECT,         0xED,           0xff,
// END
        0,              0,              0,              0,              0
        };

