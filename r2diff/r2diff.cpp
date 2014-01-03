// r2diff.cpp : Defines the entry point for the console application.
//
///todo progress, 64bit, md5
#include "stdafx.h"
#include "defines.h"
#include <iostream>
#include <assert.h>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <functional>
#include <string>
#include <stdio.h>

#include <iomanip>
using namespace std;

#include "md5.h"
#include "Platform.h"
#include "compress.h"
#include "coder.h"


const unsigned int hash_magic=65521;
//static assert max_bs<16
const int max_bs=12;
const int bsbmax=1<<max_bs;
struct hashtgt{
	offset_t offsetx;
	unsigned char digest[16];
#if WITH_FIRST16_BYTE_CHECK
	unsigned char first16[16];
#endif
};
unordered_multimap<hash_t,hashtgt> hashes;



#if PERFORMANCE_STATISTICS
int conflict=0;
int totalh=0;
#endif
inline void insert_hash(hash_t key,hashtgt &val){
#if PERFORMANCE_STATISTICS
	++totalh;
#endif
	for(auto it=hashes.find(key);it!=hashes.end();++it){
		if(memcmp(val.digest,it->second.digest,sizeof(val.digest))==0){
#if PERFORMANCE_STATISTICS
			++conflict;
#endif
			//already have the same one inserted!
			return;
		}
	}
	hashes.insert(make_pair(key,val));
}


#define RUN_TEST 0
#if RUN_TEST
const int MOD_ADLER = 65521; 
hash_t adler32d(byte *data, int len) /* where data is the location of the data in physical memory and 
                                                       len is the length of the data in bytes */
{
    hash_t a = 1, b = 0;
    int index;
 
    /* Process each byte of the data in order */
    for (index = 0; index < len; ++index)
    {
        a = (a + data[index]) % MOD_ADLER;
        b = (b + a) % MOD_ADLER;
    }
 
    return (b << 16) | a;
}
#endif


/**
Adler32 Hash Index for the input block starts from offset0
Put it into hashes
*/
void MakeAdler32Index(byte * input,int len,int64_t offset0){	
	int bsbmax=1<<max_bs;
	int bs=max_bs;
	int bsb=1<<bs;	

	for(int bi=0;bi<len;bi+=bsb){		
		hash_t a=1,b=0;
		int di=0;
		for(;di<bsb ;++di){
			a=(a+input[bi+di])%hash_magic;
			b=(b+a)%hash_magic;
		}
		hash_t key=(b<<16)+a;
		hashtgt htgt;
		assert((((bi+offset0)>>max_bs)>>32)==0);
		htgt.offsetx=(offset_t)((bi+offset0)>>max_bs);
#if WITH_FIRST16_BYTE_CHECK
		memcpy(htgt.first16,&input[bi],sizeof(htgt.first16));
#endif
		MD5_CTX context;
		MD5_Init(&context);
		MD5_Update(&context, &input[bi], bsb);
		MD5_Final(htgt.digest, &context);
		insert_hash(key,htgt);
	}
}


/*Scan and generate the diff file
more(buf,start,len,buffsize): read more date from input
*/
void Scan(function<offset_t(byte*,int64_t,offset_t,offset_t)> more,int64_t len){	
	int64_t fpos=0;	
	const int preread=5*1024*1024;
	byte * input=new byte[preread];
	{
		int toread=(int32_t)min(preread,len-fpos);
		toread=max(bsbmax,toread);	
		fpos+=more(input,0,toread,preread);
	}

	int bs=max_bs;
	int bsb=1<<bs;		

	hash_t a=1,b=0;

	
	for(int di=0;di<bsb;++di){
		a=(a+input[di%preread])%hash_magic;
		b=(b+a)%hash_magic;
	}
		

	offset_t written=0;	
	int64_t written64=0;
	while(written64<len){
		int64_t fileleft=len-fpos;
		//preread when reach 10*bsbmax byte from fpos
		if(fileleft>0 && written64+10*bsbmax>fpos){
			int toread=(int)min(preread-20*bsbmax,fileleft);
			fpos+=more(input,fpos,toread,preread);
		}

		bool find=false;			
		unsigned char digest[16];
		bool digestgen=false;
		bool after_a_match=false;
		for(auto it=hashes.find((b<<16)+a);it!=hashes.end();++it){
		
#if WITH_FIRST16_BYTE_CHECK
			{
				int s0=written%preread;
				int e0=(s0+sizeof(it->second.first16))%preread;
				//test only for this case
				if(s0<e0){
					if(memcmp(it->second.first16, &input[s0], sizeof(it->second.first16))!=0){
						continue;
					}
				}
			}
#endif
			if(!digestgen){
				digestgen=true;
				MD5_CTX context;
				MD5_Init(&context);
				{
					int s0=written%preread;
					int e0=(s0+bsb)%preread;

					if(s0<e0){
						MD5_Update(&context, &input[s0], bsb);
					}else{
						MD5_Update(&context, &input[s0], preread-s0);
						MD5_Update(&context, &input[0], e0);
					}
				}				
				MD5_Final(digest, &context);
			}
			if(memcmp(it->second.digest,digest,sizeof(digest))==0){				
				find=true;
				Coder::writeoff(it->second.offsetx);
				written+=bsb;
				written64+=bsb;


				a=1,b=0;
				for(int di=0;di<bsb;++di){
					a=(a+input[(written+di)%preread])%hash_magic;
					b=(b+a)%hash_magic;
				}
				after_a_match=true;
				break;
			}else{

			}
		}
		if(!find){
			int writesize=1;
			if(after_a_match){
				(int)((len/(1024*1024))&0x8fffffff);
				writesize=rand()%writesize+1;
			}
			after_a_match=false;

			for(int i=0;i<writesize && written64<len;++i){
				byte wbyte=input[written%preread];
				Coder::writebyte(wbyte);
				++written;	
				++written64;
			
				offset_t scan=written+bsb-1;
			//(32 bits trap) for(;scan<written+bsbmax && scan<len;++scan){
				//for [scan-bsb+1,scan]				
				int bs=max_bs;
				int bsb=1<<bs;
				hash_t olda=a;
				hash_t oldb=b;
				a=(olda+hash_magic-wbyte+input[scan%preread])%hash_magic;
				b=(oldb
					+hash_magic-(wbyte*bsb)%hash_magic
					+a-1
					)%hash_magic;
			//}

			}
		}
		
	}
	Coder::writeclose();
}

/*
Encode to diff
*/
int encode( _TCHAR* noldfile, _TCHAR* nnewfile, _TCHAR* ndifffile){
	ifstream oldfile(noldfile,ios::binary);
	ifstream newfile(nnewfile,ios::binary);
	ofstream difffile(ndifffile,ios::binary);

	if(!oldfile || !newfile || !difffile)return -1;

	oldfile.seekg(0,oldfile.end);
	int64_t oldlen=oldfile.tellg().seekpos();
	oldfile.seekg(0,oldfile.beg);
	cerr<<"Original file length:"<<oldlen<<endl;

	//Index the old file
	{
		//if(oldlen>20*1024*1024)oldlen=20*1024*1024;
		int bs=max_bs;
		int bsb=1<<bs;
		size_t reserve=(size_t)(oldlen/bsb)*4;
		//cout<<"reserve for "<<reserve<<endl;			
		hashes.rehash(reserve);

		const int blocksize=5*1024*1024;
		byte* oldbuf=new byte[blocksize];
		int64_t read=0;		
		while(read<oldlen){
			int64_t left=oldlen-read;
			int toread=(int)min(blocksize,left);
			oldfile.read((char*)oldbuf,toread);
			/*if(read==0){
				memcpy(back,&oldbuf[0xa92000],0x1000);
			}*/
			int toread_aligned=(toread+(1<<max_bs)-1)/(1<<max_bs)*(1<<max_bs);
			//padding 0
			for(int i=toread;i<toread_aligned;++i){
				oldbuf[i]=0;
			}
			MakeAdler32Index(oldbuf,toread_aligned,read);
			read+=toread;


			double per=read*100.0/oldlen;
			char buf[1024];
			sprintf_s(buf,"\rIndexing ...         %2.4lf%% [%.0lf/%.0lf]",per,(double)read,(double)oldlen);
			cerr<<buf;
		}
		delete[] oldbuf;
		cerr<<'\n';
	}


	//Scan the new file, 
	newfile.seekg(0,newfile.end);
	int64_t newlen=newfile.tellg().seekpos();
	newfile.seekg(0,newfile.beg);
	int64_t newlen_aligned=(newlen+(int64_t)(1<<max_bs)-(int64_t)1)/(1<<max_bs)*(1<<max_bs);	
	

	if(!Coder::writeinit(newlen)){
		cerr<<"Failed to write init!"<<endl;
		return -1;
	}

	function<void()> threadf=[&](){
		CompressInc(Coder::pipe,&difffile);
	};
	thread compresst(threadf);
	//newlen=newlen_aligned=3u*1024u*1024u*1024u;
	//will roll back, but will aligned to 4 bytes, so it's OK
	Scan([&](byte* data,int64_t start,offset_t len,offset_t bufsize)->offset_t{

		char buf[1024];
		sprintf_s(buf,"\nScanning ...  P: %2.4lf%%[%.0lfK/%.0lfK] C:%2.4lf%% O:%.0lfK"
			,(double)start*100.0/(double)newlen,(double)start/1024.0,(double)newlen/1024.0
			,(double)size_uncompressed*100.0/((double)start+1),(double)size_compressed/1024.0
			);
		cerr<<buf;


		size_t toread=(int32_t)min(len,(newlen-start));
		{
			int s0=start%bufsize;
			int e0=(s0+toread)%bufsize;

			if(e0>s0){
				newfile.read((char*)data+s0,e0-s0);
			}else{
				newfile.read((char*)data+s0,bufsize-s0);
				newfile.read((char*)data,e0);
			}
		}
		//padding 0
		if(toread<len){
			int s1=(start+toread)%bufsize;
			int e1=(start+len)%bufsize;

			if(e1>s1){
				memset((char*)data+s1,0,e1-s1);
			}else{
				memset((char*)data+s1,0,bufsize-s1);
				memset((char*)data,0,e1);
			}
		}
		return len;
	
	},newlen_aligned);

	compresst.join();
	Coder::pipe.Close();
	

	char buf[1024];
	sprintf_s(buf,"\nScanning ...  P: %2.4lf%%[%.0lfK/%.0lfK] C:%2.4lf%% O:%.0lfK"
			,(double)100.0,(double)newlen/1024.0,(double)newlen/1024.0
			,(double)size_uncompressed*100.0/((double)newlen),(double)size_compressed/1024.0
			);
	cerr<<buf<<endl;


	//TODO: make a final check of the generated file to make sure it's ok
	return 0;
}

int decode(_TCHAR *noldfile,_TCHAR* ndifffile,_TCHAR* nnewfile){
	ifstream oldfile(noldfile,ios::binary);	
	ifstream difffile(ndifffile,ios::binary);
	ofstream newfile(nnewfile,ios::binary);
	if(!oldfile || !newfile || !difffile)return -1;
	const unsigned int buflen=1024*1024*5;

	{
		bool p=Coder::pipe.Create();
		assert(p);
	}

	function<void()> decodetopipe=[&](){
		DecodeToPipe(difffile,Coder::pipe);
	};
	thread th(decodetopipe);
	
	{
		byte *outbuf=new byte[buflen];
		size_t outbufstart=0,outbufend=0;
		auto readbuf=[&](void * dst,size_t size)->bool{
			if(size>=buflen){throw "error";};
			if(((buflen+outbufend-outbufstart)%buflen)<size)return false;
			if(outbufend>outbufstart){
				memcpy(dst,&outbuf[outbufstart],size);
				outbufstart+=size;
				outbufstart%=buflen;
			}else{
				unsigned len0=min(buflen-outbufstart,size);

				memcpy(dst,&outbuf[outbufstart],len0);
				outbufstart+=len0;
				outbufstart%=buflen;

				int len1=size-len0;
				if(len1<=0)return true;
				memcpy((char*)dst+len0,&outbuf[0],len1);
				outbufstart=len1;
				outbufstart%=buflen;
			}
			return true;
		};
		auto readmore=[&]()->bool{
			//full
			unsigned spacereaded=((buflen+outbufend-outbufstart)%buflen);
			unsigned space=buflen-1-spacereaded;
			if(space<=0)return false;
			unsigned read=0;
			if(outbufend>=outbufstart){
				if(!Coder::pipe.Read(&outbuf[outbufend],min(buflen-outbufend,space),read))return false;
				//cout<<"Readed "<<read<<endl;
				outbufend+=read;
				outbufend%=buflen;
			}else{		
				if(!Coder::pipe.Read(&outbuf[outbufend],space,read))return false;
				//cout<<"Readed "<<read<<endl;
				outbufend+=read;
				outbufend%=buflen;
			}
			return true;			
		};
		auto freadbuf=[&](void * dst,size_t size)->bool{	
			//cout<<"Read"<<size<<endl;
			//cout<<"1:"<<outbufstart<<" "<<outbufend<<endl;
			if(readbuf(dst,size))return true;
			//cout<<"2:"<<outbufstart<<" "<<outbufend<<endl;
			while(readmore());
			//cout<<"3:"<<outbufstart<<" "<<outbufend<<endl;
			bool ret= readbuf(dst,size);
			//cout<<"4:"<<outbufstart<<" "<<outbufend<<endl;
			return ret;
		};

		byte *charbuf=new byte[buflen];
		bool eof=false;
		int64_t fleft=0;
		if(!freadbuf(&fleft,sizeof(int64_t))){
			eof=1;
			return -1;
		}
		cerr<<"Total Len :"<<std::setiosflags(ios::fixed)<<std::setprecision(0)<<(double)fleft<<endl;
		
	
		while(!eof){
			byte key;
			if(!freadbuf(&key,1)){
				eof=1;
				break;
			}
			switch(key){
			case Coder::FileFlag::bytes:
				{
					int size;
					if(!freadbuf(&size,sizeof(int))){
						eof=1;
						break;
					}
					if(!freadbuf(charbuf,size)){
						eof=1;
						break;
					}					
					if(size>fleft)size=(size_t)fleft;
#if !FAKE_WRITE_OUT
					newfile.write((char*)charbuf,size);
#endif
					fleft-=size;
				}
				break;
			case Coder::FileFlag::offset:
				{
					offset_t pos;
					if(!freadbuf(&pos,sizeof(offset_t))){
						eof=1;
						break;
					}
#if !FAKE_WRITE_OUT
					oldfile.seekg(pos<<max_bs);
					oldfile.read((char*)charbuf,1<<max_bs);
#endif
					int size=1<<max_bs;
					if(size>fleft)size=(size_t)fleft;
#if !FAKE_WRITE_OUT
					newfile.write((char*)charbuf,size);
#endif
					fleft-=size;
				}
				break;
			}
		}
		cerr<<"Fleft :"<<(double)fleft<<endl;
		delete[] outbuf;
		delete[] charbuf;
	}
	cerr<<"Exiting ... "<<endl;
	cerr.flush();
	th.join();

	Coder::pipe.Close();
	return 0;
}


int fcmp(_TCHAR* src,_TCHAR* dst){
	ifstream oldfile(src,ios::binary);	
	ifstream newfile(dst,ios::binary);	
	if(!oldfile || !newfile)return -1;

	oldfile.seekg(0,oldfile.end);
	int64_t oldlen=oldfile.tellg().seekpos();
	oldfile.seekg(0,oldfile.beg);
	cerr<<"Old file length:"<<oldlen<<endl;


	newfile.seekg(0,newfile.end);
	int64_t newlen=newfile.tellg().seekpos();
	newfile.seekg(0,newfile.beg);
	cerr<<"New file length:"<<newlen<<endl;

	if(newlen!=oldlen){
		cout<<"Len not equal!!"<<endl;
		return -1;
	}

	int64_t len=min(oldlen,newlen);
	char obuf[102400],nbuf[102400];
	for(int64_t i=0;i<len;++i){
		oldfile.read(obuf,sizeof(obuf));
		newfile.read(nbuf,sizeof(obuf));
		if((!oldfile || !newfile)){			
			return -1;
		}
		int len=(int)oldfile.gcount();
		int newlen=(int)newfile.gcount();
		if(len!=newlen){
			cout<<"Len not equal!!"<<endl;
			return -1;
		}

		if(memcmp(obuf,nbuf,min(sizeof(obuf),len))!=0){
			cout<<endl<<std::setiosflags(ios::fixed)<<std::setprecision(0)<<(double)i*sizeof(obuf)<<endl;
			return -1;			
		}
		
		cout<<'.';
	}
	return 0;
}



int _tmain(int argc, _TCHAR* argv[])
{  
	if(argc==5){
		if(0==_tcscmp(argv[1],L"-d")){
			return decode(argv[2],argv[3],argv[4]);
		}else if(0==_tcscmp(argv[1],L"-e")){	
			return encode(argv[2],argv[3],argv[4]);
		}
	}
	if(argc=4){
		if(0==_tcscmp(argv[1],L"-c")){
			return fcmp(argv[2],argv[3]);
		}
	}

	{
		std::cout<<"Usage:"<<endl;
		std::cout<<"Diff(encode): r2diff -e old new diff"<<endl;
		std::cout<<"Patching(decode): r2diff -d old diff newfile"<<endl;
		return 1;
	}	
}


