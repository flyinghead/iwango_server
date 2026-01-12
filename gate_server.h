#pragma once
#include <dcserver/asio.hpp>
#include <dcserver/shared_this.hpp>

class GateConnection;

class GateServer : public SharedThis<GateServer>
{
public:
	void start();

private:
	GateServer(asio::io_context& io_context, uint16_t port);
	void handleAccept(std::shared_ptr<GateConnection> newConnection, const std::error_code& error);

	asio::io_context& io_context;
	asio::ip::tcp::acceptor acceptor;

	friend super;
};
