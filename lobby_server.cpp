#include "lobby_server.h"
#include "gate_server.h"
#include "models.h"
#include "discord.h"
#include "database.h"
#include <fstream>
#include <unordered_map>

static asio::io_context io_context;
static std::unordered_map<std::string, std::string> Config;

void LobbyConnection::close()
{
	if (player)
		INFO_LOG(player->gameId, "[%s] Connection closed for %s", player->getIp().c_str(), player->name.c_str());
	asio::error_code ec;
	timer.cancel(ec);
	if (socket.is_open()) {
		socket.shutdown(asio::socket_base::shutdown_both, ec);
		socket.close(ec);
	}
	player.reset();
}

void LobbyConnection::onReceive(const std::error_code& ec, size_t len)
{
	if (ec || len < 10)
	{
		std::string addr;
		GameId gameId;
		if (player) {
			addr = player->getIp();
			gameId = player->gameId;
		}
		else {
			addr = "?.?.?.?";
			gameId = GameId::Unknown;
		}
		if (ec && ec != asio::error::eof && ec != asio::error::operation_aborted
				&& ec != asio::error::bad_descriptor)
			ERROR_LOG(gameId, "[%s] onReceive: %s", addr.c_str(), ec.message().c_str());
		else if (len != 0)
			ERROR_LOG(gameId, "[%s] onReceive: small packet: %zd", addr.c_str(), len);
		if (player)
			player->disconnect(false);
		return;
	}
	// Grab data and process if correct.
	uint16_t opcode = *(uint16_t *)&recvBuffer[8];
	std::vector<uint8_t> payload(&recvBuffer[10], &recvBuffer[len]);
#ifdef DEBUG
	//uint16_t unk1 = *(uint16_t *)&recvBuffer[2];
	uint16_t sequence = *(uint16_t *)&recvBuffer[4];
	//uint16_t unk2 = *(uint16_t *)&recvBuffer[6];
	std::string s((char *)&payload[0], payload.size());
	if (strlen(s.c_str()) != s.length())
	{
		std::string hexdump;
		for (uint8_t b : payload)
		{
			char hexbyte[3];
			sprintf(hexbyte, "%02x", b);
			hexdump += std::string(hexdump.empty() ? "" : " ") + std::string(hexbyte);
		}
		DEBUG_LOG(player->gameId, "Request[%d]: %04x [%s]", sequence, opcode, hexdump.c_str());
	}
	else {
		DEBUG_LOG(player->gameId, "Request[%d]: %04x [%s]", sequence, opcode, sjisToUtf8(s).c_str());
	}
#endif
	player->receive(opcode, payload);
	receive();
	timer.expires_at(asio::chrono::steady_clock::now() + asio::chrono::seconds(60));
	timer.async_wait(std::bind(&LobbyConnection::onTimeOut, shared_from_this(), asio::placeholders::error));
}

void LobbyConnection::onSent(const std::error_code& ec, size_t len)
{
	if (ec)
	{
		if (ec != asio::error::eof && ec != asio::error::bad_descriptor)
			ERROR_LOG(player ? player->gameId : GameId::Unknown, "onSent: %s", ec.message().c_str());
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

void LobbyConnection::onTimeOut(const std::error_code& ec)
{
	if (ec) {
		if (ec != asio::error::operation_aborted)
			ERROR_LOG(player ? player->gameId : GameId::Unknown, "onTimeout: %s", ec.message().c_str());
	}
	else if (player) {
		INFO_LOG(player->gameId, "[%s] Player %s time out", player->getIp().c_str(), player->name.c_str());
		auto lplayer = player;
		lplayer->disconnect(false);
	}
}

class LobbyAcceptor : public SharedThis<LobbyAcceptor>
{
public:
	void start()
	{
		LobbyConnection::Ptr newConnection = LobbyConnection::create(io_context);

		acceptor.async_accept(newConnection->getSocket(),
				std::bind(&LobbyAcceptor::handleAccept, shared_from_this(), newConnection, asio::placeholders::error));
	}

private:
	LobbyAcceptor(asio::io_context& io_context, LobbyServer& server)
		: io_context(io_context),
		  acceptor(asio::ip::tcp::acceptor(io_context,
				asio::ip::tcp::endpoint(asio::ip::tcp::v4(), server.getIpPort()))),
		  server(server)
	{
		asio::socket_base::reuse_address option(true);
		acceptor.set_option(option);
	}

	void handleAccept(LobbyConnection::Ptr newConnection, const std::error_code& error)
	{
		if (!error)
		{
			Player::Ptr player = Player::create(newConnection, server);
			INFO_LOG(player->gameId, "New connection from %s", newConnection->getSocket().remote_endpoint().address().to_string().c_str());
			newConnection->setPlayer(player);
			server.addPlayer(player);
			newConnection->receive();
		}
		start();
	}

	asio::io_context& io_context;
	asio::ip::tcp::acceptor acceptor;
	LobbyServer& server;

	friend super;
};

static void breakhandler(int signum) {
	io_context.stop();
}

static void loadConfig(const std::string& path)
{
	std::filebuf fb;
	if (!fb.open(path, std::ios::in)) {
		ERROR_LOG(GameId::Unknown, "config file %s not found", path.c_str());
		return;
	}

	std::istream istream(&fb);
	std::string line;
	while (std::getline(istream, line))
	{
		if (line.empty() || line[0] == '#')
			continue;
		auto pos = line.find_first_of("=:");
		if (pos != std::string::npos)
			Config[line.substr(0, pos)] = line.substr(pos + 1);
		else
			ERROR_LOG(GameId::Unknown, "config file syntax error: %s", line.c_str());
	}
}

std::string getConfig(const std::string& name, const std::string& default_value)
{
	auto it = Config.find(name);
	if (it == Config.end())
		return default_value;
	else
		return it->second;
}

int main(int argc, char *argv[])
{
	struct sigaction sigact;
	memset(&sigact, 0, sizeof(sigact));
	sigact.sa_handler = breakhandler;
	sigact.sa_flags = SA_RESTART;
	sigaction(SIGINT, &sigact, nullptr);
	sigaction(SIGTERM, &sigact, nullptr);
	setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);

	loadConfig(argc >= 2 ? argv[1] : "iwango.cfg");
	setDiscordWebhook(getConfig("DiscordWebhook", ""));
	setDatabasePath(getConfig("DatabasePath", ""));

	NOTICE_LOG(GameId::Unknown, "IWANGO Emulator: Gate Server by Ioncannon");
	GateServer::Ptr gateServer = GateServer::create(io_context, 9500);
	gateServer->start();

	NOTICE_LOG(GameId::Unknown, "IWANGO Emulator: Lobby Server by Ioncannon");
	LobbyServer daytonaServer(GameId::Daytona, getConfig("DaytonaServerName", "DCNet_Daytona"));
	daytonaServer.setMotd(getConfig("DaytonaMOTD", daytonaServer.getMotd()));
	LobbyAcceptor::Ptr daytonaAcceptor = LobbyAcceptor::create(io_context, daytonaServer);
	daytonaAcceptor->start();

	LobbyServer tetrisServer(GameId::Tetris, getConfig("TetrisServerName", "DCNet_Tetris"));
	tetrisServer.setMotd(getConfig("TetrisMOTD", tetrisServer.getMotd()));
	LobbyAcceptor::Ptr tetrisAcceptor = LobbyAcceptor::create(io_context, tetrisServer);
	tetrisAcceptor->start();

	LobbyServer golfServer(GameId::GolfShiyouyo, getConfig("GolfShiyou2ServerName", "DCNet_Golf_Shiyouyo_2"));
	golfServer.setMotd(getConfig("GolfShiyou2MOTD", golfServer.getMotd()));
	LobbyAcceptor::Ptr golfAcceptor = LobbyAcceptor::create(io_context, golfServer);
	golfAcceptor->start();

	LobbyServer aeroIServer(GameId::AeroDancingI, getConfig("AeroDancingServerName", "DCNet_Aero_Dancing"));
	aeroIServer.setMotd(getConfig("AeroDancingMOTD", aeroIServer.getMotd()));
	LobbyAcceptor::Ptr aeroIAcceptor = LobbyAcceptor::create(io_context, aeroIServer);
	aeroIAcceptor->start();

	LobbyServer aeroFServer(GameId::AeroDancingF, getConfig("AeroDancingServerName", "DCNet_Aero_Dancing"));
	aeroFServer.setMotd(getConfig("AeroDancingMOTD", aeroFServer.getMotd()));
	LobbyAcceptor::Ptr aeroFAcceptor = LobbyAcceptor::create(io_context, aeroFServer);
	aeroFAcceptor->start();

	LobbyServer swordsServer(GameId::HundredSwords, getConfig("HundredSwordsServerName", "DCNet"));
	swordsServer.setMotd(getConfig("HundredSwordsMOTD", swordsServer.getMotd()));
	LobbyAcceptor::Ptr swordsAcceptor = LobbyAcceptor::create(io_context, swordsServer);
	swordsAcceptor->start();

	io_context.run();

	NOTICE_LOG(GameId::Unknown, "IWANGO Emulator: terminated");
}
