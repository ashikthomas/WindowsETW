#pragma once

#include <Windows.h>
#include <evntrace.h>
#include <comutil.h>
#include <wbemidl.h>
#include <fstream>
#include <iostream>
#include <string>

class TraceConsumer
{
public:
    TraceConsumer ();
    bool openTrace (LPWSTR szLogfile);
    bool startProcess ();
    void printInfo ();

private:
    TRACEHANDLE m_hTrace;
    EVENT_TRACE_LOGFILE m_gTrace;
    PTRACE_LOGFILE_HEADER m_pTraceHeader;
    static IWbemServices *m_pServices;

private:
    static HRESULT connectToETWNamespace (BSTR bstrNamespace);
    static IWbemClassObject* GetEventCategoryClass (BSTR bstrClassGuid, int Version);
    static IWbemClassObject* GetEventClass (IWbemClassObject* pEventCategoryClass, int EventType);

protected:
    static void WINAPI ProcessEvent (PEVENT_TRACE);
    static void WINAPI ProcessEventClass (PEVENT_TRACE);
    static void WINAPI ProcessRecord (PEVENT_RECORD);
    static void WINAPI ProcessBuffer (PEVENT_TRACE_LOGFILE);
};

extern std::wfstream logfile;
extern char write_to_file;
