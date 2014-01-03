#pragma once

#include "stdint.h"
#include <fstream>
#include "Platform.h"

extern int64_t size_uncompressed;
extern int64_t size_compressed;

void DecodeToPipe(std::ifstream &difffile,Pipe &pipe);
void CompressInc(Pipe& pip,std::ostream *outfile);