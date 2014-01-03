

#include "stdafx.h"

#include "compress.h"
#include "Platform.h"
#include <assert.h>

#include "Alloc.h"
#include "LzmaLib.h"
#include "LzmaDec.h"
#include "LzmaEnc.h"
#include <iostream>
#include <fstream>
using namespace std;

#pragma comment(lib,"..\\lzma\\C\\Util\\LzmaLib\\Release\\LZMA.lib")




int64_t size_compressed=0;

static void * AllocForLzma(void *p, size_t size) { return malloc(size); }
static void FreeForLzma(void *p, void *address) { free(address); }
static ISzAlloc SzAllocForLzma = { &AllocForLzma, &FreeForLzma };



struct MyInStream
{
  Pipe &pipe;
} ;

SRes InStream_Read(void *p, void *buf, size_t *size)
{
  MyInStream *ctx = (MyInStream*)p;
  size_t read;
  if(!ctx->pipe.Read(buf,*size,read))read=0;

  *size = read; 
  return SZ_OK;
}

struct MyOutStream
{
  ISeqOutStream SeqOutStream;
  std::ostream *out;
} ;

size_t OutStream_Write(void *p, const void *buf, size_t size)
{
  MyOutStream *ctx = (MyOutStream*)p;
  if (size)
  {
	ctx->out->write((const char*)buf,size);
	size_compressed+=size;
  }
  return size;
}
void CompressInc(Pipe& pip,std::ostream *outfile)
{
  CLzmaEncHandle enc = LzmaEnc_Create(&SzAllocForLzma);

  CLzmaEncProps props;
  LzmaEncProps_Init(&props);
  props.level=9;
  props.dictSize = 1 << 26; // 64 MB
  props.writeEndMark = 1; // 0 or 1
  
  SRes res = LzmaEnc_SetProps(enc, &props);

  unsigned propsSize = LZMA_PROPS_SIZE;
  char buf[LZMA_PROPS_SIZE];

  res = LzmaEnc_WriteProperties(enc, (Byte*)buf, &propsSize);
  assert(res == SZ_OK && propsSize == LZMA_PROPS_SIZE);

  outfile->write(buf,LZMA_PROPS_SIZE);

  MyInStream inStream = { pip};
  MyOutStream outStream = { &OutStream_Write, outfile };

  res = LzmaEnc_Encode(enc,
    (ISeqOutStream*)&outStream, (ISeqInStream*)&inStream,
    0, &SzAllocForLzma, &SzAllocForLzma);
  assert(res == SZ_OK);

  LzmaEnc_Destroy(enc, &SzAllocForLzma, &SzAllocForLzma);
}

const unsigned int buflen=1024*1024*5;

void DecodeToPipe(ifstream &difffile,Pipe &pipe){
		difffile.seekg(0,difffile.end);
		int64_t difflen=difffile.tellg().seekpos();
		difffile.seekg(0,difffile.beg);
		cerr<<"Diff file length:"<<difflen<<endl;

		CLzmaDec dec;  
		LzmaDec_Construct(&dec);
		
		byte *inbuf=new byte[buflen];
		byte *outbuf=new byte[buflen];

		int64_t inbufread=0,total=difflen;
		int64_t left=difflen-inbufread;

		difffile.read((char*)inbuf,min(left,buflen));
		inbufread+=difffile.gcount();
  
		SRes res = LzmaDec_Allocate(&dec, &inbuf[0], LZMA_PROPS_SIZE, &SzAllocForLzma);
		assert(res == SZ_OK);

		LzmaDec_Init(&dec);

		unsigned inPos = LZMA_PROPS_SIZE,bufleft=(unsigned)inbufread-inPos;
		ELzmaStatus status;
		while(left>0 || bufleft>0 ){
			while (bufleft>0)
			{
				unsigned destLen = buflen;
				unsigned srcLen  = buflen - inPos;

				res = LzmaDec_DecodeToBuf(&dec,
					outbuf, &destLen,
					&inbuf[inPos], &srcLen,
					LZMA_FINISH_ANY, &status);
				assert(res == SZ_OK);
				if(res !=SZ_OK)break;
				inPos += srcLen;
				bufleft-=srcLen;
				pipe.Write(outbuf,destLen);
				if (status != LZMA_STATUS_NOT_FINISHED)
					break;
			}
			//cout<<"LZMA "<<res<<' '<<status<<endl;
			//cout<<"LZMA1 "<<bufleft<<' '<<inPos<<endl;
			if(res !=SZ_OK)break;
			/*if(status==LZMA_STATUS_NEEDS_MORE_INPUT){
				cout<<"more input?"<<inPos<<' '<<buflen<<endl;
				break;
			}*/
			if(status != LZMA_STATUS_NOT_FINISHED && status!=LZMA_STATUS_NEEDS_MORE_INPUT){
				break;
			}
			if(bufleft!=0)__asm int 3;
			difffile.read((char*)inbuf,min(left,buflen));
			bufleft=(unsigned)difffile.gcount();
			inbufread+=bufleft;	
			left=difflen-inbufread;					
			inPos=0;
			//cout<<std::setiosflags(ios::fixed)<<std::setprecision(0)<<"LZMA2 "<<(double)left<<' '<<(double)bufleft<<' '<<(double)inbufread<<' '<<(double)left<<' '<<inPos<<endl;
		}

		LzmaDec_Free(&dec, &SzAllocForLzma);
		delete [] inbuf;
		delete [] outbuf;
		pipe.Close(false,true);
	};