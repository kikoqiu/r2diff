#pragma once




#include <functional>
class Pipe{
	HANDLE hread,hwrite;
public:
	bool Create();
	void Close(bool read=true,bool write=true);
	bool Write(const void *buf,size_t len);
	bool Read(void *buf,size_t len,unsigned &read);
};

class thread{
	unsigned int pid;
	void* handle;
public:
	thread(std::function<void(void)> &func);
	void join();
};
