#ifndef USUAL_TYPES
#define USUAL_TYPES

#ifdef __STDC__
#if __STDC_VERSION__ >= 199901L
#define HAS_C99_TYPES
#endif
#endif

#ifdef HAS_C99_TYPES
typedef uint8_t	byte;
typedef uint16_t word16;
typedef uint32_t word32;
#else
typedef unsigned char	byte;	/*  8 bit */
typedef unsigned short	word16;	/* 16 bit */
typedef unsigned int	word32;	/* 32 bit */
#endif

#endif /* ?USUAL_TYPES */
