#pragma once
#include "shared_this.h"
#include "asio.h"
#include <stdio.h>
#include <vector>

class Player;

class LobbyConnection : public SharedThis<LobbyConnection>
{
public:
	asio::ip::tcp::socket& getSocket() {
		return socket;
	}
	void setPlayer(std::shared_ptr<Player> player) {
		this->player = player;
	}

	void receive() {
		recvBuffer.clear();	// FIXME do we have to handle more than 1 msg per buffer?
		asio::async_read_until(socket, asio::dynamic_vector_buffer(recvBuffer), packetMatcher,
				std::bind(&LobbyConnection::onReceive, shared_from_this(), asio::placeholders::error, asio::placeholders::bytes_transferred));
	}

	void send(const std::vector<uint8_t>& data)
	{
		memcpy(&sendBuffer[sendIdx], data.data(), data.size());
		sendIdx += data.size();
		send();
	}

	void close();

private:
	LobbyConnection(asio::io_context& io_context)
		: io_context(io_context), socket(io_context), timer(io_context) {
	}

	void send()
	{
		if (sending)
			return;
		sending = true;
		uint16_t packetSize = *(uint16_t *)&sendBuffer[0] + 2;
		asio::async_write(socket, asio::buffer(sendBuffer, packetSize),
			std::bind(&LobbyConnection::onSent, shared_from_this(),
					asio::placeholders::error,
					asio::placeholders::bytes_transferred));
	}
	void onSent(const std::error_code& ec, size_t len);
	void onTimeOut(const std::error_code& ec);

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

	void onReceive(const std::error_code& ec, size_t len);

	void sendPacket(uint16_t opcode, const std::string& payload = {})
	{
		*(uint16_t *)&sendBuffer[sendIdx] = payload.size() + 2;
		*(uint16_t *)&sendBuffer[sendIdx + 2] = opcode;
		memcpy(&sendBuffer[sendIdx + 4], payload.data(), payload.length());
		sendIdx += 4 + payload.length();
		send();
	}

	asio::io_context& io_context;
	asio::ip::tcp::socket socket;
	asio::steady_timer timer;
	std::vector<uint8_t> recvBuffer;
	std::array<uint8_t, 8500> sendBuffer;
	size_t sendIdx = 0;
	bool sending = false;
	std::shared_ptr<Player> player;

	friend super;
};

class PacketProcessor
{
public:
	static void handlePacket(std::shared_ptr<Player> player, uint16_t opcode, const std::vector<uint8_t>& payload);
};
