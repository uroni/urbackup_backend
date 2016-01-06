#pragma once

namespace
{

//from client
const uchar ID_GET_FILE=0;
const uchar ID_GET_FILE_RESUME=6;
const uchar ID_GET_FILE_RESUME_HASH=7;
const uchar ID_GET_FILE_METADATA_ONLY=12;
const uchar ID_GET_FILE_WITH_METADATA=15;
const uchar ID_GET_FILE_BLOCKDIFF=8;
const uchar ID_GET_FILE_BLOCKDIFF_WITH_METADATA=16;
	const uchar ID_COULDNT_OPEN=0;
	const uchar ID_FILESIZE=1;
	const uchar ID_BASE_DIR_LOST=2;
const uchar ID_GET_PACKET=1;
const uchar ID_GET_100_PACKETS=2;
const uchar ID_PING=3;
        const uchar ID_PONG=0;
const uchar ID_BACKUP_SOCKET=5;
		const uchar ID_CONNECTION_ID=12;
const uchar ID_BLOCK_REQUEST=9;
		const uchar ID_WHOLE_BLOCK=13;
		const uchar ID_UPDATE_CHUNK=14;
		const uchar ID_NO_CHANGE=15;
		const uchar ID_BLOCK_HASH=16;
		const uchar ID_BLOCK_ERROR=18;
const uchar ID_GET_FILE_HASH_AND_METADATA=10;
		const uchar ID_FILE_HASH_AND_METADATA=17;
const uchar ID_INFORM_METADATA_STREAM_END=11;
const uchar ID_FLUSH_SOCKET=13;
const uchar ID_SCRIPT_FINISH=14;
const uchar ID_FREE_SERVER_FILE = 18;

const unsigned int ERR_SEEKING_FAILED = 0;
const unsigned int ERR_READING_FAILED = 1;

//internal
const uchar ID_ILLEGAL = 245;

}

