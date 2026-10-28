#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "VoicemeeterRemote.h"

#define VM_UNINSTALL_KEY "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VB:Voicemeeter {17359A74-1236-5467}"
#define PATH_CAPACITY 1024

typedef long(__stdcall *VmLoginFn)(void);
typedef long(__stdcall *VmLogoutFn)(void);
typedef long(__stdcall *VmSetParameterFloatFn)(char *parameter_name, float value);

enum ExitCode {
	EXIT_CODE_OK = 0,
	EXIT_CODE_NOT_INSTALLED = 2,
	EXIT_CODE_LOAD_FAILED = 3,
	EXIT_CODE_EXPORT_MISSING = 4,
	EXIT_CODE_LOGIN_FAILED = 5,
	EXIT_CODE_NOT_RUNNING = 6,
	EXIT_CODE_RESTART_FAILED = 7
};

static LONG open_voicemeeter_key(HKEY *key)
{
	LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, VM_UNINSTALL_KEY, 0, KEY_READ, key);

	if (result != ERROR_SUCCESS) {
		result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, VM_UNINSTALL_KEY, 0,
			KEY_READ | KEY_WOW64_32KEY, key);
	}

	return result;
}

static char *find_executable_end(char *path)
{
	for (char *cursor = path; *cursor != '\0'; cursor++) {
		if (cursor[0] == '.' &&
			tolower((unsigned char)cursor[1]) == 'e' &&
			tolower((unsigned char)cursor[2]) == 'x' &&
			tolower((unsigned char)cursor[3]) == 'e') {
			return cursor + 4;
		}
	}

	return NULL;
}

static int get_voicemeeter_directory(char *directory, DWORD capacity)
{
	char uninstall_command[PATH_CAPACITY];
	DWORD command_size = sizeof(uninstall_command);
	DWORD value_type = 0;
	HKEY key;
	LONG result = open_voicemeeter_key(&key);

	if (result != ERROR_SUCCESS) {
		fprintf(stderr, "Voicemeeter installation was not found in the registry (%ld).\n", result);
		return 0;
	}

	result = RegQueryValueExA(key, "UninstallString", NULL, &value_type,
		(BYTE *)uninstall_command, &command_size);
	RegCloseKey(key);

	if (result != ERROR_SUCCESS || value_type != REG_SZ || command_size == 0) {
		fprintf(stderr, "Could not read the Voicemeeter installation path (%ld).\n", result);
		return 0;
	}

	uninstall_command[sizeof(uninstall_command) - 1] = '\0';

	char *executable_path = uninstall_command;
	while (*executable_path == ' ' || *executable_path == '\t') {
		executable_path++;
	}

	if (*executable_path == '"') {
		executable_path++;
		char *closing_quote = strchr(executable_path, '"');
		if (closing_quote == NULL) {
			fprintf(stderr, "The Voicemeeter uninstall path is malformed.\n");
			return 0;
		}
		*closing_quote = '\0';
	} else {
		char *executable_end = find_executable_end(executable_path);
		if (executable_end == NULL) {
			fprintf(stderr, "The Voicemeeter uninstall path is malformed.\n");
			return 0;
		}
		*executable_end = '\0';
	}

	char *last_separator = strrchr(executable_path, '\\');
	if (last_separator == NULL) {
		fprintf(stderr, "The Voicemeeter installation path is malformed.\n");
		return 0;
	}
	*last_separator = '\0';

	if (strlen(executable_path) + 1 > capacity) {
		fprintf(stderr, "The Voicemeeter installation path is too long.\n");
		return 0;
	}

	strcpy_s(directory, capacity, executable_path);
	return 1;
}

static int restart_audio_engine(void)
{
	char directory[PATH_CAPACITY];
	char dll_path[PATH_CAPACITY];
	const char *dll_name = sizeof(void *) == 8
		? "\\VoicemeeterRemote64.dll"
		: "\\VoicemeeterRemote.dll";
	HMODULE module = NULL;
	VmLoginFn login = NULL;
	VmLogoutFn logout = NULL;
	VmSetParameterFloatFn set_parameter_float = NULL;
	int exit_code = EXIT_CODE_OK;
	int logged_in = 0;

	if (!get_voicemeeter_directory(directory, sizeof(directory))) {
		return EXIT_CODE_NOT_INSTALLED;
	}

	if (strcpy_s(dll_path, sizeof(dll_path), directory) != 0 ||
		strcat_s(dll_path, sizeof(dll_path), dll_name) != 0) {
		fprintf(stderr, "The Voicemeeter DLL path is too long.\n");
		return EXIT_CODE_LOAD_FAILED;
	}

	module = LoadLibraryA(dll_path);
	if (module == NULL) {
		fprintf(stderr, "Could not load %s (Windows error %lu).\n", dll_path, GetLastError());
		return EXIT_CODE_LOAD_FAILED;
	}

	login = (VmLoginFn)GetProcAddress(module, "VBVMR_Login");
	logout = (VmLogoutFn)GetProcAddress(module, "VBVMR_Logout");
	set_parameter_float = (VmSetParameterFloatFn)GetProcAddress(module, "VBVMR_SetParameterFloat");
	if (login == NULL || logout == NULL || set_parameter_float == NULL) {
		fprintf(stderr, "The Voicemeeter DLL is missing a required API function.\n");
		exit_code = EXIT_CODE_EXPORT_MISSING;
		goto cleanup;
	}

	long result = login();
	if (result < 0) {
		fprintf(stderr, "Voicemeeter API login failed (%ld).\n", result);
		exit_code = EXIT_CODE_LOGIN_FAILED;
		goto cleanup;
	}
	logged_in = 1;

	if (result == 1) {
		fprintf(stderr, "Voicemeeter is installed but is not running.\n");
		exit_code = EXIT_CODE_NOT_RUNNING;
		goto cleanup;
	}

	result = set_parameter_float("Command.Restart", 1.0f);
	if (result != VBVMR_RESULT_OK) {
		fprintf(stderr, "Voicemeeter rejected the restart command (%ld).\n", result);
		exit_code = EXIT_CODE_RESTART_FAILED;
		goto cleanup;
	}

	Sleep(1000);
	printf("Voicemeeter audio-engine restart command sent.\n");

cleanup:
	if (logged_in) {
		long logout_result = logout();
		if (logout_result != VBVMR_RESULT_OK && exit_code == EXIT_CODE_OK) {
			fprintf(stderr, "Voicemeeter API logout failed (%ld).\n", logout_result);
			exit_code = EXIT_CODE_RESTART_FAILED;
		}
	}
	FreeLibrary(module);
	return exit_code;
}

int main(void)
{
	return restart_audio_engine();
}
