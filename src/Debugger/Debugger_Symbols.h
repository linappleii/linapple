#include <cstdint>


	extern 	SymbolTable_t g_aSymbols[ NUM_SYMBOL_TABLES ];


	Update_t _CmdSymbolsClear ( SymbolTable_Index_e eSymbolTable );
	Update_t _CmdSymbolsCommon ( int nArgs, SymbolTable_Index_e eSymbolTable );
	Update_t _CmdSymbolsListTables (int nArgs, int bSymbolTables );
	Update_t _CmdSymbolsUpdate ( int nArgs, int bSymbolTables );

	bool _CmdSymbolList_Address2Symbol ( int nAddress   , int bSymbolTables );
	bool _CmdSymbolList_Symbol2Address ( const char* pSymbol, int bSymbolTables );

	int ParseSymbolTable ( const std::string & pFileName, SymbolTable_Index_e eWhichTableToLoad, int nSymbolOffset = 0 );
