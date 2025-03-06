#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <queue>
#include <list>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <iomanip>
#include <limits>
#include <string>



// Structure to represent an I/O request
class IORequest {
public:
    unsigned int id;
    unsigned long arrival_time;
    unsigned int track;
    unsigned long start_time;
    unsigned long end_time;

    IORequest(unsigned int id, unsigned long arr_time, unsigned int trk)
        : id(id), arrival_time(arr_time), track(trk), start_time(0), end_time(0) {}
};

std::vector<IORequest*> parseInputFile(const std::string& filename, int& max_tracks) {
    std::ifstream infile(filename);
    if (!infile) {
        throw std::runtime_error("Unable to open input file: " + filename);
    }

    std::vector<IORequest*> requests;
    std::string line;
    unsigned int id = 0;
    max_tracks = 0;

    while (std::getline(infile, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        unsigned long arrival_time;
        unsigned int track;

        if (!(iss >> arrival_time >> track)) {
            throw std::runtime_error("Invalid input format in line: " + line);
        }

        if (track > std::numeric_limits<int>::max()) {
            throw std::runtime_error("Track number exceeds maximum allowed value: " + std::to_string(track));
        }

        requests.push_back(new IORequest(id++, arrival_time, track));
        max_tracks = std::max(max_tracks, static_cast<int>(track));
    }

    if (requests.empty()) {
        throw std::runtime_error("No valid I/O requests found in the input file");
    }

    return requests;
}



// Base class for I/O schedulers
class IOScheduler {
protected:
    std::list<IORequest*> queue;
public:
    virtual ~IOScheduler() {}
    virtual IORequest* getNextRequest(int current_track) = 0;
    virtual void addRequest(IORequest* request) { queue.push_back(request); }
    virtual bool isEmpty() { return queue.empty(); }
    virtual void printState(std::ostream& os) const {
        os << "Queue: [";
        for (const auto& req : queue) {
            os << req->id << ":" << req->track << " ";
        }
        os << "]";
    }

};

// FIFO Scheduler implementation
class FIFOScheduler : public IOScheduler {
public:
    IORequest* getNextRequest(int current_track) override {
        if (queue.empty()) return nullptr;
        IORequest* next = queue.front();
        queue.pop_front();
        return next;
    }
};

class SSTFScheduler : public IOScheduler {
public:
    IORequest* getNextRequest(int current_track) override {
        if (queue.empty()) return nullptr;
        auto closest = queue.begin();
        for (auto it = queue.begin(); it != queue.end(); ++it) {
            if (std::abs(static_cast<int>((*it)->track) - current_track) < 
                std::abs(static_cast<int>((*closest)->track) - current_track)) {
                closest = it;
            }
        }
        IORequest* next = *closest;
        queue.erase(closest);
        return next;
    }
};



class LOOKScheduler : public IOScheduler {
private:
    bool direction; // true for moving towards higher track numbers, false for lower

public:
    LOOKScheduler() : direction(true) {} // Start moving towards higher track numbers

    IORequest* getNextRequest(int current_track) override {
        if (queue.empty()) {
            return nullptr;
        }

        // Find the next request in the current direction
        auto next = std::find_if(queue.begin(), queue.end(),
            [this, current_track](const IORequest* req) {
                return (direction && req->track >= current_track) ||
                       (!direction && req->track <= current_track);
            });

        // If no requests in current direction, change direction and search again
        if (next == queue.end()) {
            direction = !direction;
            next = std::find_if(queue.begin(), queue.end(),
                [this, current_track](const IORequest* req) {
                    return (direction && req->track >= current_track) ||
                           (!direction && req->track <= current_track);
                });
        }

        // If still no request found (shouldn't happen if queue is not empty), return nullptr
        if (next == queue.end()) {
            return nullptr;
        }

        // Find the request closest to the current track in the current direction
        auto closest = next;
        for (auto it = next; it != queue.end(); ++it) {
            if ((direction && (*it)->track >= current_track && (*it)->track < (*closest)->track) ||
                (!direction && (*it)->track <= current_track && (*it)->track > (*closest)->track)) {
                closest = it;
            }
        }

        IORequest* request = *closest;
        queue.erase(closest);
        return request;
    }

    void addRequest(IORequest* request) override {
        queue.push_back(request);
        
    }
};

class CLOOKScheduler : public IOScheduler {
public:
    CLOOKScheduler() {} // Always moves towards higher track numbers

    IORequest* getNextRequest(int current_track) override {
        if (queue.empty()) {
            return nullptr;
        }

        auto lowest = queue.begin();
        auto next = queue.end();

        for (auto it = queue.begin(); it != queue.end(); ++it) {
            if ((*it)->track < (*lowest)->track) {
                lowest = it;
            }
            if ((*it)->track >= current_track && (next == queue.end() || (*it)->track < (*next)->track)) {
                next = it;
            }
        }

        if (next == queue.end()) {
            next = lowest;
        }

        IORequest* request = *next;
        queue.erase(next);
        return request;
    }

    void addRequest(IORequest* request) override {
        queue.push_back(request);
        
    }
};

class FLOOKScheduler : public IOScheduler {
private:
    std::list<IORequest*> active_queue;
    std::list<IORequest*> add_queue;
    bool direction;  // true for moving towards higher track numbers, false for lower

public:
    FLOOKScheduler() : direction(true) {}

    IORequest* getNextRequest(int current_track) override {
        if (active_queue.empty()) {
            if (add_queue.empty()) {
                return nullptr;
            }
            std::swap(active_queue, add_queue);
            // Don't change direction here
        }

        auto next = findNextRequest(current_track);

        if (next == active_queue.end()) {
            direction = !direction;
            next = findNextRequest(current_track);
        }

        if (next == active_queue.end()) {
            return nullptr;
        }

        IORequest* request = *next;
        active_queue.erase(next);
        return request;
    }

    void addRequest(IORequest* request) override {
        add_queue.push_back(request);
    }

    bool isEmpty() override {
        return active_queue.empty() && add_queue.empty();
    }

private:
    std::list<IORequest*>::iterator findNextRequest(int current_track) {
        auto best = active_queue.end();
        int min_distance = std::numeric_limits<int>::max();

        for (auto it = active_queue.begin(); it != active_queue.end(); ++it) {
            if ((direction && (*it)->track >= current_track) ||
                (!direction && (*it)->track <= current_track)) {
                int distance = std::abs(static_cast<int>((*it)->track) - current_track);
                if (distance < min_distance) {
                    min_distance = distance;
                    best = it;
                }
            }
        }

        return best;
    }
};

// Disk Simulator class
class DiskSimulator {
private:
    IOScheduler* scheduler;
    int current_track;
    unsigned long current_time;
    std::vector<IORequest*> all_requests;
    IORequest* active_request;
    unsigned long total_movement;
    unsigned long max_wait_time;
    unsigned long total_turnaround_time;
    unsigned long total_wait_time;
    unsigned long io_busy_time;
    bool verbose;
    bool option_f;
    bool option_q;
    int max_tracks;

public:
    DiskSimulator(IOScheduler* sched, bool verb = false, bool opt_f = false, bool opt_q = false) 
        : scheduler(sched), current_track(0), current_time(0), 
          active_request(nullptr), total_movement(0), max_wait_time(0),
          total_turnaround_time(0), total_wait_time(0), io_busy_time(0),
          verbose(verb), option_f(opt_f), option_q(opt_q), max_tracks(0) {
        if (sched == nullptr) {
            throw std::invalid_argument("Scheduler cannot be null");
        }
    }

    void addRequest(IORequest* request) {
        if (request == nullptr) {
            throw std::invalid_argument("Cannot add null request");
        }
        all_requests.push_back(request);
    }

    void setMaxTracks(int tracks) {
        if (tracks <= 0) {
            throw std::invalid_argument("Max tracks must be positive");
        }
        max_tracks = tracks;
    }

    void simulation() {
        try {
            if (verbose) {
                std::cout << "TRACE" << std::endl;
            }

            std::sort(all_requests.begin(), all_requests.end(), 
                      [](const IORequest* a, const IORequest* b) { return a->arrival_time < b->arrival_time; });

            auto next_req = all_requests.begin();

            while (true) {
                // Process new arrivals
                while (next_req != all_requests.end() && (*next_req)->arrival_time <= current_time) {
                    if (verbose) {
                        std::cout << current_time << ": " << (*next_req)->id 
                                  << " add " << (*next_req)->track << std::endl;
                    }
                    scheduler->addRequest(*next_req);
                    if (option_q) {
                        printQueueState();
                    }
                    next_req++;
                }

                if (!active_request) {
                    active_request = scheduler->getNextRequest(current_track);
                    if (active_request) {
                        active_request->start_time = std::max(current_time, active_request->arrival_time);
                        if (verbose) {
                            std::cout << active_request->start_time << ": " << active_request->id 
                                      << " issue " << active_request->track << " " << current_track << std::endl;
                        }
                    }
                }

                if (active_request) {
                    if (current_track == active_request->track) {
                        finishRequest();
                    } else {
                        moveHead();
                    }
                } else if (next_req == all_requests.end() && scheduler->isEmpty()) {
                    break;
                } else {
                    current_time++;
                }

                if (option_f && dynamic_cast<FLOOKScheduler*>(scheduler)) {
                    printFLOOKState();
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error during simulation: " << e.what() << std::endl;
            throw;
        }
    }

    void finishRequest() {
        if (active_request == nullptr) {
            throw std::runtime_error("No active request to finish");
        }
        active_request->end_time = current_time;
        if (verbose) {
            std::cout << current_time << ": " << active_request->id << " finish " 
                      << (current_time - active_request->arrival_time) << std::endl;
        }
        unsigned long turnaround_time = current_time - active_request->arrival_time;
        unsigned long wait_time = active_request->start_time - active_request->arrival_time;
        total_turnaround_time += turnaround_time;
        total_wait_time += wait_time;
        max_wait_time = std::max(max_wait_time, wait_time);
        active_request = nullptr;
    }

    void moveHead() {
        if (active_request == nullptr) {
            throw std::runtime_error("Cannot move head without active request");
        }
        int direction = (active_request->track > current_track) ? 1 : -1;
        current_track += direction;
        current_time++;
        total_movement++;
        io_busy_time++;
    }

    void printQueueState() {
        std::cout << "  Queue state: ";
        scheduler->printState(std::cout);
        std::cout << std::endl;
    }

    void printFLOOKState() {
        std::cout << "  FLOOK state: ";
        FLOOKScheduler* flook = dynamic_cast<FLOOKScheduler*>(scheduler);
        if (flook) {
            flook->printState(std::cout);
        }
        std::cout << std::endl;
    }

    void printSummary() {
        for (const auto& request : all_requests) {
            std::cout << std::setw(5) << request->id << ": " 
                      << std::setw(5) << request->arrival_time << " "
                      << std::setw(5) << request->start_time << " "
                      << std::setw(5) << request->end_time << std::endl;
        }

        if (all_requests.empty()) {
            std::cerr << "No requests processed" << std::endl;
            return;
        }

        double avg_turnaround = static_cast<double>(total_turnaround_time) / all_requests.size();
        double avg_wait_time = static_cast<double>(total_wait_time) / all_requests.size();
        double io_utilization = current_time > 0 ? static_cast<double>(io_busy_time) / current_time : 0.0;

        std::cout << "SUM: " << current_time << " " << total_movement << " "
                  << std::fixed << std::setprecision(4) << io_utilization << " "
                  << std::setprecision(2) << avg_turnaround << " " << avg_wait_time << " "
                  << max_wait_time << std::endl;
    }
};



// Function to create the appropriate scheduler based on command line argument
IOScheduler* createScheduler(char algo) {
    switch (algo) {
        case 'N': return new FIFOScheduler();
        case 'S': return new SSTFScheduler();
        case 'L': return new LOOKScheduler();
        case 'C': return new CLOOKScheduler();
        case 'F': return new FLOOKScheduler();
        default:
            throw std::invalid_argument("Invalid scheduler type: " + std::string(1, algo));
    }
}






int main(int argc, char* argv[]) {
    try {
        char scheduler_algo = 'N'; // Default to FIFO
        std::string input_file;
        bool verbose = false;
        bool option_f = false;
        bool option_q = false;

        // Parse command line arguments
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "-v") {
                verbose = true;
            } else if (arg == "-f") {
                option_f = true;
            } else if (arg == "-q") {
                option_q = true;
            } else if (arg.substr(0, 2) == "-s") {
                if (arg.length() == 3) {
                    scheduler_algo = arg[2];
                } else {
                    throw std::invalid_argument("Invalid scheduler option. Use -s<algo>.");
                }
            } else if (input_file.empty()) {
                input_file = arg;
            } else {
                throw std::invalid_argument("Unexpected argument: " + arg);
            }
        }

        if (input_file.empty()) {
            throw std::invalid_argument("Input file is required.");
        }

        std::unique_ptr<IOScheduler> scheduler(createScheduler(scheduler_algo));

        int max_tracks;
        std::vector<IORequest*> requests = parseInputFile(input_file, max_tracks);

        DiskSimulator simulator(scheduler.get(), verbose, option_f, option_q);

        for (const auto& request : requests) {
            simulator.addRequest(request);
        }

        simulator.setMaxTracks(max_tracks);

        simulator.simulation();
        simulator.printSummary();

        // Clean up
        for (auto request : requests) {
            delete request;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr << "Usage: " << argv[0] << " [-v] [-f] [-q] [-s<scheduler>] <input_file>" << std::endl;
        std::cerr << "Valid schedulers are: N (FIFO), S (SSTF), L (LOOK), C (CLOOK), F (FLOOK)" << std::endl;
        return 1;
    }

    return 0;
}