typedef unsigned char u8;

typedef struct sqlite3expert sqlite3expert;

#if defined(SQLITE_ENABLE_SESSION)
/*
** State information for a single open session
*/
typedef struct OpenSession OpenSession;
struct OpenSession {
  char *zName;             /* Symbolic name for this session */
  int nFilter;             /* Number of xFilter rejection GLOB patterns */
  char **azFilter;         /* Array of xFilter rejection GLOB patterns */
  sqlite3_session *p;      /* The open session */
};
#endif

typedef struct ExpertInfo ExpertInfo;
struct ExpertInfo {
  sqlite3expert *pExpert;
  int bVerbose;
};

/* A single line in the EQP output */
typedef struct EQPGraphRow EQPGraphRow;
struct EQPGraphRow {
  int iEqpId;           /* ID for this row */
  int iParentId;        /* ID of the parent row */
  EQPGraphRow *pNext;   /* Next row in sequence */
  char zText[1];        /* Text to display for this row */
};

/* All EQP output is collected into an instance of the following */
typedef struct EQPGraph EQPGraph;
struct EQPGraph {
  EQPGraphRow *pRow;    /* Linked list of all rows of the EQP output */
  EQPGraphRow *pLast;   /* Last element of the pRow list */
  char zPrefix[100];    /* Graph prefix */
};

/* Parameters affecting columnar mode result display (defaulting together) */
typedef struct ColModeOpts {
  int iWrap;            /* In columnar modes, wrap lines reaching this limit */
  u8 bQuote;            /* Quote results for .mode box and table */
  u8 bWordWrap;         /* In columnar modes, wrap at word boundaries  */
} ColModeOpts;

typedef struct ShellState ShellState;
struct ShellState {
  sqlite3 *db;           /* The database */
  u8 autoExplain;        /* Automatically turn on .explain mode */
  u8 autoEQP;            /* Run EXPLAIN QUERY PLAN prior to each SQL stmt */
  u8 autoEQPtest;        /* autoEQP is in test mode */
  u8 autoEQPtrace;       /* autoEQP is in trace mode */
  u8 scanstatsOn;        /* True to display scan stats before each finalize */
  u8 openMode;           /* SHELL_OPEN_NORMAL, _APPENDVFS, or _ZIPFILE */
  u8 doXdgOpen;          /* Invoke start/open/xdg-open in output_reset() */
  u8 nEqpLevel;          /* Depth of the EQP output graph */
  u8 eTraceType;         /* SHELL_TRACE_* value for type of trace */
  u8 bSafeMode;          /* True to prohibit unsafe operations */
  u8 bSafeModePersist;   /* The long-term value of bSafeMode */
  ColModeOpts cmOpts;    /* Option values affecting columnar mode output */
  unsigned statsOn;      /* True to display memory stats before each finalize */
  unsigned mEqpLines;    /* Mask of vertical lines in the EQP output graph */
  int inputNesting;      /* Track nesting level of .read and other redirects */
  int outCount;          /* Revert to stdout when reaching zero */
  int cnt;               /* Number of records displayed so far */
  int lineno;            /* Line number of last line read from in */
  int openFlags;         /* Additional flags to open.  (SQLITE_OPEN_NOFOLLOW) */
  FILE *in;              /* Read commands from this stream */
  FILE *out;             /* Write results here */
  FILE *traceOut;        /* Output for sqlite3_trace() */
  int nErr;              /* Number of errors seen */
  int mode;              /* An output mode setting */
  int modePrior;         /* Saved mode */
  int cMode;             /* temporary output mode for the current query */
  int normalMode;        /* Output mode before ".explain on" */
  int writableSchema;    /* True if PRAGMA writable_schema=ON */
  int showHeader;        /* True to show column names in List or Column mode */
  int nCheck;            /* Number of ".check" commands run */
  unsigned nProgress;    /* Number of progress callbacks encountered */
  unsigned mxProgress;   /* Maximum progress callbacks before failing */
  unsigned flgProgress;  /* Flags for the progress callback */
  unsigned shellFlgs;    /* Various flags */
  unsigned priorShFlgs;  /* Saved copy of flags */
  sqlite3_int64 szMax;   /* --maxsize argument to .open */
  char *zDestTable;      /* Name of destination table when MODE_Insert */
  char *zTempFile;       /* Temporary file that might need deleting */
  char zTestcase[30];    /* Name of current test case */
  char colSeparator[20]; /* Column separator character for several modes */
  char rowSeparator[20]; /* Row separator character for MODE_Ascii */
  char colSepPrior[20];  /* Saved column separator */
  char rowSepPrior[20];  /* Saved row separator */
  int *colWidth;         /* Requested width of each column in columnar modes */
  int *actualWidth;      /* Actual width of each column */
  int nWidth;            /* Number of slots in colWidth[] and actualWidth[] */
  char nullValue[20];    /* The text to print when a NULL comes back from
                         ** the database */
  char outfile[FILENAME_MAX]; /* Filename for *out */
  sqlite3_stmt *pStmt;   /* Current statement if any. */
  FILE *pLog;            /* Write log output here */
  struct AuxDb {         /* Storage space for auxiliary database connections */
    sqlite3 *db;               /* Connection pointer */
    const char *zDbFilename;   /* Filename used to open the connection */
    char *zFreeOnClose;        /* Free this memory allocation on close */
#if defined(SQLITE_ENABLE_SESSION)
    int nSession;              /* Number of active sessions */
    OpenSession aSession[4];   /* Array of sessions.  [0] is in focus. */
#endif
  } aAuxDb[5],           /* Array of all database connections */
    *pAuxDb;             /* Currently active database connection */
  int *aiIndent;         /* Array of indents used in MODE_Explain */
  int nIndent;           /* Size of array aiIndent[] */
  int iIndent;           /* Index of current op in aiIndent[] */
  char *zNonce;          /* Nonce for temporary safe-mode escapes */
  EQPGraph sGraph;       /* Information for the graphical EXPLAIN QUERY PLAN */
  ExpertInfo expert;     /* Valid if previous command was ".expert OPT..." */
#ifdef SQLITE_SHELL_FIDDLE
  struct {
    const char * zInput; /* Input string from wasm/JS proxy */
    const char * zPos;   /* Cursor pos into zInput */
    const char * zDefaultDbName; /* Default name for db file */
  } wasm;
#endif
};

int do_meta_command_r(char *zLine, struct ShellState *p);