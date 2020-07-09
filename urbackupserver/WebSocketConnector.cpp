#include "WebSocketConnector.h"
#include "../stringtools.h"
#include "../Interface/Server.h"
#include "../cryptoplugin/ICryptoFactory.h"
#include "../urbackupcommon/WebSocketPipe.h"
#include <assert.h>

extern ICryptoFactory* crypto_fak;

void WebSocketConnector::Execute(str_map& GET, THREAD_ID tid, str_map& PARAMS, IPipe* pipe, const std::string& endpoint_name)
{
	if (PARAMS["CONNECTION"] != "Upgrade")
	{
		pipe->Write("HTTP/1.1 500 Expecting Connection: Upgrade\r\nConnection: Close\r\n\r\n");
		delete pipe;
		return;
	}

	std::string protocol_list = PARAMS["SEC-WEBSOCKET-PROTOCOL"];
	std::vector<std::string> protocols;
	Tokenize(protocol_list, protocols, ",");

	for (size_t i = 0; i < protocols.size(); ++i)
		protocols[i] = trim(protocols[i]);

	if (std::find(protocols.begin(), protocols.end(), "urbackup") == protocols.end())
	{
		pipe->Write("HTTP/1.1 500 urbackup protocol not supported\r\nConnection: Close\r\n\r\n");
		delete pipe;
		return;
	}

	if (PARAMS["SEC-WEBSOCKET-VERSION"] != "13")
	{
		pipe->Write("HTTP/1.1 500 websocket protocol version not supported\r\nConnection: Close\r\n\r\n");
		delete pipe;
		return;
	}

	std::string websocket_key = trim(PARAMS["SEC-WEBSOCKET-KEY"]);
	websocket_key += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

	std::string key_response = crypto_fak->sha1Binary(websocket_key);
	pipe->Write(std::string("HTTP/1.1 101 Switching Protocols\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Accept: ") + base64_encode(reinterpret_cast<const unsigned char*>(key_response.data()),
			static_cast<unsigned int>(key_response.size())) + "\r\n"
		"Sec-WebSocket-Protocol: urbackup\r\n\r\n");

	ICustomClient* client = wrapped_service->createClient();

	str_map::iterator it_forwarded_for = PARAMS.find("X-FORWARDED-FOR");

	WebSocketPipe* ws_pipe = new WebSocketPipe(pipe, false, true, std::string(), true);

	client->Init(tid, ws_pipe, it_forwarded_for != PARAMS.end() ? it_forwarded_for->second : endpoint_name);

	while (true)
	{
		bool b = client->Run(NULL);
		if (!b)
		{
			break;
		}

		if (client->wantReceive())
		{
			if (ws_pipe->isReadable(10))
			{
				client->ReceivePackets(NULL);
			}
			else if (ws_pipe->hasError())
			{
				client->ReceivePackets(NULL);
				Server->wait(20);
			}
		}
		else
		{
			Server->wait(20);
		}
	}

	bool want_destory_pipe = client->closeSocket();

	wrapped_service->destroyClient(client);

	if (want_destory_pipe)
	{
		delete ws_pipe;
	}
}

std::string WebSocketConnector::getName()
{
	return name;
}
