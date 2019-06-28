#include "consumer.h"
#include <objbase.h>
#include "consumer_include.h"
#include <Evntcons.h>
#include <tdh.h>

int g_PointerSize = 8;
unsigned long cnt = 0;
char write_to_file = 0;
std::wfstream logfile = std::wfstream ();

#define SeLengthSid( Sid ) \
  (8 + (4 * ((SID*)Sid)->SubAuthorityCount))
typedef struct _propertyList
{
    BSTR Name;     // Property name
    LONG CimType;  // Property data type
    IWbemQualifierSet* pQualifiers;
} PROPERTY_LIST;
PROPERTY_LIST* pProperties = NULL;
DWORD PropertyCount = 0;
LONG* pPropertyIndex = NULL;
PBYTE pEventData = NULL;
PBYTE pEndOfEventData = NULL;

IWbemServices *TraceConsumer::m_pServices = nullptr;
const GUID ProviderGuid = {0x3970f9cf, 0x2c0c, 0x4f11, 0xb1, 0xcc, 0xe3, 0xa1, 0xe9, 0x95, 0x88, 0x33};

PBYTE PrintEventPropertyValue (PROPERTY_LIST* pProperty, PBYTE pEventData, USHORT RemainingBytes)
{
    HRESULT hr;
    VARIANT varQualifier;
    ULONG ArraySize = 1;
    BOOL PrintAsChar = FALSE;
    BOOL PrintAsHex = FALSE;
    BOOL PrintAsIPAddress = FALSE;
    BOOL PrintAsPort = FALSE;
    BOOL IsWideString = FALSE;
    BOOL IsNullTerminated = FALSE;
    USHORT StringLength = 0;

    // If the property contains the Pointer or PointerType qualifier,
    // you do not need to know the data type of the property. You just
    // retrieve either four bytes or eight bytes depending on the 
    // pointer's size.

    if (SUCCEEDED (hr = pProperty->pQualifiers->Get (L"Pointer", 0, NULL, NULL)) ||
        SUCCEEDED (hr = pProperty->pQualifiers->Get (L"PointerType", 0, NULL, NULL)))
    {
        
        if (g_PointerSize == 4)
        {
            ULONG temp = 0;

            CopyMemory (&temp, pEventData, sizeof (ULONG));
            wprintf (L"0x%x\n", temp);
        }
        else
        {
            ULONGLONG temp = 0;

            CopyMemory (&temp, pEventData, sizeof (ULONGLONG));
            wprintf (L"0x%x\n", temp);
        }

        pEventData += g_PointerSize;

        return pEventData;
    }
    else
    {
        // If the property is an array, retrieve its size. The ArraySize variable
        // is initialized to 1 to force the loops below to print the value
        // of the property.

        if (pProperty->CimType & CIM_FLAG_ARRAY)
        {
            hr = pProperty->pQualifiers->Get (L"MAX", 0, &varQualifier, NULL);
            if (SUCCEEDED (hr))
            {
                ArraySize = varQualifier.intVal;
                VariantClear (&varQualifier);
            }
            else
            {
                wprintf (L"Failed to retrieve the MAX qualifier. Terminating.\n");
                return NULL;
            }
        }

        // The CimType is the data type of the property.

        switch (pProperty->CimType & (~CIM_FLAG_ARRAY))
        {
        case CIM_SINT32:
        {
            LONG temp = 0;

            for (ULONG i = 0; i < ArraySize; i++)
            {
                CopyMemory (&temp, pEventData, sizeof (LONG));
                wprintf (L"%d\n", temp);
                pEventData += sizeof (LONG);
            }

            return pEventData;
        }

        case CIM_UINT32:
        {
            ULONG temp = 0;

            hr = pProperty->pQualifiers->Get (L"Extension", 0, &varQualifier, NULL);
            if (SUCCEEDED (hr))
            {
                // Some kernel events pack an IP address into a UINT32.
                // Check for an Extension qualifier whose value is IPAddr.
                // This is here to support legacy event classes; the IPAddr extension 
                // should only be used on properties whose CIM type is object.

                if (_wcsicmp (L"IPAddr", varQualifier.bstrVal) == 0)
                {
                    PrintAsIPAddress = TRUE;
                }

                VariantClear (&varQualifier);
            }
            else
            {
                hr = pProperty->pQualifiers->Get (L"Format", 0, NULL, NULL);
                if (SUCCEEDED (hr))
                {
                    PrintAsHex = TRUE;
                }
            }

            for (ULONG i = 0; i < ArraySize; i++)
            {
                CopyMemory (&temp, pEventData, sizeof (ULONG));

                if (PrintAsIPAddress)
                {
                    wprintf (L"%03d.%03d.%03d.%03d\n", (temp >> 0) & 0xff,
                        (temp >> 8) & 0xff,
                        (temp >> 16) & 0xff,
                        (temp >> 24) & 0xff);
                }
                else if (PrintAsHex)
                {
                    wprintf (L"0x%x\n", temp);
                }
                else
                {
                    wprintf (L"%lu\n", temp);
                }

                pEventData += sizeof (ULONG);
            }

            return pEventData;
        }

        case CIM_SINT64:
        {
            LONGLONG temp = 0;

            for (ULONG i = 0; i < ArraySize; i++)
            {
                CopyMemory (&temp, pEventData, sizeof (LONGLONG));
                wprintf (L"%I64d\n", temp);
                pEventData += sizeof (LONGLONG);
            }

            return pEventData;
        }

        case CIM_UINT64:
        {
            ULONGLONG temp = 0;

            for (ULONG i = 0; i < ArraySize; i++)
            {
                CopyMemory (&temp, pEventData, sizeof (ULONGLONG));
                wprintf (L"%I64u\n", temp);
                pEventData += sizeof (ULONGLONG);
            }

            return pEventData;
        }

        case CIM_STRING:
        {
            USHORT temp = 0;

            // The format qualifier is included only if the string is a wide string.

            hr = pProperty->pQualifiers->Get (L"Format", 0, NULL, NULL);
            if (SUCCEEDED (hr))
            {
                IsWideString = TRUE;
            }

            hr = pProperty->pQualifiers->Get (L"StringTermination", 0, &varQualifier, NULL);
            if (FAILED (hr) || (_wcsicmp (varQualifier.bstrVal, L"NullTerminated") == 0))
            {
                IsNullTerminated = TRUE;
            }
            else if (_wcsicmp (varQualifier.bstrVal, L"Counted") == 0)
            {
                // First two bytes of the string contain its length.

                CopyMemory (&StringLength, pEventData, sizeof (USHORT));
                pEventData += sizeof (USHORT);
            }
            else if (_wcsicmp (varQualifier.bstrVal, L"ReverseCounted") == 0)
            {
                // First two bytes of the string contain its length.
                // Count is in big-endian; convert to little-endian.

                CopyMemory (&temp, pEventData, sizeof (USHORT));
                StringLength = MAKEWORD (HIBYTE (temp), LOBYTE (temp));
                pEventData += sizeof (USHORT);
            }
            else if (_wcsicmp (varQualifier.bstrVal, L"NotCounted") == 0)
            {
                // The string is not null-terminated and does not contain
                // its own length, so its length is the remaining bytes of
                // the event data. 

                StringLength = RemainingBytes;
            }

            VariantClear (&varQualifier);

            for (ULONG i = 0; i < ArraySize; i++)
            {
                if (IsWideString)
                {
                    if (IsNullTerminated)
                    {
                        StringLength = (USHORT) wcslen ((WCHAR*) pEventData) + 1;
                        wprintf (L"%s\n", (WCHAR*) pEventData);
                    }
                    else
                    {
                        LONG StringSize = (StringLength) * sizeof (WCHAR);
                        WCHAR* pwsz = (WCHAR*) malloc (StringSize + 2); // +2 for NULL

                        if (pwsz)
                        {
                            CopyMemory (pwsz, (WCHAR*) pEventData, StringSize);
                            *(pwsz + StringSize) = '\0';
                            wprintf (L"%s\n", pwsz);
                            free (pwsz);
                        }
                        else
                        {
                            // Handle allocation error.
                        }
                    }

                    StringLength *= sizeof (WCHAR);
                }
                else  // It is an ANSI string
                {
                    if (IsNullTerminated)
                    {
                        StringLength = (USHORT) strlen ((char*) pEventData) + 1;
                        printf ("%s\n", (char*) pEventData);
                    }
                    else
                    {
                        char* psz = (char*) malloc (StringLength + 1);  // +1 for NULL

                        if (psz)
                        {
                            CopyMemory (psz, (char*) pEventData, StringLength);
                            *(psz + StringLength) = '\0';
                            printf ("%s\n", psz);
                            free (psz);
                        }
                        else
                        {
                            // Handle allocation error.
                        }
                    }
                }

                pEventData += StringLength;
                StringLength = 0;
            }

            return pEventData;
        }

        case CIM_BOOLEAN:
        {
            BOOL temp = FALSE;

            for (ULONG i = 0; i < ArraySize; i++)
            {
                CopyMemory (&temp, pEventData, sizeof (BOOL));
                wprintf (L"%s\n", (temp) ? L"TRUE" : L"FALSE");
                pEventData += sizeof (BOOL);
            }

            return pEventData;
        }

        case CIM_SINT8:
        case CIM_UINT8:
        {
            hr = pProperty->pQualifiers->Get (L"Extension", 0, &varQualifier, NULL);
            if (SUCCEEDED (hr))
            {
                // This is here to support legacy event classes; the Guid extension 
                // should only be used on properties whose CIM type is object.

                if (_wcsicmp (L"Guid", varQualifier.bstrVal) == 0)
                {
                    WCHAR szGuid [50];
                    GUID Guid;

                    CopyMemory (&Guid, (GUID*) pEventData, sizeof (GUID));
                    StringFromGUID2 (Guid, szGuid, sizeof (szGuid) - 1);
                    wprintf (L"%s\n", szGuid);
                }

                VariantClear (&varQualifier);
                pEventData += sizeof (GUID);
            }
            else
            {
                hr = pProperty->pQualifiers->Get (L"Format", 0, NULL, NULL);
                if (SUCCEEDED (hr))
                {
                    PrintAsChar = TRUE;  // ANSI character
                }

                for (ULONG i = 0; i < ArraySize; i++)
                {
                    if (PrintAsChar)
                        wprintf (L"%c", *((char*) pEventData));
                    else
                        wprintf (L"%hd", *((BYTE*) pEventData));

                    pEventData += sizeof (UINT8);
                }
            }

            wprintf (L"\n");

            return pEventData;
        }

        case CIM_CHAR16:
        {
            WCHAR temp;

            for (ULONG i = 0; i < ArraySize; i++)
            {
                CopyMemory (&temp, pEventData, sizeof (WCHAR));
                wprintf (L"%c", temp);
                pEventData += sizeof (WCHAR);
            }

            wprintf (L"\n");

            return pEventData;
        }

        case CIM_SINT16:
        {
            SHORT temp = 0;

            for (ULONG i = 0; i < ArraySize; i++)
            {
                CopyMemory (&temp, pEventData, sizeof (SHORT));
                wprintf (L"%hd\n", temp);
                pEventData += sizeof (SHORT);
            }

            return pEventData;
        }

        case CIM_UINT16:
        {
            USHORT temp = 0;

            // If the data is a port number, call the ntohs Windows Socket 2 function
            // to convert the data from TCP/IP network byte order to host byte order.
            // This is here to support legacy event classes; the Port extension 
            // should only be used on properties whose CIM type is object.

            hr = pProperty->pQualifiers->Get (L"Extension", 0, &varQualifier, NULL);
            if (SUCCEEDED (hr))
            {
                if (_wcsicmp (L"Port", varQualifier.bstrVal) == 0)
                {
                    PrintAsPort = TRUE;
                }

                VariantClear (&varQualifier);
            }

            for (ULONG i = 0; i < ArraySize; i++)
            {
                CopyMemory (&temp, pEventData, sizeof (USHORT));

                if (PrintAsPort)
                {
                    wprintf (L"%hu\n", ntohs (temp));
                }
                else
                {
                    wprintf (L"%hu\n", temp);
                }

                pEventData += sizeof (USHORT);
            }

            return pEventData;
        }

        case CIM_OBJECT:
        {
            // An object data type has to include the Extension qualifier.

            hr = pProperty->pQualifiers->Get (L"Extension", 0, &varQualifier, NULL);
            if (SUCCEEDED (hr))
            {
                if (_wcsicmp (L"SizeT", varQualifier.bstrVal) == 0)
                {
                    VariantClear (&varQualifier);

                    // You do not need to know the data type of the property, you just 
                    // retrieve either 4 bytes or 8 bytes depending on the pointer's size.

                    for (ULONG i = 0; i < ArraySize; i++)
                    {
                        if (g_PointerSize == 4)
                        {
                            ULONG temp = 0;

                            CopyMemory (&temp, pEventData, sizeof (ULONG));
                            wprintf (L"0x%x\n", temp);
                        }
                        else
                        {
                            ULONGLONG temp = 0;

                            CopyMemory (&temp, pEventData, sizeof (ULONGLONG));
                            wprintf (L"0x%x\n", temp);
                        }

                        pEventData += g_PointerSize;
                    }

                    return pEventData;
                }
                if (_wcsicmp (L"Port", varQualifier.bstrVal) == 0)
                {
                    USHORT temp = 0;

                    VariantClear (&varQualifier);

                    for (ULONG i = 0; i < ArraySize; i++)
                    {
                        CopyMemory (&temp, pEventData, sizeof (USHORT));
                        wprintf (L"%hu\n", ntohs (temp));
                        pEventData += sizeof (USHORT);
                    }

                    return pEventData;
                }
                else if (_wcsicmp (L"IPAddr", varQualifier.bstrVal) == 0 ||
                    _wcsicmp (L"IPAddrV4", varQualifier.bstrVal) == 0)
                {
                    ULONG temp = 0;

                    VariantClear (&varQualifier);

                    for (ULONG i = 0; i < ArraySize; i++)
                    {
                        CopyMemory (&temp, pEventData, sizeof (ULONG));

                        wprintf (L"%d.%d.%d.%d\n", (temp >> 0) & 0xff,
                            (temp >> 8) & 0xff,
                            (temp >> 16) & 0xff,
                            (temp >> 24) & 0xff);

                        pEventData += sizeof (ULONG);
                    }

                    return pEventData;
                }
                else if (_wcsicmp (L"Guid", varQualifier.bstrVal) == 0)
                {
                    WCHAR szGuid [50];
                    GUID Guid;

                    VariantClear (&varQualifier);

                    for (ULONG i = 0; i < ArraySize; i++)
                    {
                        CopyMemory (&Guid, (GUID*) pEventData, sizeof (GUID));

                        StringFromGUID2 (Guid, szGuid, sizeof (szGuid) - 1);
                        wprintf (L"%s\n", szGuid);

                        pEventData += sizeof (GUID);
                    }

                    return pEventData;
                }
                else if (_wcsicmp (L"Sid", varQualifier.bstrVal) == 0)
                {
                    // Get the user's security identifier and print the 
                    // user's name and domain.

                    SID* psid;
                    DWORD cchUserSize = 0;
                    DWORD cchDomainSize = 0;
                    WCHAR* pUser = NULL;
                    WCHAR* pDomain = NULL;
                    SID_NAME_USE eNameUse;
                    DWORD status = 0;
                    ULONG temp = 0;
                    USHORT CopyLength = 0;
                    BYTE buffer [SECURITY_MAX_SID_SIZE];

                    VariantClear (&varQualifier);

                    for (ULONG i = 0; i < ArraySize; i++)
                    {
                        CopyMemory (&temp, pEventData, sizeof (ULONG));

                        if (temp > 0)
                        {
                            // A property with the Sid extension is actually a 
                            // TOKEN_USER structure followed by the SID. The size
                            // of the TOKEN_USER structure differs depending on 
                            // whether the events were generated on a 32-bit or 
                            // 64-bit architecture. Also the structure is aligned
                            // on an 8-byte boundary, so its size is 8 bytes on a
                            // 32-bit computer and 16 bytes on a 64-bit computer.
                            // Doubling the pointer size handles both cases.

                            USHORT BytesToSid = g_PointerSize * 2;

                            pEventData += BytesToSid;

                            if (RemainingBytes - BytesToSid > SECURITY_MAX_SID_SIZE)
                            {
                                CopyLength = SECURITY_MAX_SID_SIZE;
                            }
                            else
                            {
                                CopyLength = RemainingBytes - BytesToSid;
                            }

                            CopyMemory (&buffer, pEventData, CopyLength);
                            psid = (SID*) &buffer;

                            LookupAccountSid (NULL, psid, pUser, &cchUserSize, pDomain, &cchDomainSize, &eNameUse);

                            status = GetLastError ();
                            if (ERROR_INSUFFICIENT_BUFFER == status)
                            {
                                pUser = (WCHAR*) malloc (cchUserSize * sizeof (WCHAR));
                                pDomain = (WCHAR*) malloc (cchDomainSize * sizeof (WCHAR));

                                if (pUser && pDomain)
                                {
                                    if (LookupAccountSid (NULL, psid, pUser, &cchUserSize, pDomain, &cchDomainSize, &eNameUse))
                                    {
                                        wprintf (L"%s\\%s\n", pDomain, pUser);
                                    }
                                    else
                                    {
                                        wprintf (L"Second LookupAccountSid failed with, %d\n", GetLastError ());
                                    }
                                }
                                else
                                {
                                    wprintf (L"Allocation error.\n");
                                }

                                if (pUser)
                                {
                                    free (pUser);
                                    pUser = NULL;
                                }

                                if (pDomain)
                                {
                                    free (pDomain);
                                    pDomain = NULL;
                                }

                                cchUserSize = 0;
                                cchDomainSize = 0;
                            }
                            else if (ERROR_NONE_MAPPED == status)
                            {
                                wprintf (L"Unable to locate account for the specified SID\n");
                            }
                            else
                            {
                                wprintf (L"First LookupAccountSid failed with, %d\n", status);
                            }

                            pEventData += SeLengthSid (psid);
                        }
                        else  // There is no SID
                        {
                            pEventData += sizeof (ULONG);
                        }
                    }

                    return pEventData;
                }
                else
                {
                    wprintf (L"Extension, %s, not supported.\n", varQualifier.bstrVal);
                    VariantClear (&varQualifier);
                    return NULL;
                }
            }
            else
            {
                wprintf (L"Object data type is missing Extension qualifier.\n");
                return NULL;
            }
        }

        default:
        {
            wprintf (L"Unknown CIM type\n");
            return NULL;
        }

        } // switch
    }
}


TraceConsumer::TraceConsumer ()
{
    ZeroMemory (&m_gTrace, sizeof m_gTrace);

    m_gTrace.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD;
    m_gTrace.EventCallback = PEVENT_CALLBACK (TraceConsumer::ProcessEvent);
    m_gTrace.BufferCallback = PEVENT_TRACE_BUFFER_CALLBACK (TraceConsumer::ProcessBuffer);
    m_gTrace.EventRecordCallback = PEVENT_RECORD_CALLBACK (TraceConsumer::ProcessRecord);
    m_pTraceHeader = & m_gTrace.LogfileHeader;
}

bool TraceConsumer::openTrace (LPWSTR lpLogfile)
{
    m_gTrace.LogFileName = lpLogfile;
    m_hTrace = OpenTrace (&m_gTrace);
    if (TRACEHANDLE (INVALID_HANDLE_VALUE) == m_hTrace)
    {
        printf ("OpenTrace failed : %d, @%s\n", GetLastError (), __FUNCTION__);
        return false;
    }
}

bool TraceConsumer::startProcess ()
{
    auto hr = connectToETWNamespace (_bstr_t (L"root\\wmi"));
    if (FAILED (hr))
    {
        wprintf (L"ConnectToETWNamespace failed with 0x%x\n", hr);
        return false;
    }

    auto status = ProcessTrace (&m_hTrace, 1, 0, 0);
    if (status != ERROR_SUCCESS && status != ERROR_CANCELLED)
    {
        wprintf (L"ProcessTrace failed with %lu\n", status);
        return false;
    }
    return true;
}

void TraceConsumer::printInfo ()
{
    wprintf (L"Logger Buffer Size           : %u\n", m_pTraceHeader->BufferSize);
    wprintf (L"Logger Version               : %u\n", m_pTraceHeader->Version);
    wprintf (L"Provider Version             : %u\n", m_pTraceHeader->ProviderVersion);
    wprintf (L"Processors                   : %u\n", m_pTraceHeader->NumberOfProcessors);
    wprintf (L"Resoultion                   : %u\n", m_pTraceHeader->TimerResolution);
    wprintf (L"Maximum File Size            : %u\n", m_pTraceHeader->MaximumFileSize);
//    wprintf (L"Logger Name                  : %s\n", m_pTraceHeader->LoggerName);
//    wprintf (L"Log File Name                : %s\n", m_pTraceHeader->LogFileName);
    wprintf (L"Events Lost                  : %u\n", m_pTraceHeader->EventsLost);
    wprintf (L"Buffers Lost                 : %u\n", m_pTraceHeader->BuffersLost);
    wprintf (L"\n");
}

HRESULT TraceConsumer::connectToETWNamespace (BSTR bstrNamespace)
{
    HRESULT hr = S_OK;
    IWbemLocator *pLocator = NULL;

    hr = CoInitialize (0);
    
    hr = CoCreateInstance (
        __uuidof (WbemLocator),
        0,
        CLSCTX_INPROC_SERVER,
        __uuidof (IWbemLocator),
        (LPVOID*) &pLocator);

    if (FAILED (hr))
    {
        printf ("CoCreateInstance failed : %d\n", hr);
        pLocator->Release ();
        return false;
    }

    hr = pLocator->ConnectServer (
        bstrNamespace,
        NULL, NULL, NULL,
        0L, NULL, NULL,
        &m_pServices);

    if (FAILED (hr))
    {
        printf ("pLocator->ConnectServer failed with %u\n", hr);
        pLocator->Release ();
        return false;
    }

    hr = CoSetProxyBlanket (
        m_pServices,
        RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
        NULL,
        RPC_C_AUTHN_LEVEL_PKT, RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, 
        EOAC_NONE);

    if (FAILED (hr))
    {
        printf ("CoSetProxyBlanket failed with %u\n", hr);
        m_pServices->Release ();
        m_pServices = nullptr;
        pLocator->Release ();
    }
    return hr;
}

IWbemClassObject* TraceConsumer::GetEventCategoryClass (BSTR bstrClassGuid, int Version)
{
    HRESULT hr = S_OK;
    HRESULT hrQualifier = S_OK;
    IEnumWbemClassObject* pClasses = NULL;
    IWbemClassObject* pClass = NULL;
    IWbemQualifierSet* pQualifiers = NULL;
    ULONG cnt = 0;
    VARIANT varGuid;
    VARIANT varVersion;


    // All ETW MOF classes derive from the EventTrace class, so you need to 
    // enumerate all the EventTrace descendants to find the correct event category class. 

    hr = m_pServices->CreateClassEnum (_bstr_t (L"EventTrace"),
        WBEM_FLAG_DEEP | WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_USE_AMENDED_QUALIFIERS,
        NULL, &pClasses);

    if (FAILED (hr))
    {
        wprintf (L"g_pServices->CreateClassEnum failed with 0x%x\n", hr);
        goto cleanup;
    }

    while (S_OK == hr)
    {
        hr = pClasses->Next (WBEM_INFINITE, 1, &pClass, &cnt);

        if (FAILED (hr))
        {
            wprintf (L"pClasses->Next failed with 0x%x\n", hr);
            goto cleanup;
        }

        // Get all the qualifiers for the class and search for the Guid qualifier. 

        hrQualifier = pClass->GetQualifierSet (&pQualifiers);

        if (pQualifiers)
        {
            hrQualifier = pQualifiers->Get (L"Guid", 0, &varGuid, NULL);

            if (SUCCEEDED (hrQualifier))
            {
                // Compare this class' GUID to the one from the event.

                if (_wcsicmp (varGuid.bstrVal, bstrClassGuid) == 0)
                {
                    // If the GUIDs are equal, check for the correct version.
                    // The version is correct if the class does not contain the EventVersion
                    // qualifier or the class version matches the version from the event.

                    hrQualifier = pQualifiers->Get (L"EventVersion", 0, &varVersion, NULL);

                    if (SUCCEEDED (hrQualifier))
                    {
                        if (Version == varVersion.intVal)
                        {
                            break; //found class
                        }

                        VariantClear (&varVersion);
                    }
                    else if (WBEM_E_NOT_FOUND == hrQualifier)
                    {
                        break; //found class
                    }
                }

                VariantClear (&varGuid);
            }

            pQualifiers->Release ();
            pQualifiers = NULL;
        }

        pClass->Release ();
        pClass = NULL;
    }

cleanup:

    if (pClasses)
    {
        pClasses->Release ();
        pClasses = NULL;
    }

    if (pQualifiers)
    {
        pQualifiers->Release ();
        pQualifiers = NULL;
    }

    VariantClear (&varVersion);
    VariantClear (&varGuid);

    return pClass;
}

IWbemClassObject* TraceConsumer::GetEventClass (IWbemClassObject* pEventCategoryClass, int EventType)
{
    HRESULT hr = S_OK;
    HRESULT hrQualifier = S_OK;
    IEnumWbemClassObject* pClasses = NULL;
    IWbemClassObject* pClass = NULL;
    IWbemQualifierSet* pQualifiers = NULL;
    ULONG cnt = 0;
    VARIANT varClassName;
    VARIANT varEventType;
    BOOL FoundEventClass = FALSE;

    // Get the name of the event category class so you can enumerate its children classes.

    hr = pEventCategoryClass->Get (L"__RELPATH", 0, &varClassName, NULL, NULL);

    if (FAILED (hr))
    {
        wprintf (L"pEventCategoryClass->Get failed with 0x%x\n", hr);
        goto cleanup;
    }

    hr = m_pServices->CreateClassEnum (varClassName.bstrVal,
        WBEM_FLAG_SHALLOW | WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_USE_AMENDED_QUALIFIERS,
        NULL, &pClasses);

    if (FAILED (hr))
    {
        wprintf (L"g_pServices->CreateClassEnum failed with 0x%x\n", hr);
        goto cleanup;
    }

    // Loop through the enumerated classes and find the event class that 
    // matches the event. The class is a match if the event type from the 
    // event matches the value from the EventType class qualifier. 

    while (S_OK == hr)
    {
        hr = pClasses->Next (WBEM_INFINITE, 1, &pClass, &cnt);

        if (FAILED (hr))
        {
            wprintf (L"pClasses->Next failed with 0x%x\n", hr);
            goto cleanup;
        }

        // Get all the qualifiers for the class and search for the EventType qualifier. 

        hrQualifier = pClass->GetQualifierSet (&pQualifiers);

        if (FAILED (hrQualifier))
        {
            wprintf (L"pClass->GetQualifierSet failed with 0x%x\n", hrQualifier);
            pClass->Release ();
            pClass = NULL;
            goto cleanup;
        }

        hrQualifier = pQualifiers->Get (L"EventType", 0, &varEventType, NULL);

        if (FAILED (hrQualifier))
        {
            wprintf (L"pQualifiers->Get(eventtype) failed with 0x%x\n", hrQualifier);
            pClass->Release ();
            pClass = NULL;
            goto cleanup;
        }

        // If multiple events provide the same data, the EventType qualifier
        // will contain an array of types. Loop through the array and find a match.

        if (varEventType.vt & VT_ARRAY)
        {
            HRESULT hrSafe = S_OK;
            int ClassEventType;
            SAFEARRAY* pEventTypes = varEventType.parray;

            for (LONG i = 0; (ULONG) i < pEventTypes->rgsabound->cElements; i++)
            {
                hrSafe = SafeArrayGetElement (pEventTypes, &i, &ClassEventType);

                if (ClassEventType == EventType)
                {
                    FoundEventClass = TRUE;
                    break;  //for loop
                }
            }
        }
        else
        {
            if (varEventType.intVal == EventType)
            {
                FoundEventClass = TRUE;
            }
        }

        VariantClear (&varEventType);

        if (TRUE == FoundEventClass)
        {
            break;  //while loop
        }

        pClass->Release ();
        pClass = NULL;
    }

cleanup:

    if (pClasses)
    {
        pClasses->Release ();
        pClasses = NULL;
    }

    if (pQualifiers)
    {
        pQualifiers->Release ();
        pQualifiers = NULL;
    }

    VariantClear (&varClassName);
    VariantClear (&varEventType);

    return pClass;
}

BOOL GetPropertyList (IWbemClassObject* pClass, PROPERTY_LIST** ppProperties, DWORD* pPropertyCount, LONG** ppPropertyIndex)
{
    HRESULT hr = S_OK;
    SAFEARRAY* pNames = NULL;
    LONG j = 0;
    VARIANT var;

    // Retrieve the property names.

    hr = pClass->GetNames (NULL, WBEM_FLAG_LOCAL_ONLY, NULL, &pNames);
    if (pNames)
    {
        *pPropertyCount = pNames->rgsabound->cElements;

        // Allocate a block of memory to hold an array of PROPERTY_LIST structures.

        *ppProperties = (PROPERTY_LIST*) malloc (sizeof (PROPERTY_LIST) * (*pPropertyCount));
        if (NULL == *ppProperties)
        {
            hr = E_OUTOFMEMORY;
            goto cleanup;
        }

        // WMI may not return the properties in the order as defined in the MOF. Allocate
        // an array of indexes that map into the property list array, so you can retrieve
        // the event data in the correct order.

        *ppPropertyIndex = (LONG*) malloc (sizeof (LONG) * (*pPropertyCount));
        if (NULL == *ppPropertyIndex)
        {
            hr = E_OUTOFMEMORY;
            goto cleanup;
        }

        for (LONG i = 0; (ULONG) i < *pPropertyCount; i++)
        {
            //Save the name of the property.

            hr = SafeArrayGetElement (pNames, &i, &((*ppProperties + i)->Name));
            if (FAILED (hr))
            {
                goto cleanup;
            }

            //Save the qualifiers. Used latter to help determine how to read the event data.

            hr = pClass->GetPropertyQualifierSet ((*ppProperties + i)->Name, &((*ppProperties + i)->pQualifiers));
            if (FAILED (hr))
            {
                goto cleanup;
            }

            // Use the WmiDataId qualifier to determine the correct property order.
            // Index[0] points to the property list element that contains WmiDataId("1"),
            // Index[1] points to the property list element that contains WmiDataId("2"),
            // and so on. 

            hr = (*ppProperties + i)->pQualifiers->Get (L"WmiDataId", 0, &var, NULL);
            if (SUCCEEDED (hr))
            {
                j = var.intVal - 1;
                VariantClear (&var);
                *(*ppPropertyIndex + j) = i;
            }
            else
            {
                goto cleanup;
            }

            // Save the data type of the property.

            hr = pClass->Get ((*ppProperties + i)->Name, 0, NULL, &((*ppProperties + i)->CimType), NULL);
            if (FAILED (hr))
            {
                goto cleanup;
            }
        }
    }

cleanup:

    if (pNames)
    {
        SafeArrayDestroy (pNames);
    }

    if (FAILED (hr))
    {
//        if (*ppProperties)

        return FALSE;
    }

    return TRUE;
}

DWORD GetEventInformation (PEVENT_RECORD pEvent, PTRACE_EVENT_INFO & pInfo)
{
    DWORD status = ERROR_SUCCESS;
    DWORD BufferSize = 0;

    // Retrieve the required buffer size for the event metadata.

    status = TdhGetEventInformation (pEvent, 0, NULL, pInfo, &BufferSize);

    if (ERROR_INSUFFICIENT_BUFFER == status)
    {
        pInfo = (TRACE_EVENT_INFO*) malloc (BufferSize);
        if (pInfo == NULL)
        {
            wprintf (L"Failed to allocate memory for event info (size=%lu).\n", BufferSize);
            status = ERROR_OUTOFMEMORY;
            goto cleanup;
        }

        // Retrieve the event metadata.

        status = TdhGetEventInformation (pEvent, 0, NULL, pInfo, &BufferSize);
    }

    if (ERROR_SUCCESS != status)
    {
//        wprintf (L"TdhGetEventInformation failed with 0x%x.\n", status);
    }

cleanup:

    return status;
}

void TraceConsumer::ProcessRecord (PEVENT_RECORD pEvent)
{
    DWORD status = ERROR_SUCCESS;
    PTRACE_EVENT_INFO pInfo = NULL;
    LPWSTR pwsEventGuid = NULL;
    ULONGLONG TimeStamp = 0;
    ULONGLONG Nanoseconds = 0;
    SYSTEMTIME st;
    SYSTEMTIME stLocal;
    FILETIME ft;
    GetEventInformation (pEvent, pInfo);
    if (pEvent->UserDataLength == 0)
        return;
    cnt++;
    if (write_to_file == 1)
        logfile >> pEvent->UserData;
    if (write_to_file == 2)
        std::wcout << pEvent->UserData << std::endl;
//    wprintf (L"|%s|\n", pEvent->UserData);
}

void TraceConsumer::ProcessEvent (PEVENT_TRACE pEvent)
{
    auto &pHeader = pEvent->Header;

    WCHAR EventClassGuid [50];
    IWbemClassObject *pEventCategoryClass = nullptr;
    IWbemClassObject* pEventClass = nullptr;

    StringFromGUID2 (pHeader.Guid, EventClassGuid, sizeof EventClassGuid);
    wprintf (L"Event Class GUID             : %s\n", EventClassGuid);
    wprintf (L"MOF Length                   : %u\n", pEvent->MofLength);

//    if (IsEqualGUID (pHeader.Guid, ProviderGuid) && pHeader.Class.Type == EVENT_TRACE_TYPE_INFO)
//        return;
    
    if (pEvent->MofLength > 0)
    {
        pEventCategoryClass = GetEventCategoryClass (_bstr_t (EventClassGuid), pHeader.Class.Version);
        if (pEventCategoryClass)
        {
            pEventClass = GetEventClass (pEventCategoryClass, pEvent->Header.Class.Type);
            if (pEventClass)
            {
                // Enumerate the properties and retrieve the event data.

                if (TRUE == GetPropertyList (pEventClass, &pProperties, &PropertyCount, &pPropertyIndex))
                {
                    // Print the property name and value.

                    // Get a pointer to the beginning and end of the event data.
                    // These pointers are used to calculate the number of bytes of event
                    // data left to read. This is only useful if the last data 
                    // element is a string that contains the StringTermination("NotCounted") qualifier.

                    pEventData = (PBYTE) (pEvent->MofData);
                    pEndOfEventData = ((PBYTE) (pEvent->MofData) + pEvent->MofLength);

                    for (LONG i = 0; (DWORD) i < PropertyCount; i++)
                    {
                        //                        PrintPropertyName (pProperties + pPropertyIndex [i]);

                        pEventData = PrintEventPropertyValue (pProperties + pPropertyIndex [i],
                            pEventData,
                            (USHORT) (pEndOfEventData - pEventData));

                        if (NULL == pEventData)
                        {
                            //Error reading the data. Handle as appropriate for your application.
                            break;
                        }
                    }

                    //                    FreePropertyList (pProperties, PropertyCount, pPropertyIndex);
                }



            }
            wprintf (L"|%s|\n", pEvent->MofData);
            pEventClass->Release ();
            pEventClass = NULL;
        }
        else
        {
            wprintf (L"Unable to find the MOF class for the event.\n");
        }

    }
}


void TraceConsumer::ProcessBuffer (PEVENT_TRACE_LOGFILE)
{
//    printf ("ProcessBuffer\n");
}