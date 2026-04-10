#ifndef DEBUGGER_PARSER_H
#define DEBUGGER_PARSER_H

#include <cstdint>
#include <vector>
#include "Util_Text.h"
#include "Debugger_Types.h"

const char * ParserFindToken( const char *pSrc, const TokenTable_t *aTokens, const int nTokens, ArgToken_e * pToken_ );
const char * FindTokenOrAlphaNumeric ( const char *pSrc, const TokenTable_t *aTokens, const int nTokens, ArgToken_e * pToken_ );
int RemoveWhiteSpaceReverse( char *pSrc );
void TextConvertTabsToSpaces( char *pDeTabified_, const char* pText, const int nDstSize, int nTabStop = 0 );

inline const char* SkipUntilToken( const char *pSrc, const TokenTable_t *aTokens, const int nTokens, ArgToken_e *pToken_ )
	{
		if ( pToken_)
			*pToken_ = NO_TOKEN;

		while (pSrc && (*pSrc))
		{
			if (ParserFindToken( pSrc, aTokens, nTokens, pToken_ ))
				return pSrc;

			pSrc++;
		}
		return pSrc;
	}

// Globals __________________________________________________________________

	extern	int   g_nArgRaw;
	extern	Arg_t g_aArgRaw[ MAX_ARGS ]; // pre-processing
	extern	Arg_t g_aArgs  [ MAX_ARGS ]; // post-processing

	extern	const char * g_pConsoleFirstArg; // points to first arg

	extern	const TokenTable_t g_aTokens[ NUM_TOKENS ];

	extern	const char TCHAR_LF    ;
	extern	const char TCHAR_CR    ;
	extern	const char TCHAR_SPACE ;
	extern	const char TCHAR_TAB   ;
	extern	const char TCHAR_QUOTE_DOUBLE;
	extern	const char TCHAR_QUOTE_SINGLE;

	extern	std::vector<int> g_vPotentialCommands;

// Prototypes _______________________________________________________________

	void _strupr(char* s);
	int FindParam(const char* pLookupName, Match_e eMatch, int & iParam_, int iParamBegin = 0, int iParamEnd = NUM_PARAMS - 1);
	int FindCommand(const char* pName, CmdFuncPtr_t & pFunction_, int * iCommand_ = NULL);
	void DisplayAmbigiousCommands(int nFound);
	int  ParseInput(char* pConsoleInput, bool bCook = true);

// Arg - Command Processing
	Update_t Help_Arg_1( int iCommandHelp );
	int _Arg_1     ( int nValue );
	int _Arg_1     ( char* pName );
	int _Arg_Shift ( int iSrc, int iEnd, int iDst = 0 );
	int _Args_Insert( int iSrc, int iEnd, int nLen );
	void ArgsClear ();

	bool ArgsGetValue ( Arg_t *pArg, unsigned short * pAddressValue_, const int nBase = 16 );
	bool ArgsGetImmediateValue ( Arg_t *pArg, unsigned short * pAddressValue_ );
	int	 ArgsGet ( char * pInput );
	bool ArgsGetRegisterValue ( Arg_t *pArg, unsigned short * pAddressValue_ );
	void ArgsRawParse ( void );
	int ArgsCook ( const int nArgs );

#endif
