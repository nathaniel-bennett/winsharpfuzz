//#include <windows.h>
//#include <stdio.h>
//#include <conio.h>
//#include <tchar.h>
//#include <fileapi.h>

#pragma comment(lib, "bcrypt.lib")

#define NOMINMAX
#define WIN32_NO_STATUS // We need these or ntstatus.h makes a fuss...
#include <windows.h>
#undef WIN32_NO_STATUS
#include <bcrypt.h>
#include <ntstatus.h>


#include <string>
#include <ctime>
#include <mutex>
#include <limits>

#define MAP_SIZE (1 << 16)
#define DATA_SIZE (1 << 24)

#define LEN_FLD_SIZE 4

#define SHM_ID_VAR "__LIBFUZZER_SHM_ID"

__attribute__((weak, section("__libfuzzer_extra_counters")))
uint8_t extra_counters[MAP_SIZE];

static const char *target_path_name = "--target_path";
static const char *target_arg_name = "--target_arg";

static std::string target_path;
static std::string target_arg;

static HANDLE ctlPipe;
static HANDLE stPipe;

static uint8_t *trace_bits;
static HANDLE hMapFile;


static unsigned long envId = 0;
static std::mutex envIdMutex;


static void die(const char *msg)
{
	printf("%s\n", msg);
	exit(1);
}

static void die_sys(const char *msg)
{
	printf("%s: %lu\n", msg, GetLastError());
	exit(1);
}

static void remove_shm()
{
	UnmapViewOfFile(trace_bits);
	CloseHandle(hMapFile);
}

static void close_pipes() {
	CloseHandle(ctlPipe);
	CloseHandle(stPipe);
}

// Read the flag value from the single command line parameter. For example,
// read_flag_value("--target_path=binary", "--target-path") will return "binary".
static std::string read_flag_value(const char *param, const char *name)
{
	size_t len = strlen(name);

	if (strstr(param, name) == param && param[len] == '=' && param[len + 1])
	{
		return std::string(&param[len + 1]);
	}

	return std::string();
}

// Read target_path (the path to .NET executable) and target_arg (optional command
// line argument that can be passed to .NET executable) from the command line parameters.
static void parse_flags(int argc, char **argv)
{
	// printf("Entered parse_flags...\n");

	// printf("%d flag(s) detected.\n", argc-1);
	for (int i = 1; i < argc; ++i)
	{
		char *param = argv[i];

		if (target_path.empty())
		{
			// printf("target_path empty--reading flag to target_path\n");
			target_path = read_flag_value(param, target_path_name);
		}

		if (target_arg.empty())
		{
			// printf("target_arg empty--reading flag to target_arg\n");
			target_arg = read_flag_value(param, target_arg_name);
		}
	}

	// printf("Exiting parse_flags...\n");
}

static unsigned long generateRandNum()
{
	unsigned char buf[sizeof(unsigned long)] = {0};
	unsigned long randNum;

	BCRYPT_ALG_HANDLE hAlgorithm;

	NTSTATUS status = BCryptOpenAlgorithmProvider(
									&hAlgorithm,
									BCRYPT_RNG_ALGORITHM,
									nullptr,
									0);
	if (status != STATUS_SUCCESS) {
		printf("Failed to generate random number.\n");
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
extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	// printf("Entered LLVMFuzzerInitialize...\n");

	unsigned long localEnvId;

	envIdMutex.lock(); // Important to have if multiple jobs are being run in this process

	if (envId == 0) {
		envId = generateRandNum(); // Different instances need to used different pipe names to work
	}

	localEnvId = envId;
	envId = (envId == std::numeric_limits<unsigned long>::max()) ? 1 : (envId + 1); // Increment, wrapping safely if necessary

	envIdMutex.unlock();

	parse_flags(*argc, *argv);

	// printf("Parsed flags...\n");

	if (target_path.empty())
	{
		die("You must specify the target path by using the --target_path command line flag.");
	}

	// printf("Target path found.\n");

	SECURITY_ATTRIBUTES securityAttrs = {
		sizeof(SECURITY_ATTRIBUTES),
		nullptr,
		true,
	};

	
	std::string randomPipeId = std::to_string(localEnvId); // TODO: change this!

	std::string CTL_PIPE_NAME = "__LIBFUZZER_CTL_PIPE_" + randomPipeId;
	std::string ST_PIPE_NAME = "__LIBFUZZER_ST_PIPE_" + randomPipeId;
	std::string SZ_NAME = "__LIBFUZZER_MAPPING_" + randomPipeId;

	// printf("ctl pipe name (C++ side): %s\n", CTL_PIPE_NAME.c_str());
	// printf("st pipe name (C++ side): %s\n", ST_PIPE_NAME.c_str());

	ctlPipe = CreateNamedPipe(
							("\\\\.\\pipe\\" + CTL_PIPE_NAME).c_str(),
							PIPE_ACCESS_OUTBOUND,
							PIPE_TYPE_BYTE | PIPE_WAIT,
							2, // TODO: change to 1?
							65536,
							65536,
							0,
							&securityAttrs);

	if (ctlPipe == INVALID_HANDLE_VALUE) {
		die_sys("Could not create ctl pipe");
	}

	stPipe = CreateNamedPipe(
							("\\\\.\\pipe\\" + ST_PIPE_NAME).c_str(),
							PIPE_ACCESS_INBOUND,
							PIPE_TYPE_BYTE | PIPE_WAIT,
							2, // TODO: change to 1?
							65536,
							65536,
							0,
							&securityAttrs);
	
	if (stPipe == INVALID_HANDLE_VALUE) {
		die_sys("Could not create st pipe");
	}

	atexit(close_pipes);

	BOOL result;

	result = SetEnvironmentVariable((LPCTSTR) "__LIBFUZZER_CTL_PIPE", (LPCTSTR) (CTL_PIPE_NAME).c_str());
	if (result == FALSE) {
		die_sys("Could not set CTL pipe env variable");
	}

	result = SetEnvironmentVariable((LPCTSTR) "__LIBFUZZER_ST_PIPE", (LPCTSTR) (ST_PIPE_NAME).c_str());
	if (result == FALSE) {
		die_sys("Could not set ST pipe env variable");
	}

	//printf("Creating file mapping...\n");

	hMapFile = CreateFileMapping(
					INVALID_HANDLE_VALUE,	// use paging file
					&securityAttrs,			// default security
					PAGE_READWRITE,			// r/w access
					0,						// max object size (high-order)
					MAP_SIZE + DATA_SIZE,	// max object size (low-order)
					(LPCTSTR) SZ_NAME.c_str());			// name of mapping obj

	if (hMapFile == nullptr) {
		die_sys("Could not create file mapping object");
	}

	// printf("File mapping created.\n");

	trace_bits = (uint8_t*) MapViewOfFile(hMapFile,
								FILE_MAP_ALL_ACCESS,
								0,
								0,
								MAP_SIZE + DATA_SIZE);

	if (trace_bits == nullptr) {
		die_sys("Could not map view of file");
	}

	atexit(remove_shm);

	result = SetEnvironmentVariable(LPCTSTR(SHM_ID_VAR), (LPCTSTR) SZ_NAME.c_str());
	if (result == FALSE) {
		die_sys("Could not set SHM ID variable");
	}


	// printf("Creating C# process...\n");

	STARTUPINFO startupInfo = {0};
	PROCESS_INFORMATION processInfo = {0};

	BOOL processResult = CreateProcess(
							target_path.c_str(),
							(target_arg.empty() ? nullptr : (LPSTR) target_arg.c_str()),
							nullptr,
							nullptr,
							TRUE,
							0,
							nullptr, // lpEnvironment--env variables
							nullptr,
							&startupInfo, // lpStartupInfo--might need to be non-null
							&processInfo); // lpProcessInformation--might need to be non-null

	if (processResult == FALSE) {
		die_sys("CreateProcessA failed");
	}

	// printf("C# process created.\n");

	int32_t status;
	DWORD totalBytesRead = 0;
	DWORD bytesRead = 0;

	// printf("Connecting to st pipe...\n");

	BOOL connected = ConnectNamedPipe(stPipe, nullptr);
	if (connected == FALSE && (GetLastError() != ERROR_PIPE_CONNECTED)) {
		die_sys("ConnectNamedPipe() failed for st");
	}

	// printf("Connected to st pipe.\n");


	// printf("Connecting to ctl pipe...\n");

	connected = ConnectNamedPipe(ctlPipe, nullptr);
	if (connected == FALSE && (GetLastError() != ERROR_PIPE_CONNECTED)) {
		die_sys("ConnectNamedPipe() failed for ctl");
	}

	// printf("Connected to ctl pipe.\n");

	BOOL fileWasRead = ReadFile(stPipe, (LPVOID) &status, LEN_FLD_SIZE - totalBytesRead, &bytesRead, nullptr);
	if (fileWasRead == FALSE) {
		// Check GetLastError() for EINTR?
		die_sys("ReadFile() failed");
	}

	totalBytesRead += bytesRead;

	if (totalBytesRead != LEN_FLD_SIZE)
	{
		die_sys("Short read: expected 4 bytes but got less than that during startup");
	}

	// printf("Exiting LLVMFuzzerInitialize...\n");

	return 0;
}

// Fuzz the data by writing it to the shared memory segment, sending
// the size of the data to the .NET process (which will then run
// its own fuzzing function on the shared memory data), and receiving
// the status of the executed operation.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	// printf("Entering LLVMFuzzerTestOneInput...\n");

	if (size > DATA_SIZE)
	{
		die("Size of the input data must not exceed 1 MiB.");
	}

	// printf("Setting trace_bits data...\n");

	memset(trace_bits, 0, MAP_SIZE);
	memcpy(trace_bits + MAP_SIZE, data, size);

	// printf("Trace_bits data set.\n");

	// printf("Writing %d to ctl pipe...\n", (uint32_t) size);

	DWORD bytesWritten = 0;
	BOOL writeResult = WriteFile(ctlPipe, &size, LEN_FLD_SIZE, &bytesWritten, nullptr);
	if (writeResult == FALSE) {
		die_sys("WriteFile() failed for ctl pipe");
	}

	// printf("ctl pipe written to.\n");

	if (bytesWritten != LEN_FLD_SIZE) {
		die("short write: expected 4 bytes, got less than that for ctl pipe");
	}


	int32_t status;
	DWORD bytesRead = 0;

	BOOL readResult = ReadFile(stPipe, &status, LEN_FLD_SIZE, &bytesRead, nullptr);

	memcpy(extra_counters, trace_bits, MAP_SIZE);

	if (readResult == FALSE) {
		die_sys("ReadFile() failed for st pipe");
	}

	if (bytesRead == 0) {
		die("The child process terminated unexpectedly.");
	}

	if (bytesRead != LEN_FLD_SIZE) {
		die("short read: expected 4 bytes, got less");
	}

	if (status) {
		__builtin_trap();
	}


	// printf("Exiting LLVMFuzzerTestOneInput...\n");

	return 0;
}