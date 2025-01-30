#include "lobby_server.h"
#include "gate_server.h"
#include "models.h"

static asio::io_context io_context;

void LobbyConnection::onReceive(const std::error_code& ec, size_t len)
{
	if (ec || len == 0)
	{
		if (ec && ec != asio::error::eof)
			fprintf(stderr, "ERROR: onReceive: %s\n", ec.message().c_str());
		else
			printf("Connection closed\n");
		if (player)
			player->disconnect(false);
		return;
	}
	// Grab data and process if correct.
	//uint16_t unk1 = *(uint16_t *)&recvBuffer[2];
	uint16_t sequence = *(uint16_t *)&recvBuffer[4];
	//uint16_t unk2 = *(uint16_t *)&recvBuffer[6];
	uint16_t opcode = *(uint16_t *)&recvBuffer[8];
	std::vector<uint8_t> payload(&recvBuffer[10], &recvBuffer[len]);
	printf("lobby: Request[%d]: %04x [%s]\n", sequence, opcode, std::string((char *)&payload[0], payload.size()).c_str());
	player->receive(opcode, payload);
	receive();
}

void LobbyConnection::onSent(const std::error_code& ec, size_t len)
{
	if (ec)
	{
		fprintf(stderr, "ERROR: onSent: %s\n", ec.message().c_str());
		if (player)
			player->disconnect(false);
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

class LobbyServer : public SharedThis<LobbyServer>
{
public:
	void start()
	{
		LobbyConnection::Ptr newConnection = LobbyConnection::create(io_context);

		acceptor.async_accept(newConnection->getSocket(),
				std::bind(&LobbyServer::handleAccept, shared_from_this(), newConnection, asio::placeholders::error));
	}

private:
	LobbyServer(asio::io_context& io_context, uint16_t port)
		: io_context(io_context),
		  acceptor(asio::ip::tcp::acceptor(io_context,
				asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)))
	{
		asio::socket_base::reuse_address option(true);
		acceptor.set_option(option);
	}

	void handleAccept(LobbyConnection::Ptr newConnection, const std::error_code& error)
	{
		if (!error)
		{
			printf("New connection from %s\n", newConnection->getSocket().remote_endpoint().address().to_string().c_str());
			Player::Ptr player = Player::create(newConnection);
			newConnection->setPlayer(player);
			Server::instance().addPlayer(player);
			newConnection->receive();
		}
		start();
	}

	asio::io_context& io_context;
	asio::ip::tcp::acceptor acceptor;

	friend super;
};

static void breakhandler(int signum) {
	io_context.stop();
}

int main(int argc, char *argv[])
{
	struct sigaction sigact;
	memset(&sigact, 0, sizeof(sigact));
	sigact.sa_handler = breakhandler;
	sigact.sa_flags = SA_RESTART;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);

	printf("IWANGO Emulator: Gate Server by Ioncannon\n");
	GateServer::Ptr gateServer = GateServer::create(io_context, 9500);
	gateServer->start();

	printf("IWANGO Emulator: Lobby Server by Ioncannon\n");
	Server& server = Server::instance();
	server.addGame("Daytona");
	server.createLobby("2P_Red", 100);
	server.createLobby("4P_Yellow", 100);
	server.createLobby("2P_Blue", 100);
	server.createLobby("2P_Green", 100);
	server.createLobby("4P_Purple", 100);
	server.createLobby("4P_Orange", 100);

	LobbyServer::Ptr lobbyServer = LobbyServer::create(io_context, 9501);
	lobbyServer->start();

	io_context.run();

	printf("IWANGO Emulator: terminated\n");
}
