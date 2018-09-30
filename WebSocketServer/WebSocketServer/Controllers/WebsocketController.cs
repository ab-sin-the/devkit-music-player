namespace DemoBotApp.Controllers
{
    using System;
    using System.Collections.Generic;
    using System.Configuration;
    using System.IO;
    using System.Linq;
    using System.Net;
    using System.Net.Http;
    using System.Text;
    using System.Threading;
    using System.Threading.Tasks;
    using System.Web;
    using System.Web.Http;
    using System.Diagnostics;
    using DemoBotApp.WebSocket;
    using Microsoft.Bot.Connector.DirectLine;
    using NAudio.Wave;
    using Microsoft.WindowsAzure;
    using Microsoft.WindowsAzure.Storage;
    using Microsoft.WindowsAzure.Storage.Auth;
    using Microsoft.WindowsAzure.Storage.Blob;
    using Microsoft.WindowsAzure.Storage.File;
    using Microsoft.WindowsAzure.Storage.RetryPolicies;

    [RoutePrefix("chat")]
    public class WebsocketController : ApiController
    {
        private readonly CancellationTokenSource cts = new CancellationTokenSource();
        private DirectLineClient directLineClient;
        private WebSocketHandler defaultHandler = new WebSocketHandler();
        private static Dictionary<string, WebSocketHandler> handlers = new Dictionary<string, WebSocketHandler>();
        private int musicId;
        //CloudStorageAccount storageAccount = CreateStorageAccountFromConnectionString(ConfigurationManager.AppSettings["StorageConnectionString"]);

        static string storageAccountName = "";

        static string storageKey = "";

        StorageCredentials storageCredentials = new StorageCredentials(storageAccountName, storageKey);

        string baseURL = "";


        public WebsocketController()
        {
            musicId = 1;
            // Setup bot client
        }

        [Route]
        [HttpGet]
        public async Task<HttpResponseMessage> Connect(string nickName)
        {
            Trace.WriteLine(nickName);
            if (string.IsNullOrEmpty(nickName))
            {
                throw new HttpResponseException(HttpStatusCode.BadRequest);
            }

            WebSocketHandler webSocketHandler = new WebSocketHandler();

            // Handle the case where client forgot to close connection last time
            if (handlers.ContainsKey(nickName))
            {
                WebSocketHandler origHandler = handlers[nickName];
                handlers.Remove(nickName);

                try
                {
                    await origHandler.Close();
                }
                catch
                {
                    // unexcepted error when trying to close the previous websocket
                }
            }

            handlers[nickName] = webSocketHandler;

            string conversationId = string.Empty;
            string watermark = null;

            webSocketHandler.OnOpened += ((sender, arg) =>
            {
            });

            webSocketHandler.OnTextMessageReceived += (async (sender, message) =>
            {
                Trace.WriteLine("Text");
                // Do nothing with heartbeat message
                // Send text message to bot service for non-heartbeat message
                Trace.TraceInformation(message);
                if (string.Equals(message, "next", StringComparison.OrdinalIgnoreCase))
                {
                    musicId++;
                    await OnBinaryMessageReceived(webSocketHandler, conversationId, watermark, musicId);
                }

                if (!string.Equals(message, "heartbeat", StringComparison.OrdinalIgnoreCase))
                {
                    await OnTextMessageReceived(webSocketHandler, message, conversationId, watermark);
                }
            });

            webSocketHandler.OnBinaryMessageReceived += (async (sender, bytes) =>
            {
                await OnBinaryMessageReceived(webSocketHandler, conversationId, watermark, 1);
            });

            webSocketHandler.OnClosed += (sender, arg) =>
            {
                Trace.WriteLine("Closed");
                handlers.Remove(nickName);
            };

            HttpContext.Current.AcceptWebSocketRequest(webSocketHandler);
            return Request.CreateResponse(HttpStatusCode.SwitchingProtocols);
        }

        private async Task OnTextMessageReceived(WebSocketHandler handler, string message, string conversationId, string watermark)
        {
            await handler.SendMessage($"You said: {message}");
        }

        private async Task OnBinaryMessageReceived(WebSocketHandler handler, string conversationId, string watermark, int musicId)
        {
            string fileName = musicId.ToString() +".wav";
            Uri uri = new Uri(baseURL + fileName);
            CloudFile file = new CloudFile(uri, storageCredentials);
            bool ifExist = await file.ExistsAsync();
            if (!ifExist)
            {
                if (musicId == 1)
                {
                    Trace.TraceError("No music!!");
                    return;
                }
                else
                {
                    musicId = 1;
                    fileName = musicId.ToString() + ".wav";
                    uri = new Uri(baseURL + fileName);
                    file = new CloudFile(uri, storageCredentials);
                }
            }
            file.FetchAttributes();
            byte[] totalBytes = new byte[file.Properties.Length];
            try
            {
                using (AutoResetEvent waitHandle = new AutoResetEvent(false))
                {
                    ICancellableAsyncResult result = file.BeginDownloadRangeToByteArray(totalBytes, 0, null, null, ar => waitHandle.Set(), null);
                    waitHandle.WaitOne();
                }
            } catch(Exception ex)
            {
                Trace.TraceError(ex.ToString());
            }
            Trace.TraceInformation(String.Format("File length {0}", totalBytes.Length));
           // totalBytes = totalBytes.Skip(startIdx).ToArray();
            await handler.SendBinary(totalBytes, cts.Token);
        }
 
    }
}
