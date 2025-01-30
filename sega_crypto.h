#include <stdint.h>
#include <array>
#include <vector>
#include <string>

constexpr const char *KEY_FILENAME = "DAYTKEY_";
constexpr unsigned VMS_SIZE = 0x800;

class SegaCrypto
{
	std::array<uint32_t, 18> P;
	std::array<std::array<uint32_t, 256>, 4> S;

	void encipherBlock(uint32_t& xl, uint32_t& xr);
	void decipherBlock(uint32_t& xl, uint32_t& xr);
	uint32_t F(uint32_t x);

public:
	SegaCrypto() : SegaCrypto("iloveosamu27") {}
	SegaCrypto(const std::string& key);
	void encrypt(uint8_t *data, int length);
	void decrypt(uint8_t *data, int length);

};

std::vector<uint8_t> cutKey(const std::string& username, const std::string& ip);
std::vector<uint8_t> generateVMI(int fileSize);
std::vector<uint8_t> generateVMS(const std::vector<uint8_t> keyData);
