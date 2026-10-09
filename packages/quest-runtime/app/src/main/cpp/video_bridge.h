#pragma once
#include <atomic>
#include <thread>
#include <mutex>
#include <string>
#include <set>
#include <vector>
#include <future>
class VideoBridge {
 int server=-1;std::string path;std::thread worker;
 std::set<int> clients;std::vector<std::future<void>> sessions;
 std::atomic<bool> stopping{false};std::mutex lock;
 void serve();
 void serveClient(int connection);
public:
 void initialize(const std::string& directory);
 void shutdown();
};
