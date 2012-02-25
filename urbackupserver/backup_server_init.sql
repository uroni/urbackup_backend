PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE clients (
 id INTEGER PRIMARY KEY,
 name TEXT,
 lastbackup DATE,
 lastseen DATE
, lastbackup_image DATE, bytes_used_files INTEGER, bytes_used_images INTEGER);
CREATE TABLE settings (
  key TEXT,
  value TEXT , clientid INTEGER);
CREATE TABLE backups (
 id INTEGER PRIMARY KEY,
 clientid INTEGER,
 backuptime DATE DEFAULT CURRENT_TIMESTAMP,
 incremental INTEGER,
 path TEXT
, complete INTEGER, running DATE, size_bytes INTEGER, done INTEGER);
CREATE TABLE files (
 backupid INTEGER,
 fullpath TEXT,
 shahash BLOB,
 filesize INTEGER,
 created DATE DEFAULT CURRENT_TIMESTAMP
, rsize INTEGER, did_count INTEGER);
CREATE TABLE backup_images (
 id INTEGER PRIMARY KEY,
 clientid INTEGER,
 backuptime DATE DEFAULT CURRENT_TIMESTAMP,
 incremental INTEGER,
 incremental_ref INTEGER,
 path TEXT,
 complete INTEGER
, running DATE, size_bytes INTEGER);
CREATE TABLE files_del ( 
backupid INTEGER,
fullpath TEXT,
shahash BLOB,
filesize INTEGER,
created DATE,
rsize INTEGER,
clientid INTEGER, incremental INTEGER);
CREATE TABLE del_stats (
backupid INTEGER,
image INTEGER,
delsize INTEGER,
created DATE DEFAULT CURRENT_TIMESTAMP
, clientid INTEGER REFERENCES clients(id) ON DELETE CASCADE, incremental INTEGER, stoptime DATE);
CREATE TABLE si_users
(
id INTEGER PRIMARY KEY,
name TEXT,
password_md5 TEXT,
salt TEXT
);
CREATE TABLE si_permissions
(
clientid INTEGER REFERENCES si_users(id) ON DELETE CASCADE,
t_right TEXT,
t_domain TEXT
);
CREATE TABLE clients_hist (
 id INTEGER REFERENCES clients(id) ON DELETE CASCADE,
 name TEXT,
 lastbackup DATE,
 lastseen DATE,
 lastbackup_image DATE,
 bytes_used_files INTEGER,
 bytes_used_images INTEGER,
 created DATE DEFAULT CURRENT_TIMESTAMP
, hist_id INTEGER REFERENCES clients_hist_id(id) ON DELETE CASCADE);
CREATE TABLE clients_hist_id (
 id INTEGER PRIMARY KEY,
 created DATE DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE logs (
 id INTEGER PRIMARY KEY,
 clientid INTEGER REFERENCES clients(id) ON DELETE CASCADE,
 created DATE DEFAULT CURRENT_TIMESTAMP,
 sent INTEGER DEFAULT 0,
 logdata TEXT
, errors INTEGER, warnings INTEGER, infos INTEGER, image INTEGER, incremental INTEGER);
CREATE TABLE misc (
 id INTEGER PRIMARY KEY,
 tkey TEXT,
 tvalue TEXT
);
INSERT INTO "misc" VALUES(1,'db_version','3');
CREATE INDEX files_idx ON files (shahash);
CREATE INDEX clients_hist_created_idx ON clients_hist (created);
COMMIT;
