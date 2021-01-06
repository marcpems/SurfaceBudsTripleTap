#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#include <Dbt.h>
#include <initguid.h>
#include "../BudsWindowApp/BudsTapDetector.h"

#pragma comment(lib, "advapi32.lib")

// MessageText:
//
//  An error has occurred (%2).
#define SVC_ERROR                        ((DWORD)0xC0020001L)

// Bluetooth device class
// {B62C4E8D-62CC-404b-BBBF-BF3E3BBB1374}
DEFINE_GUID(g_guidBtthClass, 0xb62c4e8d, 0x62cc, 0x404b, 0xbb, 0xbf, 0xbf, 0x3e, 0x3b, 0xbb, 0x13, 0x74);

DEFINE_GUID(g_guidBthPortDevice, 0x0850302A, 0xB344, 0x4fDA, 0x9B, 0xE9, 0x90, 0x57, 0x6B, 0x8D, 0x46, 0xF0);
DEFINE_GUID(g_guidBthDeviceInterface, 0x00F40965, 0xE89D, 0x4487, 0x98, 0x90, 0x87, 0xC3, 0xAB, 0xB2, 0x11, 0xF4);
DEFINE_GUID(g_guidBthLEDeviceInterface, 0x781aee18, 0x7733, 0x4ce4, 0xad, 0xd0, 0x91, 0xf4, 0x1c, 0x67, 0xb5, 0x92);

constexpr auto SVCNAME = L"BudsTapSvc";

SERVICE_STATUS          gSvcStatus;
SERVICE_STATUS_HANDLE   gSvcStatusHandle;
HANDLE                  ghSvcStopEvent = NULL;

VOID SvcInstall(void);
VOID SvcUninstall(void);
VOID SvcStart(void);
VOID SvcStop(void);
DWORD WINAPI SvcCtrlHandler(DWORD, DWORD, LPVOID, LPVOID);
VOID WINAPI SvcMain(DWORD, LPTSTR*);

VOID ReportSvcStatus(DWORD, DWORD, DWORD);
VOID SvcInit(DWORD, LPTSTR*);
VOID SvcReportEvent(LPTSTR);


int __cdecl _tmain(int argc, TCHAR* argv[])
{
    if (S_OK != CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))
    {
        // err not sure what to do..
        return -1;
    }

    DEV_BROADCAST_DEVICEINTERFACE hdr{ 0 };
    hdr.dbcc_size = sizeof(hdr);
    hdr.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    hdr.dbcc_classguid = g_guidBtthClass;

    if (lstrcmpi(argv[1], TEXT("update")) == 0)
    {
        SvcStop();
        Sleep(2000);
        SvcUninstall();
        Sleep(2000);
        SvcInstall();
        Sleep(2000);
        SvcStart();
        return 0;
    }

    if (lstrcmpi(argv[1], TEXT("install")) == 0)
    {
        SvcInstall();
        return 0;
    }

    if (lstrcmpi(argv[1], TEXT("uninstall")) == 0)
    {
        SvcUninstall();
        return 0;
    }

    if (lstrcmpi(argv[1], TEXT("start")) == 0)
    {
        SvcStart();
        return 0;
    }

    // TO_DO: Add any additional services for the process to this table.
    SERVICE_TABLE_ENTRY DispatchTable[] =
    {
        { (LPWSTR)SVCNAME, (LPSERVICE_MAIN_FUNCTION)SvcMain },
        { NULL, NULL }
    };

    // This call returns when the service has stopped. 
    // The process should simply terminate when the call returns.

    if (!StartServiceCtrlDispatcher(DispatchTable))
    {
        SvcReportEvent((LPTSTR)L"StartServiceCtrlDispatcher");
    }

    return 0;
 }

//
// Purpose: 
//   Installs a service in the SCM database
//
// Parameters:
//   None
// 
// Return value:
//   None
//
VOID SvcInstall()
{
    SC_HANDLE schSCManager;
    SC_HANDLE schService;
    TCHAR szPath[MAX_PATH];

    if (!GetModuleFileName(NULL, szPath, MAX_PATH))
    {
        printf("Cannot install service (%d)\n", GetLastError());
        return;
    }

    // Get a handle to the SCM database. 

    schSCManager = OpenSCManager(
        NULL,                    // local computer
        NULL,                    // ServicesActive database 
        SC_MANAGER_CREATE_SERVICE);  // full access rights 

    if (NULL == schSCManager)
    {
        printf("OpenSCManager failed (%d)\n", GetLastError());
        return;
    }

    // Create the service

    schService = CreateService(
        schSCManager,              // SCM database 
        SVCNAME,                   // name of service 
        SVCNAME,                   // service name to display 
        SERVICE_ALL_ACCESS,        // TODO: reduce this access !! SERVICE_ALL_ACCESS
//        SERVICE_WIN32_OWN_PROCESS, // service type 
        SERVICE_USER_OWN_PROCESS, // service type 
        SERVICE_AUTO_START,        // start type 
        SERVICE_ERROR_NORMAL,      // error control type 
        szPath,                    // path to service's binary 
        NULL,                      // no load ordering group 
        NULL,                      // no tag identifier 
        NULL,                      // no dependencies 
        NULL,                      // LocalSystem account 
        NULL);                     // no password 

    if (schService == NULL)
    {
        printf("CreateService failed (%d)\n", GetLastError());
        CloseServiceHandle(schSCManager);
        return;
    }
    else printf("Service installed successfully\n");

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}

//
// Purpose: 
//   Uninstall the service in the SCM database
//
// Parameters:
//   None
// 
// Return value:
//   None
//
VOID SvcUninstall()
{
    SC_HANDLE schSCManager;
    SC_HANDLE schService;

    // Get a handle to the SCM database. 
    schSCManager = OpenSCManager(
        NULL,                    // local computer
        NULL,                    // ServicesActive database 
        GENERIC_ALL);  // full access rights 

    if (NULL == schSCManager)
    {
        printf("OpenSCManager failed (%d)\n", GetLastError());
        return;
    }

    schService = OpenService(
        schSCManager,              // SCM database 
        SVCNAME,                   // name of service 
        DELETE);

    if (schService == NULL)
    {
        printf("OpenService failed (%d)\n", GetLastError());
        CloseServiceHandle(schSCManager);
        return;
    }

    if (!DeleteService(schService))
    {
        printf("DeleteService failed (%d)\n", GetLastError());
    }
    else printf("Service uninstalled successfully\n");

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}

//
// Purpose: 
//   Start the service 
//
// Parameters:
//   None
// 
// Return value:
//   None
//
VOID SvcStart()
{
    SC_HANDLE schSCManager;
    SC_HANDLE schService;

    // Get a handle to the SCM database. 
    schSCManager = OpenSCManager(
        NULL,          // local computer
        NULL,          // ServicesActive database 
        GENERIC_ALL); 

    if (NULL == schSCManager)
    {
        printf("OpenSCManager failed (%d)\n", GetLastError());
        return;
    }

    schService = OpenService(
        schSCManager,              // SCM database 
        SVCNAME,                   // name of service 
        SERVICE_START);

    if (schService == NULL)
    {
        printf("OpenService failed (%d)\n", GetLastError());
        CloseServiceHandle(schSCManager);
        return;
    }

    if (!StartService(schService, 0, NULL))
    {
        printf("StartService failed (%d)\n", GetLastError());
    }
    else printf("Service started successfully\n");

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}

//
// Purpose: 
//   Start the service 
//
// Parameters:
//   None
// 
// Return value:
//   None
//
VOID SvcStop()
{
    SC_HANDLE schSCManager;
    SC_HANDLE schService;
    SERVICE_STATUS serviceStatus { 0 };

    // Get a handle to the SCM database. 
    schSCManager = OpenSCManager(
        NULL,          // local computer
        NULL,          // ServicesActive database 
        GENERIC_ALL);

    if (NULL == schSCManager)
    {
        printf("OpenSCManager failed (%d)\n", GetLastError());
        return;
    }

    schService = OpenService(
        schSCManager,              // SCM database 
        SVCNAME,                   // name of service 
        SERVICE_STOP);

    if (schService == NULL)
    {
        printf("OpenService failed (%d)\n", GetLastError());
        CloseServiceHandle(schSCManager);
        return;
    }

    if (ControlService(schService, SERVICE_CONTROL_STOP, &serviceStatus))
    {
        printf("Service stop requested successfully\n");
    }
    else 
    {
        printf("Stop service request failed (%d)\n", GetLastError());
    }

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}


//
// Purpose: 
//   Entry point for the service
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
// 
// Return value:
//   None.
//
VOID WINAPI SvcMain(DWORD dwArgc, LPTSTR* lpszArgv)
{
    // Register the handler function for the service

    gSvcStatusHandle = RegisterServiceCtrlHandlerEx(
        SVCNAME,
        SvcCtrlHandler,
        NULL);

    if (!gSvcStatusHandle)
    {
        SvcReportEvent((LPTSTR)_T("RegisterServiceCtrlHandler"));
        return;
    }

    // These SERVICE_STATUS members remain as set here
//    gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    gSvcStatus.dwServiceType = SERVICE_USER_OWN_PROCESS;
    gSvcStatus.dwServiceSpecificExitCode = 0;

    // Report initial status to the SCM

    ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    // Perform service-specific initialization and work.
    // Register for device change notifications
    DEV_BROADCAST_DEVICEINTERFACE hdr;
    ZeroMemory(&hdr, sizeof(hdr));
    hdr.dbcc_size = sizeof(hdr);
    hdr.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    hdr.dbcc_classguid = g_guidBthPortDevice;

    if (NULL == RegisterDeviceNotification(gSvcStatusHandle, &hdr, DEVICE_NOTIFY_SERVICE_HANDLE | DEVICE_NOTIFY_ALL_INTERFACE_CLASSES))
    {
        ReportSvcStatus(SERVICE_STOPPED, GetLastError(), 0);
        return;
    }


    SvcInit(dwArgc, lpszArgv);
}

//
// Purpose: 
//   The service code
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
// 
// Return value:
//   None
//
VOID SvcInit(DWORD dwArgc, LPTSTR* lpszArgv)
{
    // Create an event. The control handler function, SvcCtrlHandler,
    // signals this event when it receives the stop control code.

    ghSvcStopEvent = CreateEvent(
        NULL,    // default security attributes
        TRUE,    // manual reset event
        FALSE,   // not signaled
        NULL);   // no name

    if (ghSvcStopEvent == NULL)
    {
        ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
        return;
    }

    // check for buds
    init();

    // Report running status when initialization is complete.

    ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);

    // TO_DO: Perform work until service stops.

    while (1)
    {
        // Check whether to stop the service.

        WaitForSingleObject(ghSvcStopEvent, INFINITE);

        ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
        return;
    }
}

//
// Purpose: 
//   Sets the current service status and reports it to the SCM.
//
// Parameters:
//   dwCurrentState - The current state (see SERVICE_STATUS)
//   dwWin32ExitCode - The system error code
//   dwWaitHint - Estimated time for pending operation, 
//     in milliseconds
// 
// Return value:
//   None
//
VOID ReportSvcStatus(DWORD dwCurrentState,
    DWORD dwWin32ExitCode,
    DWORD dwWaitHint)
{
    static DWORD dwCheckPoint = 1;

    // Fill in the SERVICE_STATUS structure.
    gSvcStatus.dwCurrentState = dwCurrentState;
    gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
    gSvcStatus.dwWaitHint = dwWaitHint;

    if (dwCurrentState == SERVICE_START_PENDING)
    {
        gSvcStatus.dwControlsAccepted = 0;
    }
    else
    {
        gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    }

    if ((dwCurrentState == SERVICE_RUNNING) ||
        (dwCurrentState == SERVICE_STOPPED))
    {
        gSvcStatus.dwCheckPoint = 0;
    }
    else
    {
        gSvcStatus.dwCheckPoint = dwCheckPoint++;
    }

    // Report the status of the service to the SCM.
    SetServiceStatus(gSvcStatusHandle, &gSvcStatus);
}

//
// Purpose: 
//   Called by SCM whenever a control code is sent to the service
//   using the ControlService function.
//
// Parameters:
//   dwCtrl - control code
// 
// Return value:
//   None
//
DWORD WINAPI SvcCtrlHandler(DWORD dwCtrl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext)
{
    DWORD res = ERROR_CALL_NOT_IMPLEMENTED;
    // Handle the requested control code. 

    switch (dwCtrl)
    {
    case SERVICE_CONTROL_STOP:
    {
        ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);

        // Signal the service to stop.
        SetEvent(ghSvcStopEvent);
        ReportSvcStatus(gSvcStatus.dwCurrentState, NO_ERROR, 0);
        res = NO_ERROR;
    }
    break;

    case SERVICE_CONTROL_INTERROGATE:
    {
        res = NO_ERROR;
    }
    break;

    case SERVICE_CONTROL_DEVICEEVENT:
    {
        switch (dwEventType)
        {
        case DBT_DEVICEARRIVAL:
        {
            DEV_BROADCAST_DEVICEINTERFACE* hdr = (DEV_BROADCAST_DEVICEINTERFACE*)lpEventData;
            // Devices change - go check for Bth devices
            findandlisten();
            res = NO_ERROR;
        }
            break;

        default:
            res = NO_ERROR;// ERROR_CALL_NOT_IMPLEMENTED;
            break;
        }
    }
    break;

    default:
        res = ERROR_CALL_NOT_IMPLEMENTED;
        break;
    }

    return res;
}

//
// Purpose: 
//   Logs messages to the event log
//
// Parameters:
//   szFunction - name of function that failed
// 
// Return value:
//   None
//
// Remarks:
//   The service must have an entry in the Application event log.
//
VOID SvcReportEvent(LPTSTR szFunction)
{
    HANDLE hEventSource;
    LPCTSTR lpszStrings[2];
    TCHAR Buffer[80];

    hEventSource = RegisterEventSource(NULL, SVCNAME);

    if (NULL != hEventSource)
    {
        StringCchPrintf(Buffer, 80, TEXT("%s failed with %d"), szFunction, GetLastError());

        lpszStrings[0] = SVCNAME;
        lpszStrings[1] = Buffer;

        ReportEvent(hEventSource,        // event log handle
            EVENTLOG_ERROR_TYPE, // event type
            0,                   // event category
            SVC_ERROR,           // event identifier
            NULL,                // no security identifier
            2,                   // size of lpszStrings array
            0,                   // no binary data
            lpszStrings,         // array of strings
            NULL);               // no binary data

        DeregisterEventSource(hEventSource);
    }
}