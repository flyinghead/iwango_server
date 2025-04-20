#include "common.h"
#include "shared_this.h"
#include "database.h"
#include "gate_server.h"
#include "models.h"
#include <stdio.h>
#include <vector>
#include <algorithm>
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
			if (ec != asio::error::eof && ec != asio::error::bad_descriptor)
				ERROR_LOG(GameId::Unknown, "gate: onSent: %s", ec.message().c_str());
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
		if (ec || len < 2)
		{
			if (ec && ec != asio::error::eof && ec != asio::error::bad_descriptor)
				ERROR_LOG(GameId::Unknown, "gate: onReceive: %s", ec.message().c_str());
			else if (len != 0)
				ERROR_LOG(GameId::Unknown, "gate: onReceive: small packet: %zd", len);
			else
				DEBUG_LOG(GameId::Unknown, "gate: Connection closed");
			return;
		}
		// Grab data and process if correct.
		std::string payload = std::string(&recvBuffer[2], &recvBuffer[len]);
		INFO_LOG(GameId::Unknown, "gate: [%s] Request [%s]", socket.remote_endpoint().address().to_string().c_str(), payload.c_str());
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

			std::string userName = split[1];
			if (userName == "flycast1" || userName == "flycast2" || userName == "dream")
			{
				// Forcibly assign a 'PlayerN' handle
				std::string handleName;
				LobbyServer *server = LobbyServer::getServer(gameId);
				for (int i = 1; i < 100 && server != nullptr; i++)
				{
					handleName =  "Player" + std::to_string(i);
					if (server->getPlayer(handleName) == nullptr)
						break;
					handleName = "";
				}
				if (!handleName.empty())
					sendPacket(0x3F2, "1" + utf8ToSjis(handleName, gameId == GameId::GolfShiyouyo));
				else
					sendPacket(0x3F2);
			}
			else
			{
				std::string handleName = userName;
				std::transform(handleName.begin(), handleName.end(), handleName.begin(), [](char c) {
					if (c == ' ' || c == '#' || c == '&' || c == '*' || c == '=')
						return '_';
					else
						return c;
				});
				// IWANGO max handle length is 19 chars. But Golf Shiyou 2 only accepts
				// full-width shift-JIS chars which take up 2 bytes each.
				// Hundred Swords UI only has space for 6 chars but no other issue.
				unsigned maxLength = gameId == GameId::GolfShiyouyo ? 9 : 19;
				if (gameId == GameId::Daytona)
					handleName = handleName.substr(0, maxLength - 3) + ".us";
				else
					handleName = handleName.substr(0, maxLength);
				std::vector<std::string> handles = getHandles(gameId, userName, handleName);
				sstream ss;
				for (unsigned i = 0; i < handles.size(); i++)
				{
					if (i > 0)
						ss << ' ';
					ss << (i + 1) << utf8ToSjis(handles[i], gameId == GameId::GolfShiyouyo);
				}
				sendPacket(0x3F2, ss.str());
			}
		}
		else if (split[0 ]== "HANDLE_ADD")
		{
			if (split.size() < 5) {
				sendPacket(ERROR1);
				return;
			}

			std::string userName = split[1];
			if (userName == "flycast1" || userName == "flycast2" || userName == "dream") {
				sendPacket(NAME_IN_USE1);
				return;
			}
			GameId gameId = identifyGame(split[2]);
			int handleIndx = atoi(split[3].c_str());
			std::string handlename = sjisToUtf8(split[4]);

			try {
				if (createHandle(gameId, userName, handleIndx, handlename))
					sendPacket(0x3F3, "1 " + utf8ToSjis(handlename, gameId == GameId::GolfShiyouyo));
				else
					sendPacket(ERROR1);
			} catch (const AlreadyExistsException&) {
				sendPacket(NAME_IN_USE1);
			}
		}
		else if (split[0] == "HANDLE_REPLACE")
		{
			if (split.size() < 5) {
				sendPacket(ERROR1);
				return;
			}

			std::string userName = split[1];
			if (userName == "flycast1" || userName == "flycast2" || userName == "dream") {
				sendPacket(NAME_IN_USE1);
				return;
			}
			GameId gameId = identifyGame(split[2]);
			int handleIndx = atoi(split[3].c_str());
			std::string newHandleName = sjisToUtf8(split[4]);

			try {
				if (replaceHandle(gameId, userName, handleIndx, newHandleName))
					sendPacket(0x3F4, "1 " + utf8ToSjis(newHandleName, gameId == GameId::GolfShiyouyo));
				else
					sendPacket(ERROR1);
			} catch (const AlreadyExistsException&) {
				sendPacket(NAME_IN_USE1);
			}
		}
		else if (split[0] == "HANDLE_DELETE")
		{
			if (split.size() < 5) {
				sendPacket(ERROR1);
				return;
			}

			std::string userName = split[1];
			GameId gameId = identifyGame(split[2]);
			int handleIndx = atoi(split[3].c_str());

			if (deleteHandle(gameId, userName, handleIndx))
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
		INFO_LOG(GameId::Unknown, "gate: New connection from %s", newConnection->getSocket().remote_endpoint().address().to_string().c_str());
		newConnection->receive();
	}
	start();
}
