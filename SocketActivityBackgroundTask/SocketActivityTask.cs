//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************
using System;
using System.Linq;
using System.Text;
using Windows.ApplicationModel.Background;
using Windows.Networking;
using Windows.Networking.Sockets;
using Windows.Storage;
using Windows.Storage.Streams;
using Windows.UI.Notifications;

namespace SocketActivityBackgroundTask
{
    public sealed class SocketActivityTask : Windows.ApplicationModel.Background.IBackgroundTask
    {
        public async void Run(IBackgroundTaskInstance taskInstance)
        {
            var deferral = taskInstance.GetDeferral();
            try
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
            catch (Exception exception)
            {
                ShowToast(exception.Message);
            }
            finally
            {
                deferral.Complete();
            }
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
    }
}
