// DASM80.cpp : Defines the entry point for the console application.
//

#define _TRACE_ACTIVE_ 0
#define _TRACE_ if ( _TRACE_ACTIVE_ ) errprintf

char version[] = "** Z-80(tm) DISASSEMBLER V1.20beta2+DEV - (c) 2015-20 GmEsoft, All rights reserved. **";

/* Version History
   ---------------

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

static symbol_t  symbols[SYMSIZE];
static int     nsymbols = 0;

static void errprintf( char* msg, ... )
{
	va_list argptr;

	va_start( argptr, msg );
	vfprintf( stderr, msg, argptr );
}


static void pause()
{
	fputc( '\n', stderr );
	if ( isatty( fileno( stdout ) ) )
	{
		fputs( "Press any key to continue . . . ", stderr );
		getch();
		fputc( '\n', stderr );
	}
}

static void usage()
{
	fputs(	"\n"
			"usage: dasm80 [-C:]infile[.cmd]|-H:infile[.hex] [-O:outfile[.asm]|-P:outfile[.prn]] [-options]\n"
			"              options: -S:file -E:file -M:file -NE -NQ -NH -W[W]\n"
			"       dasm80 -?  for more details about options.\n"
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

enum{ SEG_CODE='!', SEG_BYTE='%', SEG_WORD='#', SEG_CHAR='$' };

static segment_t segments[SYMSIZE];
static int nsegments = 0;

static FILE *out;

static void addsegment( ushort beg, char type, int offset, ushort offsetbeg, ushort offsetend )
{
	int i,pos;

	_TRACE_( "addsegment( %04X, '%c', %04X, %04X, %04X );\n", beg, type, (ushort)offset, offsetbeg, offsetend );
	
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

static segment_t* getsegment( ushort addr )
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
static void symload(char *filename)
{
	FILE *file;
	char s[256];
	uint n;
	char seg = 0;
	char symname[80];
	uint symval;
	uint bit;
	char *p;

	//trace(cprintf("symload(""%s"")\r\n", filename) );
	file = fopen(filename,"r");
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
						nsymbols < SYMSIZE
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
							strcpy(symbols[nsymbols].name, symname);
							if (seg == 'B')
								if (symval<0x80)
									symbols[nsymbols].val = ((symval-0x20)<<3) + bit;
								else
									symbols[nsymbols].val = symval + bit;
							else
								symbols[nsymbols].val = symval;
							symbols[nsymbols].lval = symbols[nsymbols].val;
							symbols[nsymbols].seg = seg;
							symbols[nsymbols].label = 0;
							symbols[nsymbols].newsym = 0;
							symbols[nsymbols].ds = 1;
						}
						//trace(cprintf("%c:%x\t%s\r\n", seg, symval, symname));
						//tgetch();
						++nsymbols;
						if ( nsymbols == SYMSIZE ) 
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
		errprintf( "*** Error opening MAP file: %s", filename );
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
	*/
static void scrload( char *filename )
{
	FILE *file;
	char s[256];
	uint n;
	ushort beg, end, reloc;
	ushort *pval;
	char type, type0;
	segment_t *pseg;
	int offset = 0;
	ushort offsetbeg = 0, offsetend = 0;


	//trace(cprintf("scrload(""%s"")\r\n", filename) );
	file = fopen(filename,"r");
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

				if ( s[n] == '$' || s[n] == '#' || s[n] == '%' || s[n] == '!' || s[n] == '@' )
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
							pseg = getsegment( beg );
							if ( pseg )
								type0 = pseg->type;
							addsegment( beg, type0, offset, offsetbeg, offsetend );
						}
						
						errprintf( "--- Relocating %04X-%04X to %04X - offset=%04X\n", offsetbeg, offsetend, reloc, (ushort)offset );
					}
					else
					{
						type0 = SEG_CODE;
						if ( pseg = getsegment( beg ) )
							type0 = pseg->type;
						if ( pval == &beg )
							end = beg + ( type == SEG_WORD ? 1 : 0 );
						if ( end && getsegment( end+1 ) == pseg )
							addsegment( end+1, type0, offset, offsetbeg, offsetend ); // or old offset?
						addsegment( beg, type, offset, offsetbeg, offsetend );
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
		errprintf( "*** Error opening SCR file: %s", filename );
		errexit( 1 );
	}
}

// Load equates file
static void equload( char *filename )
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

	//trace(cprintf("equload(""%s"")\r\n", filename) );
	file = fopen(filename,"r");
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
								for ( n=0; n<nsymbols; ++n )
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

								if ( n == nsymbols )
									++nsymbols;

								if ( nsymbols == SYMSIZE )
								{
									errprintf( "*** %s - symbol table overflow: %s.", filename, name );
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
		errprintf( "*** Error opening EQU file: %s", filename );
		errexit( 1 );
	}
}

// Load Binary file
static uint binload( char *filename, uint org )
{
    FILE *file;
	char s[256];
	uint i,ptr,tra,count;

	ptr = tra = org;
    file = fopen(filename,"rb");
    if (file!=NULL) {
        while (!feof(file)) {
			count = fread( s, 1, sizeof( s ), file );
			for ( i=0; i<count; ++i )
				putdata( ptr++, s[i] );
        }
        fclose (file);
    }
	else
	{
		errprintf( "*** Error opening BIN file: %s", filename );
		errexit( 1 );
	}
	return tra;
}
// Load Hex-Intel file
static uint hexload( char *filename )
{
    FILE *file;
    char s[256];
    uint i,t,n,ptr;
	uint tra=0;

    file = fopen(filename,"r");
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
							putdata( ptr++, htoc (s,i) );
                        }
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
		errprintf( "*** Error opening HEX file: %s", filename );
		errexit( 1 );
	}
	return tra;
}

char comment[257] = { 0 };

uint loadfile(FILE* file)
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
				putdata( addr, byte );
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
uint loadcmdfile ( const char *name )
{
	FILE *file;
	uint tra = 0;

	file = fopen( name, "rb" );
	if ( file )
	{
		tra = loadfile( file );
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
static int allocmemory(char * *data)
{
	*data = malloc (0x24001);
	if (*data == NULL)
		return -1;
	memset( *data, 0x76, 0x24001 );
	return 0;
}

// Free allocated memory
static void freememory (char * *data)
{
	free (*data);
}

static char isprintbytes = 1;

static void printdata( ushort pc0, ushort pc )
{
	ushort p;

	fprintf( out, "%04X ", pc0 );
	for ( p=pc0; p<pc; ++p )
		fprintf( out, "%02X", getdata( p ) );
	for ( ;p<pc0+4; ++p )
		fprintf( out, "  " );
	fprintf( out, "\t" );
}

static void printtab()
{
	if ( isprintbytes )
	{
		fputs( "\t\t", out );
	}
}

static void printbytes( ushort pc0 )
{
	if ( isprintbytes )
	{
		pc = pc0;
		source();
		printdata( pc0, pc );
		pc = pc0;
	}
}

static void printaddr( ushort pc0 )
{
	if ( isprintbytes )
		printdata( pc0, pc0 );
}

static void printchars( ushort pc0, ushort pc )
{
	uchar c;
	if ( isprintbytes )
	{
		for ( ; pc0<pc; ++pc0 )
		{
			c = getdata( pc0 );
			if ( c <= 0x20 || c >= 0x7F )
				fputc( '.', out );
			else
				fputc( c, out );
		}
	}
}

static void printlabel( ushort addr )
{
	char *s;
	s = getlabel( addr, 0 );
	fputs( s, out );
	//if ( strlen( s ) < 8 )
		fputc( '\t', out );
}

static void printhexbyte( uchar byte )
{
	if ( byte >= 0xA0 )
		fputc( '0', out );
	fprintf( out, "%02XH", byte );
}

static void printhexword( ushort word )
{
	if ( word >= 0xA000 )
		fputc( '0', out );
	fprintf( out, "%04XH", word );
}

static char *eol = "\n";

static void printeol()
{
	fprintf( out, eol );
}

static void printLabelComment( uint address )
{
	getlabel( address, 1 );
	if ( getLastComment() )
	{
		if ( isprintbytes )
		{
			fputs( "\t\t", out );
		}
		fprintf( out, "\t%s\n", getLastComment() );
	}
}

char* timestr()
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

char* packsource()
{
	char *s, *p, *d, *l;
	char c, f, cmt;

	s = source();
	if ( !isprintbytes )
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
				f = 1;
			}

			if ( cmt || f || *p != ' ' )
			{
				if ( *p == ';' )
					cmt = 1; // comment found
				*d++ = *p;
				l = d;
			}
			else if ( !(c&7) )
			{
				*d++ = '\t';
			}
			++p;
		}
		*l = 0;
	}
	else
	{
		s[48] = 0;
	}
	return s;
}


static void adddefaultext( char *name, char *ext )
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
	int i, npass, nrange;
	char *s;
	uchar ch;

	char outfilename[80];
	char hexfilename[80];
	char cmdfilename[80];
	char binfilename[80];
	char scrfilename[80];
	char symfilename[8][80];
	int  nsymfiles = 0;
	char equfilename[8][80];
	int  nequfiles = 0;
	int  nosquot = 0;
	int  zmac = 0;
	char nonewsymflag  = 0;
	char noheader = 0;
	unsigned short tra = 0;
	segment_t *pseg = 0;
	char type;
	char outformat = 'A';
	short width = 63;
	ushort dsmax = 0x200;
	int org;

	//	char *sourceline;
//	char buf[256];

	outfilename[0] = '\0';
 	scrfilename[0] = '\0';
	hexfilename[0] = '\0';
	cmdfilename[0] = '\0';
	binfilename[0] = '\0';

	out = stdout;

	fputs( version, stderr );
	fputc( '\n', stderr );

	for (i=1; i<argc; i++) 
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
					outformat = toupper( *s );
				break;
			case 'M':   // symbol table
				if ( *s == ':' )
					s++;
				if ( nsymfiles == 8 )
				{
					errprintf( "*** %s - More than 8 .map files specified.", argv[i] );
					errexit( 1 );
				}
				strcpy (symfilename[nsymfiles], s);
				adddefaultext( symfilename[nsymfiles], ".map" );
				nsymfiles++;
				break;
			case 'H':	// Intel Hex file
				if ( *s == ':' )
					s++;
				if ( *hexfilename || *cmdfilename || *binfilename)
				{
					errprintf( "*** %s - Only one input file allowed.", argv[i] );
					errexit( 1 );
				}
				strcpy( hexfilename, s );
				adddefaultext( hexfilename, ".hex" );
				break;
			case 'C':	// DOS CMD file
				if ( *s == ':' )
					s++;
				if ( *hexfilename || *cmdfilename || *binfilename)
				{
					errprintf( "*** %s - Only one input file allowed.", argv[i] );
					errexit( 1 );
				}
				strcpy( cmdfilename, s );
				adddefaultext( cmdfilename, ".cmd" );
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
				if ( *hexfilename || *cmdfilename || *binfilename)
				{
					errprintf( "*** %s - Only one input file allowed.", argv[i] );
					errexit( 1 );
				}
				strcpy( binfilename, s );
				adddefaultext( binfilename, ".bin" );
				break;
			case 'S':	// Screening file
				if ( *s == ':' )
					s++;
				strcpy( scrfilename, s );
				adddefaultext( scrfilename, ".scr" );
				break;
			case 'E':	// Equate file
				if ( *s == ':' )
					s++;
				if ( nequfiles == 8 )
				{
					errprintf( "*** %s - More than 8 equ files specified.", argv[i] );
					errexit( 1 );
				}
				strcpy( equfilename[nequfiles], s );
				adddefaultext( equfilename[nequfiles], ".equ" );
				++nequfiles;
				break;
			case 'O':	// Output ASM file
				if ( *s == ':' )
					s++;
				if ( *outfilename )
				{
					errprintf( "*** %s - Only one output file allowed.", argv[i] );
					errexit( 1 );
				}
				strcpy( outfilename, s );
				isprintbytes = 0;
				break;
			case 'P':	// Output PRN file
				if ( *s == ':' )
					s++;
				if ( *outfilename )
				{
					errprintf( "*** %s - Only one output file allowed.", argv[i] );
					errexit( 1 );
				}
				strcpy( outfilename, s );
				adddefaultext( outfilename, ".prn" );
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
					nonewsymflag = 1;
					break;
				case 'H':	// No Header
					noheader = 1;
					break;
				case 'Q':	// No Single Quote
					nosquot = 1;
					break;
				default:
					--s;
				}
				break;
			case '-':
				if ( !stricmp( s, "zmac" ) )
				{
					zmac = 1;
				}
				else
				{
					errprintf( "*** %s - Unrecognized option.", argv[i] );
				}
				break;
			case '!':	// Wait keypress before starting
				fputs( "Press ENTER to start.", stderr );
				getchar();
				break;
			case '?':	// Help
				fputs ( "\r\n"
					"DASM80 [-H:file]|[[-C:]file] [-S:file] [-O:file]|[-P:file] [-E:file [-E:file...]]\r\n"
					"       [-M:file [-M:file...]] [-W[W]] [-NE] [-NH] [-NQ] [--ZMAC]\r\n"
					"where:  -H:file      = code file in hex intel format [.HEX]\r\n"
					"        [-C:]file    = code file in DOS loader format [.CMD]\r\n"
					"        -B[org]:file = code file in binary [.BIN]\r\n"
					"        -S:file      = screening file [.SCR]\r\n"
					"        -O:file      = output [.ASM]\r\n"
					"        -P:file      = listing output [.PRN]\r\n"
					"        -E:file      = one or more equate files [.EQU]\r\n"
					"        -M:file      = one or more symbol tables [.MAP]\r\n"
					"        -W[W]        = [super] wide mode\r\n"
					"        -NE          = no new EQUates\r\n"
					"        -NH          = no header\r\n"
					"        -NQ          = no single quotes\r\n"
					"        --ZMAC       = ZMAC compatibility\r\n"
					, stderr );
				errexit( 0 );
				break;
			default:
				errprintf( "*** %s - Unrecognized option.", argv[i] );
				errexit( 1 );
			}
		}
		else
		{
			if ( *hexfilename || *cmdfilename )
			{
				errprintf( "*** %s - Only one input file allowed.", argv[i] );
				errexit( 1 );
			}
			strcpy( cmdfilename, argv[i] );
			adddefaultext( cmdfilename, ".cmd" );
		}
	}

	if ( !*hexfilename && !*cmdfilename && !*binfilename )
	{
		errprintf( "*** Missing input filename." );
		errexit( 1 );
	}

	addsegment( 0, SEG_CODE, 0, 0, 0 );
	addsegment( 0xFFFF, SEG_CODE, 0, 0, 0 );

	if ( *scrfilename )
		scrload( scrfilename );

	for ( i=0; i<nsymfiles; ++i )
		symload( symfilename[i] );

	for ( i=0; i<nequfiles; ++i )
		equload( equfilename[i] );

	setZ80Symbols( symbols, nsymbols, SYMSIZE );

	setZ80MemIO( getdata );

	if ( *hexfilename )
	{
		tra = hexload( hexfilename );
	}

	if ( *cmdfilename )
	{
		tra = loadcmdfile( cmdfilename );
	}

	if ( *binfilename )
	{
		tra = binload( binfilename, org );
	}

	pseg = getsegment( tra );

	if ( *outfilename )
	{
		switch( outformat )
		{
		case 'A':
			adddefaultext( outfilename, ".asm" );
			break;
		case 'C':
			adddefaultext( outfilename, ".c" ); // currently not supported
			break;
		case 'H':
			adddefaultext( outfilename, ".txt" ); // currently not supported
			break;
		default:
			errprintf( "Unrecognized output format: %c", outformat );
			errexit( 1 );
		}

		out = fopen( outfilename, "wb" );

		if ( !noheader )
		{
			fprintf( out, ";%s", version );
			printeol();
			fprintf( out, ";" );
			printeol();
			fprintf( out, ";\t%s", timestr() );
			printeol();
			fprintf( out, ";" );
			printeol();

			if ( *cmdfilename )
			{
				fprintf( out, ";\tDisassembly of : %s", cmdfilename );
				printeol();
			}
			else if ( *hexfilename )
			{
				fprintf( out, ";\tDisassembly of : %s", hexfilename );
				printeol();
			}

			for ( i=0; i<nequfiles; ++i )
			{
				fprintf( out, ";\tEquates file   : %s", equfilename[i] );
				printeol();
			}

			if ( *scrfilename )
			{
				fprintf( out, ";\tScreening file : %s", scrfilename );
				printeol();
			}

			printeol();
		}

		if ( *comment )
		{
			printtab();
			fprintf( out, "\tCOM\t'<%s>'", comment );
			printeol();
			printeol();
		}

	}

	for ( npass=0; npass<3; ++npass )
	{
		int offset = 0;
		ushort org = 0xFFFF;

		resetZ80Symbols();

		pcoffsetseg = 'Z'+1;
		pcoffset = 0;

		_TRACE_( "*** PASS %d\n", npass );
		switch( npass )
		{
		case 2:
			for ( i = 0; s = getmacroline( i ); ++i )
			{
				printtab();
				fputs( s, out );
				printeol();
			}

			nonewequ = nonewsymflag;
			for ( i=0; i<nZ80symbols; ++i )
			{
				symbol_t *sym = &Z80symbols[i];
				if ( !sym->label && sym->ref && ( !nonewsymflag || !sym->newsym ) )
				{
					// TODO: bug when used with relocatable blocks (ex: backup/cmd)
					printaddr( sym->lval );
					fprintf( out, "%s\tEQU\t", sym->name );
					printhexword( sym->val ); //////////////////////////////
					if ( *sym->comment )
						fprintf( out, "\t\t%s", sym->comment );
					printeol();
				}
			}
			printeol();
			break;
		}

		for ( nrange = 0; nrange < nranges; ++nrange )
		{
			char skip = 0;
			char type = SEG_CODE;
			ushort pcbeg = ranges[nrange].beg;
			ushort pcend = ranges[nrange].end;
			ushort nqchars;

			for ( pc = pcbeg; ( pc < pcend || ( pc<0x10000 && !pcend ) ); )
			{
				ushort pc0, segbegin = 0, segend = 0xFFFF;
				int segoffset = 0;
				ushort segoffsetbeg, segoffsetend;

				char isquote = 0;
				short w;

				if ( !pseg || pc < pseg->beg || pc >= (pseg+1)->beg )
				{
					pseg = getsegment( pc );
				}

				if ( pseg )
				{
					skip = skip || ( type == SEG_CODE ) != ( pseg->type == SEG_CODE );
					type = pseg->type;
					segbegin = pseg->beg;
					segend = (pseg+1)->beg;
					segoffset = pseg->offset;
					segoffsetbeg = pseg->offsetbeg;
					segoffsetend = pseg->offsetend;
					if ( offset != segoffset || pc != org )
					{
						_TRACE_( "*** offset=%04X segoffset=%04X - pc=%04X org=%04X ", offset, segoffset, pc, org );
						_TRACE_( "*** Segment: %04X-%04X '%c' offset %04X %04X-%04X )\n", 
							segbegin, segend, type, (ushort)segoffset, segoffsetbeg, segoffsetend );
					}
				}

				pc0 = pc;

				switch( npass )
				{
				case 0:	// get references
					if ( offset != segoffset )
					{
						--pcoffsetseg;
						offset = segoffset;
						pcoffset = segoffset;
						pcoffsetbeg = segoffsetbeg;
						pcoffsetend = segoffsetend;
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
						getladdr();
#else
						getxaddr( getdata( pc ) | ( getdata( pc+1 ) << 8 ) );
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
						if ( org < 0xFFFF /*&& *getlabel( org )*/ )
						{
							for ( pc = org + 1; pc <= pc0 /*&& pc <= segend */&& pc <= org + dsmax; ++pc )
							{
								if ( pc == pc0 || *getlabel( pc, 1 ) )
								{
									getlabel( org, 1 );
									org = pc;
								}
							}

							if ( pc0 != org )
							{
								getlabel( org, 1 ) ;
							}
						}

						pc = pc0;

#else
						if ( pc0 > org && pc0 >= segbegin && pc0 < segend && pc0 - org <= dsmax )
						{
							for ( ; org < pc0; ++org )
							{
								getlabel( org );
							}
						}
#endif
						if ( offset != segoffset )
						{
							--pcoffsetseg;
							pcoffset = 0;
							//getlabel( pc0, 1 ); 	// moved down
							offset = segoffset;
							pcoffset = segoffset;
							pcoffsetbeg = segoffsetbeg;
							pcoffsetend = segoffsetend;
							getlabel( pc0, 1 ); 	// moved here
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
						getladdr();
						break;
					}

					for ( ; pc0 < pc; ++pc0 )
						getlabel( pc0, 0 );
					org = pc;
					break;
				case 2: // generate output
					// Generate new origin with DS/EQU/ORG
					if ( pc0 != org || offset != segoffset )
					{
						skip = 0;
#if 1
						if ( org < 0xFFFF /* && *getlabel( org ) */)
						{
							printeol();

							//	label	DS		nnnn
							for ( pc = org + 1; pc <= pc0 /*&& pc <= segend */&& pc <= org + dsmax; ++pc )
							{
								if ( pc == pc0 || *getlabel( pc, 1 ) )
								{
									printLabelComment( org );
									printaddr( org );
									fprintf( out, "%s\tDS\t", getlabel( org, 1 ) );
									setlabelgen( org );
									printhexword( pc - org );
									printeol();
									org = pc;
								}
							}

							//	label	EQU		$
							if ( pc0 != org && *getlabel( org, 1 ) )
							{
								printLabelComment( org );
								printaddr( org );
								fprintf( out, "%s\tEQU\t$", getxaddr( org ) );
								setlabelgen( org );
								printeol();
							}
						}

						pc = pc0;

						// PHASE change
						if ( offset != segoffset )
						{
							--pcoffsetseg;
							printeol();

							// end PHASE
							//			ORG		$-ORG$+LORG$
							//			LORG	$
							if ( offset )
							{
								printeol();

								if ( zmac )
								{
									printaddr( pc0 );
									fputs( "\tDEPHASE", out );
									printeol();
									printeol();
									_TRACE_( "*** DEPHASE\n" );
								}
								else
								{
									printaddr( pc0 );
									fputs( "\tORG\t$-ORG$+LORG$", out );
									printeol();
									_TRACE_( "*** ORG $-ORG$+LORG$\n" );

									printaddr( pc0 );
									fputs( "\tLORG\t$", out );
									printeol();
									_TRACE_( "*** LORG $\n" );
								}
							}

							pcoffset = 0;

							// New ORG
							//			ORG		nnnn
							if ( pc0 != org )
							{
								printeol();

								printaddr( pc0 );
								fputs( "\tORG\t", out );
								printhexword( pc0 );
								printeol();
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
									printeol();
									printaddr( pc0 );
									fprintf( out, "\tPHASE\t%s", getxaddr( pc0 - segoffset ) );
									printeol();
									printeol();
									_TRACE_( "*** PHASE %s\n", getxaddr( pc0 - segoffset ) );
								}
								else
								{

									printaddr( pc0 );
									fputs( "LORG$\tDEFL\t$", out );
									printeol();

									printeol();

									if ( *getlabel( pc0, 1 ) )
									{
										printaddr( org );
										fprintf( out, "%s\tEQU\t$", getxaddr( pc0 ) );
										setlabelgen( pc0 );
										printeol();
									}

									printeol();

									printaddr( pc0 - segoffset );
									fprintf( out, "\tORG\t%s", getxaddr( pc0 - segoffset ) );
									printeol();

									printaddr( pc0 );
									fputs( "ORG$\tDEFL\t$", out );
									printeol();

									printaddr( pc0 );
									fputs( "\tLORG\tLORG$", out );
									printeol();

									printeol();

									_TRACE_( "*** ORG=%04X %s LORG=%04X\n", pc0-segoffset, getxaddr( pc0 - segoffset ), pc0 );
								}
							}
							/*else
							{
								printeol();
								printaddr( pc0 );
								fputs( "\tLORG\t$", out );
								printeol();
								//_TRACE_( "*** LORG=$\n" );
							}*/

							offset = segoffset;
							pcoffset = segoffset;
							pcoffsetbeg = segoffsetbeg;
							pcoffsetend = segoffsetend;
							_TRACE_( "*** Offset from %04X to %04X by %04X\n", pcoffsetbeg, pcoffsetend, (ushort)pcoffset );
						}
						else if ( pc0 != org )
						{
							printeol();

							printaddr( pc0 );
							fputs( "\tORG\t", out );
							printhexword( pc0 );
							printeol();
						}

#else
						if ( pc0 > org && pc0 >= segbegin && pc0 < segend && pc0 - org <= orgmin )
						{
							for ( pc = org+1; pc < pc0; ++pc )
							{
								s = getlabel( pc );
								if ( *s )
								{
									printaddr( org );
									fprintf( out, "%s\tDS\t", getlabel( org ) );
									printhexword( pc - org );									
									printeol();
									org = pc;
								}
							}
							printaddr( org );
							fprintf( out, "%s\tDS\t", getlabel( org ) );
							printhexword( pc0 - org );
							printeol();
						}
						else
						{
							printeol();
							printaddr( org );
							fputs( "\tORG\t", out );
							printhexword( pc0 );
							printeol();
						}
#endif


					}

					if ( skip )
					{
						printeol();
						skip = 0;
					}

					printLabelComment( pc0 );

					switch( type )
					{
					case SEG_CODE:
						ch = getdata( pc0 );
						// add blank line on JR, JP, RET, JP (HL), JP (IX) or JP (IY)
						skip = ch == 0x18 || ch == 0xC3 || ch == 0xC9 || ch == 0xE9
								|| ( ( ch == 0xDD || ch == 0xFD ) && getdata( pc0+1 ) == 0xE9 );
						printbytes( pc0 );
						printlabel( pc0 );
						fputs( packsource(), out );
						printchars( pc0, pc );
						break;
					case SEG_BYTE:
						printaddr( pc0 );
						printlabel( pc0 );
						fprintf( out, "DB\t" );
						printhexbyte( getdata( pc++ ) );
						while ( ( pc < pcend || pc<0x10000 && !pcend ) && pc < segend && pc < pc0+8 && !*getlabel( pc, 0 ) )
						{
							fputc( ',', out  );
							printhexbyte( getdata( pc++ ) );
						}
						break;
					case SEG_CHAR:
						printaddr( pc0 );
						printlabel( pc0 );
						ch = getdata( pc++ );
						w = 16;
						fputs( "DB\t", out );
						nqchars = 0;
						if ( ch < 0x20 || ch >= 0x7F || ( /*nosquot && */ ch == '\'' ) )
						{	// no starting squote for MRAS
							printhexbyte( ch );
							w += ch < 0xA0 ? 3 : 4;
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

						while ( ( pc < pcend || pc<0x10000 && !pcend ) && pc < segend && w + (isquote?2:5) < width && !*getlabel( pc, 0 ) )
						{
							ch = getdata( pc );
							if ( ch < 0x20 || ch >= 0x7F || ( ( nqchars < 2 || nosquot ) && ch == '\'' ) )
							{
								if ( isquote )
								{
									fputc( '\'', out );
									++w;
								}
								isquote = 0;
								fputc( ',', out );
								printhexbyte( ch );
								w += ch < 0xA0 ? 4 : 5;
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
						printaddr( pc0 );
						printlabel( pc0 );
#if 1
						fprintf( out, "DW\t%s", getladdr() );
						while ( ( pc < pcend || pc<0x10000 && !pcend ) && pc < segend && pc < pc0+8 && !*getlabel( pc-1, 0 ) && !*getlabel( pc, 0 ) )
						{
							fprintf( out, ",%s", getladdr() );
						}
#else
						fprintf( out, "DW\t%s", getxaddr( getdata( pc ) | ( getdata( pc+1 ) << 8 ) ) );
						pc += 2;
						while ( ( pc < pcend || pc<0x10000 && !pcend ) && pc < segend && pc < pc0+8 && !*getlabel( pc-1 ) && !*getlabel( pc ) )
						{
							fprintf( out, ",%s", getxaddr( getdata( pc ) | ( getdata( pc+1 ) << 8 ) ) );
							pc += 2;
						}
#endif						
						break;
					}
					printeol();

					for ( ++pc0; pc0 < pc; ++pc0 )
					{
						s = getlabel( pc0, 0 );

						//	label	EQU		$-dd
						if ( *s )
						{
							printaddr( org );
							fprintf( out, "%s\tEQU\t$-%d", /*getxaddr( pc0 )*/s, pc-pc0 );
							printeol();
						}
					}

					org = pc;
				}
			}
		}

		switch( npass )
		{
		case 0:
			// get references
			getxaddr( tra );
			updateZ80Symbols();
			break;
		case 1:
			// get labels
			for ( pc = org + 1; pc < 0xFFFF && pc <= org + dsmax; ++pc )
			{
				if ( *getlabel( pc, 1 ) )
				{
					getlabel( org, 1 );
					org = pc;
				}
			}
			getlabel( org, 1 );
			break;
		case 2:
			// generate output
			printeol();
			// end of file remaining labels:
			//	label	EQU		$+nnnn
			for ( pc = org + 1; pc < 0xFFFF && pc <= org + dsmax; ++pc )
			{
				if ( *getlabel( pc, 1 ) )
				{
					printLabelComment( org );
					printaddr( org );
					fprintf( out, "%s\tDS\t", getlabel( org, 1 ) );
					setlabelgen( org );
					printhexword( pc - org );
					printeol();
					org = pc;
				}
			}

			// end of file last label:
			//	label	EQU		$
			if ( *getlabel( org, 1 ) )
			{
				printLabelComment( org );
				printaddr( org );
				fprintf( out, "%s\tEQU\t$", getxaddr( org ) );
				setlabelgen( org );
				printeol();
			}

			pcoffset = 0;
			printeol();
			printaddr( tra );
			fprintf( out, "\tEND\t%s", getxaddr( tra ) );
			printeol();
			printeol();
			break;
		}
	}

	if ( isprintbytes )
	{
		fputs( "** Symbols Table **", out );
		printeol();
		printeol();
		fputs( "Name\t\tSeg Flags Addr  Comment", out );
		printeol();
		fputs( "------------------------------------------------------------", out );
		printeol();
		for ( i=0; i<getNumZ80Symbols(); ++i )
		{
			//if ( !symbols[i].ref )
			//	continue;
			fprintf( out, "%-15s %c   %c%c%c%c  %04X  %s", 
				symbols[i].name, symbols[i].seg, symbols[i].gen?' ':'!', symbols[i].newsym ?'+':' ', 
				symbols[i].ref ?' ':'?', symbols[i].label ?' ':'=', symbols[i].val, symbols[i].comment );
			printeol();
		}
		printeol();
		fputs( "Flags:\n", out );
		fputs( "------\n", out );
		fputs( "! Not generated\n", out );
		fputs( "+ New symbol\n", out );
		fputs( "? Not referenced\n", out );
		fputs( "= EQUate\n", out );

	}

	if ( *outfilename )
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
