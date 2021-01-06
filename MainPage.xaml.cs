using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Windows.ApplicationModel.Background;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Rfcomm;
using Windows.Devices.Enumeration;
using Windows.Networking.Sockets;
using Windows.Storage.Streams;
using Windows.UI.Popups;
using Windows.UI.Xaml.Controls;

namespace SurfaceBudsTripleTap
{
    /// <summary>
    /// An empty page that can be used on its own or navigated to within a Frame.
    /// </summary>
    public sealed partial class MainPage : Page
    {
        //
        // 1> when the app starts grab a watcher that is looking for the presence of the Buds.
        // 2> when the buds are found start looking for the triple tap
        // 3> when the triple tap is received, launch Spotify
        //
        // 4> when the app closes, end the socket and close the watcher. 
        // 5> when the devices are removed, close the socket.
        // 
        // how to do this in the background??

        private static readonly Guid RfcommLaunchCommandUuid = Guid.Parse("9B26D8C0-A8ED-440B-95B0-C4714A518BCC");
        private static readonly string SurfaceBudsName = "Surface Earbuds";

        private DeviceWatcher deviceWatcher = null;

        private List<BudsDeviceInfo> deviceList = new List<BudsDeviceInfo>();

        private Semaphore mre = new Semaphore(1,1);
        public static IBackgroundTaskRegistration BkTask { get; private set; }
        private bool LaunchOnTap { get; set; }

        public MainPage()
        {
            this.InitializeComponent();
            UpdateStatusMessage("Looking for devices...");
            StartListener();

            _ = BackgroundExecutionManager.RequestAccessAsync();

            // prep the background tasks
            foreach (var current in BackgroundTaskRegistration.AllTasks)
            {
                if (current.Value.Name == "SocketActivityBackgroundTask")
                {
                    BkTask = current.Value;
                    break;
                }
            }

            // If there is no task allready created, create a new one
            if (BkTask == null)
            {
                var socketTaskBuilder = new BackgroundTaskBuilder();
                socketTaskBuilder.Name = "SocketActivityBackgroundTask";

                // moved to inproc so no task entry point
                //socketTaskBuilder.TaskEntryPoint = "SocketActivityBackgroundTask.SocketActivityTask";

                var trigger = new SocketActivityTrigger();
                socketTaskBuilder.SetTrigger(trigger);
                BkTask = socketTaskBuilder.Register();
            }
        }

        private void UpdateStatusMessage(string newText)
        {
            _ = Dispatcher.RunAsync(Windows.UI.Core.CoreDispatcherPriority.Normal, () => this.StatusMessage.Text = newText);
        }

        private void StartListener()
        {
            var requestedProperties = new string[] { "System.Devices.Aep.DeviceAddress", "System.Devices.Aep.IsConnected" };
            deviceWatcher = DeviceInformation.CreateWatcher("(System.Devices.Aep.ProtocolId:=\"{e0cbf06c-cd8b-4647-bb8a-263b43f0f974}\")", requestedProperties, DeviceInformationKind.AssociationEndpoint); // ClassGuid = {e0cbf06c-cd8b-4647-bb8a-263b43f0f974} includes all Bluetooth devices //  AND System.ItemNameDisplay:~<\"Surface Earbuds\"
            deviceWatcher.Added += DeviceWatcher_Added;
            deviceWatcher.Updated += DeviceWatcher_Updated;
            deviceWatcher.Removed += DeviceWatcher_Removed;
            deviceWatcher.Start();
        }

        private void DeviceWatcher_Removed(DeviceWatcher sender, DeviceInformationUpdate args)
        {

        }

        private void DeviceWatcher_Updated(DeviceWatcher sender, DeviceInformationUpdate args)
        {
            try
            {
                mre.WaitOne();

                // check the device is in our list 
                foreach (var device in deviceList)
                {
                    if (device.DeviceId == args.Id)
                    {
                        // got the device. Now look for the property 
                        foreach (var prop in args.Properties)
                        {
                            if (prop.Key == "System.Devices.Aep.IsConnected" && prop.Value.GetType() == typeof(bool))
                            {
                                device.IsConnected = (bool)prop.Value;
                            }
                        }

                        break;
                    }
                }

            }
            finally
            {
                mre.Release();
            }

            var countConnected = 0;
            foreach (var device in deviceList)
            {
                if (device.IsConnected)
                {
                    countConnected++;
                }
            }

            UpdateStatusMessage(string.Format("Found {0} device(s). Connected: {1}", deviceList.Count, countConnected));
        }

        private async void DeviceWatcher_Added(DeviceWatcher sender, DeviceInformation args)
        {
            try
            {
                mre.WaitOne();

                // Find the Surface Buds
                // check the device isnt already in the list.
                foreach (var device in deviceList)
                {
                    if (device.DeviceId == args.Id)
                    {
                        // done - drop right out.
                        return;
                    }
                }

                // check it's what we are after
                DeviceAccessStatus accessStatus = DeviceAccessInformation.CreateFromId(args.Id).CurrentStatus;
                if (accessStatus != DeviceAccessStatus.DeniedByUser &&
                    accessStatus != DeviceAccessStatus.DeniedBySystem)
                {
                        var bluetoothDevice = await BluetoothDevice.FromIdAsync(args.Id);

                        if (bluetoothDevice != null)
                        {
                            // add it to the list.
                            var result = await bluetoothDevice.GetRfcommServicesForIdAsync(RfcommServiceId.FromUuid(RfcommLaunchCommandUuid));
                        if (result.Services.Count > 0)
                        {
                            var newDevice = new BudsDeviceInfo();
                            newDevice.DeviceId = args.Id;
                            newDevice.oneTouchService = result.Services[0];

                            // now check to see if the device is connected
                            foreach (var prop in args.Properties)
                            {
                                if (prop.Key == "System.Devices.Aep.IsConnected" && prop.Value.GetType() == typeof(bool) && (bool)prop.Value == true)
                                {
                                    newDevice.IsConnected = true;
                                }
                            }
                            deviceList.Add(newDevice);

                            var countConnected = 0;
                            foreach (var device in deviceList)
                            {
                                if (device.IsConnected)
                                {
                                    countConnected++;
                                }
                            }

                            UpdateStatusMessage(string.Format("Found {0} device(s). Connected: {1}", deviceList.Count, countConnected));
                        }
                    }
                }
            }
            catch
            {
                // any exception means its not our device, just ignore it
            }
            finally
            {
                mre.Release();
            }
        }
    }

    public class BudsDeviceInfo
    {
        private bool connected = false;
        public string DeviceId { get; set; }

        public bool IsConnected 
        { 
            get => connected; 
            set
            {
                if (connected != value)
                {
                    connected = value;

                    if (connected)
                    {
                        Task.Run(async () =>
                        {
                            // open a socket and listen
                            ConnectedSocket = new StreamSocket();

                            ConnectedSocket.EnableTransferOwnership(MainPage.BkTask.TaskId, SocketActivityConnectedStandbyAction.Wake);
                            await ConnectedSocket.ConnectAsync(oneTouchService.ConnectionHostName, oneTouchService.ConnectionServiceName);
                            await ConnectedSocket.CancelIOAsync();
                            ConnectedSocket.TransferOwnership(DeviceId);
                        });

                    }
                    else
                    {
                        // Close everything down
                        // TODO: not sure how to do this. The sockets will close when the device is removed anyway. 
                    }
                }
            }
        }

        private StreamSocket ConnectedSocket { get; set; }

        public RfcommDeviceService oneTouchService { get; set; }
    }
}
