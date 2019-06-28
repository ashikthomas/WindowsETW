#include "consumer.h"
#include <stdio.h>

#pragma comment (lib, "advapi32.lib")
#pragma comment (lib, "comsupp.lib")
#pragma comment (lib, "ws2_32.lib")
#pragma comment (lib, "tdh.lib")

extern unsigned long cnt;

int wmain (int argc, wchar_t *argv[])
{
    if (argc != 2 && argc != 3)
    {
        printf ("consumer <etl> [outfile]\n");
        return 0;
    }

    if (argv [2])
    {
        if (!lstrcmpW (argv [2], L"stdout"))
        {
            write_to_file = 2;
        }
        else
        {
            logfile.open (argv [2], std::ios::out);
            if (logfile.is_open () == false)
            {
                wprintf (L"Could not open %s for writing\n", argv [2]);
                return 0;
            }
            write_to_file = 1;
        }
    }
    
    TraceConsumer c;
    c.openTrace (argv [1]);
    c.printInfo ();
    auto t1 = GetTickCount64 ();
    c.startProcess ();
    if (logfile.is_open ()) logfile.close ();
    auto t2 = GetTickCount64 ();

    printf ("%u records decoded in %u s\n", cnt, (t2 - t1) / 1000);
    return 0;
}