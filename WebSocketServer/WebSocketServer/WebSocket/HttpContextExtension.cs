namespace DemoBotApp.WebSocket
{
    using System.Web;
    using System.Diagnostics;
    public static class HttpContextExtension
    {
        public static void AcceptWebSocketRequest(this HttpContext context, WebSocketHandler handler)
        {
            context.AcceptWebSocketRequest(handler.ProcessRequest);
        }
    }
}