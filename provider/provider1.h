#pragma once

#include <windows.h>
#include <TraceLoggingProvider.h>
#include <stdio.h>

TRACELOGGING_DECLARE_PROVIDER (hProvider1);

static void provider1_init ()
{
    auto hr = TraceLoggingRegister (hProvider1);
    if (!SUCCEEDED (hr))
        wprintf (L"TraceLoggingRegister failed with %d\n", GetLastError ());
}

static void provider1_release ()
{
    TraceLoggingUnregister (hProvider1);
}

static void provider1_test ()
{
    TraceLoggingWrite (
        hProvider1,
        "Event 1",
        TraceLoggingValue ("Field Value 1", "Field Name 1"));
    TraceLoggingWrite (
        hProvider1,
        "Event 1",
        TraceLoggingValue ("Field Value 2", "Field Name 2"));
    TraceLoggingWrite (
        hProvider1,
        "Event 3",
        TraceLoggingValue ("Field Value 3", "Field Name 3"));
}