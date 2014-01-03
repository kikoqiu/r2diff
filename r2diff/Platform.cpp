#include "stdafx.h"
#include "Platform.h"
#include <iostream>

#include <strsafe.h>

void ErrorExit(LPTSTR lpszFunction) 
{ 
    // Retrieve the system error message for the last-error code

    LPVOID lpMsgBuf;
    LPVOID lpDisplayBuf;
    DWORD dw = GetLastError(); 

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR) &lpMsgBuf,
        0, NULL );

    // Display the error message and exit the process

    lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT, 
        (lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 400) * sizeof(TCHAR)); 
    StringCchPrintf((LPTSTR)lpDisplayBuf, 
        LocalSize(lpDisplayBuf) / sizeof(TCHAR),
        TEXT("%s failed with error %d: %s"), 
        lpszFunction, dw, lpMsgBuf); 
	std::wcerr<<L"Pipe Read Failed ->"<<(TCHAR*)lpDisplayBuf<<std::endl;

    LocalFree(lpMsgBuf);
    LocalFree(lpDisplayBuf);
    //ExitProcess(dw); 
}





bool Pipe::Create(){
	hread=INVALID_HANDLE_VALUE;
	hwrite=INVALID_HANDLE_VALUE;
	return !!CreatePipe(&hread,&hwrite,0,1024*1024*2);
}
void Pipe::Close(bool read,bool write){
	if(read && hread!=INVALID_HANDLE_VALUE){
		CloseHandle(hread);
		hread=INVALID_HANDLE_VALUE;
	}
	if(write&& hwrite!=INVALID_HANDLE_VALUE){
		CloseHandle(hwrite);
		hwrite=INVALID_HANDLE_VALUE;
	}
}

static unsigned wt=0;
bool Pipe::Write(const void *buf,size_t len){
	DWORD written;
	bool ret= !!WriteFile(hwrite,buf,len,&written,0);
	wt+=written;
	//std::cout<<"wt "<<wt<<std::endl;
	return ret;
}
static unsigned readedt=0;
bool Pipe::Read(void *buf,size_t len,unsigned &read){	
	DWORD rr;
	if(ReadFile(hread,buf,len,&rr,0)){
		read=rr;
		//std::cout<<"PRead "<<rr<<std::endl;
		readedt+=rr;
		//std::cout<<"PRead "<<readedt<<std::endl;
		return true;
	}
	//ErrorExit(_T("Pipe Read Failed"));
	//std::cerr<<"\nPipe Read Failed"<<len<<std::endl;
	return false;
}

static DWORD WINAPI ThreadFunc(LPVOID param){
	try{
		std::function<void(void)> *f=(std::function<void(void)>*)param;
		(*f)();
	}catch(...){
		std::cerr<<"Thread Exception"<<std::endl;
	}
	//std::cerr<<"Thread Exiting"<<std::endl;
	return 0;
}
thread::thread(std::function<void(void)> &func){
	handle=CreateThread(0,0,ThreadFunc,&func,0,(LPDWORD)&pid);
}

void thread::join(){
	WaitForSingleObject((HANDLE)handle,INFINITE);
}