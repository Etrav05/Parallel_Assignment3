#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include "ThreadPool.h"

#define FILE_PATH "Hamlet.txt"

int main() {
	
	std::ifstream file(FILE_PATH);

	if (!file.is_open()) {
		std::cerr << "[Error] File could not open" << std::endl;
		return 1;
	}

	// Push line by line into a vector
	std::string line;
	std::vector<std::string> lines;
	while (std::getline(file, line)) {
		lines.push_back(line);
	}

	file.close();
	return 0;
}