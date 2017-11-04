/*
Programmed by Alexey Pechnikov (pechnikov@mobigroup.ru) for SQLite
    on base of the PgDBF codes
*/

/* PgDBF - Quickly convert DBF files to PostgreSQL                       */
/* Copyright (C) 2008-2010  Kirk Strauser <kirk@daycos.com>              */
/*                                                                       */
/* This program is free software: you can redistribute it and/or modify  */
/* it under the terms of the GNU General Public License as published by  */
/* the Free Software Foundation, either version 3 of the License, or     */
/* (at your option) any later version.                                   */
/*                                                                       */
/* This program is distributed in the hope that it will be useful,       */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of        */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         */
/* GNU General Public License for more details.                          */
/*                                                                       */
/* You should have received a copy of the GNU General Public License     */
/* along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This should be big enough to hold most of the varchars and memo fields
 * that you'll be processing.  If a given piece of data won't fit in a
 * buffer of this size, then a temporary buffer will be allocated for it. */
#define STATICBUFFERSIZE 1024 * 1024

/* Attempt to read approximately this many bytes from the .dbf file at once.
 * The actual number may be adjusted up or down as appropriate. */
#define DBFBATCHTARGET 128 * 1024

/* Old versions of FoxPro (and probably other programs) store the memo file
 * record number in human-readable ASCII. Newer versions of FoxPro store it
 * as a 32-bit packed int. */
#define NUMERICMEMOSTYLE 0
#define PACKEDMEMOSTYLE 1

static char staticbuf[STATICBUFFERSIZE + 1];

typedef struct {
    int8_t   signature;
    int8_t   year;
    int8_t   month;
    int8_t   day;
    uint32_t recordcount;
    uint16_t headerlength;
    uint16_t recordlength;
    int8_t   reserved1[2];
    int8_t   incomplete;
    int8_t   encrypted;
    int8_t   reserved2[4];	/* Free record thread */
    int8_t   reserved3[8];	/* Reserved for multi-user dBASE */
    int8_t   mdx;
    int8_t   language;
    int8_t   reserved4[2];
} DBFHEADER;

typedef struct 
{
    char    name[11];
    char    type;
    int32_t memaddress;
    uint8_t length;
    uint8_t decimals;
    int16_t flags;		/* Reserved for multi-user dBase */
    char    workareaid;
    char    reserved1[2];	/* Reserved for multi-user dBase */
    char    setfields;
    char    reserved2[7];
    char    indexfield;
} DBFFIELD;

typedef struct 
{
    char nextblock[4];
    char reserved1[2];
    char blocksize[2];
    char reserved2[504];
} MEMOHEADER;

typedef struct
{
    char *formatstring;
    int   memonumbering;
} PGFIELD;

static void exitwitherror(const char *message, const int systemerror)
{
    /* Print the given error message to stderr, then exit.  If systemerror
     * is true, then use perror to explain the value in errno. */
    if(systemerror) {
        perror(message);
    } else {
        fprintf(stderr, "%s\n", message);
    }
    exit(EXIT_FAILURE);
}

static void safeprintbuf(const char *buf, const size_t inputsize)
{
    /* Print a string, insuring that it's fit for use in a tab-delimited
     * text file */
    char       *targetbuf;
    const char *s;
    const char *lastchar;
    char       *t;
    int         realsize = 0;

    /* Shortcut for empty strings */
    if(*buf == '\0') {
        printf("''");
	return;
    }

    /* Find the rightmost non-space, non-null character */
    for(s = buf + inputsize - 1; s >= buf; s--) {
	if(*s != ' ' && *s != '\0') {
	    break;
	}
    }

    /* If there aren't any non-space characters, skip the output part */
    if(s < buf) {
        printf("''");
	return;
    }

    lastchar = s;
    realsize = s - buf + 1;
    if(realsize * 2 < STATICBUFFERSIZE) {
	targetbuf = staticbuf;
    } else {
	targetbuf = malloc(realsize * 2 + 1);
	if(targetbuf == NULL) {
	    exitwitherror("Unable to malloc the escape output buffer", 1);
	}
    }

    /* Re-write invalid characters to their SQL-safe alternatives */
    t = targetbuf;
    for(s = buf; s <= lastchar; s++) {
	switch(*s) {
	case '\\':
	    *t++ = '\\';
	    *t++ = '\\';
	    break;
	case '\n':
	    *t++ = '\\';
	    *t++ = 'n';
	    break;
	case '\r':
	    *t++ = '\\';
	    *t++ = 'r';
	    break;
	case '\t':
	    *t++ = '\\';
	    *t++ = 't';
	    break;
	default:
	    *t++ = *s;
	}
    }
    *t = '\0';
    printf("'%s'", targetbuf);

    if(targetbuf != staticbuf) {
	free(targetbuf);
    }
}

/* Endian-specific code.  Define functions to convert input data to the
 * required form depending on the endianness of the host architecture. */

/* Integer-to-integer */

static int64_t nativeint64_t(const int64_t rightend)
{
    /* Leave a 64-bit integer alone */
    return rightend;
}

static int64_t swappedint64_t(const int64_t wrongend)
{
    /* Change the endianness of a 64-bit integer */
    return (int64_t) (((wrongend & 0xff00000000000000LL) >> 56) |
		      ((wrongend & 0x00ff000000000000LL) >> 40) |
		      ((wrongend & 0x0000ff0000000000LL) >> 24) |
		      ((wrongend & 0x000000ff00000000LL) >> 8)  |
		      ((wrongend & 0x00000000ff000000LL) << 8)  |
		      ((wrongend & 0x0000000000ff0000LL) << 24) |
		      ((wrongend & 0x000000000000ff00LL) << 40) |
		      ((wrongend & 0x00000000000000ffLL) << 56));
}

static int32_t nativeint32_t(const int32_t rightend)
{
    /* Leave a 32-bit integer alone */
    return rightend;
}

static int32_t swappedint32_t(const int32_t wrongend)
{
    /* Change the endianness of a 32-bit integer */
    return (int32_t) (((wrongend & 0xff000000) >> 24) |
		      ((wrongend & 0x00ff0000) >> 8)  |
		      ((wrongend & 0x0000ff00) << 8)  |
		      ((wrongend & 0x000000ff) << 24));
}

static int16_t nativeint16_t(const int16_t rightend)
{
    /* Leave a 16-bit integer alone */
    return rightend;
}

static int16_t swappedint16_t(const int16_t wrongend)
{
    /* Change the endianness of a 16-bit integer */
    return (int16_t) (((wrongend & 0xff00) >> 8) |
		      ((wrongend & 0x00ff) << 8));
}

/* String-to-integer */

static int64_t snativeint64_t(const char *buf) 
{
    /* Interpret the first 8 bytes of buf as a 64-bit int */
    int64_t output;
    memcpy(&output, buf, 8);
    return output;
}

static int64_t sswappedint64_t(const char *buf)
{
    /* The byte-swapped version of snativeint64_t */
    int64_t output;
    memcpy(&output, buf, 8);
    return swappedint64_t(output);
}

static int32_t snativeint32_t(const char *buf) 
{
    /* Interpret the first 4 bytes of buf as a 32-bit int */
    int32_t output;
    memcpy(&output, buf, 4);
    return output;
}

static int32_t sswappedint32_t(const char *buf)
{
    /* The byte-swapped version of snativeint32_t */
    int32_t output;
    memcpy(&output, buf, 4);
    return swappedint32_t(output);
}

static int16_t snativeint16_t(const char *buf) 
{
    /* Interpret the first 2 bytes of buf as a 16-bit int */
    int16_t output;
    memcpy(&output, buf, 2);
    return output;
}

static int16_t sswappedint16_t(const char *buf) 
{
    /* The byte-swapped version of snativeint16_t */
    int16_t output;
    memcpy(&output, buf, 2);
    return swappedint16_t(output);
}

#ifdef WORDS_BIGENDIAN
#define bigint64_t     nativeint64_t
#define littleint64_t  swappedint64_t

#define bigint32_t     nativeint32_t
#define littleint32_t  swappedint32_t

#define bigint16_t     nativeint16_t
#define littleint16_t  swappedint16_t

#define sbigint64_t    snativeint64_t
#define slittleint64_t sswappedint64_t

#define sbigint32_t    snativeint32_t
#define slittleint32_t sswappedint32_t

#define sbigint16_t    snativeint16_t
#define slittleint16_t sswappedint16_t

static double sdouble(const char *buf)
{
    /* Doubles are stored as 64-bit little-endian, so swap ends */
    union 
    {
	int64_t asint64;
	double  asdouble;
    } inttodouble;

    inttodouble.asint64 = slittleint64_t(buf);
    return inttodouble.asdouble;
}
#else
#define bigint64_t     swappedint64_t
#define littleint64_t  nativeint64_t

#define bigint32_t     swappedint32_t
#define littleint32_t  nativeint32_t

#define bigint16_t     swappedint16_t
#define littleint16_t  nativeint16_t

#define sbigint64_t    sswappedint64_t
#define slittleint64_t snativeint64_t

#define sbigint32_t    sswappedint32_t
#define slittleint32_t snativeint32_t

#define sbigint16_t    sswappedint16_t
#define slittleint16_t snativeint16_t

static double sdouble(const char *buf)
{
    /* Interpret the first 8 bytes of buf as a double */
    double output;
    memcpy(&output, buf, 8);
    return output;
}

#endif
