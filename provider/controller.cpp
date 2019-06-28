#include "controller.h"
#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <strsafe.h>
#include <wmistr.h>
#include <evntrace.h>

TRACEHANDLE hTraceSession = 0;
EVENT_TRACE_PROPERTIES *pSessionProperties = NULL;

const GUID SessionGuid = {0xae44cb98, 0xbd11, 0x4069, {0x80, 0x93, 0x77, 0xe, 0xc9, 0x25, 0x8a, 0x14 }};
const GUID ProviderGuid = {0x3970f9cf, 0x2c0c, 0x4f11, 0xb1, 0xcc, 0xe3, 0xa1, 0xe9, 0x95, 0x88, 0x33};

extern TRACEHANDLE hTraceSession;

void controller_start (LPCWSTR szLogfilePath, LPCWSTR szSessionName)
{
    auto logfilePathSize = (lstrlen (szLogfilePath) + 1) * sizeof *szLogfilePath;
    auto sessionNameSize = (lstrlen (szSessionName) + 1) * sizeof *szSessionName;
    auto sessionPropSize = sizeof EVENT_TRACE_PROPERTIES + logfilePathSize + sessionNameSize;

    // Set up EVENT_TRACE_PROPERTIES
    pSessionProperties = reinterpret_cast <PEVENT_TRACE_PROPERTIES> (malloc (sessionPropSize));
    ZeroMemory (pSessionProperties, sessionPropSize);
    pSessionProperties->Wnode.BufferSize = sessionPropSize;
    pSessionProperties->Wnode.Guid = SessionGuid;
    pSessionProperties->Wnode.ClientContext = 1;
    pSessionProperties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    pSessionProperties->BufferSize = 0;
    pSessionProperties->MinimumBuffers = 0;
    pSessionProperties->MaximumBuffers = 0;
    pSessionProperties->MaximumFileSize = 500;                                    // 1 MB
    pSessionProperties->LogFileMode = EVENT_TRACE_FILE_MODE_SEQUENTIAL;
    pSessionProperties->LoggerNameOffset = sizeof EVENT_TRACE_PROPERTIES;
    pSessionProperties->LogFileNameOffset = sizeof  EVENT_TRACE_PROPERTIES + sessionNameSize;
    StringCbCopy ((LPWSTR) ((char*) pSessionProperties + pSessionProperties->LogFileNameOffset), logfilePathSize, szLogfilePath);


    // create trace session
    auto ret = StartTrace (
        reinterpret_cast <PTRACEHANDLE> (&hTraceSession),
        szSessionName,
        pSessionProperties);
    switch (ret)
    {
    case ERROR_ALREADY_EXISTS:
        printf ("Already exists  %d\n", hTraceSession);
        goto cleanup;
    case ERROR_SUCCESS:
        break;
    default:
        wprintf (L"StartTrace() failed with %lu\n", ret);
        goto cleanup;
    }


    // Enable provider
    ret = EnableTraceEx2 (
        hTraceSession,
        (LPCGUID) &ProviderGuid,
        EVENT_CONTROL_CODE_ENABLE_PROVIDER,
        TRACE_LEVEL_INFORMATION,
        0,
        0,
        0,
        NULL
    );
    if (ERROR_SUCCESS != ret)
    {
        wprintf (L"EnableTrace() failed with %lu\n", ret);
        //        TraceOn = FALSE;
        goto cleanup;
    }

    wprintf (L"Controller started trace\n");

cleanup:
    return;
}

void controller_stop (LPCWSTR szSessionName)
{
    if (hTraceSession)
    {
        auto ret = EnableTraceEx2 (
            hTraceSession,
            (LPCGUID) &ProviderGuid,
            EVENT_CONTROL_CODE_DISABLE_PROVIDER,
            TRACE_LEVEL_INFORMATION,
            0,
            0,
            0,
            NULL
        );

        ret = ControlTrace (hTraceSession, szSessionName, pSessionProperties, EVENT_TRACE_CONTROL_STOP);
        switch (ret)
        {
        case ERROR_SUCCESS:
            break;
        default:
            wprintf (L"ControlTrace(stop) failed with %lu\n", ret);
            break;
        }
    }

    if (pSessionProperties)
    {
        free (pSessionProperties);
        pSessionProperties = NULL;
    }
}