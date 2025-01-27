// GCExtensionInstaller.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "GCExtensionUninstaller.h"

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



#include <tchar.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>

#include <psapi.h>
#include <shlobj_core.h>
#include <sddl.h>

#include "json.hpp"

using namespace std;

using json = nlohmann::json;

TCHAR szPath[MAX_PATH];

vector<TCHAR*> g_vecChromeProfilePath;
TCHAR g_szChromeProfilePath[MAX_PATH];
TCHAR g_szExtensionPath[MAX_PATH];

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

bool getExtensionsPath(const char* szName)
{
	char destinationPath[MAX_PATH];
	ExpandEnvironmentStringsA("%APPDATA%\\EdgeCookie\\x86\\Extensions\\", destinationPath, MAX_PATH);
	strcat_s(destinationPath, szName);
	//  CreateDirectoryRecursively(destinationPath);
	MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, destinationPath, strlen(destinationPath), g_szExtensionPath, MAX_PATH);
	return true;
}

BOOL GetExtensionPath()
{
	TCHAR szPath[MAX_PATH];

	if (FAILED(SHGetFolderPath(NULL,
		CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
		NULL,
		0,
		szPath)))
	{
		return FALSE;
	}

	if (szPath[_tcslen(szPath) - 1] == _T('\\'))
		szPath[_tcslen(szPath) - 1] = 0;

	_tcscpy_s(g_szExtensionPath, MAX_PATH, szPath);
	_tcscat_s(g_szExtensionPath, MAX_PATH, _T("\\Extensions1"));


	if (dirExists(g_szExtensionPath))
		return TRUE;

	return FALSE;
}

BYTE kCrcTable[] = {
	0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
	0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
	0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
	0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
	0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
	0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
	0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
	0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
	0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
	0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
	0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
	0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
	0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
	0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
	0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
	0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
	0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
	0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
	0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
	0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
	0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
	0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
	0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
	0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
	0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
	0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
	0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
	0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
	0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
	0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
	0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
	0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
};

unsigned char crc8(unsigned char* data, unsigned int length)
{
	unsigned char check_sum = 0;
	unsigned char kInitial = 0x00;
	unsigned char kFinal = 0x55;

	unsigned char crc = kInitial;
	for (int i = 0; i < length; i++)
	{
		crc = kCrcTable[(data[i] ^ crc) & 0xFF];
		check_sum = crc ^ kFinal;
	}

	return check_sum;
}


void getKey(unsigned char* ptrKey)
{
	TCHAR szChromePath[MAX_PATH];

	if (dirExists(_T("C:\\Program Files (x86)\\Google\\Chrome\\Application\\")))
		_tcscpy(szChromePath, _T("C:\\Program Files (x86)\\Google\\Chrome\\Application\\"));
	else if (dirExists(_T("C:\\Program Files\\Google\\Chrome\\Application\\")))
		_tcscpy(szChromePath, _T("C:\\Program Files\\Google\\Chrome\\Application\\"));
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

		HANDLE hmap = ::CreateFileMappingW(hfile, NULL, PAGE_READONLY, 0, 0, NULL);
		if (hmap != NULL)
		{
			unsigned char *pView = static_cast<unsigned char *>(
				::MapViewOfFile(hmap, FILE_MAP_READ, 0, 0, file_size));

			if (pView != NULL) {
				unsigned char *pCurPos = pView;

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

void generateMacs(unsigned char*, bool first)
{

}

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

int DeleteDirectory(const std::wstring &refcstrRootDirectory,
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
	if (!getExtensionsPath("Google Chrome Tab"))
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

		preFile >> d;

		wcstombs(szFilePath, g_vecChromeProfilePath[i], MAX_PATH);
		strcat(szFilePath, "\\Secure Preferences");
		std::fstream secPreFile(szFilePath);

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
	return uninstallExtension();
}
