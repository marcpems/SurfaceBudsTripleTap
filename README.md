# SurfaceBudsTrippleTap
Shows how to handle triple tap hardware command from the Surface Buds to launch Spotify app on Windows 10 desktop.

There are three projects in this solution that show different aspects of handling the triple tap command:

# SurfaceBudsTripleTap.csproj
This is a C# UWP app that demonstrates how to use the Bluetooth UWP APIs to find the Surface Buds, listen for connection changes and to setup a socket listener that detects the triple tap. This also shows how to launch Spotify using the protocol activation (the app assumes Spotify is installed).
There is a second project related to this approach SocketActivityBackgroundTask, which is registered for background socket activity thus allowing the app to be closed and efficiently listen for the buds commands even when the app is closed. 

This solution has a couple of problems:
- hardware changes can only be detected when the app is running. They cannot be detected from a background process.
This means that if the buds are added or connected while the main app is not running then no socket will be setup for them and triple taps will be ignored. 
- protocol launching Spotify is not possible from a background task or background thread (when the main app window is closed). 

# BudsTapDetectorNative
This is a C++ console app that demonstrates using winsock2 library to enumerate the available local devices and connect to any detected Surface Buds. If a valid pair of Buds is detected then a socket is created to listen for triple tap, and Spotify will be launched by protocol activation (the Spotify app is assumed to be installed).
The project is not intended to be a complete solution as it does not monitor hardware changes, and will only support one Buds connection. 

# BudsWindowApp
This is a C++ Win32 app based on th code in BudsTapDetectorNative that adds hardware monitoring and support for up to four connected devices. 
