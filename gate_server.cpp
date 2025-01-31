#include "common.h"
#include "shared_this.h"
#include "database.h"
#include "gate_server.h"
#include "models.h"
#include <stdio.h>
#include <vector>
#include <signal.h>

using sstream = std::stringstream;

class GateConnection : public SharedThis<GateConnection>
{
public:
	asio::ip::tcp::socket& getSocket() {
		return socket;
	}

	void receive() {
		recvBuffer.clear();	// FIXME do we have to handle more than 1 msg per buffer?
		asio::async_read_until(socket, asio::dynamic_vector_buffer(recvBuffer), packetMatcher,
				std::bind(&GateConnection::onReceive, shared_from_this(), asio::placeholders::error, asio::placeholders::bytes_transferred));
	}

	void send(const std::vector<uint8_t>& data)
	{
		memcpy(&sendBuffer[sendIdx], data.data(), data.size());
		sendIdx += data.size();
		send();
	}

private:
	GateConnection(asio::io_context& io_context)
		: io_context(io_context), socket(io_context) {
	}

	void send()
	{
		if (sending)
			return;
		sending = true;
		uint16_t packetSize = *(uint16_t *)&sendBuffer[0] + 2;
		asio::async_write(socket, asio::buffer(sendBuffer, packetSize),
			std::bind(&GateConnection::onSent, shared_from_this(),
					asio::placeholders::error,
					asio::placeholders::bytes_transferred));
	}
	void onSent(const std::error_code& ec, size_t len)
	{
		if (ec)
		{
			fprintf(stderr, "ERROR: onSent: %s\n", ec.message().c_str());
			return;
		}
		sending = false;
		assert(len <= sendIdx);
		sendIdx -= len;
		if (sendIdx != 0) {
			memmove(&sendBuffer[0], &sendBuffer[len], sendIdx);
			send();
		}
	}

	using iterator = asio::buffers_iterator<asio::const_buffers_1>;

	std::pair<iterator, bool>
	static packetMatcher(iterator begin, iterator end)
	{
		if (end - begin < 3)
			return std::make_pair(begin, false);
		iterator i = begin;
		uint16_t len = (uint8_t)*i++;
		len |= uint8_t(*i++) << 8;
		len += 2;
		if (end - begin < len)
			return std::make_pair(begin, false);
		return std::make_pair(begin + len, true);
	}

	void onReceive(const std::error_code& ec, size_t len)
	{
		if (ec || len == 0)
		{
			if (ec && ec != asio::error::eof)
				fprintf(stderr, "ERROR: onReceive: %s\n", ec.message().c_str());
			else
				printf("Connection closed\n");
			return;
		}
		// Grab data and process if correct.
		std::string payload = std::string(&recvBuffer[2], &recvBuffer[len]);
		printf("gate: Request: [%s]\n", payload.c_str());
		processRequest(payload);
		receive();
	}

	void sendPacket(uint16_t opcode, const std::string& payload = {})
	{
		*(uint16_t *)&sendBuffer[sendIdx] = payload.size() + 2;
		*(uint16_t *)&sendBuffer[sendIdx + 2] = opcode;
		memcpy(&sendBuffer[sendIdx + 4], payload.data(), payload.length());
		sendIdx += 4 + payload.length();
		send();
	}

	//What is 0x3F6 and 0x3FF for?
	void processRequest(const std::string& request)
	{
		std::vector<std::string> split = splitString(request, ' ');
		if (split[0] == "REQUEST_FILTER")
		{
			if (split.size() < 2) {
				sendPacket(ERROR1);
				return;
			}
			GameId gameId = identifyGame(split[1]);
			// Lobby servers list
			sendPacket(0x3E8);
			LobbyServer *server = LobbyServer::getServer(gameId);
			if (server != nullptr)
			{
				sstream ss;
				ss << server->getName() << ' ' << socket.local_endpoint().address().to_string()
				   << ' ' << server->getIpPort() << " 1";
				sendPacket(0x3E9, ss.str());
			}
			sendPacket(0x3EA);
		}
		else if (split[0] == "HANDLE_LIST_GET")
		{
			if (split.size() < 4) {
				sendPacket(ERROR1);
				return;
			}
			GameId gameId = identifyGame(split[2]);
			LobbyServer *server = LobbyServer::getServer(gameId);

			// TODO golf wants max 9 chars for handle. cause of non connection for player 2?
			// tetris max 8 when typing (but no issue with generated flycast1_1)
			std::string daytonaHash = split[1];
			std::string handleName;
			for (int i = 0; i < 100 && server != nullptr; i++)
			{
				if (daytonaHash == "flycast1" || daytonaHash == "flycast2")
				{
					handleName = "Player";
					if (i == 0)
						i = 1;
				}
				else {
					handleName = daytonaHash;
				}
				if (i > 0)
					handleName += std::to_string(i);
				if (gameId == GameId::Daytona)
					handleName += ".us";
				if (server->getPlayer(handleName) == nullptr)
					break;
				handleName = "";
			}
			if (handleName.empty()) {
				sendPacket(ERROR1);
				return;
			}
			// TODO database

			std::string payload = "1" + utf8ToSjis(handleName, gameId == GameId::GolfShiyouyo);
			sendPacket(0x3F2, payload);
		}
		else if (split[0 ]== "HANDLE_ADD")
		{
			if (split.size() < 5) {
				sendPacket(ERROR1);
				return;
			}

			std::string daytonaHash = split[1];
			GameId gameId = identifyGame(split[2]);
			//int handleIndx = atoi(split[3].c_str());
			std::string handlename = sjisToUtf8(split[4]);

			int result = database.createHandle(daytonaHash, handlename);
			if (result == 1)
				sendPacket(0x3F3, "1 " + utf8ToSjis(handlename, gameId == GameId::GolfShiyouyo));
			else if (result == 0)
				sendPacket(ERROR1);
			else if (result == -1)
				sendPacket(NAME_IN_USE1);
		}
		else if (split[0] == "HANDLE_REPLACE")
		{
			if (split.size() < 5) {
				sendPacket(ERROR1);
				return;
			}

			std::string daytonaHash = split[1];
			GameId gameId = identifyGame(split[2]);
			int handleIndx = atoi(split[3].c_str());
			std::string newHandleName = sjisToUtf8(split[4]);

			int result = database.replaceHandle(daytonaHash, handleIndx, newHandleName);
			if (result == 0)
				sendPacket(0x3F4, "1 " + utf8ToSjis(newHandleName, gameId == GameId::GolfShiyouyo));
			else if (result == 0)
				sendPacket(ERROR1);
			else if (result == -1)
				sendPacket(NAME_IN_USE1);
		}
		else if (split[0] == "HANDLE_DELETE")
		{
			if (split.size() < 5) {
				sendPacket(ERROR1);
				return;
			}

			std::string daytonaHash = split[1];
			int handleIndx = atoi(split[3].c_str());

			if (database.deleteHandle(daytonaHash, handleIndx))
				sendPacket(0x3F5);
			else
				sendPacket(ERROR1);
		}
	}

	enum Errors {
		ERROR1 = 0x3FC,
		NAME_IN_USE1 = 0x3FD,
		NAME_IN_USE2 = 0x3FE,
		ERROR2 = 0x3FF,
	};
	asio::io_context& io_context;
	asio::ip::tcp::socket socket;
	std::vector<uint8_t> recvBuffer;
	std::array<uint8_t, 1024> sendBuffer;
	size_t sendIdx = 0;
	bool sending = false;

	friend super;
};

GateServer::GateServer(asio::io_context& io_context, uint16_t port)
	: io_context(io_context),
	  acceptor(asio::ip::tcp::acceptor(io_context,
			asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)))
{
	asio::socket_base::reuse_address option(true);
	acceptor.set_option(option);
}

void GateServer::start()
{
	GateConnection::Ptr newConnection = GateConnection::create(io_context);

	acceptor.async_accept(newConnection->getSocket(),
			std::bind(&GateServer::handleAccept, shared_from_this(), newConnection, asio::placeholders::error));
}

void GateServer::handleAccept(GateConnection::Ptr newConnection, const std::error_code& error)
{
	if (!error) {
		printf("New connection from %s\n", newConnection->getSocket().remote_endpoint().address().to_string().c_str());
		newConnection->receive();
	}
	start();
}
