#include "sega_crypto.h"
#include <stdio.h>
#include <string>
#include <vector>
#include <unistd.h>

static int loadAndDecrypt(const std::string& path)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (f == nullptr) {
		perror(path.c_str());
		return -1;
	}
	fseek(f, 0, SEEK_END);
	size_t size = ftell(f);
	fseek(f, 0, SEEK_SET);
	std::vector<uint8_t> vms;
	vms.resize(size);
	if (fread(vms.data(), size, 1, f) != 1) {
		perror("read error");
		return -1;
	}
	fclose(f);
	if (vms.size() != VMS_SIZE) {
		fprintf(stderr, "Not a valid Daytona Key VMS file: wrong size\n");
		return -1;
	}
	std::string signature(&vms[0], &vms[0x10]);
	if (signature != "KEY DATA        ") {
		fprintf(stderr, "Not a valid Daytona Key VMS file: wrong signature\n");
		return -1;
	}
	SegaCrypto crypto;
	uint8_t *keyData = &vms[0x680];
	crypto.decrypt(keyData, 0x50);
	std::string username(keyData, keyData + 0x20);
	std::string ip(keyData + 0x20, keyData + 0x30);
	//username = username.Remove(username.IndexOf('\0'));
	//ip = ip.Remove(ip.IndexOf('\0'));
	printf("Loaded and decrypted key!\n->Username: %s\n->IP: %s\n", username.c_str(), ip.c_str());

	return 0;
}

static bool writeFile(const std::string& path, const std::vector<uint8_t> data)
{
	FILE *f = fopen(path.c_str(), "wb");
	if (f == nullptr) {
		perror(path.c_str());
		return false;
	}
	if (fwrite(&data[0], data.size(), 1, f) != 1) {
		perror("VMS write error");
		fclose(f);
		return false;
	}
	fclose(f);
	return true;
}

int main(int argc, char *argv[])
{
	std::string decryptPath;
	std::string encryptPath = ".";
	std::string userName;
	std::string password;
	std::string ip = "192.168.1.31";
	int opt;
	while ((opt = getopt(argc, argv, "d:u:p:i:o:")) != -1)
	{
		switch (opt)
		{
		case 'd':
			decryptPath = optarg;
			break;
		case 'u':
			userName = optarg;
			break;
		case 'p':
			password = optarg;
			break;
		case 'i':
			ip = optarg;
			break;
		case 'o':
			encryptPath = optarg;
			break;
		default:
			fprintf(stderr, "Usage: encrypt: %s -u <username> [-p <password>] [-i <ip address>] [-o <path>]\n decrypt: %s -d <path>\n", argv[0], argv[0]);
			exit(1);
		}
	}
	printf("Daytona USA KeyCutter by Ioncannon... creates online key files for the Dreamcast game\n");
	if (!decryptPath.empty())
		return loadAndDecrypt(decryptPath);

	if (userName.empty()) {
		fprintf(stderr, "Specify a user name\n");
		exit(1);
	}
	printf("Cutting you a new key...\n");
	std::vector<uint8_t> encryptedKey = cutKey(userName, ip);
	std::vector<uint8_t> vmsFile = generateVMS(encryptedKey);
	std::vector<uint8_t> vmiFile = generateVMI(vmsFile.size());
	if (!writeFile(encryptPath + "/DAYTKEY_.VMS", vmsFile))
		return 1;
	if (!writeFile(encryptPath + "/DAYTKEY_.VMI", vmiFile))
		return 1;
	printf("DAYTKEY_.VMS and DAYTKEY_.VMI generated.\n");

	return 0;
}
