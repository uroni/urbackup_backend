CREATE TABLE backups (
 id INTEGER PRIMARY KEY,
 clientid INTEGER,
 backuptime DATE DEFAULT CURRENT_TIMESTAMP,
 incremental INTEGER,
 path TEXT
);
 
CREATE TABLE clients (
 id INTEGER PRIMARY KEY,
 name TEXT,
 lastbackup DATE,
 lastseen DATE
);

CREATE TABLE settings (
  key TEXT,
  value TEXT );

CREATE TABLE files (
 backupid INTEGER,
 fullpath TEXT,
 shahash BLOB,
 filesize INTEGER,
 created DATE DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE backup_images (
 id INTEGER PRIMARY KEY,
 clientid INTEGER,
 backuptime DATE DEFAULT CURRENT_TIMESTAMP,
 incremental INTEGER,
 incremental_ref INTEGER,
 path TEXT,
 complete INTEGER
);

ALTER TABLE clients ADD
 lastbackup_image DATE;

ALTER TABLE settings ADD
 clientid INTEGER;
 
ALTER TABLE backups ADD
 complete INTEGER;
 
UPDATE settings SET clientid=0 WHERE clientid='' OR clientid IS NULL;
UPDATE backups SET complete=1 WHERE complete='' OR complete IS NULL;

CREATE TEMPORARY TABLE files_tmp (
 backupid INTEGER,
 fullpath TEXT,
 shahash BLOB,
 filesize INTEGER,
 created DATE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX files_idx ON files (shahash);

ALTER TABLE backup_images ADD
	running DATE;
	
UPDATE backup_images SET running=datetime(0, 'unixepoch');

ALTER TABLE backups ADD
	running DATE;
	
UPDATE backups SET running=datetime(0, 'unixepoch');

ALTER TABLE files ADD
	rsize INTEGER;
	
ALTER TABLE files ADD
	did_count INTEGER;

CREATE TABLE files_del ( 
	backupid INTEGER,
	fullpath TEXT,
	shahash BLOB,
	filesize INTEGER,
	created DATE,
	rsize INTEGER,
	clientid INTEGER);
	
ALTER TABLE backups ADD
	size_bytes INTEGER;
	
CREATE TABLE del_stats (
	backupid INTEGER,
	image INTEGER,
	delsize INTEGER,
	created DATE DEFAULT CURRENT_TIMESTAMP
);

ALTER TABLE backup_images ADD
	size_bytes INTEGER;
	
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
);

CREATE TABLE clients_hist_id (
 id INTEGER PRIMARY KEY,
 created DATE DEFAULT CURRENT_TIMESTAMP
);

ALTER TABLE clients_hist ADD
	hist_id INTEGER REFERENCES clients_hist_id(id) ON DELETE CASCADE;

ALTER TABLE clients ADD
	bytes_used_files INTEGER;

ALTER TABLE clients ADD
	bytes_used_images INTEGER;
	
UPDATE clients SET bytes_used_images=0 WHERE bytes_used_images IS NULL;
UPDATE clients SET bytes_used_files=0 WHERE bytes_used_files IS NULL;

ALTER TABLE backups ADD
	done INTEGER;
	
UPDATE backups SET done=1 WHERE done IS NULL;

CREATE TABLE logs (
 id INTEGER PRIMARY KEY,
 clientid INTEGER REFERENCES clients(id) ON DELETE CASCADE,
 created DATE DEFAULT CURRENT_TIMESTAMP,
 sent INTEGER DEFAULT 0,
 logdata TEXT
);

ALTER TABLE del_stats ADD
	clientid INTEGER REFERENCES clients(id) ON DELETE CASCADE;
	
ALTER TABLE del_stats ADD
	incremental INTEGER;
	
ALTER TABLE del_stats ADD
	stoptime DATE;
	
ALTER TABLE files_del ADD
	incremental INTEGER;

ALTER TABLE logs ADD
	errors INTEGER;

ALTER TABLE logs ADD
	warnings INTEGER;

ALTER TABLE logs ADD
	infos INTEGER;

ALTER TABLE logs ADD
	image INTEGER;

ALTER TABLE logs ADD
	incremental INTEGER;
