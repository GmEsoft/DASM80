// DASM80.cpp : Defines the entry point for the console application.
//

#define _TRACE_ACTIVE_ 0
#define _TRACE_ if ( _TRACE_ACTIVE_ ) errprintf

char version[] = "** Z-80(tm) DISASSEMBLER V1.40beta1 - (c) 2015-24 GmEsoft, All rights reserved. **";

/* Version History
   ---------------

1.40b1:
* MOD - Code cleaning
* NEW - -V: Verbose
* NEW - -LS: commented label on separate line

1.32b1:
* FIX - --SVC option not working
* FIX - add comments in $SVC macro calls
* FIX - Short help
* FIX - trim commented source lines

1.31b1:
* NEW - --SVC to enable LS-DOS $SVC (RST 28H) macro generation
* MOD - Generate DB 'x'+80H in SEG_CHAR segments
* MOD - Generate ; 'x' comments to show ASCII equivalents of 8-bit literals

1.30b1:
* NEW - -LC to add a colon after labels; reformatted commented and long labels

1.02b8:
* FIX - DS/EQU$ labels generated more than once (LDOS SYS0/SYS)

1.02b7:
* FIX - code labels duplicated as DS/EQU$ labels

*/


#define _CRT_SECURE_NO_WARNINGS 1
#include <stdio.h>
#include <conio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>

#include "disasZ80.h"
#include "memZ80.h"

static symbol_t	symbols[SYMSIZE];
static uint		nSymbols = 0;

static char		verbose = 0;
static char		cmtdLblsOnSepLine = 0;


int isatty( int ); // from unistd.h

static void errprintf( char* msg, ... )
{
	va_list argptr;

	va_start( argptr, msg );
	vfprintf( stderr, msg, argptr );
}


static void pause()
{
	fputc( '\n', stderr );
	if ( isatty( _fileno( stdout ) ) )
	{
		fputs( "Press any key to continue . . . ", stderr );
		_getch();
		fputc( '\n', stderr );
	}
}

static void usage()
{
	fputs(	"\n"
			"usage: dasm80 [-C:]infile[.CMD]|-H:infile[.HEX]|-B[org]:infile[.BIN] [-O:outfile[.ASM]|-P:outfile[.PRN]] [-options]\n"
			"              options: -S:file -E:file -M:file -NE -NQ -NH -W[W] -LC -LS -V --SVC --ZMAC\n"
			"       dasm80 -?  for more details about options.\n"
			, stderr );
}

static void help()
{
	fputs ( "\r\n"
		"DASM80 [-C:]file|-H:file|-B[org]:file [-O:file|-P:file]\r\n" // Unsupported [-F:A|H|C]"
		"       [-S:file ] [-E:file [-E:file...]] [-M:file [-M:file...]]\r\n"
		"       [-W[W]] [-NE] [-NH] [-NQ] [-LC] [-LS] [-V] [--SVC] [--ZMAC]\r\n"
		"where:  [-C:]file    = code file in DOS loader format [.CMD]\r\n"
		"        -H:file      = code file in hex intel format [.HEX]\r\n"
		"        -B[org]:file = code file in binary [.BIN]\r\n"
		"        -O:file      = output [.ASM]\r\n"
		"        -P:file      = listing output [.PRN]\r\n"
		"        -S:file      = screening file [.SCR]\r\n"
		"        -E:file      = one or more equate files [.EQU]\r\n"
		"        -M:file      = one or more symbol tables [.MAP]\r\n"
		"        -W[W]        = [super] wide mode\r\n"
		"        -NE          = no new EQUates\r\n"
		"        -NH          = no header\r\n"
		"        -NQ          = no single quotes\r\n"
		"        -LC          = add colon after labels\r\n"
		"        -LS          = commented labels on separate line\r\n"
		"        -V           = verbose output of processing\r\n"
		"        --SVC        = generate LS-DOS SVC calls\r\n"
		"        --ZMAC       = ZMAC compatibility\r\n"
		"\n"
		"Screening file:\r\n"
		"        ;comment     = a comment\r\n"
		"        !range       = a range of code lines\r\n"
		"        %range       = a range of DB byte data byte\r\n"
		"        $range       = a range of DB char data\r\n"
		"        #range       = a range of DW word data\r\n"
		"        /range       = a range of DB/DW jump table data\r\n"
		"        @range:dest  = a relocatable range (@0000 to stop)\r\n"
		"range:  aaaa         = one to three bytes at aaaa\r\n"
		"        bbbb-cccc    = a range from bbbb to cccc\r\n"
		"        -dddd        = a range from 0000 to dddd\r\n"
		"        eeee-        = a range from eeee to FFFEh\r\n"
		, stderr );
}

static void errexit( int ex )
{
	if ( ex )
		usage();
	pause();
	exit( ex );
}

typedef struct segment
{
	ushort	beg;
	char	type;
	int		offset;
	ushort	offsetbeg;
	ushort	offsetend;
} segment_t;

static enum{ SEG_CODE='!', SEG_BYTE='%', SEG_WORD='#', SEG_CHAR='$', SEG_JUMP='/' };

static segment_t segments[SYMSIZE];
static int nsegments = 0;

static FILE *out;

static void addSegment( ushort beg, char type, int offset, ushort offsetbeg, ushort offsetend )
{
	int i,pos;

	_TRACE_( "addSegment( %04X, '%c', %04X, %04X, %04X );\n", beg, type, (ushort)offset, offsetbeg, offsetend );
	
	for ( pos=0; pos<nsegments; ++pos )
	{
		if ( beg <= segments[pos].beg )
			break;
	}

	if ( pos == nsegments || beg < segments[pos].beg )
	{
		++nsegments;
		
		if ( nsegments == SYMSIZE )
		{
			errprintf( "*** Screening table overflow: %c%04X- %04X-%04X:%04X.",  type, beg, offsetbeg, offsetend, (ushort)(offsetbeg-offset) );
			errexit( 1 );
		}

		i = nsegments-1;
		while ( i>pos )
		{
			segments[i] = segments[i-1];
			--i;
		}
	}

	segments[pos].beg = beg;
	segments[pos].type = type;
	segments[pos].offset = offset;
	segments[pos].offsetbeg = offsetbeg;
	segments[pos].offsetend = offsetend;

}

static segment_t* getSegment( ushort addr )
{
	int pos = 0;

	while ( pos<nsegments && segments[pos].beg<=addr )
	{
		++pos;
	}
	return !pos ? NULL : &segments[pos-1];
}

// return byte from hexadecimal string *s at offset i
static int htoc (char *s, int i)
{
	int x,y;
	x = s[i]-'0';
	y = s[i+1]-'0';
	if (x>9)
		x-=7;
	if (y>9)
		y-=7;
	return (x<<4)|y;
}

// Load symbol tables
static void symLoad(char *fileName)
{
	FILE *file;
	char s[256];
	uint n;
	char seg = 0;
	char symname[80];
	uint symval;
	uint bit;
	char *p;

	//trace(cprintf("symLoad(""%s"")\r\n", fileName) );
	file = fopen(fileName,"r");
	if (file!=NULL) {
		while (!feof(file)) 
		{
			n = 0;
			while ( n<255 && !feof(file) )
			{
				s[n] = fgetc(file);
				if ( s[n] == '\n' || s[n] == '\r' )
					break;
				++n;
			}

			if ( n == 0 )
				continue;

			s[n] = 0;

			if (strcmp(s,"code") == 0) 
			{
				seg = 'C';
			} 
			else if (strcmp(s,"data") == 0) 
			{
				seg = 'D';
			} 
			else if (strcmp(s,"stkseg") == 0) 
			{
				seg = 'D';
			} 
			else if (strcmp(s,"bit") == 0) 
			{
				seg = 'B';
			} 
			else if (strcmp(s,"svc") == 0) 
			{
				seg = 'S';
			} 
			else if ( ( s[0] == ' ' || s[0] == '\t' ) && seg != 0 ) 
			{
				if ( strstr(s,"        ") == s || s[0] == '\t' ) 
				{
					if (strstr(s,"ADDRESS") == NULL &&
						strpbrk(s,"-#") == NULL &&
						nSymbols < SYMSIZE
						) 
					{
						symval = -1;
						sscanf(s, " %40s %x%*c%x", symname, &symval, &bit);
						if (symval != -1 && symname[0] != ';') 
						{
							for ( p=symname; *p; ++p )
							{
								*p = toupper( *p );
							}
							strcpy(symbols[nSymbols].name, symname);
							if (seg == 'B')
								if (symval<0x80)
									symbols[nSymbols].val = ((symval-0x20)<<3) + bit;
								else
									symbols[nSymbols].val = symval + bit;
							else
								symbols[nSymbols].val = symval;
							symbols[nSymbols].lval = symbols[nSymbols].val;
							symbols[nSymbols].seg = seg;
							symbols[nSymbols].label = 0;
							symbols[nSymbols].newsym = 0;
							symbols[nSymbols].ds = 1;
						}
						//trace(cprintf("%c:%x\t%s\r\n", seg, symval, symname));
						//tgetch();
						++nSymbols;
						if ( nSymbols == SYMSIZE ) 
						{
							errprintf( "*** Symbol Table Overflow (%s).", symname );
							errexit( 1 );
						}
					}
				}
			} 
			else
			{
				seg = 0;
			}
		}
		fclose (file);
		//trace(cprintf("ok\r\n"));
		//tgetch();
	}
	else
	{
		errprintf( "*** Error opening MAP file: %s", fileName );
		errexit( 1 );
	}
}

/*	Load screening file
	
	Screening file syntax:
		rangedef[,rangedef...]
		[rangedef[,rangedef...]]
		...
	where rangedef is:
		[T]aaaa[-[bbbb]]
		-or-
		@aaaa[-bbbb]:cccc;
			relocate (code from aaaa to bbbb is to be moved to cccc),
			applies to all subsequent segments
	where:
		aaaa = start of range in hex
		bbbb = end of range in hex (dflt: aaaa or FFFF if '-' specified)
		cccc = relocation address
		T is one of:
			% = range of DB bytes (default)
			$ = range of DB chars
			# = range of DW words
			! = range of code
			/ = range of (DB chars, DW words)
*/
static void scrLoad( char *fileName )
{
	FILE *file;
	char s[256];
	uint n;
	ushort beg, end, reloc;
	ushort *pval;
	char type, type0;
	segment_t *pSeg;
	int offset = 0;
	ushort offsetbeg = 0, offsetend = 0;


	//trace(cprintf("scrLoad(""%s"")\r\n", fileName) );
	file = fopen(fileName,"r");
	if (file!=NULL) {
		while (!feof(file)) 
		{
			n = 0;
			while ( n<255 && !feof(file) )
			{
				s[n] = fgetc(file);
				if ( feof( file ) || s[n] == '\n' || s[n] == '\r' )
					break;
				++n;
			}

			if ( n == 0 )
				continue;

			s[n] = 0;

			n = 0;
			beg = end = reloc = 0;
			type = SEG_BYTE;
			pval = &beg;

			while ( 1 )
			{
				if ( s[n] == ';' )
					break;

				if ( s[n] == '$' || s[n] == '#' || s[n] == '%' || s[n] == '!' || s[n] == '/' || s[n] == '@' )
				{
					type = s[n];
					//if ( type=='#' ) type = '%';
					++n;
				}

				//if ( !s[n] )
				//	break;

				while ( isalnum( s[n] ) )
				{
					*pval = ( *pval << 4 ) + toupper( s[n] ) - '0';
					if ( isalpha( s[n] ) )
						*pval -= 7;
					++n;
				}

				if ( s[n] == '-' )
				{
					pval = &end;
					++n;
				}
				else if ( s[n] == ':' )
				{
					pval = &reloc;
					++n;
				}
				else if ( !s[n] || s[n] == ',' || s[n] == ' ' || s[n] == '\t' || s[n] == ';' )
				{
					if ( type == '@' )
					{
						if ( !beg && !reloc )
						{
							offset = 0;
							offsetbeg = offsetend = 0;
						}
						else
						{
							offset = (int)beg - (int)reloc;
							offsetbeg = beg;
							offsetend = end;

							type0 = SEG_CODE;
							pSeg = getSegment( beg );
							if ( pSeg )
								type0 = pSeg->type;
							addSegment( beg, type0, offset, offsetbeg, offsetend );
						}
						
						errprintf( "--- Relocating %04X-%04X to %04X - offset=%04X\n", offsetbeg, offsetend, reloc, (ushort)offset );
					}
					else
					{
						type0 = SEG_CODE;
						if ( pSeg = getSegment( beg ) )
							type0 = pSeg->type;
						if ( pval == &beg )
							end = beg + ( type == SEG_WORD ? 1 : 0 ) + ( type == SEG_JUMP ? 2 : 0 );
						if ( end && getSegment( end+1 ) == pSeg )
							addSegment( end+1, type0, offset, offsetbeg, offsetend ); // or old offset?
						addSegment( beg, type, offset, offsetbeg, offsetend );
					}
					pval = &beg;
					beg = end = 0;
					type = SEG_BYTE;
					if ( !s[n] )
						break;
					++n;
				}
				else
				{
					++n;
				}
			}
		}
		fclose (file);
		//trace(cprintf("ok\r\n"));
		//tgetch();
	}
	else
	{
		errprintf( "*** Error opening SCR file: %s", fileName );
		errexit( 1 );
	}
}

// Load equates file
static void equLoad( char *fileName )
{
	FILE *file;
	char s[256];
	uint n;
	ushort dec;
	ushort hex;
	char c, seg;
	int token;
	char name[16];
	char equ[8];
	int count;
	uint comment;

	//trace(cprintf("equLoad(""%s"")\r\n", fileName) );
	file = fopen(fileName,"r");
	if (file!=NULL) {
		while (!feof(file)) 
		{
			n = 0;
			while ( n<255 && !feof(file) )
			{
				s[n] = fgetc(file);
				if ( feof( file ) || s[n] == '\n' || s[n] == '\r' )
					break;
				++n;
			}

			if ( n == 0 )
				continue;

			s[n] = 0;

			n = 0;

			token = count = 0;
			dec = hex = 0;

			while ( s[n] && s[n] != ';' )
			{
				c = toupper( s[n] );

				if ( c == ' ' || c == '\t' )
				{
					if ( !token || count )
					{
						++token;
						count = 0;
					}
				}
				else
				{
					switch( token )
					{
					case 0:
						if( count < 15 )
						{
							name[count++] = c;
							name[count] = 0;
						}
						break;
					case 1:
						if( count < 7 )
						{
							equ[count++] = c;
							equ[count] = 0;
						}
						break;
					case 2:
						if ( !strcmp( equ, "EQU" ) )
						{
							seg = 'C';

							if ( isalpha( c ) )
							{
								// Relocatable blocks
								// SYM	EQU		Znnnn
								seg = c;
								++n;
								c = toupper( s[n] );
							}

							while ( isalnum( c ) && c != 'H' )
							{
								++count;
								c -= '0';
								dec = dec * 10 + c;
								hex = ( hex << 4 ) + c;
								if ( c > 9 )
									hex -= 7;
								++n;
								c = toupper( s[n] );
							}

							comment = 0;
							while ( !comment && s[n] )
							{
								if ( s[n] == ';' )
									comment = n;
								++n;
							}

							if ( count )
							{
								if ( c != 'H' )
									hex = dec;
								seg = ( *name == '@' && hex < 128 ) ? 'S' : seg;
								for ( n=0; n<nSymbols; ++n )
									if ( symbols[n].val == hex && symbols[n].seg == seg )
										break;
								strcpy( symbols[n].name, name );
								// segment = 'S' for LS-DOS SVC calls, 'C' otherwise
								symbols[n].seg = seg;
								symbols[n].val = hex;
								symbols[n].lval = symbols[n].val;
								symbols[n].label = isdigit( *name );
								symbols[n].newsym = 0;
								symbols[n].ds = 1;

								if ( comment )
									strncpy_s( symbols[n].comment, sizeof( symbols[n].comment ), 
									           s + comment, sizeof( symbols[n].comment ) - 1 );
								else
									*symbols[n].comment = 0;

								if ( n == nSymbols )
									++nSymbols;

								if ( nSymbols == SYMSIZE )
								{
									errprintf( "*** %s - symbol table overflow: %s.", fileName, name );
									errexit( 1 );
								}
							}
						}
						++token;
					}
					if ( token > 2 )
						break;
				}
				++n;
			}
		}
		fclose (file);
		//trace(cprintf("ok\r\n"));
		//tgetch();
	}
	else
	{
		errprintf( "*** Error opening EQU file: %s", fileName );
		errexit( 1 );
	}
}

// Load Binary file
static uint binLoad( char *fileName, uint org )
{
    FILE *file;
	char s[256];
	uint i,ptr,tra;
	size_t count;

	ptr = tra = org;
    file = fopen(fileName,"rb");
    if (file!=NULL) {
        while (!feof(file)) {
			count = fread( s, 1, sizeof( s ), file );
			for ( i=0; i<count; ++i )
				putData( ptr++, s[i] );
        }
        fclose (file);
		errprintf( "Binary file end: %04X\n", ptr );
    }
	else
	{
		errprintf( "*** Error opening BIN file: %s", fileName );
		errexit( 1 );
	}
	return tra;
}
// Load Hex-Intel file
static uint hexLoad( char *fileName )
{
    FILE *file;
    char s[256];
    uint i,t,n,ptr;
	uint tra=0;

    file = fopen(fileName,"r");
    if (file!=NULL) {
        while (!feof(file)) {
            fscanf (file, "%250s\r\n", s);
            if (s[0] == ':') {
                n = htoc (s,1);
                ptr = (htoc (s,3)<<8) | htoc(s,5);
                t = htoc (s,7);
                switch (t) {
                    case 0:
                        for (i=9;i<9+n+n;i+=2) {
							putData( ptr++, htoc (s,i) );
                        }
                        break;
                    case 3:
                        tra = (htoc (s,9) << 8) | htoc (s,11);
                        break;
                    case 1:
                        tra = ptr;
                        break;
                }
            }
        }
        fclose (file);
    }
	else
	{
		errprintf( "*** Error opening HEX file: %s", fileName );
		errexit( 1 );
	}
	return tra;
}

static char comment[257] = { 0 };

uint loadCmdFile(FILE* file)
{
	uint addr;
	uint tra = 0;
	char byte;
	char counter;
	char *s;

	//cprintf("loading...\r\n");
	while ( !feof( file ) )
	{
		byte = fgetc( file );
		//cprintf("%02x",byte);
		switch( byte )
		{
		case 0x01:
			counter = fgetc( file ) - 2;
			addr = fgetc( file );
			addr += ( fgetc( file ) << 8 );
			//cprintf(" CMD data: addr=%04x length=%02x\r\n", addr, counter);
			do
			{
				byte = fgetc( file );
				//cprintf("%02x",byte);
				putData( addr, byte );
				addr++;
				counter--;
			} while( counter && !feof( file ) );
			break;
		case 0x02:
			fgetc( file );
			tra = fgetc( file );
			tra += ( fgetc( file ) << 8 );
			//cprintf(" CMD trans: addr=%04x\r\n", tra);
			return tra;
		case 0x05:
			counter = fgetc( file );
			//cprintf(" CMD data: addr=%04x length=%02x\r\n", addr, counter);
			fputs( "CMD Name: ", stderr );
			do
			{
				byte = fgetc( file );
				fputc( ( byte >= 0x20 && byte < 0x7F ) ? byte : '.', stderr );
				counter--;
			} while( counter && !feof( file ) );
			fputc( '\n', stderr );
			break;
		case 0x1F:
			counter = fgetc( file );
			s = comment;						
			//cprintf(" CMD data: addr=%04x length=%02x\r\n", addr, counter);
			do
			{
				byte = fgetc( file );
				//cprintf("%02x",byte);
				*s++ = byte;
				counter--;
			} while( counter && !feof( file ) );
			*s = 0;
			fputs( comment, stderr );
			break;
		}
	}

	//cprintf("\nloaded...\r\n");
	return tra;

}

// load file in TRS-80 load file format
uint cmdLoad ( const char *name )
{
	FILE *file;
	uint tra = 0;

	file = fopen( name, "rb" );
	if ( file )
	{
		tra = loadCmdFile( file );
		fclose( file );
	}
	else
	{
		errprintf( "*** Error opening CMD file: %s", name );
		errexit( 1 );
	}
	return tra;
}

// Allocate memory
static int allocMemory(char * *data)
{
	*data = malloc (0x24001);
	if (*data == NULL)
		return -1;
	memset( *data, 0x76, 0x24001 );
	return 0;
}

// Free allocated memory
static void freeMemory (char * *data)
{
	free (*data);
}

static char isPrintBytes = 1;

static void printData( ushort pc0, ushort pc )
{
	ushort p;

	fprintf( out, "%04X ", pc0 );
	for ( p=pc0; p<pc; ++p )
		fprintf( out, "%02X", getData( p ) );
	for ( ;p<pc0+4; ++p )
		fprintf( out, "  " );
	fprintf( out, "\t" );
}

static void printTab()
{
	if ( isPrintBytes )
	{
		fputs( "\t\t", out );
	}
}

static void printBytes( ushort pc0 )
{
	if ( isPrintBytes )
	{
		pc = pc0;
		source();
		printData( pc0, pc );
		pc = pc0;
	}
}

static void printAddr( ushort pc0 )
{
	if ( isPrintBytes )
		printData( pc0, pc0 );
}

static void printChars( ushort pc0, ushort pc )
{
	uchar c;
	if ( isPrintBytes )
	{
		for ( ; pc0<pc; ++pc0 )
		{
			c = getData( pc0 );
			if ( c <= 0x20 || c >= 0x7F )
				fputc( '.', out );
			else
				fputc( c, out );
		}
	}
}

static void printLabel( ushort addr )
{
	char *lbl;
	lbl = getLabel( addr, DS_NO );
	if ( *lbl )
	{
		fputs( lbl, out );
	}
	//if ( !labelcolon || strlen( lbl ) < 8 )
		fputc( '\t', out );
}

static void printLabelComment( ushort addr, uchar ds )
{
	if ( cmtdLblsOnSepLine )
{
	char *lbl, *cmt;
	lbl = getLabel( addr, ds );
	cmt = getLastComment();
	if ( cmt || strlen( lbl ) >= 8 )
	{
		if ( isPrintBytes )
		{
			//fputs( "\t\t", out );
			fprintf( out, "=%04X\t\t", addr );
		}
		lbl = getLabel( addr, DS_NO );
		printLabel( addr );
		
		fprintf( out, "%s\n", cmt ? cmt : "" );
	}
}
	else
	{
		getLabel( addr, ds );
		if ( getLastComment() )
		{
			if ( isPrintBytes )
			{
				fputs( "\t\t", out );
			}
			fprintf( out, "\t%s\n", getLastComment() );
		}
	}
}

static void printLabelIfNotDoneInComment( ushort addr )
{
	if ( cmtdLblsOnSepLine )
{
	char *cmt, *lbl;
	lbl = getLabel( addr, DS_NO );
	cmt = getLastComment();
	if ( !cmt && strlen( lbl ) < 8 )
	{
		printLabel( addr );
	}
	else
	{
		fputc( '\t', out );
	}
}
	else
	{
		printLabel( addr );
	}
}

static void printHexByte( uchar byte )
{
	if ( byte >= 0xA0 )
		fputc( '0', out );
	fprintf( out, "%02XH", byte );
}

static void printHexWord( ushort word )
{
	if ( word >= 0xA000 )
		fputc( '0', out );
	fprintf( out, "%04XH", word );
}

static char *eol = "\n";

static void printEol()
{
	fprintf( out, eol );
}


char* timeStr()
{
	struct tm *ptime;
	time_t clock;
	char *s, *p;

	time( &clock );
	ptime = localtime( &clock );
	s = asctime( ptime );
	p = s + strlen( s ) - 1;
	if ( *p == 0x0A )
		*p = 0;
	return s;
}

char* packSource()
{
	char *s, *p, *d, *l;
	char c, f, cmt;

	s = source();
	if ( !isPrintBytes )
	{
		p = d = s;
		c = 0;
		f = 0;
		l = 0;
		cmt = 0;

		while ( *p )
		{
			++c;
			if ( *p == '\'' )
			{
				f = 1;			// Quote flag => disable pack
			}

			if ( cmt || f || *p != ' ' )
			{
				if ( *p == ';' )
					cmt = 1;	// comment found
				*d++ = *p;		// Copy char
				if ( *p != ' ' )
					l = d;		// Last non-blank
			}
			else if ( !(c&7) )	// Tab pos ?
			{
				*d++ = '\t';	// Replace blanks with tab
			}
			++p;
		}
		*l = 0;					// Trim
	}
	else
	{
		s[48] = 0;
	}
	return s;
}


static void addDefaultExt( char *name, char *ext )
{
	if	(	strrchr( name, '.' ) == NULL 
		||	strrchr( name, '.' ) < strrchr( name, '\\' )
		)
	{
		strcat( name, ext );
	}
}

int main(int argc, char* argv[])
{
	uint i, nPass, nRange;
	char *s;
	uchar ch;

	char outFileName[80];
	char hexFileName[80];
	char cmdFileName[80];
	char binFileName[80];
	char scrFileName[80];
	char symFileName[8][80];
	uint nSymFiles = 0;
	char equFileName[8][80];
	uint nEquFiles = 0;
	char noSingleQuote = 0;
	char zmac = 0;
	char noNewSymbol  = 0;
	char noHeader = 0;
	ushort tra = 0;
	segment_t *pSeg = 0;
	char outFormat = 'A';
	ushort width = 63;
	ushort dsMax = 0x200; // Max value for DS instruction
	int org;

	//	char *sourceline;
//	char buf[256];

	outFileName[0] = '\0';
 	scrFileName[0] = '\0';
	hexFileName[0] = '\0';
	cmdFileName[0] = '\0';
	binFileName[0] = '\0';

	out = stdout;

	fputs( version, stderr );
	fputc( '\n', stderr );

	for (i=1; i<(uint)argc; i++) 
	{
		s = argv[i];

		ch = *(s++);

		if ( ch=='/' || ch=='-' ) 
		{
			switch ( toupper(*(s++)) )
			{
			case 'F':   // output format (currently not supported)
				if ( *s == ':' )
					s++;
				if ( isalpha( *s ) )
					outFormat = toupper( *s );
				break;
			case 'M':   // symbol table
				if ( *s == ':' )
					s++;
				if ( nSymFiles == 8 )
				{
					errprintf( "*** %s - More than 8 .map files specified.", argv[i] );
					errexit( 1 );
				}
				strcpy (symFileName[nSymFiles], s);
				addDefaultExt( symFileName[nSymFiles], ".map" );
				nSymFiles++;
				break;
			case 'H':	// Intel Hex file
				if ( *s == ':' )
					s++;
				if ( *hexFileName || *binFileName || *cmdFileName )
				{
					errprintf( "*** %s - Only one input file allowed.", argv[i] );
					errexit( 1 );
				}
				strcpy( hexFileName, s );
				addDefaultExt( hexFileName, ".hex" );
				break;
			case 'C':	// DOS CMD file
				if ( *s == ':' )
					s++;
				if ( *hexFileName || *binFileName || *cmdFileName )
				{
					errprintf( "*** %s - Only one input file allowed.", argv[i] );
					errexit( 1 );
				}
				strcpy( cmdFileName, s );
				addDefaultExt( cmdFileName, ".cmd" );
				break;
			case 'B':	// BIN file
				org = 0;
				if ( isdigit( *s ) )
				{
					org = *(s++) - '0';
					while ( isalnum( *s ) )
					{
						org = (org<<4) + ( *s > '9' ? toupper( *s ) + 10 - 'A' : *s - '0' );
						++s;
					}
				}
				if ( *s == ':' )
					s++;
				if ( *hexFileName || *binFileName || *cmdFileName )
				{
					errprintf( "*** %s - Only one input file allowed.", argv[i] );
					errexit( 1 );
				}
				strcpy( binFileName, s );
				addDefaultExt( binFileName, ".bin" );
				break;
			case 'S':	// Screening file
				if ( *s == ':' )
					s++;
				strcpy( scrFileName, s );
				addDefaultExt( scrFileName, ".scr" );
				break;
			case 'E':	// Equate file
				if ( *s == ':' )
					s++;
				if ( nEquFiles == 8 )
				{
					errprintf( "*** %s - More than 8 equ files specified.", argv[i] );
					errexit( 1 );
				}
				strcpy( equFileName[nEquFiles], s );
				addDefaultExt( equFileName[nEquFiles], ".equ" );
				++nEquFiles;
				break;
			case 'O':	// Output ASM file
				if ( *s == ':' )
					s++;
				if ( *outFileName )
				{
					errprintf( "*** %s - Only one output file allowed.", argv[i] );
					errexit( 1 );
				}
				strcpy( outFileName, s );
				isPrintBytes = 0;
				break;
			case 'P':	// Output PRN file
				if ( *s == ':' )
					s++;
				if ( *outFileName )
				{
					errprintf( "*** %s - Only one output file allowed.", argv[i] );
					errexit( 1 );
				}
				strcpy( outFileName, s );
				addDefaultExt( outFileName, ".prn" );
				break;
			case 'W':	// Wide mode
				width = 79;
				if ( toupper( *s ) != 'W' )
					break;
				++s;
				width = 130;
				break;
			case 'N':	// No flags
				switch( toupper( *s++ ) )
				{
				case 'E':	// No New EQUates
					noNewSymbol = 1;
					break;
				case 'H':	// No Header
					noHeader = 1;
					break;
				case 'Q':	// No Single Quote
					noSingleQuote = 1;
					break;
				default:
					--s;
				}
				break;
			case 'L':	// Labels
				switch( toupper( *s++ ) )
				{
				case 'C':	// Colon after labels
					labelColon = 1;
					break;
				case 'S':	// Commented labels on separate line
					cmtdLblsOnSepLine = 1;
					break;
				default:
					--s;
				}
				break;
			case '-':
				if ( !_stricmp( s, "zmac" ) )
				{
					zmac = 1;
				}
				else if ( !_stricmp( s, "svc" ) )
				{
					usesvc = 1;
				}
				else
				{
					errprintf( "*** %s - Unrecognized option.", argv[i] );
				}
				break;
			case 'V':	// Verbose
				verbose = 1;
				break;
			case '!':	// Wait keypress before starting
				fputs( "Press ENTER to start.", stderr );
				getchar();
				break;
			case '?':	// Help
				help();
				errexit( 0 );
				break;
			default:
				errprintf( "*** %s - Unrecognized option.", argv[i] );
				errexit( 1 );
			}
		}
		else
		{
			if ( *hexFileName || *binFileName || *cmdFileName )
			{
				errprintf( "*** %s - Only one input file allowed.", argv[i] );
				errexit( 1 );
			}
			strcpy( cmdFileName, argv[i] );
			addDefaultExt( cmdFileName, ".cmd" );
		}
	}

	if ( !*hexFileName && !*binFileName && !*cmdFileName )
	{
		errprintf( "*** Missing input filename." );
		errexit( 1 );
	}

	addSegment( 0, SEG_CODE, 0, 0, 0 );
	addSegment( 0xFFFF, SEG_CODE, 0, 0, 0 );

	if ( *scrFileName )
		scrLoad( scrFileName );

	for ( i=0; i<nSymFiles; ++i )
		symLoad( symFileName[i] );

	for ( i=0; i<nEquFiles; ++i )
		equLoad( equFileName[i] );

	setSymbols( symbols, nSymbols, SYMSIZE );

	setGetData( getData );

	if ( *hexFileName )
	{
		tra = hexLoad( hexFileName );
	}

	if ( *cmdFileName )
	{
		tra = cmdLoad( cmdFileName );
	}

	if ( *binFileName )
	{
		tra = binLoad( binFileName, org );
	}

	if ( verbose )
	{
		errprintf( "Ranges: %d\n", nRanges );
		for ( nRange = 0; nRange < nRanges; ++nRange )
		{
			errprintf( "%d:\t%04X-%04X\n", nRange, ranges[nRange].beg, ranges[nRange].end );
		}
	}

	pSeg = getSegment( tra );

	if ( *outFileName )
	{
		switch( outFormat )
		{
		case 'A':
			addDefaultExt( outFileName, ".asm" );
			break;
		case 'C':
			addDefaultExt( outFileName, ".c" ); // currently not supported
			break;
		case 'H':
			addDefaultExt( outFileName, ".txt" ); // currently not supported
			break;
		default:
			errprintf( "Unrecognized output format: %c", outFormat );
			errexit( 1 );
		}

		out = fopen( outFileName, "wb" );

		if ( !noHeader )
		{
			fprintf( out, ";%s", version );
			printEol();
			fprintf( out, ";" );
			printEol();
			fprintf( out, ";\t%s", timeStr() );
			printEol();
			fprintf( out, ";" );
			printEol();

			if ( *binFileName )
			{
				fprintf( out, ";\tDisassembly of : %s", binFileName );
				printEol();
			}
			else if ( *hexFileName )
			{
				fprintf( out, ";\tDisassembly of : %s", hexFileName );
				printEol();
			}
			else if ( *cmdFileName )
			{
				fprintf( out, ";\tDisassembly of : %s", cmdFileName );
				printEol();
			}
			
			for ( i=0; i<nEquFiles; ++i )
			{
				fprintf( out, ";\tEquates file   : %s", equFileName[i] );
				printEol();
			}

			if ( *scrFileName )
			{
				fprintf( out, ";\tScreening file : %s", scrFileName );
				printEol();
			}

			printEol();
		}

		if ( *comment )
		{
			printTab();
			fprintf( out, "\tCOM\t'<%s>'", comment );
			printEol();
			printEol();
		}

	}

	for ( nPass=0; nPass<3; ++nPass )
	{
		int offset = 0;
		ushort org = 0xFFFF;

		resetSymbols();

		pcOffsetSeg = 'Z'+1;
		pcOffset = 0;

		_TRACE_( "*** PASS %d\n", nPass );
		switch( nPass )
		{
		case 2:
			for ( i = 0; s = getMacroLine( i ); ++i )
			{
				printTab();
				fputs( s, out );
				printEol();
			}

			noNewEqu = noNewSymbol;
			
			for ( i=0; i<nSymbols; ++i )
			{
				symbol_t *sym = &symbols[i];
				if ( !sym->label && sym->ref && ( !noNewSymbol || !sym->newsym ) )
				{
					// TODO: bug when used with relocatable blocks (ex: backup/cmd)
					printAddr( sym->lval );
					fprintf( out, "%s\tEQU\t", sym->name );
					printHexWord( sym->val ); //////////////////////////////
					if ( *sym->comment )
						fprintf( out, "\t\t%s", sym->comment );
					printEol();
				}
			}
			printEol();
			break;
		}

		for ( nRange = 0; nRange < nRanges; ++nRange )
		{
			char skip = 0;
			char type = SEG_CODE;
			uint pcbeg = ranges[nRange].beg;
			uint pcend = ranges[nRange].end;
			ushort nqchars;

			for ( pc = pcbeg; ( pc < pcend || ( pc<0x10000 && !pcend ) ); )
			{
				ushort pc0, segbegin = 0, segend = 0xFFFF;
				int segoffset = 0;
				ushort segoffsetbeg, segoffsetend;

				char isquote = 0;
				short w;

				if ( !pSeg || pc < pSeg->beg || pc >= (pSeg+1)->beg )
				{
					pSeg = getSegment( pc );
				}

				if ( pSeg )
				{
					skip = skip || ( type == SEG_CODE ) != ( pSeg->type == SEG_CODE );
					type = pSeg->type;
					segbegin = pSeg->beg;
					segend = (pSeg+1)->beg;
					segoffset = pSeg->offset;
					segoffsetbeg = pSeg->offsetbeg;
					segoffsetend = pSeg->offsetend;
					if ( offset != segoffset || pc != org )
					{
						_TRACE_( "*** offset=%04X segoffset=%04X - pc=%04X org=%04X ", offset, segoffset, pc, org );
						_TRACE_( "*** Segment: %04X-%04X '%c' offset %04X %04X-%04X )\n", 
							segbegin, segend, type, (ushort)segoffset, segoffsetbeg, segoffsetend );
					}
				}

				pc0 = pc;

				switch( nPass )
				{
				case 0:	// get references
					if ( offset != segoffset )
					{
						--pcOffsetSeg;
						offset = segoffset;
						pcOffset = segoffset;
						pcOffsetBeg = segoffsetbeg;
						pcOffsetEnd = segoffsetend;
					}

					switch( type )
					{
					case SEG_CODE:
						source();
						break;
					case SEG_BYTE:
					case SEG_CHAR:
						++pc;
						break;
					case SEG_WORD:
#if 1
						getLAddr();
#else
						getXAddr( getData( pc ) | ( getData( pc+1 ) << 8 ) );
						pc+=2;
#endif
						break;
					case SEG_JUMP:
						++pc;
#if 1
						getLAddr();
#else
						getXAddr( getData( pc ) | ( getData( pc+1 ) << 8 ) );
						pc+=2;
#endif
						break;
					}
					org = pc;
					break;
				case 1: // flag symbols as labels
					if ( pc0 != org || offset != segoffset )
					{
#if 1
						if ( org < 0xFFFF /*&& *getLabel( org )*/ )
						{
							for ( pc = org + 1; pc <= pc0 /*&& pc <= segend */&& pc <= (uint)(org + dsMax); ++pc )
							{
								if ( pc == pc0 || *getLabel( pc, DS_YES ) )
								{
									getLabel( org, DS_YES );
									org = pc;
								}
							}

							if ( pc0 != org )
							{
								getLabel( org, DS_YES ) ;
							}
						}

						pc = pc0;

#else
						if ( pc0 > org && pc0 >= segbegin && pc0 < segend && pc0 - org <= dsMax )
						{
							for ( ; org < pc0; ++org )
							{
								getLabel( org );
							}
						}
#endif
						if ( offset != segoffset )
						{
							--pcOffsetSeg;
							pcOffset = 0;
							//getLabel( pc0, DS_YES );
							offset = segoffset;
							pcOffset = segoffset;
							pcOffsetBeg = segoffsetbeg;
							pcOffsetEnd = segoffsetend;
							getLabel( pc0, DS_YES );
						}
					}

					switch( type )
					{
					case SEG_CODE:
						source();
						break;
					case SEG_BYTE:
					case SEG_CHAR:
						++pc;
						break;
					case SEG_WORD:
						getLAddr();
						break;
					case SEG_JUMP:
						++pc;
						getLAddr();
						break;
					}

					for ( ; pc0 < pc; ++pc0 )
						getLabel( pc0, DS_NO );
					org = pc;
					break;
				case 2: // generate output
					// Generate new origin with DS/EQU/ORG
					if ( pc0 != org || offset != segoffset )
					{
						skip = 0;
#if 1
						if ( org < 0xFFFF /* && *getLabel( org ) */)
						{
							printEol();

							//	label	DS		nnnn
							for ( pc = org + 1; pc <= pc0 /*&& pc <= segend */&& pc <= (uint)(org + dsMax); ++pc )
							{
								if ( pc == pc0 || *getLabel( pc, DS_YES ) )
								{
									printLabelComment( org, DS_YES );
									printAddr( org );
									fprintf( out, "%s\tDS\t", getLabel( org, DS_YES ) );
									setLabelGen( org );
									printHexWord( pc - org );
									printEol();
									org = pc;
								}
							}

							//	label	EQU		$
							if ( pc0 != org && *getLabel( org, DS_YES ) )
							{
								printLabelComment( org, DS_YES );
								printAddr( org );
								fprintf( out, "%s\tEQU\t$", getXAddr( org ) );
								setLabelGen( org );
								printEol();
							}
						}

						pc = pc0;

						// PHASE change
						if ( offset != segoffset )
						{
							--pcOffsetSeg;
							printEol();

							// end PHASE
							//			ORG		$-ORG$+LORG$
							//			LORG	$
							if ( offset )
							{
								printEol();

								if ( zmac )
								{
									printAddr( pc0 );
									fputs( "\tDEPHASE", out );
									printEol();
									printEol();
									_TRACE_( "*** DEPHASE\n" );
								}
								else
								{
									printAddr( pc0 );
									fputs( "\tORG\t$-ORG$+LORG$", out );
									printEol();
									_TRACE_( "*** ORG $-ORG$+LORG$\n" );

									printAddr( pc0 );
									fputs( "\tLORG\t$", out );
									printEol();
									_TRACE_( "*** LORG $\n" );
								}
							}

							pcOffset = 0;

							// New ORG
							//			ORG		nnnn
							if ( pc0 != org )
							{
								printEol();

								printAddr( pc0 );
								fputs( "\tORG\t", out );
								printHexWord( pc0 );
								printEol();
							}

							// New PHASE block
							//	LORG$	DEFL	$
							//	label	EQU		$
							//			ORG		nnnn
							//	ORG$	DEFL	$
							//			LORG	LORG$
							if ( segoffset )
							{
								if ( zmac )
								{
									printEol();
									printAddr( pc0 );
									fprintf( out, "\tPHASE\t%s", getXAddr( pc0 - segoffset ) );
									printEol();
									printEol();
									_TRACE_( "*** PHASE %s\n", getXAddr( pc0 - segoffset ) );
								}
								else
								{

									printAddr( pc0 );
									fputs( "LORG$\tDEFL\t$", out );
									printEol();

									printEol();

									if ( *getLabel( pc0, DS_YES ) )
									{
										printAddr( org );
										fprintf( out, "%s\tEQU\t$", getXAddr( pc0 ) );
										setLabelGen( pc0 );
										printEol();
									}

									printEol();

									printAddr( pc0 - segoffset );
									fprintf( out, "\tORG\t%s", getXAddr( pc0 - segoffset ) );
									printEol();

									printAddr( pc0 );
									fputs( "ORG$\tDEFL\t$", out );
									printEol();

									printAddr( pc0 );
									fputs( "\tLORG\tLORG$", out );
									printEol();

									printEol();

									_TRACE_( "*** ORG=%04X %s LORG=%04X\n", pc0-segoffset, getXAddr( pc0 - segoffset ), pc0 );
								}
							}
							/*else
							{
								printEol();
								printAddr( pc0 );
								fputs( "\tLORG\t$", out );
								printEol();
								//_TRACE_( "*** LORG=$\n" );
							}*/

							offset = segoffset;
							pcOffset = segoffset;
							pcOffsetBeg = segoffsetbeg;
							pcOffsetEnd = segoffsetend;
							_TRACE_( "*** Offset from %04X to %04X by %04X\n", pcOffsetBeg, pcOffsetEnd, (ushort)pcOffset );
						}
						else if ( pc0 != org )
						{
							printEol();

							printAddr( pc0 );
							fputs( "\tORG\t", out );
							printHexWord( pc0 );
							printEol();
						}

#else
						if ( pc0 > org && pc0 >= segbegin && pc0 < segend && pc0 - org <= orgmin )
						{
							for ( pc = org+1; pc < pc0; ++pc )
							{
								s = getLabel( pc );
								if ( *s )
								{
									printAddr( org );
									fprintf( out, "%s\tDS\t", getLabel( org ) );
									printHexWord( pc - org );									
									printEol();
									org = pc;
								}
							}
							printAddr( org );
							fprintf( out, "%s\tDS\t", getLabel( org ) );
							printHexWord( pc0 - org );
							printEol();
						}
						else
						{
							printEol();
							printAddr( org );
							fputs( "\tORG\t", out );
							printHexWord( pc0 );
							printEol();
						}
#endif


					}

					if ( skip )
					{
						printEol();
						skip = 0;
					}

					printLabelComment( pc0, DS_NO );

					switch( type )
					{
					case SEG_CODE:
						ch = getData( pc0 );
						// add blank line after JR, JP, RET, JP (HL), JP (IX) or JP (IY)
						skip = ch == 0x18 || ch == 0xC3 || ch == 0xC9 || ch == 0xE9
								|| ( ( ch == 0xDD || ch == 0xFD ) && getData( pc0+1 ) == 0xE9 );
						printBytes( pc0 );
						printLabelIfNotDoneInComment( pc0 );
						fputs( packSource(), out );
						printChars( pc0, pc );
						break;
					case SEG_BYTE:
						printAddr( pc0 );
						printLabelIfNotDoneInComment( pc0 );
						fprintf( out, "DB\t" );
						printHexByte( getData( pc++ ) );
						while ( ( pc < pcend || pc<0x10000 && !pcend ) && pc < segend && pc < (uint)(pc0+8) && !*getLabel( pc, DS_NO ) )
						{
							fputc( ',', out  );
							printHexByte( getData( pc++ ) );
						}
						break;
					case SEG_CHAR:
						printAddr( pc0 );
						printLabelIfNotDoneInComment( pc0 );
						ch = getData( pc++ );
						w = 16;
						fputs( "DB\t", out );
						nqchars = 0;
						if ( ch < 0x20 || ch >= 0x7F || ( /*noSingleQuote && */ ch == '\'' ) )
						{	// no starting squote for MRAS
							if ( ch >= 0xA0 && ch < 0xFF && ch != '\'' + 0x80 )
							{
								fputc( '\'', out );
								fputc( ch & 0x7F, out );
								fputs( "\'+80H", out );
								w += 7;
							}
							else
							{
								printHexByte( ch );
								w += ch < 0xA0 ? 3 : 4;
							}
						}
						else
						{
							isquote = 1;
							fputc( '\'', out );
							fputc( ch, out );
							if ( ch == '\'' )
							{
								fputc( ch, out );
								++w;
							}
							++nqchars;
							w += 2;
						}

						while ( ( pc < pcend || pc<0x10000 && !pcend ) && pc < segend && w + (isquote?2:5) < width && !*getLabel( pc, DS_NO ) )
						{
							ch = getData( pc );
							if ( ch < 0x20 || ch >= 0x7F || ( ( nqchars < 2 || noSingleQuote ) && ch == '\'' ) )
							{
								if ( isquote )
								{
									fputc( '\'', out );
									++w;
								}
								isquote = 0;
								fputc( ',', out );
								if ( ch >= 0xA0 && ch < 0xFF && ch != '\'' + 0x80 )
								{
									fputc( '\'', out );
									fputc( ch & 0x7F, out );
									fputs( "\'+80H", out );
									w += 8;
								}
								else
								{
									printHexByte( ch );
									w += ch < 0xA0 ? 4 : 5;
								}
								nqchars = 0;
							}
							else
							{
								if ( !isquote )
									break;
								isquote = 1;
								fputc( ch, out );
								if ( ch == '\'' )
								{
									fputc( ch, out );
									++w;
								}
								++nqchars;
								++w;
							}
							++pc;
						}

						if ( isquote )
							fputc( '\'', out );
						break;
					case SEG_WORD:
						printAddr( pc0 );
						printLabelIfNotDoneInComment( pc0 );
#if 1
						fprintf( out, "DW\t%s", getLAddr() );
						while ( ( pc < pcend || pc<0x10000 && !pcend ) && pc < segend && pc < (uint)(pc0+8) && !*getLabel( pc-1, DS_NO ) && !*getLabel( pc, DS_NO ) )
						{
							fprintf( out, ",%s", getLAddr() );
						}
#else
						fprintf( out, "DW\t%s", getXAddr( getData( pc ) | ( getData( pc+1 ) << 8 ) ) );
						pc += 2;
						while ( ( pc < pcend || pc<0x10000 && !pcend ) && pc < segend && pc < pc0+8 && !*getLabel( pc-1 ) && !*getLabel( pc ) )
						{
							fprintf( out, ",%s", getXAddr( getData( pc ) | ( getData( pc+1 ) << 8 ) ) );
							pc += 2;
						}
#endif						
						break;
					case SEG_JUMP:
						printAddr( pc0 );
						printLabelIfNotDoneInComment( pc0 );
						fprintf( out, "DB\t" );
						printHexByte( ch = getData( pc++ ) );
						if ( ch >= ' ' && ch < 0x7F )
							fprintf( out, "\t\t; '%c'", ch );
						printEol();
						printAddr( pc );
						printLabel( pc );
#if 1
						fprintf( out, "DW\t%s", getLAddr() );
#else
						fprintf( out, "DW\t%s", word = getXAddr( getData( pc ) | ( getData( pc+1 ) << 8 ) ) );
						pc += 2;
#endif
						if ( getLastComment() )
							fprintf( out, "\t\t%s", getLastComment() );
						break;
					}
					printEol();

					for ( ++pc0; pc0 < pc; ++pc0 )
					{
						s = getLabel( pc0, DS_NO );

						//	label	EQU		$-dd
						if ( *s )
						{
							printAddr( org );
							fprintf( out, "%s\tEQU\t$-%d", /*getXAddr( pc0 )*/s, pc-pc0 );
							printEol();
						}
					}

					org = pc;
				}
			}
		}

		switch( nPass )
		{
		case 0:
			// get references
			getXAddr( tra );
			updateSymbols();
			break;
		case 1:
			// get labels
			for ( pc = org + 1; pc < 0xFFFF && pc <= (uint)(org + dsMax); ++pc )
			{
				if ( *getLabel( pc, DS_YES ) )
				{
					getLabel( org, DS_YES );
					org = pc;
				}
			}
			getLabel( org, DS_YES );
			break;
		case 2:
			// generate output
			printEol();
			// end of file remaining labels:
			//	label	EQU		$+nnnn
			for ( pc = org + 1; pc < 0xFFFF && pc <= (uint)(org + dsMax); ++pc )
			{
				if ( *getLabel( pc, DS_YES ) )
				{
					printLabelComment( org, DS_YES );
					printAddr( org );
					fprintf( out, "%s\tDS\t", getLabel( org, DS_YES ) );
					setLabelGen( org );
					printHexWord( pc - org );
					printEol();
					org = pc;
				}
			}

			// end of file last label:
			//	label	EQU		$
			if ( *getLabel( org, DS_YES ) )
			{
				printLabelComment( org, DS_YES );
				printAddr( org );
				fprintf( out, "%s\tEQU\t$", getXAddr( org ) );
				setLabelGen( org );
				printEol();
			}

			pcOffset = 0;
			printEol();
			printAddr( tra );
			fprintf( out, "\tEND\t%s", getXAddr( tra ) );
			printEol();
			printEol();
			break;
		}
	}

	if ( isPrintBytes )
	{
		fputs( "** Symbols Table **", out );
		printEol();
		printEol();
		fputs( "Name\t\tSeg Flags Addr  Comment", out );
		printEol();
		fputs( "------------------------------------------------------------", out );
		printEol();
		for ( i=0; i<getNumSymbols(); ++i )
		{
			//if ( !symbols[i].ref )
			//	continue;
			fprintf( out, "%-15s %c   %c%c%c%c  %04X  %s", 
				symbols[i].name, symbols[i].seg, symbols[i].gen?' ':'!', symbols[i].newsym ?'+':' ', 
				symbols[i].ref ?' ':'?', symbols[i].label ?' ':'=', symbols[i].val, symbols[i].comment );
			printEol();
		}
		printEol();
		fputs( "Flags:\n", out );
		fputs( "------\n", out );
		fputs( "! Not generated\n", out );
		fputs( "+ New symbol\n", out );
		fputs( "? Not referenced\n", out );
		fputs( "= EQUate\n", out );

	}

	if ( *outFileName )
	{
		fclose( out );
		fputs( "Disassembly finished.\n", stderr );
	}
	else
	{
		pause();
	}
	exit( 0 );
}
