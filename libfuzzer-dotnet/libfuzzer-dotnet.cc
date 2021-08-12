/* 
This source code was developed at and released by ManTech (www.mantech.com) 
for use under the following license:

Copyright (c) 2021 Nathaniel Bennett <nathaniel.bennett@mantech.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/


#pragma comment(lib, "bcrypt.lib")

#include <limits>
#include <mutex>
#include <string>

#define NOMINMAX
#define WIN32_NO_STATUS // We need these or ntstatus.h makes a fuss
#include <windows.h>
#undef WIN32_NO_STATUS

#include <bcrypt.h>
#include <ntstatus.h>


#define MAP_SIZE (1 << 16)
#define DATA_SIZE (1 << 24)
#define LEN_FLD_SIZE 4

#define SHM_ENV_LABEL "__LIBFUZZER_SHM_ID"
#define ST_ENV_LABEL "__LIBFUZZER_ST_PIPE"
#define CTL_ENV_LABEL "__LIBFUZZER_CTL_PIPE"

__attribute__((weak, section("__libfuzzer_extra_counters")))
uint8_t extra_counters[MAP_SIZE];

static const char *target_path_name = "--target_path";
static const char *target_arg_name = "--target_arg";

static std::string target_path;
static std::string target_arg;

static HANDLE ctlPipe = INVALID_HANDLE_VALUE;
static HANDLE stPipe = INVALID_HANDLE_VALUE;

static uint8_t *trace_bits = NULL;
static HANDLE hMapFile = INVALID_HANDLE_VALUE;


static unsigned long envId = 0;
static std::mutex envIdMutex;


static void die(const char *msg) {
	printf("%s\n", msg);
	exit(1);
}

static void die_sys(const char *msg) {
	printf("%s: %lu\n", msg, GetLastError());
	exit(1);
}

static void remove_shm() {
	UnmapViewOfFile(trace_bits);
	CloseHandle(hMapFile);
}

static void close_pipes() {
	CloseHandle(ctlPipe);
	CloseHandle(stPipe);
}

// Read the flag value from the single command line parameter. For example,
// read_flag_value("--target_path=binary", "--target-path") will return "binary".
static std::string read_flag_value(const char *param, const char *name) {

	size_t len = strlen(name);

	if (strstr(param, name) == param && param[len] == '=' && param[len + 1]) {
		return std::string(&param[len + 1]);
	}

	return std::string();
}

// Read target_path (the path to .NET executable) and target_arg (optional command
// line argument that can be passed to .NET executable) from the command line parameters.
static void parse_flags(int argc, char **argv) {

	for (int i = 1; i < argc; ++i) {
		char *param = argv[i];

		if (target_path.empty()) {
			target_path = read_flag_value(param, target_path_name);
		}

		if (target_arg.empty()) {
			target_arg = read_flag_value(param, target_arg_name);
		}
	}
}

static unsigned long generateRandNum() {

	unsigned char buf[sizeof(unsigned long)] = {0};
	unsigned long randNum;

	BCRYPT_ALG_HANDLE hAlgorithm;

	NTSTATUS status = BCryptOpenAlgorithmProvider(
							&hAlgorithm,
							BCRYPT_RNG_ALGORITHM,
							NULL,
							0);
	if (status != STATUS_SUCCESS) {
		printf("Failed to instantiate random number provider.\n");
		exit(1);
	}

	status = BCryptGenRandom(
					hAlgorithm,
					buf,
					sizeof(unsigned long),
					0);

	if (status != STATUS_SUCCESS) {
		printf("Failed to generate random number.\n");
		exit(1);
	}

	BCryptCloseAlgorithmProvider(hAlgorithm, 0);

	memcpy(&randNum, buf, sizeof(unsigned long));
	return randNum;
}

// Start the .NET child process and initialize two pipes and one shared
// memory segment for the communication between the parent and the child.
extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {

	unsigned long localEnvId;

	envIdMutex.lock(); // Important to have if multiple jobs are being run in this process

	if (envId == 0) {
		envId = generateRandNum(); // Different instances need to used different pipe names to work
	}

	localEnvId = envId;
	envId = (envId == std::numeric_limits<unsigned long>::max()) ? 1 : (envId + 1); // Increment, wrapping safely if necessary

	envIdMutex.unlock();

	parse_flags(*argc, *argv);

	if (target_path.empty()) {
		die("You must specify the target path by using the --target_path command line flag.");
	}

	SECURITY_ATTRIBUTES securityAttrs = {
		sizeof(SECURITY_ATTRIBUTES),
		NULL,
		TRUE,
	};
	
	std::string randomPipeId = std::to_string(localEnvId);

	std::string CTL_PIPE_ID = "__LIBFUZZER_CTL_PIPE_" + randomPipeId;
	std::string CTL_PIPE_PATH = "\\\\.\\pipe\\" + CTL_PIPE_ID;
	std::string ST_PIPE_ID = "__LIBFUZZER_ST_PIPE_" + randomPipeId;
	std::string ST_PIPE_PATH = "\\\\.\\pipe\\" + ST_PIPE_ID;
	std::string SHM_ID = "__LIBFUZZER_SHM_" + randomPipeId;

	ctlPipe = CreateNamedPipe(
					CTL_PIPE_PATH.c_str(),
					PIPE_ACCESS_OUTBOUND,
					PIPE_TYPE_BYTE | PIPE_WAIT,
					1,
					65536,
					65536,
					0,
					&securityAttrs);

	if (ctlPipe == INVALID_HANDLE_VALUE) {
		die_sys("Could not create ctl pipe");
	}

	stPipe = CreateNamedPipe(
				ST_PIPE_PATH.c_str(),
				PIPE_ACCESS_INBOUND,
				PIPE_TYPE_BYTE | PIPE_WAIT,
				1,
				65536,
				65536,
				0,
				&securityAttrs);
	
	if (stPipe == INVALID_HANDLE_VALUE) {
		CloseHandle(ctlPipe);
		die_sys("Could not create st pipe");
	}

	atexit(close_pipes);


	BOOL result;

	result = SetEnvironmentVariable((LPCTSTR) CTL_ENV_LABEL, (LPCTSTR) CTL_PIPE_ID.c_str());
	if (result == FALSE) {
		die_sys("Could not set CTL pipe env variable");
	}

	result = SetEnvironmentVariable((LPCTSTR) ST_ENV_LABEL, (LPCTSTR) ST_PIPE_ID.c_str());
	if (result == FALSE) {
		die_sys("Could not set ST pipe env variable");
	}


	hMapFile = CreateFileMapping(
					INVALID_HANDLE_VALUE,		// use paging file
					&securityAttrs,				// default security
					PAGE_READWRITE,				// r/w access
					0,							// max object size (high-order)
					MAP_SIZE + DATA_SIZE,		// max object size (low-order)
					(LPCTSTR) SHM_ID.c_str());	// name of mapping obj

	if (hMapFile == INVALID_HANDLE_VALUE) {
		die_sys("Could not create file mapping object");
	}

	trace_bits = (uint8_t*) MapViewOfFile(hMapFile,
								FILE_MAP_ALL_ACCESS,
								0,
								0,
								MAP_SIZE + DATA_SIZE);

	if (trace_bits == NULL) {
		CloseHandle(hMapFile);
		die_sys("Could not map view of file");
	}

	atexit(remove_shm);


	result = SetEnvironmentVariable((LPCTSTR) SHM_ENV_LABEL, (LPCTSTR) SHM_ID.c_str());
	if (result == FALSE) {
		die_sys("Could not set Shared Memory environment variable.");
	}


	STARTUPINFO startupInfo = {0};
	PROCESS_INFORMATION processInfo = {0};

	BOOL processResult = CreateProcess(
							target_path.c_str(),
							(target_arg.empty() ? NULL : (LPSTR) target_arg.c_str()),
							NULL,
							NULL,
							TRUE,
							0,
							NULL,
							NULL,
							&startupInfo,
							&processInfo);

	if (processResult == FALSE) {
		die_sys("Failed to instantiate C# process.");
	}

	CloseHandle(processInfo.hProcess); // TODO: use this to check for child process death instead
	CloseHandle(processInfo.hThread);

	int32_t status;
	DWORD totalBytesRead = 0;
	DWORD bytesRead = 0;

	BOOL connected = ConnectNamedPipe(stPipe, NULL);
	if (connected == FALSE && (GetLastError() != ERROR_PIPE_CONNECTED)) {
		die_sys("ConnectNamedPipe() failed for st");
	}

	connected = ConnectNamedPipe(ctlPipe, NULL);
	if (connected == FALSE && (GetLastError() != ERROR_PIPE_CONNECTED)) {
		die_sys("ConnectNamedPipe() failed for ctl");
	}

	BOOL fileWasRead = ReadFile(stPipe, (LPVOID) &status, LEN_FLD_SIZE - totalBytesRead, &bytesRead, NULL);
	if (fileWasRead == FALSE) {
		die_sys("ReadFile() failed");
	}

	totalBytesRead += bytesRead;

	if (totalBytesRead != LEN_FLD_SIZE) {
		die_sys("Short read: expected 4 bytes but got less than that during startup");
	}

	return 0;
}

// Fuzz the data by writing it to the shared memory segment, sending
// the size of the data to the .NET process (which will then run
// its own fuzzing function on the shared memory data), and receiving
// the status of the executed operation.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {

	if (size > DATA_SIZE) {
		die("Size of the input data must not exceed 1 MiB.");
	}

	memset(trace_bits, 0, MAP_SIZE);
	memcpy(trace_bits + MAP_SIZE, data, size);

	DWORD bytesWritten = 0;
	BOOL writeResult = WriteFile(ctlPipe, &size, LEN_FLD_SIZE, &bytesWritten, NULL);
	if (writeResult == FALSE) {
		die_sys("WriteFile() failed for ctl pipe");
	}

	if (bytesWritten != LEN_FLD_SIZE) {
		die("short write: expected 4 bytes, got less than that for ctl pipe");
	}

	int32_t status;
	DWORD bytesRead = 0;

	BOOL readResult = ReadFile(stPipe, &status, LEN_FLD_SIZE, &bytesRead, NULL);
	if (readResult == FALSE) {
		die_sys("ReadFile() failed for st pipe");
	}

	if (bytesRead == 0) {
		die("The child process terminated unexpectedly.");
	}

	if (bytesRead != LEN_FLD_SIZE) {
		die("short read: expected 4 bytes, got less");
	}

	memcpy(extra_counters, trace_bits, MAP_SIZE);

	if (status) {
		__builtin_trap();
	}

	return 0;
}
