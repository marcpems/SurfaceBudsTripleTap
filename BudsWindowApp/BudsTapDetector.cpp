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
DEFINE_GUID(g_guidRfCommClass, 0xe0cbf06c, 0xcd8b, 0x4647, 0xbb, 0x8a, 0x26, 0x3b, 0x43, 0xf0, 0xf9, 0x74);

#define CXN_BDADDR_STR_LEN                17   // 6 two-digit hex values plus 5 colons
#define CXN_MAX_INQUIRY_RETRY             3
#define CXN_DELAY_NEXT_INQUIRY            500
#define CXN_SUCCESS                       0
#define CXN_ERROR                         1
#define CXN_DEFAULT_LISTEN_BACKLOG        4
#define WAIT_LIMIT                        100000

// defines the maximum number of concurrent listening activities.
#define MAX_LISTEN_THREADS                40
LONG * g_iListeningThreads = NULL;
SOCKADDR_BTH g_RemoteBthAddr [MAX_LISTEN_THREADS] = { 0 };
unsigned int g_RemoteBthAddrCount = 0;
HANDLE g_mutex = NULL;

int  g_ulMaxCxnCycles = 1;

void ProcessNewDevices(bool flush);
DWORD WINAPI ListenForTaps(_In_ LPVOID BthAddress);
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

// Kick off the process of finding any new devices
void findandlisten()
{
    // start a new thread to look for a device and listen for taps
    CreateThread(NULL, 0, StartListenThread, 0, 0, 0);
}

DWORD WINAPI StartListenThread(LPVOID lpParam)
{
    auto threadCount = InterlockedIncrement(g_iListeningThreads);

    // only actually allow one thread to do this at a time
    if (threadCount == 1)
    {
        bool flush = false;

        while (threadCount)
        {
            __try
            {
                ProcessNewDevices(flush);
            }
            __finally
            {
                threadCount = InterlockedDecrement(g_iListeningThreads);
            }

            if (threadCount)
            {
                // Pause for some time before retrying 
                Sleep(CXN_DELAY_NEXT_INQUIRY);
                flush = true;
            }
        }
    }

    return 0;
}

// ProcessNewDevices reads through the list of Bluetooth devices and 
// attempts to setup a listening socket for each RfComm bluetooth address,
void ProcessNewDevices(bool flushCache)
{
    INT             iResult = CXN_SUCCESS;
    UINT            uiIndex = -1;
    BOOL            bContinueLookup = FALSE;
    ULONG           ulFlags = 0, ulPQSSize = sizeof(WSAQUERYSET);
    HANDLE          hLookup = NULL;
    PWSAQUERYSET    pWSAQuerySet = NULL;
    BTH_QUERY_SERVICE queryService{ 0 };
    BLOB            blob{ 0 };

    pWSAQuerySet = (PWSAQUERYSET)new byte[ulPQSSize];

    if (NULL == pWSAQuerySet)
    {
        iResult = STATUS_NO_MEMORY;
    }

    // Search the devices 
    if (CXN_SUCCESS == iResult)
    {
        // WSALookupService is used for both service search and device inquiry
        // LUP_CONTAINERS is the flag which signals that we're doing a device inquiry.
        ulFlags = LUP_CONTAINERS;

        // BTH_ADDR will be returned in lpcsaBuffer member of WSAQUERYSET
        ulFlags |= LUP_RETURN_ADDR;

        if (flushCache)
        {
            // Flush the device cache for all inquiries, except for the first inquiry
            // By setting LUP_FLUSHCACHE flag, we're asking the lookup service to do
            // a fresh lookup instead of pulling the information from device cache.
            ulFlags |= LUP_FLUSHCACHE;
        }

        queryService.type = SDP_SERVICE_SEARCH_ATTRIBUTE_REQUEST;
        queryService.serviceHandle = 0;
        queryService.uuids[0].u.uuid128 = g_guidServiceClass;
        queryService.uuids[0].uuidType = SDP_ST_UUID128;

        blob.cbSize = sizeof(queryService);
        blob.pBlobData = (BYTE*)&queryService;

        // Start the lookup service
        iResult = CXN_SUCCESS;
        hLookup = 0;
        bContinueLookup = FALSE;
        ZeroMemory(pWSAQuerySet, ulPQSSize);
        pWSAQuerySet->dwNameSpace = NS_BTH;
        pWSAQuerySet->dwSize = sizeof(WSAQUERYSET);
//        pWSAQuerySet->lpServiceClassId = (LPGUID)&g_guidRfCommClass; //0x03
        pWSAQuerySet->lpBlob = &blob;

        iResult = WSALookupServiceBegin(pWSAQuerySet, ulFlags, &hLookup);

        iResult = WSAGetLastError();

        // drop through on error
        if ((NO_ERROR == iResult) && (NULL != hLookup))
        {
            bContinueLookup = TRUE;
        }

        while (bContinueLookup)
        {
            // Get information about next bluetooth device
            if (NO_ERROR == WSALookupServiceNext(hLookup, ulFlags, &ulPQSSize, pWSAQuerySet))
            {
                // lock the mutex to add a new handler 
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
                                // already got this one 
                                break;
                            }
                        }

                        if (count == g_RemoteBthAddrCount && g_RemoteBthAddrCount < MAX_LISTEN_THREADS)
                        {
                            CopyMemory(&g_RemoteBthAddr[count],
                                (PSOCKADDR_BTH)pWSAQuerySet->lpcsaBuffer->RemoteAddr.lpSockaddr,
                                sizeof(g_RemoteBthAddr[count]));
                            g_RemoteBthAddrCount++;

                            // start a socket attempt.
                            CreateThread(NULL, 0, ListenForTaps, (LPVOID)&g_RemoteBthAddr[count], 0, 0);
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
                if (WSAEFAULT == iResult)
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
#ifdef _DEBUG
                    if (WSA_E_NO_MORE == iResult)
                    {
                        // No more devices found.  Exit the lookup.
                        // ... log?
                    }
#endif
                    bContinueLookup = FALSE;
                }
            }
        }

        // End the lookup service
        WSALookupServiceEnd(hLookup);
    }

    if (NULL != pWSAQuerySet)
    {
        delete[] pWSAQuerySet;
        pWSAQuerySet = NULL;
    }
}

//
// ListenForTaps takes the address and tries to open a socket, connect to it
// and wait for a tap command. On error the connection is closed and the address removed from the list.
//
DWORD WINAPI ListenForTaps(_In_ LPVOID BthAddress)
{
    SOCKADDR_BTH    SockAddrBthServer = *(SOCKADDR_BTH*)BthAddress;

    __try
    {
        SOCKET          LocalSocket = INVALID_SOCKET;

        // Setting address family to AF_BTH indicates winsock2 to use Bluetooth sockets
        // Port should be set to 0 if ServiceClassId is spesified.
        SockAddrBthServer.addressFamily = AF_BTH;
        SockAddrBthServer.serviceClassId = g_guidServiceClass;
        SockAddrBthServer.port = 0;

        // Open a bluetooth socket using RFCOMM protocol
        LocalSocket = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
        if (INVALID_SOCKET != LocalSocket)
        {
            // Connect the socket (pSocket) to a given remote socket represented by address (pServerAddr)
            if (SOCKET_ERROR != connect(LocalSocket,
                (struct sockaddr*)&SockAddrBthServer,
                sizeof(SOCKADDR_BTH)))
            {
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
            }
#ifdef _DEBUG
            else
            {
                // get the socket error code (debug only)
                auto errorCode = WSAGetLastError();
            }
#endif
        }

        closesocket(LocalSocket);
        LocalSocket = INVALID_SOCKET;
    }
    __finally
    {
        // we must remove the address from the list.
        if (WAIT_OBJECT_0 == WaitForSingleObject(g_mutex, WAIT_LIMIT))
        {
            __try
            {
                UINT findCount = 0;
                ZeroMemory(&SockAddrBthServer.serviceClassId, sizeof(SockAddrBthServer.serviceClassId));
                for (; findCount < g_RemoteBthAddrCount; findCount++)
                {
                    if (0 == memcmp(&g_RemoteBthAddr[findCount], &SockAddrBthServer, sizeof(g_RemoteBthAddr[findCount])))
                    {
                        // found it! If its in the middle, move everything up
                        for (UINT count = findCount; count + 1 < g_RemoteBthAddrCount; count++)
                        {
                            CopyMemory(&g_RemoteBthAddr[count],
                                &g_RemoteBthAddr[count + 1],
                                sizeof(g_RemoteBthAddr[count]));
                        }
                        break;
                    }
                }

                g_RemoteBthAddrCount--;
            }
            __finally
            {
                ReleaseMutex(g_mutex);
            }
        }
    }

    return 0; 
}
