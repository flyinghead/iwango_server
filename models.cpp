#include "models.h"
#include "lobby_server.h"

std::vector<LobbyServer *> LobbyServer::servers;

void Lobby::addPlayer(std::shared_ptr<Player> player)
{
	members.push_back(player);

	// Confirm Join Lobby
	player->send(0x13, player->fromUtf8(name) + " " + player->fromUtf8(player->name));

	// Send player info to all members
	for (auto& p : members)
	{
		if (p == player)
			continue;
		p->send(0x30, player->getSendDataPacket());
	}
}

void Lobby::removePlayer(std::shared_ptr<Player> player)
{
	for (auto it = members.begin(); it != members.end(); ++it)
	{
		if ((*it) == player)
		{
			// Remove player from list
			members.erase(it);

			// Confirm Leave Lobby
			player->send(0xCB);

			// Tell all members to remove the player
			for (auto& p : members)
				p->send(0x2C, p->fromUtf8(player->name));
			return;
		}
	}
	fprintf(stderr, "Player %s not found in lobby %s\n", player->name.c_str(), name.c_str());
}

void Lobby::sendChat(const std::string& from, const std::string& message) {
	for (auto& player : members)
		player->send(0x2D, player->fromUtf8(from) + " " + player->fromUtf8(message));
}

std::shared_ptr<Team> Lobby::createTeam(std::shared_ptr<Player> creator, const std::string& name, unsigned capacity, const std::string& type)
{
	Team::Ptr team = Team::create(shared_from_this(), name, capacity, creator);
	teams.push_back(team);
	creator->team = team;

	sstream ss;
	ss << creator->fromUtf8(name) << ' ' << creator->fromUtf8(creator->name) << ' ' << capacity << " 0 " << gameName;
	for (auto& p : members)
		p->send(0x28, ss.str());

	return team;
}
void Lobby::deleteTeam(std::shared_ptr<Team> team)
{
	for (auto it = teams.begin(); it != teams.end(); ++it)
	{
		if ((*it) == team) {
			teams.erase(it);
			break;
		}
	}
	// Tell all members to remove team
	for (auto& p : members)
		p->send(0x3A, p->fromUtf8(team->name));
}

std::shared_ptr<Team> Lobby::getTeam(const std::string& name)
{
	for (auto& team : teams)
		if (team->name == name)
			return team;
	return nullptr;
}

std::string Player::getIp()
{
	if (connection == nullptr)
		return "0.0.0.0";
	else
		return connection->getSocket().remote_endpoint().address().to_string();
}
uint32_t Player::getIpUint32()
{
	if (connection == nullptr)
		return 0;
	else
		return connection->getSocket().remote_endpoint().address().to_v4().to_ulong();
}
std::array<uint8_t, 4> Player::getIpBytes()
{
	if (connection == nullptr)
		return {};
	else
		return connection->getSocket().remote_endpoint().address().to_v4().to_bytes();
}
uint16_t Player::getPort()
{
	if (connection == nullptr)
		return 0;
	else
		return connection->getSocket().remote_endpoint().port();
}

void Player::disconnect(bool sendDCPacket)
{
	if (disconnected)
		return;
	disconnected = true;

	// Tell client to d/c if actually still connected
	if (sendDCPacket)
		send(0x17);

	// Remove player from everything
	if (team) {
		team->removePlayer(shared_from_this());
		team.reset();
	}
	if (lobby) {
		lobby->removePlayer(shared_from_this());
		lobby.reset();
	}
	server.removePlayer(shared_from_this());

	// Close if need be
	if (connection)
		connection->close();
		// this might have been deleted at this point
}

void Player::setSharedMem(const std::vector<uint8_t>& data)
{
	if (data.size() != 0x1e) {
		fprintf(stderr, "WARN: invalid player sharedMem size: %zd. Ignored\n", data.size());
		return;
	}
	memcpy(sharedMem.data(), &data[0], data.size());
	if (team)
		team->sendSharedMemPlayer(shared_from_this(), sharedMem);
}

std::vector<uint8_t> Player::getSendDataPacket()
{
	sstream ss;
	if (lobby)
		ss << fromUtf8(lobby->name) << ' ';
	else
		ss << "# ";
	if (team && team->host == shared_from_this())
		ss << '*';
	ss << fromUtf8(name) << ' ' << flags << ' ';
	if (team)
		ss << '*' << fromUtf8(team->name);
	else
		ss << '#';
	ss << " *" << server.getGameName();
	std::string strData = ss.str();

	std::vector<uint8_t> data(1 + strData.length() + 1 + sharedMem.size() + 4);
	size_t idx = 0;
	data[idx++] = strData.length();
	memcpy(&data[idx], &strData[0], strData.length());
	idx += strData.length();
	data[idx++] = 1;
	memcpy(&data[idx], &sharedMem[0], sharedMem.size());
	idx += sharedMem.size();
	memcpy(&data[idx], getIpBytes().data(), 4);

	return data;
}

void Player::createTeam(const std::string& name, unsigned capacity, const std::string& type)
{
	if (lobby != nullptr)
	{
		if (lobby->getTeam(name) == nullptr)
		{
			Team::Ptr newTeam = lobby->createTeam(shared_from_this(), name, capacity, type);
			team = newTeam;
			return;
		}
	}
	fprintf(stderr, "WARN: createTeam: team %s already exists\n", name.c_str());
	send(0x03); // Name already in use
}
void Player::joinTeam(const std::string& name)
{
	if (lobby != nullptr)
	{
		Team::Ptr team = lobby->getTeam(name);
		if (team == nullptr) {
			fprintf(stderr, "WARN: joinTeam: team %s not found\n", name.c_str());
			return; // TODO Some Error, team didn't exist
		}

		if (team->addPlayer(shared_from_this()))
			this->team = team;
	}
	else {
		fprintf(stderr, "WARN: joinTeam: user %s not any lobby\n", this->name.c_str());
		// TODO Some Error
	}
}
void Player::leaveTeam()
{
	if (lobby != nullptr && team != nullptr) {
		team->removePlayer(shared_from_this());
		this->team = nullptr;
	}
	else {
		fprintf(stderr, "WARN: leaveTeam: user %s not any lobby or team\n", this->name.c_str());
		// TODO Some Error
	}
}

void Player::sendExtraMem(const uint8_t* extraMem, int offset, int length)
{
	std::vector<uint8_t> payload(length + 2);
	*(uint16_t *)&payload[0] = (uint16_t)length;
	memcpy(&payload[2], extraMem + offset, length);

	send(0x50);
	send(0x51, payload);
	send(0x52);
}

int Player::send(uint16_t opcode, const uint8_t *payload, unsigned length)
{
	if (connection == nullptr) {
		fprintf(stderr, "WARNING: player %s has a null connection\n", name.c_str());
		return 0;
	}
	std::vector<uint8_t> data = makePacket(opcode, payload, length);
	connection->send(data);
	return data.size();
}

std::vector<uint8_t> Player::makePacket(uint16_t opcode, const uint8_t *payload, unsigned length)
{
	unsigned size = length + 2;
	std::vector<uint8_t> data(size + 2);
	// Size
	data[0] = size;
	data[1] = size >> 8;
	// Opcode
	data[2] = opcode;
	data[3] = opcode >> 8;
	// Payload
	memcpy(&data[4], payload, length);
	return data;
}

void Player::receive(uint16_t opcode, const std::vector<uint8_t> payload) {
	PacketProcessor::handlePacket(shared_from_this(), opcode, payload);
}

std::string Player::toUtf8(const std::string& str) const {
	return sjisToUtf8(str);
}

std::string Player::fromUtf8(const std::string& str) const {
	return utf8ToSjis(str, gameId == GameId::GolfShiyouyo);
}
