#include <cstdint>

#ifndef DEBUGGER_HELP_H
#define DEBUGGER_HELP_H

#include "Debugger_Types.h"

// Types ____________________________________________________________________

	enum HelpType_e
	{
		  HELP_TYPE_USAGE
		, HELP_TYPE_NOTE
		, HELP_TYPE_EXAMPLE
		, HELP_TYPE_RANGE
		, HELP_TYPE_SEE_ALSO
		, NUM_HELP_TYPES
	};

	struct HelpEntry_t
	{
		int iCommand;
		HelpType_e eType;
		const char *pText;
	};

// Prototypes _______________________________________________________________

	Update_t HelpLastCommand();

inline void  UnpackVersion( const unsigned int nVersion,
		int & nMajor_, int & nMinor_, int & nFixMajor_ , int & nFixMinor_ )
	{
		nMajor_    = (nVersion >> 24) & 0xFF;
		nMinor_    = (nVersion >> 16) & 0xFF;
		nFixMajor_ = (nVersion >>  8) & 0xFF;
		nFixMinor_ = (nVersion >>  0) & 0xFF;
	}

	bool  TestStringCat ( char * pDst, const char* pSrc, const int nDstSize );
	bool  TryStringCat ( char * pDst, const char* pSrc, const int nDstSize );
	int  StringCat( char * pDst, const char* pSrc, const int nDstSize );

#endif
