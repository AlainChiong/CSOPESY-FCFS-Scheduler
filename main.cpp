#include <iostream>
#include <vector>
#include <queue>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <fstream>
#include <format>
#include <atomic>
#include <sstream>


struct Process {
    std::string name;
    std::string arrival_time;
    int total_commands = 100;
    std::atomic<int> completed_commands{0};
    int assigned_core = -1;
    bool is_finished = false;
    

    std::vector<std::string> log_lines;
    std::mutex log_mtx;
};


std::string get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto zoned = std::chrono::zoned_time{std::chrono::current_zone(), now};
    // Format matching: MM/DD/YYYY HH:MM:SS(AM/PM)
    return std::format("{:%m/%d/%Y %I:%M:%S%p}", zoned);
}


std::vector<std::shared_ptr<Process>> all_processes;
std::queue<std::shared_ptr<Process>> ready_queue;
std::mutex queue_mtx;
std::condition_variable_any queue_cv;

std::mutex print_mtx; // Prevents console output interleaving


void cpu_core_worker(std::stop_token stop_tok, int core_id) {
    while (!stop_tok.stop_requested()) {
        std::shared_ptr<Process> proc = nullptr;

        {
            std::unique_lock<std::mutex> lock(queue_mtx);
            queue_cv.wait(lock, stop_tok, [&] { 
                return !ready_queue.empty() || stop_tok.stop_requested(); 
            });

            if (stop_tok.stop_requested() && ready_queue.empty()) {
                break;
            }

            if (!ready_queue.empty()) {
                proc = ready_queue.front();
                ready_queue.pop();
            }
        }

        if (proc) {
            proc->assigned_core = core_id;

    
            while (proc->completed_commands < proc->total_commands) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Simulate work burst
                
                int current_cmd = ++proc->completed_commands;
                std::string ts = get_current_timestamp();
                std::string log_entry = std::format("({}) Core:{} \"Hello world from {}!\"", ts, core_id, proc->name);
                
                {
                    std::lock_guard<std::mutex> log_lock(proc->log_mtx);
                    proc->log_lines.push_back(log_entry);
                }
            }

            proc->is_finished = true;

            // Generate the temporary homework file per instructions
            std::ofstream out_file(proc->name + ".txt");
            if (out_file.is_open()) {
                out_file << "Process name: " << proc->name << "\nLogs:\n\n";
                std::lock_guard<std::mutex> log_lock(proc->log_mtx);
                for (const auto& line : proc->log_lines) {
                    out_file << line << "\n";
                }
            }
        }
    }
}



void display_screen_ls() {
    std::lock_guard<std::mutex> lock(print_mtx);
    std::cout << "\n-----------------------------------------\n";
    std::cout << "Running processes:\n";
    for (const auto& proc : all_processes) {
        if (!proc->is_finished && proc->assigned_core != -1) {
            std::cout << std::format("{:<10} ({})   Core: {}    {} / {}\n", 
                proc->name, proc->arrival_time, proc->assigned_core, 
                proc->completed_commands.load(), proc->total_commands);
        }
    }

    std::cout << "\nFinished processes:\n";
    for (const auto& proc : all_processes) {
        if (proc->is_finished) {
            std::cout << std::format("{:<10} ({})   Finished    {} / {}\n", 
                proc->name, proc->arrival_time, 
                proc->completed_commands.load(), proc->total_commands);
        }
    }
    std::cout << "-----------------------------------------\n";
}


int main() {
    const int NUM_CORES = 4;
    const int INITIAL_PROCESSES = 10;

    for (int i = 1; i <= INITIAL_PROCESSES; ++i) {
        auto proc = std::make_shared<Process>();
        proc->name = std::format("process{:02d}", i);
        proc->arrival_time = get_current_timestamp();
        
        all_processes.push_back(proc);
        {
            std::lock_guard<std::mutex> lock(queue_mtx);
            ready_queue.push(proc);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5)); 
    }


    std::vector<std::jthread> cpu_cores;
    for (int i = 0; i < NUM_CORES; ++i) {
        cpu_cores.emplace_back(cpu_core_worker, i);
    }
    
    
    queue_cv.notify_all();

    std::cout << "OS Emulator Started. 10 processes initialized.\n";
    std::cout << "Commands available: 'screen -ls', 'exit'\n";

    
    std::string input;
    while (true) {
        std::cout << "\n> ";
        if (!std::getline(std::cin, input)) break;

        if (input == "screen -ls") {
            display_screen_ls();
        } 
        else if (input == "exit") {
            std::cout << "Exiting emulator...\n";
            break;
        } 
        else if (!input.empty()) {
            std::cout << "Unknown command.\n";
        }
    }


    for (auto& core : cpu_cores) {
        core.request_stop();
    }
    queue_cv.notify_all(); 

    return 0;
}