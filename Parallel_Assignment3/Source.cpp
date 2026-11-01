#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <mutex>
#include <algorithm>
#include <cctype>
#include "ThreadPool.h"

#define FILE_PATH		"Hamlet.txt"
#define POOL_MULT		4


// ====================== PARALLEL SECTION ====================== 
std::vector<int> chunk_data(size_t numLines, int chunkCount) {	
	(int)numLines;

	int size = numLines / chunkCount;
	int r = numLines % chunkCount;

	std::vector<int> chunks(chunkCount, size);

	for (int i = 0; i < r; i++) chunks[i]++;

	return chunks;
}

void count_target_words_in_file(int start, int end, 
								const std::vector<std::string>& targets, 
								std::vector<int>& counters,
								const std::vector<std::string>& lines,
								std::mutex& mutex) {

	std::vector<int> localCounters(targets.size(), 0); // horatio, and, hamlet, god

	// Process the line
	for (int i = start; i < end; i++) {
		std::string line = lines[i];

		// CLEAN EDGE CASE
		size_t pos = 0;
		while ((pos = line.find("--", pos)) != std::string::npos) {
			line.replace(pos, 2, " ");
		}
		
		// SPLIT
		std::istringstream stream(line);
		std::string word;

		while (stream >> word) {

			// CLEAN
			std::erase_if(word, [](unsigned char c) { 
				return std::ispunct(c); 
				});

			std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) { 
				return std::tolower(c); 
				});

			// COUNT
			for (size_t target = 0; target < targets.size(); target++) {
				if (word == targets[target]) {
					localCounters[target]++;
					break;
				}
			}
		}
	}

	// WRITE
	{
		std::scoped_lock<std::mutex> lock(mutex);
		for (size_t i = 0; i < counters.size(); i++) {
			counters[i] += localCounters[i];
		}
	}
}

void count_all_words_in_file(int start, int end, int& result,
							 const std::vector<std::string>& lines) {
	int localResult = 0;

	// Process the line
	for (int i = start; i < end; i++) {
		std::string line = lines[i];

		// CLEAN EDGE CASE
		size_t pos = 0;
		while ((pos = line.find("--", pos)) != std::string::npos) {
			line.replace(pos, 2, " ");
		}

		// SPLIT
		std::istringstream stream(line);
		std::string word;

		while (stream >> word) {
			// COUNT (ignore 't and t')
			if (word != "'t" && word != "t'") {
				localResult++;
			}
		}
	}

	// WRITE - 1 write at the end due to the issue of false sharing
	result = localResult; 
}


// ---------------------- SEQUENTIAL SECTION ----------------------
void sequential_count_target_words_in_file(const std::vector<std::string>& targets, 
										   const std::vector<std::string>& lines,
										   std::vector<int>& counters) {

	// Process the line
	for (int i = 0; i < lines.size(); i++) {
		std::string line = lines[i];

		// CLEAN EDGE CASE
		size_t pos = 0;
		while ((pos = line.find("--", pos)) != std::string::npos) {
			line.replace(pos, 2, " ");
		}

		// SPLIT
		std::istringstream stream(line);
		std::string word;

		while (stream >> word) {

			// CLEAN
			std::erase_if(word, [](unsigned char c) {
				return std::ispunct(c);
				});

			std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) {
				return std::tolower(c);
				});

			// COUNT
			for (size_t target = 0; target < targets.size(); target++) {
				if (word == targets[target]) {
					counters[target]++;
					break;
				}
			}
		}
	}
}

void count_all_words_in_file(const std::vector<std::string>& lines, int& result) {
	// Process the line
	for (size_t i = 0; i < lines.size(); i++) {
		std::string line = lines[i];

		// CLEAN EDGE CASE
		size_t pos = 0;
		while ((pos = line.find("--", pos)) != std::string::npos) {
			line.replace(pos, 2, " ");
		}

		// SPLIT
		std::istringstream stream(line);
		std::string word;

		while (stream >> word) {
			// COUNT (ignore 't and t')
			if (word != "'t" && word != "t'") {
				result++;
			}
		}
	}
}


void display(const std::vector<int>& counters, int totalWords) {
	std::cout << counters[0] << " Horatio\n";
	std::cout << counters[1] << " and\n";
	std::cout << counters[2] << " Hamlet\n";
	std::cout << counters[3] << " God\n";
	std::cout << "Total word count = " << totalWords << std::endl;
}

void display_measures(const std::vector<int>& counters, int totalWords) {
	std::cout << counters[0] << " Horatio\n";
	std::cout << counters[1] << " and\n";
	std::cout << counters[2] << " Hamlet\n";
	std::cout << counters[3] << " God\n";
	std::cout << "Total word count = " << totalWords << std::endl;
}

int main() {
	unsigned int hardware = std::thread::hardware_concurrency();
	int threadCount = (hardware == 0) ? 4 : static_cast<int>(hardware);

	// TARGETS
	std::vector<std::string> targetWords = { "horatio", "and", "hamlet", "god" };

	// LOAD AND STORE DATA
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

	// PRE-PHASE - SEQUENTIAL IMPLEMENTATION 
	std::vector<int> seqTargetCounters(targetWords.size(), 0);
	int seqTotal = 0;

	sequential_count_target_words_in_file(std::cref(targetWords), std::cref(lines), std::ref(seqTargetCounters));
	count_all_words_in_file(std::cref(lines), seqTotal);


	// PHASE 1 - Targeted word count - Standard parallel threading
	std::vector<int> parTargetCounters(targetWords.size(), 0); // horatio, and, hamlet, god
	
	std::vector<int> p1Chunks = chunk_data(lines.size(), threadCount);
	
	std::vector<std::thread> threads = {};
	std::mutex counterLock;

	int p1Start = 0;
	for (int size : p1Chunks) {
		threads.emplace_back(count_target_words_in_file, p1Start, p1Start + size,
						std::cref(targetWords), std::ref(parTargetCounters),
						std::cref(lines), std::ref(counterLock));
		p1Start += size;
	}

	for (std::thread& thread : threads) {
		thread.join();
	}


	// PHASE 2 - Total word count - ThreadPooling
	std::vector<int> p2Chunks = chunk_data(lines.size(), threadCount * POOL_MULT);
	std::vector<int> results(p2Chunks.size(), 0);

	{
		ThreadPool pool((size_t)threadCount);

		int p2Start = 0;
		int i = 0;
		for (int size : p2Chunks) {
			int end = p2Start + size;

			// ENQUEUE --> Lambda captures this chunk's range and results slot index
			pool.enqueue([p2Start, end, i, &results, &lines] {
				count_all_words_in_file(p2Start, end, results[i], lines);
				});
			p2Start += size;
			i++;
		}
	}

	// SUM & DISPLAY
	int parTotal = 0;
	for (int r : results) {
		parTotal += r;
	}


	std::cout << "SEQUENTIAL\n";
	display(std::cref(seqTargetCounters), parTotal);

	std::cout << std::endl;

	std::cout << "PARALLEL\n";
	display(std::cref(parTargetCounters), seqTotal);

	file.close();
	return 0;
}