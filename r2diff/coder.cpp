#include "stdafx.h"
#include "defines.h"
#include "Platform.h"
#include <vector>
#include <stdint.h>
#include <iostream>
using namespace std;



int64_t size_uncompressed=0;



namespace Coder{
Pipe pipe;
namespace FileFlag{
	const byte offset=1;
	const byte bytes=2;
}

vector<byte> buf;
const int charbufmax=1024*1024;
byte charbuf[charbufmax];
int charbuflen=0;

bool writeinit(int64_t newfilelen){
	buf.reserve(2*1024*1024);
	bool ret= pipe.Create();
	if(!ret)return ret;
	return pipe.Write(&newfilelen,sizeof(newfilelen));
}
void writeflush(bool force=true){
	if(buf.empty())return;
	if(!force && buf.size()<1024*1024)return;

	pipe.Write(&buf[0],buf.size());
	size_uncompressed+=buf.size();
	buf.clear();	
}
void writeflushchar(){
	if(charbuflen==0)return;
	buf.push_back(FileFlag::bytes);

	int len=charbuflen;
	byte *po=(byte*)&len;
	buf.insert(buf.end(),po,po+sizeof(int));
	buf.insert(buf.end(),charbuf,charbuf+charbuflen);
	charbuflen=0;
	writeflush(false);
	cout<<'*';//<<len<<'*';
}
void writeoff(offset_t off){
	writeflushchar();
	buf.push_back(FileFlag::offset);
	byte *po=(byte*)&off;
	buf.insert(buf.end(),po,po+sizeof(offset_t));
	writeflush(false);
	cout<<"-";
}
void writebyte(byte bt){	
	charbuf[charbuflen++]=bt;
	if(charbuflen>=charbufmax){
		writeflushchar();
	}
	/*__asm{
		int 3;
	}*/
}
void writeclose(){
	writeflushchar();
	writeflush(true);	
	pipe.Close(false,true);
}
}