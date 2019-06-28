#pragma once

#include <Windows.h>
#include <evntprov.h>
#include <stdio.h>
#include <fstream>
#include <string>
#include <vector>

extern const GUID ProviderGuid;
extern REGHANDLE ProviderRegHandle;

static void provider2_init ()
{
    auto ret = EventRegister (
        &ProviderGuid,
        NULL,
        NULL,
        &ProviderRegHandle
    );
    if (ret != ERROR_SUCCESS)
        wprintf (L"EventRegister failed with %d, ret %d\n", GetLastError (), ret);
}

static void provider2_test ()
{
    EventWriteString (
        ProviderRegHandle,
        0,
        0,
        L"Hello World"
    );
    EventWriteString (
        ProviderRegHandle,
        0,
        0,
        L"Event 2"
    );
    EventWriteString (
        ProviderRegHandle,
        0,
        0,
        L"Event 3"
    );
}

static void provider2_test2 (const char *path, long count)
{
    unsigned long total = 0;
    std::wifstream file (path);
    std::vector <std::wstring> lines;
    if (!file.is_open ())
    {
        printf ("Could not open %s\n", path);
        return;
    }
    while (!file.eof ())
    {
        std::wstring line;
        std::getline (file, line);
        lines.push_back (std::move (line));
    }
    printf ("%u lines read to memory\n", lines.size ());
    
    auto t1 = GetTickCount64 ();
    for (size_t ii = 0, jj = 0; jj < count; jj++)
    {
        EventWriteString (ProviderRegHandle, 0, 0, lines [ii].c_str ());
        if (++ii == lines.size ())
            ii = 0;
        if (jj % 10000 == 0)
            printf ("%u lines...\n", jj + 1);
    }
    auto t2 = GetTickCount64 ();
    printf ("%u lines in %u s\n", count, (t2-t1)/1000);
    
}