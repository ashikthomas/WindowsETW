#pragma once

#include <Windows.h>

void controller_start (LPCWSTR szLogfilePath, LPCWSTR szSessionName);
void controller_stop (LPCWSTR szSessionName);