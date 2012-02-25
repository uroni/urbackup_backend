#ifndef CSTREAM_H
#define CSTREAM_H

typedef unsigned char uchar;

const uchar ID_GET_FILE=0;
const uchar ID_GET_FILE_RESUME=6;
	const uchar ID_COULDNT_OPEN=0;
	const uchar ID_FILESIZE=1;
	const uchar ID_BASE_DIR_LOST=2;
const uchar ID_GET_PACKET=1;
const uchar ID_GET_100_PACKETS=2;
const uchar ID_PING=3;
        const uchar ID_PONG=0;
const uchar ID_GET_GAMELIST=4;
        const uchar ID_GAMELIST=0;

#endif
