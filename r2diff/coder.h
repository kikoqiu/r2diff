#pragma once
#include "defines.h"


extern int64_t size_uncompressed;




namespace Coder{
extern Pipe pipe;
namespace FileFlag{
	const byte offset=1;
	const byte bytes=2;
}

bool writeinit(int64_t newfilelen);
void writeflush(bool force=true);
void writeflushchar();
void writeoff(offset_t off);
void writebyte(byte bt);
void writeclose();
}

