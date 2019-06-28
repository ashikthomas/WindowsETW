#include "provider1.h"
#include "provider2.h"
#include "controller.h"
#include <stdio.h>
#include <iostream>

int main (int argc, char *argv [])
{
    if (!lstrcmpA (argv [1], "controller"))
    {
        if (argc != 2)
        {
            printf ("provider>\n");
            return 0;
        }
        controller_start (L"log.etf", L"VERALogSession");
        printf ("Press any key to stop trace\n");
        std::cin.get ();
        controller_stop (L"VERALogSession");
        return 0;
    }
    
    if (!lstrcmpA (argv [1], "provider"))
    {
        if (argc != 4)
        {
            printf ("provider <log> <lines=0>\n");
            return 0;
        }
        provider2_init ();
        provider2_test2 (argv [2], atol (argv [3]));
        return 0;
    }
    return 0;
}
