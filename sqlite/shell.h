typedef unsigned char u8;

typedef struct sqlite3expert sqlite3expert;

typedef struct ExpertInfo ExpertInfo;
struct ExpertInfo {
    sqlite3expert* pExpert;
    int bVerbose;
};

/* A single line in the EQP output */
typedef struct EQPGraphRow EQPGraphRow;
struct EQPGraphRow {
    int iEqpId;           /* ID for this row */
    int iParentId;        /* ID of the parent row */
    EQPGraphRow* pNext;   /* Next row in sequence */
    char zText[1];        /* Text to display for this row */
};

/* All EQP output is collected into an instance of the following */
typedef struct EQPGraph EQPGraph;
struct EQPGraph {
    EQPGraphRow* pRow;    /* Linked list of all rows of the EQP output */
    EQPGraphRow* pLast;   /* Last element of the pRow list */
    char zPrefix[100];    /* Graph prefix */
};

typedef struct ShellState ShellState;
struct ShellState {
    sqlite3* db;           /* The database */
    u8 autoExplain;        /* Automatically turn on .explain mode */
    u8 autoEQP;            /* Run EXPLAIN QUERY PLAN prior to seach SQL stmt */
    u8 autoEQPtest;        /* autoEQP is in test mode */
    u8 autoEQPtrace;       /* autoEQP is in trace mode */
    u8 statsOn;            /* True to display memory stats before each finalize */
    u8 scanstatsOn;        /* True to display scan stats before each finalize */
    u8 openMode;           /* SHELL_OPEN_NORMAL, _APPENDVFS, or _ZIPFILE */
    u8 doXdgOpen;          /* Invoke start/open/xdg-open in output_reset() */
    u8 nEqpLevel;          /* Depth of the EQP output graph */
    u8 eTraceType;         /* SHELL_TRACE_* value for type of trace */
    unsigned mEqpLines;    /* Mask of veritical lines in the EQP output graph */
    int outCount;          /* Revert to stdout when reaching zero */
    int cnt;               /* Number of records displayed so far */
    int lineno;            /* Line number of last line read from in */
    int openFlags;         /* Additional flags to open.  (SQLITE_OPEN_NOFOLLOW) */
    FILE* in;              /* Read commands from this stream */
    FILE* out;             /* Write results here */
    FILE* traceOut;        /* Output for sqlite3_trace() */
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
    sqlite3_int64 szMax;   /* --maxsize argument to .open */
    char* zDestTable;      /* Name of destination table when MODE_Insert */
    char* zTempFile;       /* Temporary file that might need deleting */
    char zTestcase[30];    /* Name of current test case */
    char colSeparator[20]; /* Column separator character for several modes */
    char rowSeparator[20]; /* Row separator character for MODE_Ascii */
    char colSepPrior[20];  /* Saved column separator */
    char rowSepPrior[20];  /* Saved row separator */
    int colWidth[100];     /* Requested width of each column when in column mode*/
    int actualWidth[100];  /* Actual width of each column */
    char nullValue[20];    /* The text to print when a NULL comes back from
                           ** the database */
    char outfile[FILENAME_MAX]; /* Filename for *out */
    const char* zDbFilename;    /* name of the database file */
    char* zFreeOnClose;         /* Filename to free when closing */
    const char* zVfs;           /* Name of VFS to use */
    sqlite3_stmt* pStmt;   /* Current statement if any. */
    FILE* pLog;            /* Write log output here */
    int* aiIndent;         /* Array of indents used in MODE_Explain */
    int nIndent;           /* Size of array aiIndent[] */
    int iIndent;           /* Index of current op in aiIndent[] */
    EQPGraph sGraph;       /* Information for the graphical EXPLAIN QUERY PLAN */
#if defined(SQLITE_ENABLE_SESSION)
    int nSession;             /* Number of active sessions */
    OpenSession aSession[4];  /* Array of sessions.  [0] is in focus. */
#endif
    ExpertInfo expert;        /* Valid if previous command was ".expert OPT..." */
};

int do_meta_command_r(char *zLine, struct ShellState *p);