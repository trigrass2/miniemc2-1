void RemoveEndLine( char * line );
void LoadAllRungs_V1(char * BaseName,StrRung * Rungs,int * TheFirst,int * TheLast,int * TheCurrent);
void LoadAllRungs(char * BaseName,StrRung * Rungs);
void SaveAllRungs(char * BaseName);
char * ConvRawLineOfNumbers(char * RawLine,char NbrParams,int * ValuesFnd);
int ConvBaseInMilliSecsToId(int NbrMilliSecs);
char LoadTimersParams(char * FileName,StrTimer * BufTimers);
char SaveTimersParams(char * FileName,StrTimer * BufTimers);
char LoadMonostablesParams(char * FileName,StrMonostable * BufMonostables);
char SaveMonostablesParams(char * FileName,StrMonostable * BufMonostables);
char LoadCountersParams(char * FileName);
char SaveCountersParams(char * FileName);
void DumpRung(StrRung * TheRung);
char LoadArithmeticExpr(char * FileName);
char SaveArithmeticExpr(char * FileName);
char LoadSectionsParams(char * FileName);
char SaveSectionsParams(char * FileName);
char LoadIOConfParams(char * FileName);
char SaveIOConfParams(char * FileName);
char LoadModbusIOConfParams(char * FileName);
char SaveModbusIOConfParams(char * FileName);
char LoadSymbols(char * FileName);
void SymbolsAutoAssign(int VariableBuf,char SymbolBuf[],char CommentBuf[]);
char SaveSymbols(char * FileName);

void LoadAllLadderDatas(char * DatasDirectory);
void SaveAllLadderDatas(char * DatasDirectory);
void VerifyDirectorySelected( char * NewDir );

void InitTempDir( void );
char LoadProjectFiles( char * FileProject );
char SaveProjectFiles( char * FileProject );
void CleanTmpDirectory( char * Directory, char DestroyDir );
char JoinFiles( char * DirAndNameOfProject, char * TmpDirectoryFiles );
char SplitFiles( char * DirAndNameOfProject, char * TmpDirectoryFiles );
