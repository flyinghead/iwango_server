#pragma once
#include <string>
#include <vector>

class Database
{
public:
	std::string iwangoGetVerification(const std::string& daytonaHash) {
		return "joejoe";
	}
	int createHandle(const std::string& daytonaHash, const std::string& handlename) {
		return 1;
	}
	int replaceHandle(const std::string& daytonaHash, int handleIndx, const std::string& newHandleName) {
		return 1;
	}
	bool deleteHandle(const std::string& daytonaHash, int handleIndx) {
		return true;
	}
	std::string dreamPipeGetVerification(const std::string& daytonaHash) {
		return "joejoe2";
	}
	bool addUserIfMissing(const std::string& username, const std::string& daytonaHash) {
		return true;
	}
	const std::vector<std::string>& getHandles(const std::string& username) {
		return handles;
	}

private:
	std::vector<std::string> handles { "joejoe", "jack", "william" };
};
static Database database;
