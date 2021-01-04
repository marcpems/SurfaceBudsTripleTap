using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Text;
using Windows.ApplicationModel;
using Windows.ApplicationModel.Activation;
using Windows.ApplicationModel.Background;
using Windows.Foundation;
using Windows.Foundation.Collections;
using Windows.Networking.Sockets;
using Windows.Storage.Streams;
using Windows.UI.Notifications;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Controls.Primitives;
using Windows.UI.Xaml.Data;
using Windows.UI.Xaml.Input;
using Windows.UI.Xaml.Media;
using Windows.UI.Xaml.Navigation;

namespace SurfaceBudsTripleTap
{
    /// <summary>
    /// Provides application-specific behavior to supplement the default Application class.
    /// </summary>
    sealed partial class App : Application
    {
        /// <summary>
        /// Initializes the singleton application object.  This is the first line of authored code
        /// executed, and as such is the logical equivalent of main() or WinMain().
        /// </summary>
        public App()
        {
            this.InitializeComponent();
            this.Suspending += OnSuspending;
        }

        /// <summary>
        /// Invoked when the application is launched normally by the end user.  Other entry points
        /// will be used such as when the application is launched to open a specific file.
        /// </summary>
        /// <param name="e">Details about the launch request and process.</param>
        protected override void OnLaunched(LaunchActivatedEventArgs e)
        {
            Frame rootFrame = Window.Current.Content as Frame;

            // Do not repeat app initialization when the Window already has content,
            // just ensure that the window is active
            if (rootFrame == null)
            {
                // Create a Frame to act as the navigation context and navigate to the first page
                rootFrame = new Frame();

                rootFrame.NavigationFailed += OnNavigationFailed;

                if (e.PreviousExecutionState == ApplicationExecutionState.Terminated)
                {
                    //TODO: Load state from previously suspended application
                }

                // Place the frame in the current Window
                Window.Current.Content = rootFrame;
            }

            if (e.PrelaunchActivated == false)
            {
                if (rootFrame.Content == null)
                {
                    // When the navigation stack isn't restored navigate to the first page,
                    // configuring the new page by passing required information as a navigation
                    // parameter
                    rootFrame.Navigate(typeof(MainPage), e.Arguments);
                }
                // Ensure the current window is active
                Window.Current.Activate();
            }
        }

        /// <summary>
        /// Invoked when Navigation to a certain page fails
        /// </summary>
        /// <param name="sender">The Frame which failed navigation</param>
        /// <param name="e">Details about the navigation failure</param>
        void OnNavigationFailed(object sender, NavigationFailedEventArgs e)
        {
            throw new Exception("Failed to load Page " + e.SourcePageType.FullName);
        }

        /// <summary>
        /// Invoked when application execution is being suspended.  Application state is saved
        /// without knowing whether the application will be terminated or resumed with the contents
        /// of memory still intact.
        /// </summary>
        /// <param name="sender">The source of the suspend request.</param>
        /// <param name="e">Details about the suspend request.</param>
        private void OnSuspending(object sender, SuspendingEventArgs e)
        {
            var deferral = e.SuspendingOperation.GetDeferral();
            //TODO: Save application state and stop any background activity
            deferral.Complete();
        }

        public void ShowToast(string msg)
        {
            var toastNotifier = ToastNotificationManager.CreateToastNotifier();
            var toastXml = ToastNotificationManager.GetTemplateContent(ToastTemplateType.ToastText02);
            var textNodes = toastXml.GetElementsByTagName("text");
            textNodes.First().AppendChild(toastXml.CreateTextNode($"Triple tap detected: {msg}"));
            var toastNotification = new ToastNotification(toastXml);
            toastNotifier.Show(new ToastNotification(toastXml));

            // Launch Spotify
            var testAppUri = new Uri("spotify:"); // The protocol handled by the launched app
            _ = Windows.System.Launcher.LaunchUriAsync(testAppUri);
        }

        protected override async void OnBackgroundActivated(BackgroundActivatedEventArgs args)
        {
            base.OnBackgroundActivated(args);
            IBackgroundTaskInstance taskInstance = args.TaskInstance;
            var deferral = taskInstance.GetDeferral();
            try
            {
                if ((taskInstance.TriggerDetails as SocketActivityTriggerDetails)?.Reason == SocketActivityTriggerReason.SocketActivity)
                {
                    var details = taskInstance.TriggerDetails as SocketActivityTriggerDetails;
                    var socketInformation = details.SocketInformation;
                    switch (details.Reason)
                    {
                        case SocketActivityTriggerReason.SocketActivity:
                            var socket = socketInformation.StreamSocket;
                            DataReader reader = new DataReader(socket.InputStream);

                            //// TODO: the value returned is likely to change (David Born from Spotify):
                            //// " the payload of the message is a bit different than what the earbuds send today
                            //// it needs to contain three separate fields in the future: a cliend-id specific for microsoft headphone integrations
                            //// a brand field(obviously "mircosoft") and a model("surface earbuds" or what the official model name is)
                            //// the whole thing is then tlv encoded "
                            await reader.LoadAsync(12);
                            ShowToast(socketInformation.Id);

                            socket.TransferOwnership(socketInformation.Id);
                            break;

                        case SocketActivityTriggerReason.KeepAliveTimerExpired:
                            // I dont think this should ever be called
                            socket = socketInformation.StreamSocket;
                            DataWriter writer = new DataWriter(socket.OutputStream);
                            writer.WriteBytes(Encoding.UTF8.GetBytes("Keep alive"));
                            await writer.StoreAsync();
                            writer.DetachStream();
                            writer.Dispose();
                            socket.TransferOwnership(socketInformation.Id);
                            break;

                        case SocketActivityTriggerReason.SocketClosed:
                            // nothing doing...
                            // buds have probably been disconnected. Remove them from the list. 
                            break;

                        default:
                            break;
                    }
                }
            }
            catch (Exception exception)
            {
                ShowToast(exception.Message);
            }
            finally
            {
                deferral.Complete();
            }
        }
    }
}
