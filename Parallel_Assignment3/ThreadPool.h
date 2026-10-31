#pragma once
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

class ThreadPool {
private:
	std::vector<std::thread> threads;

	std::queue<std::function<void()>> tasks;

	std::mutex poolLock;

	std::atomic<bool> stop;

	std::condition_variable condition;

	void workerThread() {
		while (true) { // Exits when stop is true and all tasks are gone
			std::function<void()> temp;

			{
				std::unique_lock<std::mutex> lock(poolLock);

				// Wait until stop is true, or if we have tasks to do
				condition.wait(lock, [this] { return stop || !tasks.empty(); });

				if (stop && tasks.empty()) {
					return;
				}

				temp = std::move(tasks.front());

				tasks.pop();
			}

			temp();
		}
	};

public:
	explicit ThreadPool(size_t numThread) {
		stop = false;

		for (size_t i = 0; i < numThread; i++) {
			threads.emplace_back(&ThreadPool::workerThread, this);
		}
	};

	void enqueue(std::function<void()> task) {
		{
			std::unique_lock<std::mutex> lock(poolLock);

			tasks.push(task);
		}

		condition.notify_one();
	};

	~ThreadPool() {
		{
			std::unique_lock<std::mutex> lock(poolLock);

			stop = true;
		}

		condition.notify_all();

		for (std::thread& t : threads) {
			t.join();
		}
	};
};
