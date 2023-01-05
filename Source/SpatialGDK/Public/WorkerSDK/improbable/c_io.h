#pragma once
#include <stddef.h>
#include <stdint.h>
#include "WorkerSDK/improbable/pch.h"
typedef struct Io_Stream
{
    	
} Io_Stream;

typedef struct Io_RotatingFileStreamParameters
{
	const char* filename_prefix;
	const char* filename_suffix;
	int64_t max_file_size_bytes;
	int32_t max_file_count;
	
}Io_RotatingFileStreamParameters;

enum Io_OpenMode : unsigned short {
	IO_OPEN_MODE_READ = 1,
	IO_OPEN_MODE_WRITE = 1 << 1,
	IO_OPEN_MODE_RW = 1 << 2,
	IO_OPEN_MODE_CREATE = 1 << 3,
	IO_OPEN_MODE_CREATE_AND_FAIL_IF_EXISTS = 1 << 4,
	/**
	 * Will set the length of a file to 0
	 * Only works if file is open with READ and WRITE mode
	 */
	IO_OPEN_MODE_SET_LENGTH_0 = 1 << 5,
};


WORKERSDK_API void Io_Stream_Destroy(Io_Stream* StreamToDestroy);

WORKERSDK_API int Io_Stream_Flush(Io_Stream *Stream);

WORKERSDK_API char* Io_Stream_GetLastError(Io_Stream *Stream);

WORKERSDK_API Io_Stream* Io_CreateRotatingFileStream(Io_RotatingFileStreamParameters *Param);

WORKERSDK_API Io_Stream* Io_CreateFileStream(char* fullname,Io_OpenMode mode);