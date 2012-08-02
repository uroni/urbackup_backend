CREATE TABLE IF NOT EXISTS logdata (
 id INTEGER PRIMARY KEY,
 logid INTEGER,
 loglevel INTEGER,
 message TEXT,
 idx INTEGER
);

CREATE TABLE IF NOT EXISTS  logs (
 id INTEGER PRIMARY KEY,
 ttime DATE DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS shadowcopies (
 id INTEGER PRIMARY KEY,
 vssid BLOB,
 ssetid BLOB,
 target TEXT,
 path TEXT
);

ALTER TABLE shadowcopies ADD orig_target TEXT;
ALTER TABLE shadowcopies ADD filesrv INTEGER;