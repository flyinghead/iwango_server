#pragma once
#include "common.h"
#include "shared_this.h"

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
