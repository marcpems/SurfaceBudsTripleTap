// BudsTapDetectorNative.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <stdio.h>
#include <initguid.h>
#include <winsock2.h>
#include <ws2bth.h>
#include <strsafe.h>
#include <intsafe.h>


// Bluetooth device class
// {B62C4E8D-62CC-404b-BBBF-BF3E3BBB1374}
// DEFINE_GUID(g_guidServiceClass, 0xb62c4e8d, 0x62cc, 0x404b, 0xbb, 0xbf, 0xbf, 0x3e, 0x3b, 0xbb, 0x13, 0x74);

// {9B26D8C0-A8ED-440B-95B0-C4714A518BCC}
DEFINE_GUID(g_guidServiceClass, 0x9B26D8C0, 0xA8ED, 0x440B, 0x95, 0xB0, 0xC4, 0x71, 0x4A, 0x51, 0x8B, 0xCC);


#define CXN_BDADDR_STR_LEN                17   // 6 two-digit hex values plus 5 colons
#define CXN_MAX_INQUIRY_RETRY             3
#define CXN_DELAY_NEXT_INQUIRY            15
#define CXN_SUCCESS                       0
#define CXN_ERROR                         1
#define CXN_DEFAULT_LISTEN_BACKLOG        4

#define CXN_TEST_DATA_STRING              (L"~!@#$%^&*()-_=+?<>1234567890abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")
#define CXN_TRANSFER_DATA_LENGTH          (sizeof(CXN_TEST_DATA_STRING))

int  g_ulMaxCxnCycles = 1;

ULONG NameToBthAddr(_In_ LPCWSTR pszRemoteName, _Out_ PSOCKADDR_BTH pRemoteBthAddr);
ULONG RunClientMode(_In_ SOCKADDR_BTH ululRemoteBthAddr, _In_ int iMaxCxnCycles = 1);

int main()
{
    ULONG       ulRetCode = CXN_SUCCESS;
    WSADATA     WSAData = { 0 };
    SOCKADDR_BTH RemoteBthAddr = { 0 };

    std::cout << "Looking for Buds...\n";

    ulRetCode = WSAStartup(MAKEWORD(2, 2), &WSAData);
    if (CXN_SUCCESS != ulRetCode)
    {
        std::cout << "-FATAL- | Unable to initialize Winsock version 2.2\n";
        return -1;
    }

    //
    // Get address from the name of the remote device and run the application
    // in client mode
    //
    ulRetCode = NameToBthAddr((LPCWSTR)L"Surface Earbuds", &RemoteBthAddr);
    if (CXN_SUCCESS != ulRetCode)
    {
        std::cout << "-FATAL- | Can't find the buds \n";
        return-1;
    }

    // Is the device connected??

    ulRetCode = RunClientMode(RemoteBthAddr, g_ulMaxCxnCycles);
    return 0;
}

//
// NameToBthAddr converts a bluetooth device name to a bluetooth address,
// if required by performing inquiry with remote name requests.
// This function demonstrates device inquiry, with optional LUP flags.
//
ULONG NameToBthAddr(_In_ LPCWSTR pszRemoteName, _Out_ PSOCKADDR_BTH pRemoteBtAddr)
{
    INT             iResult = CXN_SUCCESS;
    BOOL            bContinueLookup = FALSE, bRemoteDeviceFound = FALSE;
    ULONG           ulFlags = 0, ulPQSSize = sizeof(WSAQUERYSET);
    HANDLE          hLookup = NULL;
    PWSAQUERYSET    pWSAQuerySet = NULL;

    ZeroMemory(pRemoteBtAddr, sizeof(*pRemoteBtAddr));

    pWSAQuerySet = (PWSAQUERYSET)new byte[ulPQSSize];

    if (NULL == pWSAQuerySet) 
    {
        iResult = STATUS_NO_MEMORY;
        std::cout << "!ERROR! | Unable to allocate memory for WSAQUERYSET\n";
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

            if (0 == iRetryCount)
            {
                std::cout << "Inquiring device from cache...\n";
            }
            else
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
                std::cout << "Unable to find device.  Waiting for " << CXN_DELAY_NEXT_INQUIRY << " seconds before re-inquiry...\n";
                Sleep(CXN_DELAY_NEXT_INQUIRY * 1000);

                std::cout << "Inquiring device ...\n";
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
                std::cout << "WSALookupServiceBegin() failed with error code " << iResult << ", WSAGetLastError = " << WSAGetLastError() << "\n";
                break;
            }

            while (bContinueLookup) 
            {
                // Get information about next bluetooth device
                if (NO_ERROR == WSALookupServiceNext(hLookup, ulFlags, &ulPQSSize, pWSAQuerySet)) 
                {
                    // Compare the name to see if this is the device we are looking for.
                    if ((pWSAQuerySet->lpszServiceInstanceName != NULL) &&
                        (CXN_SUCCESS == _wcsicmp(pWSAQuerySet->lpszServiceInstanceName, pszRemoteName))) 
                    {
                        // Found a remote bluetooth device with matching name.
                        // Get the address of the device and exit the lookup.
                        CopyMemory(pRemoteBtAddr,
                            (PSOCKADDR_BTH)pWSAQuerySet->lpcsaBuffer->RemoteAddr.lpSockaddr,
                            sizeof(*pRemoteBtAddr));
                        bRemoteDeviceFound = TRUE;
                        bContinueLookup = FALSE;
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
                        delete [] pWSAQuerySet;
                        pWSAQuerySet = (PWSAQUERYSET)new byte[ulPQSSize];
                        if (NULL == pWSAQuerySet) 
                        {
                            std::cout << "!ERROR, Unable to allocate memory for WSAQERYSET\n";
                            iResult = STATUS_NO_MEMORY;
                            bContinueLookup = FALSE;
                        }
                    }
                    else 
                    {
                        std::cout << "WSALookupServiceNext() failed with error code "<< iResult << "\n";
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
        delete [] pWSAQuerySet;
        pWSAQuerySet = NULL;
    }

    return bRemoteDeviceFound ? CXN_SUCCESS : CXN_ERROR;
}

//
// RunClientMode runs the application in client mode.  It opens a socket, connects it to a
// remote socket, transfer some data over the connection and closes the connection.
//
ULONG RunClientMode(_In_ SOCKADDR_BTH RemoteAddr, _In_ int iMaxCxnCycles)
{
    ULONG           ulRetCode = CXN_SUCCESS;
    int             iCxnCount = 0;
    SOCKET          LocalSocket = INVALID_SOCKET;
    SOCKADDR_BTH    SockAddrBthServer = RemoteAddr;

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

            wprintf(L"\n");

            // Open a bluetooth socket using RFCOMM protocol
            LocalSocket = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
            if (INVALID_SOCKET == LocalSocket) 
            {
                wprintf(L"=CRITICAL= | socket() call failed. WSAGetLastError = [%d]\n", WSAGetLastError());
                ulRetCode = CXN_ERROR;
                break;
            }

            // Connect the socket (pSocket) to a given remote socket represented by address (pServerAddr)
            if (SOCKET_ERROR == connect(LocalSocket,
                (struct sockaddr*)&SockAddrBthServer,
                sizeof(SOCKADDR_BTH))) 
            {
                wprintf(L"=CRITICAL= | connect() call failed. WSAGetLastError=[%d]\n", WSAGetLastError());
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

                // Launch Spotify!
                ser = ShellExecute(NULL, NULL, L"spotify:", NULL, NULL, SW_SHOWDEFAULT);
            }

            //
            // Close the socket
            //
            if (SOCKET_ERROR == closesocket(LocalSocket)) 
            {
                wprintf(L"=CRITICAL= | closesocket() call failed w/socket = [0x%I64X]. WSAGetLastError=[%d]\n", (ULONG64)LocalSocket, WSAGetLastError());
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

    return(ulRetCode);
}
