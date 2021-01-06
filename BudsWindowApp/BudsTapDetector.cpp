// BudsTapDetectorNative.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <stdio.h>
#include <initguid.h>
#include <winsock2.h>
#include <ws2bth.h>
#include <strsafe.h>
#include <intsafe.h>
#include "BudsTapDetector.h"

// Bluetooth service RfComm
// {9B26D8C0-A8ED-440B-95B0-C4714A518BCC}
DEFINE_GUID(g_guidServiceClass, 0x9B26D8C0, 0xA8ED, 0x440B, 0x95, 0xB0, 0xC4, 0x71, 0x4A, 0x51, 0x8B, 0xCC);

#define CXN_BDADDR_STR_LEN                17   // 6 two-digit hex values plus 5 colons
#define CXN_MAX_INQUIRY_RETRY             3
#define CXN_DELAY_NEXT_INQUIRY            2
#define CXN_SUCCESS                       0
#define CXN_ERROR                         1
#define CXN_DEFAULT_LISTEN_BACKLOG        4
#define WAIT_LIMIT                        100000

// defines the maximum number of concurrent listening activities.
#define MAX_LISTEN_THREADS                4
LONG * g_iListeningThreads = NULL;
SOCKADDR_BTH g_RemoteBthAddr [MAX_LISTEN_THREADS] = { 0 };
unsigned int g_RemoteBthAddrCount = 0;
HANDLE g_mutex = NULL;

int  g_ulMaxCxnCycles = 1;

UINT NameToBthAddr();
ULONG RunClientMode(_In_ UINT index, _In_ int iMaxCxnCycles = 1);
DWORD WINAPI StartListenThread(LPVOID lpParam);


bool init()
{
    bool bret = false;
    ULONG       ulRetCode = CXN_SUCCESS;
    WSADATA     WSAData = { 0 };

    g_mutex = CreateMutex(NULL, false, NULL);

    g_iListeningThreads = (LONG *)_aligned_malloc(sizeof(LONG), 32);
    if (g_iListeningThreads)
    {
        *g_iListeningThreads = 0l;

        ulRetCode = WSAStartup(MAKEWORD(2, 2), &WSAData);
        bret = CXN_SUCCESS == ulRetCode;

        if (bret)
        {
            findandlisten();
        }
    }

    return bret;
}

void findandlisten()
{
    // start a new thread to look for a device and listen for taps
    CreateThread(NULL, 0, StartListenThread, 0, 0, 0);
}

DWORD WINAPI StartListenThread(LPVOID lpParam)
{
    auto threadCount = InterlockedIncrement(g_iListeningThreads);

    __try
    {
        // check we are permitted to run
        if (threadCount == 1) // MAX_LISTEN_THREADS)
        {
            ULONG       ulRetCode = CXN_SUCCESS;
            SOCKADDR_BTH RemoteBthAddr = { 0 };

            //
            // Get address from the name of the remote device and run the application
            // in client mode
            //
            auto index = NameToBthAddr();
            if (index != -1)
            {
                // Is the device connected??
                ulRetCode = RunClientMode(index, g_ulMaxCxnCycles);
            }
        }
    }
    __finally
    {
        InterlockedDecrement(g_iListeningThreads);
    }

    return 0;
}

//
// NameToBthAddr converts a bluetooth device name to a bluetooth address,
// if required by performing inquiry with remote name requests.
// This function demonstrates device inquiry, with optional LUP flags.
//
UINT NameToBthAddr()
{
    INT             iResult = CXN_SUCCESS;
    UINT            uiIndex = -1;
    BOOL            bContinueLookup = FALSE, bRemoteDeviceFound = FALSE;
    ULONG           ulFlags = 0, ulPQSSize = sizeof(WSAQUERYSET);
    HANDLE          hLookup = NULL;
    PWSAQUERYSET    pWSAQuerySet = NULL;

    pWSAQuerySet = (PWSAQUERYSET)new byte[ulPQSSize];

    if (NULL == pWSAQuerySet)
    {
        iResult = STATUS_NO_MEMORY;
    }

    // Search for the device with the correct name
    if (CXN_SUCCESS == iResult)
    {
        for (INT iRetryCount = 0;
            !bRemoteDeviceFound && (iRetryCount < CXN_MAX_INQUIRY_RETRY);
            iRetryCount++)
        {
            // WSALookupService is used for both service search and device inquiry
            // LUP_CONTAINERS is the flag which signals that we're doing a device inquiry.
            ulFlags = LUP_CONTAINERS;

            // Friendly device name (if available) will be returned in lpszServiceInstanceName
            ulFlags |= LUP_RETURN_NAME;

            // BTH_ADDR will be returned in lpcsaBuffer member of WSAQUERYSET
            ulFlags |= LUP_RETURN_ADDR;

            if (iRetryCount)
            {
                // Flush the device cache for all inquiries, except for the first inquiry
                // By setting LUP_FLUSHCACHE flag, we're asking the lookup service to do
                // a fresh lookup instead of pulling the information from device cache.
                ulFlags |= LUP_FLUSHCACHE;

                // Pause for some time before all the inquiries after the first inquiry
                // Remote Name requests will arrive after device inquiry has
                // completed.  Without a window to receive IN_RANGE notifications,
                // we don't have a direct mechanism to determine when remote
                // name requests have completed.
                Sleep(CXN_DELAY_NEXT_INQUIRY * 1000);
            }

            // Start the lookup service
            iResult = CXN_SUCCESS;
            hLookup = 0;
            bContinueLookup = FALSE;
            ZeroMemory(pWSAQuerySet, ulPQSSize);
            pWSAQuerySet->dwNameSpace = NS_BTH;
            pWSAQuerySet->dwSize = sizeof(WSAQUERYSET);
            iResult = WSALookupServiceBegin(pWSAQuerySet, ulFlags, &hLookup);

            // Even if we have an error, we want to continue until we
            // reach the CXN_MAX_INQUIRY_RETRY
            if ((NO_ERROR == iResult) && (NULL != hLookup))
            {
                bContinueLookup = TRUE;
            }
            else if (0 < iRetryCount)
            {
                break;
            }

            while (bContinueLookup)
            {
                // Get information about next bluetooth device
                if (NO_ERROR == WSALookupServiceNext(hLookup, ulFlags, &ulPQSSize, pWSAQuerySet))
                {
                    if (WAIT_OBJECT_0 == WaitForSingleObject(g_mutex, WAIT_LIMIT))
                    {
                        __try
                        {
                            // Check if the address exist in the list already
                            UINT count = 0;
                            for (; count < g_RemoteBthAddrCount; count++)
                            {
                                if (0 == memcmp(&g_RemoteBthAddr[count], pWSAQuerySet->lpcsaBuffer->RemoteAddr.lpSockaddr, sizeof(g_RemoteBthAddr[count])))
                                {
                                    // this is not the Bth device we are looking for. 
                                    break;
                                }
                            }

                            if (count == g_RemoteBthAddrCount && g_RemoteBthAddrCount < MAX_LISTEN_THREADS)
                            {
                                CopyMemory(&g_RemoteBthAddr[count],
                                    (PSOCKADDR_BTH)pWSAQuerySet->lpcsaBuffer->RemoteAddr.lpSockaddr,
                                    sizeof(g_RemoteBthAddr[count]));

                                bRemoteDeviceFound = TRUE;
                                bContinueLookup = FALSE;
                                uiIndex = count;
                                g_RemoteBthAddrCount++;
                            }
                        }
                        __finally
                        {
                            ReleaseMutex(g_mutex);
                        }
                    }
                }
                else
                {
                    iResult = WSAGetLastError();
                    if (WSA_E_NO_MORE == iResult)
                    {
                        // No more devices found.  Exit the lookup.
                        bContinueLookup = FALSE;
                    }
                    else if (WSAEFAULT == iResult)
                    {
                        // The buffer for QUERYSET was insufficient.
                        // In such case 3rd parameter "ulPQSSize" of function "WSALookupServiceNext()" receives
                        // the required size.  So we can use this parameter to reallocate memory for QUERYSET.
                        delete[] pWSAQuerySet;
                        pWSAQuerySet = (PWSAQUERYSET)new byte[ulPQSSize];
                        if (NULL == pWSAQuerySet)
                        {
                            iResult = STATUS_NO_MEMORY;
                            bContinueLookup = FALSE;
                        }
                    }
                    else
                    {
                        bContinueLookup = FALSE;
                    }
                }
            }

            // End the lookup service
            WSALookupServiceEnd(hLookup);

            if (STATUS_NO_MEMORY == iResult)
            {
                break;
            }
        }
    }

    if (NULL != pWSAQuerySet)
    {
        delete[] pWSAQuerySet;
        pWSAQuerySet = NULL;
    }

    return uiIndex;
}

//
// RunClientMode runs the application in client mode.  It opens a socket, connects it to a
// remote socket, transfer some data over the connection and closes the connection.
//
ULONG RunClientMode(_In_ UINT index, _In_ int iMaxCxnCycles)
{
    ULONG           ulRetCode = CXN_SUCCESS;
    int             iCxnCount = 0;
    SOCKET          LocalSocket = INVALID_SOCKET;
    SOCKADDR_BTH    SockAddrBthServer = g_RemoteBthAddr[index];

    if (CXN_SUCCESS == ulRetCode)
    {
        // Setting address family to AF_BTH indicates winsock2 to use Bluetooth sockets
        // Port should be set to 0 if ServiceClassId is spesified.
        SockAddrBthServer.addressFamily = AF_BTH;
        SockAddrBthServer.serviceClassId = g_guidServiceClass;
        SockAddrBthServer.port = 0;

        // Run the connection/data-transfer for user specified number of cycles
        for (iCxnCount = 0;
            (0 == ulRetCode) && (iCxnCount < iMaxCxnCycles || iMaxCxnCycles == 0);
            iCxnCount++)
        {
            // Open a bluetooth socket using RFCOMM protocol
            LocalSocket = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
            if (INVALID_SOCKET == LocalSocket)
            {
                ulRetCode = CXN_ERROR;
                break;
            }

            // Connect the socket (pSocket) to a given remote socket represented by address (pServerAddr)
            if (SOCKET_ERROR == connect(LocalSocket,
                (struct sockaddr*)&SockAddrBthServer,
                sizeof(SOCKADDR_BTH)))
            {
                ulRetCode = WSAGetLastError();
                ulRetCode = CXN_ERROR;
                break;
            }

            char* buf = new char[12]{ 0 };
            bool cont = true;
            HINSTANCE ser = 0;
            while (cont)
            {
                auto result = recv(LocalSocket, buf, 12, MSG_WAITALL);
                cont = result == 12;

                // got a triple tap
                // TODO - in future need to verify the data is correct
                if (cont)
                {
                    // Launch Spotify!
                    ser = ShellExecute(NULL, NULL, L"spotify:", NULL, NULL, SW_SHOWDEFAULT);
                }
            }

            //
            // Close the socket
            //
            if (SOCKET_ERROR == closesocket(LocalSocket))
            {
                ulRetCode = CXN_ERROR;
                break;
            }

            LocalSocket = INVALID_SOCKET;
        }
    }

    if (INVALID_SOCKET != LocalSocket)
    {
        closesocket(LocalSocket);
        LocalSocket = INVALID_SOCKET;
    }

    if (WAIT_OBJECT_0 == WaitForSingleObject(g_mutex, WAIT_LIMIT))
    {
        __try
        {
            // remove the socket address from the list
            if (index + 1 < g_RemoteBthAddrCount)
            {
                for (UINT count = index; count + 1 < g_RemoteBthAddrCount; count++)
                {
                    CopyMemory(&g_RemoteBthAddr[count],
                        &g_RemoteBthAddr[count + 1],
                        sizeof(g_RemoteBthAddr[count]));
                }
            }

            g_RemoteBthAddrCount--;
        }
        __finally
        {
            ReleaseMutex(g_mutex);
        }
    }

    return(ulRetCode);
}
