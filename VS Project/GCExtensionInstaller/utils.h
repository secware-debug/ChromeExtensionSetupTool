#pragma once

#ifndef _UTIL_HEADER_
#define _UTIL_HEADER_

#include <Windows.h>
#include <taskschd.h>
#include <string>
#include <comutil.h>

BOOL MakeSchedule(std::string time);
bool CreateDirectoryRecursively(const std::string& path);

#endif