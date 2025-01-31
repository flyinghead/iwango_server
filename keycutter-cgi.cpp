#include "sega_crypto.h"
#include "common.h"
#include <iostream>
#include <iomanip>
#include <stdio.h>
#include <stdlib.h>
#include <string>

using sstream = std::stringstream;

static int badParams()
{
	std::cout << "Status: 400 Bad parameters\n\n"
				 "<html><head><title>Bad request</title></head>"
				 "<body>Bad request</body></html>\n";
	return 1;
}

static std::string urlDecode(const std::string& in)
{
	sstream ss;
	for (unsigned i = 0; i < in.length(); i++)
	{
		if (in[i] == '%')
		{
			if (i + 2 >= in.length())
				break;
			std::string hex = in.substr(i + 1, i + 3);
			char c = strtoul(hex.c_str(), nullptr, 16);
			ss << c;
			i += 2;
		}
		else if (in[i] == '+') {
			ss << ' ';
		}
		else {
			ss << in[i];
		}
	}
	return ss.str();
}

static std::string urlEncode(const std::string& in)
{
	sstream ss;
	for (char c : in)
	{
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
				|| c == '-' || c == '.' || c == '_' || c == '~')
			ss << c;
		else
			ss << '%' << std::hex << std::uppercase << std::setw(2) << (uint32_t)(uint8_t)c;
	}
	return ss.str();
}

int main(int argc, char *argv[])
{
	const char *ipaddr = getenv("SERVER_ADDR");
	if (ipaddr == nullptr) {
		std::cout << "Status: 400 Bad parameters\n\n"
					 "<html><head><title>Bad request</title></head>"
					 "<body>Missing SERVER_ADDR</body></html>\n";
		return 1;
	}
	const char *queryString = getenv("QUERY_STRING");
	if (queryString == nullptr)
		return badParams();
	std::vector<std::string> params = splitString(queryString, '&');
	std::string username, file;
	for (const auto& param : params)
	{
		if (param.substr(0, 5) == "file=")
			file = param.substr(5);
		else if (param.substr(0, 9) == "username=")
			username = urlDecode(param.substr(9));
	}
	std::string vmi = std::string(KEY_FILENAME) + ".VMI";
	std::string vms = std::string(KEY_FILENAME) + ".VMS";
	if (username.empty())
		return badParams();
	if (file.empty()) {
		// Redirect to .VMI file
		std::cout << "Location: /" + urlEncode(username) + "/" + std::string(KEY_FILENAME) + ".VMI\n\n";
		return 0;
	}
	if (file == vmi)
	{
		// Deliver .VMI file
		std::vector<uint8_t> data = generateVMI(VMS_SIZE);
		std::cout << "Content-type: application/x-dreamcast-vms-info\n";
		std::cout << "Content-length: " << data.size() << "\n\n";
		fwrite(&data[0], data.size(), 1, stdout);
	}
	else if (file == vms)
	{
		// Deliver .VMS file
		std::vector<uint8_t> keyData = cutKey(username, ipaddr);
		std::vector<uint8_t> data = generateVMS(keyData);
		std::cout << "Content-type: application/x-dreamcast-vms\n";
		std::cout << "Content-length: " << data.size() << "\n\n";
		fwrite(&data[0], data.size(), 1, stdout);
	}
	else {
		return badParams();
	}
	return 0;
}
