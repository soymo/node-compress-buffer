/* node-compress-buffer (C) 2011 Egor Egorov <me@egorfine.com>  */

#include <node.h>
#include <node_events.h>
#include <node_buffer.h>
#include <string.h>
#include <v8.h>
#include <math.h>
#include <stdlib.h>
#include <assert.h>
#include "zlib.h"
#ifdef __APPLE__
#include <malloc/malloc.h>
#endif

// we don't compress less than that. Actually, zlib refuses to compress less than 18 bytes, but 32 looks
// like a better number :) 
#define MIN_BYTES 32 


int compressMemory(char *bufferIn, char *bufferOut, size_t bytesIn, size_t *bytesOut, int compressionLevel) {
	int err;
	
	z_stream strm;
	strm.zalloc=0;
	strm.zfree=0;
	strm.next_in=(Bytef*) bufferIn;
	strm.avail_in=bytesIn;
	strm.next_out=(Bytef*) bufferOut;
	strm.avail_out=*bytesOut;
	
	if ((uLong)strm.avail_out != *bytesOut) {
		return Z_BUF_ERROR;
	}

	err=deflateInit2(&strm, compressionLevel, Z_DEFLATED,  16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
	if (err != Z_OK) {
		return err;
	}

	err=deflate(&strm, Z_FINISH);
	if (err != Z_STREAM_END) {
		deflateEnd(&strm);
		return err == Z_OK ? Z_BUF_ERROR : err;
	}
	*bytesOut=strm.total_out;

	return deflateEnd(&strm);
}

// FIXME make uncompress cyclic so it eats not so much memory
int uncompressMemory(char *source, char *dest, size_t sourceLen, size_t *bytesUncompressed) {
	int err;

	z_stream strm;
	strm.zalloc=0;
	strm.zfree=0;
	strm.next_in=(Bytef*) source;
	strm.avail_in=sourceLen;
	strm.next_out=(Bytef*) dest;
	strm.avail_out=*bytesUncompressed;
	
	if ((uLong)strm.avail_out != *bytesUncompressed) {
		return Z_BUF_ERROR;
	}

	err=inflateInit2(&strm, 16 + MAX_WBITS);
	
	if (err != Z_OK) { 
		return err;
	}

	err=inflate(&strm, Z_FINISH);
	if (err != Z_STREAM_END) {
		inflateEnd(&strm);
		if (err == Z_NEED_DICT || (err == Z_BUF_ERROR && strm.avail_in == 0)) { 
			return Z_DATA_ERROR;
		}
		return err;
	}
	*bytesUncompressed=strm.total_out;

	return inflateEnd(&strm);
}


using namespace v8;
using namespace node;


Handle<Value> compress(const Arguments& args) {
	HandleScope scope;
	size_t bytesIn=0;
	size_t bytesCompressed=0;
	char *dataPointer=NULL;
	int shouldFreeDataPointer=0;
	
	if (args.Length() < 1) { 
		return Undefined();
	}
	
	if (args[0]->IsString()) {
		String::AsciiValue string(args[0]->ToString());
		bytesIn = strlen(*string);
		
		if (bytesIn<=MIN_BYTES) {
			return scope.Close(args[0]);
		}
		
		// FIXME why do I have to copy data, why just this doesn't work: dataPointer = (char *) *string ? 
		dataPointer = (char*)malloc(bytesIn);
		strncpy(dataPointer, *string, bytesIn);
		shouldFreeDataPointer=1;
		
	} else if (Buffer::HasInstance(args[0])) {
		Local<Object> bufferIn=args[0]->ToObject();
		bytesIn=Buffer::Length(bufferIn);
		
		if (bytesIn<=MIN_BYTES) {
			return scope.Close(args[0]);
		}
		
		dataPointer=Buffer::Data(bufferIn);
	}
	
	int compressionLevel = 5;
	if (args.Length() > 1) { 
		compressionLevel = args[1]->IntegerValue();
		if (compressionLevel <= 0 || compressionLevel > 9) {
			compressionLevel = 5;
		}
	}
	
	bytesCompressed=compressBound(bytesIn);
	char *bufferOut=(char*) malloc(bytesCompressed);

	int result = compressMemory(dataPointer, bufferOut, bytesIn, &bytesCompressed, compressionLevel);
	if (result!=Z_OK) {
		if (shouldFreeDataPointer) { 
			free(dataPointer);
		}
		free(bufferOut);
		return Undefined();
	}

	Buffer *BufferOut=Buffer::New(bufferOut, bytesCompressed);
	free(bufferOut);
	
	if (shouldFreeDataPointer) { 
		free(dataPointer);
	}

	return scope.Close(BufferOut->handle_);
}
	

Handle<Value> uncompress(const Arguments &args) {
	if (args.Length() < 1 || !Buffer::HasInstance(args[0])) {
		return Undefined();
	}
	
	Local<Object> bufferIn=args[0]->ToObject();

	size_t bytesUncompressed=999*1024*1024; // it's about max size that V8 supports
	char *bufferOut=(char*) malloc(bytesUncompressed);

	int result=uncompressMemory(Buffer::Data(bufferIn), bufferOut, Buffer::Length(bufferIn), &bytesUncompressed);
	if (result!=Z_OK) {
		free(bufferOut);
		return Undefined();
	}

	Buffer *BufferOut=Buffer::New(bufferOut, bytesUncompressed);
	free(bufferOut);

	HandleScope scope;
	return scope.Close(BufferOut->handle_);
}


extern "C" void
init (Handle<Object> target) {
	NODE_SET_METHOD(target, "compress", compress);
	NODE_SET_METHOD(target, "uncompress", uncompress);
}