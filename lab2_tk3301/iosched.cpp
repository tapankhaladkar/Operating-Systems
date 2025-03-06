#include <iostream>
#include <fstream>
#include <queue>
#include <vector>
#include <string>
#include <getopt.h>
#include <cstdlib>
#include <cstring>
#include <stack>

// Process States
enum ProcessState {
    STATE_CREATED,
    STATE_READY,
    STATE_RUNNING,
    STATE_BLOCKED,
    STATE_FINISHED
};

class Event;
struct EventComparator {
    bool operator()(Event* e1, Event* e2);
};

class Process {
public:
    int pid;
    int arrival_time;
    int total_cpu_time;
    int cpu_burst;
    int io_burst;
    int static_priority;
    int dynamic_priority;
    
    int finish_time;
    int cpu_waiting_time;
    int io_time;
    int cpu_time_remaining;
    int current_cpu_burst;  // Track remaining burst time
    int state_ts;  // timestamp of last state change
    ProcessState state;
    
    Process(int _pid, int at, int tc, int cb, int io) {
        pid = _pid;
        arrival_time = at;
        total_cpu_time = tc;
        cpu_burst = cb;
        io_burst = io;
        cpu_time_remaining = tc;
        current_cpu_burst = 0;
        static_priority = 0;  // Will be set using myrandom
        dynamic_priority = 0;
        state = STATE_CREATED;
        finish_time = cpu_waiting_time = io_time = state_ts = 0;
    }
};

// Event types and Event class
enum Transition {
    TRANS_TO_READY,
    TRANS_TO_RUN,
    TRANS_TO_BLOCK,
    TRANS_TO_PREEMPT
};

class Event {
public:
    int timestamp;
    Process* process;
    Transition transition;
    
    Event(int ts, Process* p, Transition trans) : 
        timestamp(ts), process(p), transition(trans) {}
};

bool EventComparator::operator()(Event* e1, Event* e2) {
    if (e1->timestamp == e2->timestamp) {
        if (e1->process && e2->process) {
            return e1->process->pid > e2->process->pid;
        }
        return e1 > e2;
    }
    return e1->timestamp > e2->timestamp;
}

// Event Queue
class EventQueue {
private:
    std::priority_queue<Event*, std::vector<Event*>, EventComparator> events;
    
public:
    void add_event(Event* evt) {
        events.push(evt);
    }
    
    Event* get_next_event() {
        if (events.empty()) return nullptr;
        Event* evt = events.top();
        events.pop();
        return evt;
    }
    
    Event* peek() {
        if (events.empty()) return nullptr;
        return events.top();
    }

    bool empty() const {
        return events.empty();
    }
};

// Base Scheduler class
class Scheduler {
protected:
    int quantum;
    int maxprio;
    
public:
    Scheduler(int q = 10000, int mp = 4) : quantum(q), maxprio(mp) {}
    virtual ~Scheduler() {}
    
    virtual void add_process(Process* p) = 0;
    virtual Process* get_next_process() = 0;
    virtual bool test_preempt(Process* p, Process* current_running, Event* next_event, int current_time) { 
    return false; 
}
    virtual int get_quantum() { return quantum; }
    virtual std::string get_name() = 0;
    
    int get_max_prio() { return maxprio; }
};


class FCFSScheduler : public Scheduler {
private:
    std::queue<Process*> runqueue;
    
public:
    FCFSScheduler() : Scheduler() {}
    
    void add_process(Process* p) override {
        runqueue.push(p);
    }
    
    Process* get_next_process() override {
        if (runqueue.empty()) {
            return nullptr;
        }
        
        Process* next_process = runqueue.front();
        runqueue.pop();
        
        return next_process;
    }
    
    std::string get_name() override {
        return "FCFS";
    }
    
   
    
    
}; 

class LCFSScheduler : public Scheduler {
private:
    std::stack<Process*> runstack;  
    
public:
    LCFSScheduler() : Scheduler() {}
    
    void add_process(Process* p) override {
        runstack.push(p);
    }
    
    Process* get_next_process() override {
        if (runstack.empty()) {
            return nullptr;
        }
        
        Process* next_process = runstack.top();
        runstack.pop();
        
        return next_process;
    }
    
    std::string get_name() override {
        return "LCFS";
    }
};
class SRTFScheduler : public Scheduler {
private:
    struct SRTFComparator {
        bool operator()(const Process* p1, const Process* p2) const {
            if (p1->cpu_time_remaining == p2->cpu_time_remaining) {
                return p1->pid > p2->pid;
            }
            return p1->cpu_time_remaining > p2->cpu_time_remaining;
        }
    };
    
    std::priority_queue<Process*, std::vector<Process*>, SRTFComparator> runqueue;
    
public:
    SRTFScheduler() : Scheduler() {}
    
    void add_process(Process* p) override {
        runqueue.push(p);
    }
    
    Process* get_next_process() override {
        if (runqueue.empty()) {
            return nullptr;
        }
        
        Process* next_process = runqueue.top();
        runqueue.pop();
        return next_process;
    }
    
    std::string get_name() override {
        return "SRTF";
    }
     
};

class RRScheduler : public Scheduler {
private:
    std::queue<Process*> runqueue;
    
public:
    RRScheduler(int quantum) : Scheduler(quantum) {
        if (quantum <= 0) {
            throw std::invalid_argument("Quantum must be positive");
        }
    }
    
    void add_process(Process* p) override {
        p->dynamic_priority = p->static_priority - 1;
        
        runqueue.push(p);
    }
    
    Process* get_next_process() override {
        if (runqueue.empty()) {
            return nullptr;
        }
        
        Process* next = runqueue.front();
        runqueue.pop();
        return next;
    }
    
    bool test_preempt(Process* p, Process* current_running, Event* next_event, int current_time) override {
        return false;
    }
    
    std::string get_name() override {
        return "RR " + std::to_string(quantum);
    }
};

class PrioScheduler : public Scheduler {
private:
    struct QueueLevel {
        std::queue<Process*> processes;
    };
    
    std::vector<QueueLevel> activeQ;    
    std::vector<QueueLevel> expiredQ;  
    
public:
    PrioScheduler(int quantum, int maxprio = 4) : Scheduler(quantum, maxprio) {
        activeQ.resize(maxprio);
        expiredQ.resize(maxprio);
    }
    
    void add_process(Process* p) override {
        if (p->dynamic_priority < 0) {
            p->dynamic_priority = p->static_priority - 1;
            expiredQ[p->dynamic_priority].processes.push(p);
        } else {
            activeQ[p->dynamic_priority].processes.push(p);
        }
    }
    
    Process* get_next_process() override {
        for (int prio = maxprio - 1; prio >= 0; prio--) {
            if (!activeQ[prio].processes.empty()) {
                Process* next = activeQ[prio].processes.front();
                activeQ[prio].processes.pop();
                return next;
            }
        }
        
        if (has_expired_processes()) {
            activeQ.swap(expiredQ);
            for (int prio = maxprio - 1; prio >= 0; prio--) {
                if (!activeQ[prio].processes.empty()) {
                    Process* next = activeQ[prio].processes.front();
                    activeQ[prio].processes.pop();
                    return next;
                }
            }
        }
        
        return nullptr;
    }
    
    bool has_expired_processes() const {
        for (const auto& level : expiredQ) {
            if (!level.processes.empty()) {
                return true;
            }
        }
        return false;
    }
    
    std::string get_name() override {
        return "PRIO " + std::to_string(quantum);
    }
};

class PrePrioScheduler : public PrioScheduler {
public:
    PrePrioScheduler(int quantum, int maxprio = 4) : PrioScheduler(quantum, maxprio) {}
    
    bool test_preempt(Process* p, Process* current_running, Event* next_event, int current_time) override {
        if (!current_running) {
            return false;
        }
        
        if (p->dynamic_priority > current_running->dynamic_priority) {
            // Check if there's a pending event at the current time
            if (next_event && next_event->timestamp == current_time) {
                return false;
            }
            return true;
        }
        return false;
    }
    
    std::string get_name() override {
        return "PREPRIO " + std::to_string(quantum);
    }
};

// Discrete Event Simulator
class DES_Layer {
private:
    int CURRENT_TIME;
    EventQueue event_queue;
    Scheduler* scheduler;
    Process* CURRENT_RUNNING_PROCESS;
    bool CALL_SCHEDULER;
    std::vector<Process*> processes;
    std::vector<int> randvals;
    int rand_index;
    bool verbose;
    int processes_in_io;
    int total_cpu_time;
    int total_io_time;
    
    int get_next_event_time() {
        Event* next = event_queue.peek();
        return next ? next->timestamp : -1;
    }
    
    void update_waiting_time(Process* proc, int time_in_state) {
        if (proc->state == STATE_READY) {
            proc->cpu_waiting_time += time_in_state;
        }
    }

public:
    DES_Layer() : 
        CURRENT_TIME(0),
        scheduler(nullptr),
        CURRENT_RUNNING_PROCESS(nullptr),
        CALL_SCHEDULER(false),
        rand_index(0),
        verbose(false),
        processes_in_io(0),
        total_cpu_time(0),
        total_io_time(0) {}

    void set_verbose(bool v) { verbose = v; }
    void set_scheduler(Scheduler* s) { scheduler = s; }  
    
    int myrandom(int burst) {
        if (rand_index >= randvals.size()) {
            rand_index = 0;
        }
        return 1 + (randvals[rand_index++] % burst);
    }

    void read_rfile(const std::string& filename) {
        std::ifstream rfile(filename);
        if (!rfile.is_open()) {
            std::cerr << "Error: Cannot open random number file: " << filename << std::endl;
            exit(1);
        }

        int count;
        rfile >> count;
        randvals.reserve(count);
        
        int val;
        while (count-- > 0 && rfile >> val) {
            randvals.push_back(val);
        }

        if (count > 0) {
            std::cerr << "Error: Random file has fewer numbers than specified\n";
            exit(1);
        }

        rfile.close();
    }

    void read_input_file(const std::string& filename) {
        std::ifstream infile(filename);
        if (!infile.is_open()) {
            std::cerr << "Error: Cannot open input file: " << filename << std::endl;
            exit(1);
        }

        int pid = 0;
        int at, tc, cb, io;
        
        while (infile >> at >> tc >> cb >> io) {
            Process* proc = new Process(pid, at, tc, cb, io);
            proc->static_priority = myrandom(scheduler->get_max_prio());
            proc->dynamic_priority = proc->static_priority - 1;
            
            if (verbose) {
                std::cout << "Read process " << pid << ": "
                         << "arrival=" << at 
                         << " total_cpu=" << tc
                         << " cpu_burst=" << cb
                         << " io_burst=" << io
                         << " prio=" << proc->static_priority << std::endl;
            }
            
            processes.push_back(proc);
            add_event(at, proc, TRANS_TO_READY);
            pid++;
        }

        infile.close();
    }

    void add_event(int timestamp, Process* proc, Transition trans) {
        Event* evt = new Event(timestamp, proc, trans);
        if (verbose) {
            std::cout << "Event added: time=" << timestamp 
                     << " pid=" << proc->pid 
                     << " transition=" << trans << std::endl;
        }
        event_queue.add_event(evt);
    }

    void run_simulation() {
        Event* evt;
        int last_time = 0;

        while ((evt = event_queue.get_next_event())) {
            Process* proc = evt->process;
            
            CURRENT_TIME = evt->timestamp;
            int timeInPrevState = CURRENT_TIME - proc->state_ts;
            if (proc->state == STATE_READY) {
                proc->cpu_waiting_time += timeInPrevState;
            }
            
            if (processes_in_io > 0) {
                total_io_time += (CURRENT_TIME - last_time);
            }
            last_time = CURRENT_TIME;
            
            if (verbose) {
                std::cout << "Current Time: " << CURRENT_TIME 
                         << " Process: " << proc->pid 
                         << " Previous state time: " << timeInPrevState << std::endl;
            }
            
            Transition transition = evt->transition;
            delete evt;
            
            switch(transition) {
                case TRANS_TO_READY: {
                    if (proc->state == STATE_BLOCKED) {
                        proc->io_time += timeInPrevState;
                        processes_in_io--;
                        proc->dynamic_priority = proc->static_priority - 1;
                    }
                    
                    proc->state = STATE_READY;
                    proc->state_ts = CURRENT_TIME;

                    Event* next_event = event_queue.peek();
                    if (CURRENT_RUNNING_PROCESS && 
                        scheduler->test_preempt(proc, CURRENT_RUNNING_PROCESS, next_event, CURRENT_TIME)) {
                        if (next_event && next_event->process == CURRENT_RUNNING_PROCESS) {
                            event_queue.get_next_event();  // Remove the event
                            delete next_event;
                        }
                        
                        add_event(CURRENT_TIME, CURRENT_RUNNING_PROCESS, TRANS_TO_PREEMPT);
                    }

                    scheduler->add_process(proc);
                    CALL_SCHEDULER = true;
                    break;
                }
                
                case TRANS_TO_RUN: {
                    proc->state = STATE_RUNNING;
                    proc->state_ts = CURRENT_TIME;
                    
                    if (proc->current_cpu_burst == 0) {
                        proc->current_cpu_burst = myrandom(proc->cpu_burst);
                        if (proc->current_cpu_burst > proc->cpu_time_remaining) {
                            proc->current_cpu_burst = proc->cpu_time_remaining;
                        }
                    }
                    
                    int quantum = scheduler->get_quantum();
                    int remaining_burst = proc->current_cpu_burst;
    
                    if (dynamic_cast<RRScheduler*>(scheduler)) {
                        if (remaining_burst > quantum) {
                            add_event(CURRENT_TIME + quantum, proc, TRANS_TO_PREEMPT);
                        } else {
                            add_event(CURRENT_TIME + remaining_burst, proc, TRANS_TO_BLOCK);
                        }
                    } else {
                        if (remaining_burst > quantum) {
                            add_event(CURRENT_TIME + quantum, proc, TRANS_TO_PREEMPT);
                        } else {
                            add_event(CURRENT_TIME + remaining_burst, proc, TRANS_TO_BLOCK);
                        }
                    }
                    break;
                }
                
                case TRANS_TO_BLOCK: {
                    total_cpu_time += timeInPrevState;
                    proc->cpu_time_remaining -= proc->current_cpu_burst;
                    proc->current_cpu_burst = 0;
                    
                    if (proc->cpu_time_remaining <= 0) {
                        proc->state = STATE_FINISHED;
                        proc->finish_time = CURRENT_TIME;
                    } else {
                        proc->state = STATE_BLOCKED;
                        processes_in_io++;
                        int io_burst = myrandom(proc->io_burst);
                        add_event(CURRENT_TIME + io_burst, proc, TRANS_TO_READY);
                    }
                    
                    proc->state_ts = CURRENT_TIME;
                    CURRENT_RUNNING_PROCESS = nullptr;
                    CALL_SCHEDULER = true;
                    break;
                }
                
                case TRANS_TO_PREEMPT: {
                    total_cpu_time += timeInPrevState;
                    proc->cpu_time_remaining -= timeInPrevState;
                    proc->current_cpu_burst -= timeInPrevState;
                    
                    proc->state = STATE_READY;
                    proc->state_ts = CURRENT_TIME;
                    
                    if (dynamic_cast<RRScheduler*>(scheduler)) {
                        proc->dynamic_priority = proc->static_priority - 1;
                    } else {
                        proc->dynamic_priority--;
                        if (proc->dynamic_priority < 0) {
                            proc->dynamic_priority = proc->static_priority - 1;
                        }
                    }
                    
                    scheduler->add_process(proc);
                    CURRENT_RUNNING_PROCESS = nullptr;
                    CALL_SCHEDULER = true;
                    break;
                }
            }

            if (CALL_SCHEDULER) {
                if (get_next_event_time() == CURRENT_TIME) {
                    continue;
                }
                
                CALL_SCHEDULER = false;
                if (CURRENT_RUNNING_PROCESS == nullptr) {
                    CURRENT_RUNNING_PROCESS = scheduler->get_next_process();
                    if (CURRENT_RUNNING_PROCESS) {
                        add_event(CURRENT_TIME, CURRENT_RUNNING_PROCESS, TRANS_TO_RUN);
                    }
                }
            }
        }
    }

    void print_statistics() {
        std::cout << scheduler->get_name() << std::endl;

        for (Process* proc : processes) {
            printf("%04d: %4d %4d %4d %4d %1d | %5d %5d %5d %5d\n",
                proc->pid,
                proc->arrival_time,
                proc->total_cpu_time,
                proc->cpu_burst,
                proc->io_burst,
                proc->static_priority,
                proc->finish_time,
                proc->finish_time - proc->arrival_time,  // turnaround time
                proc->io_time,
                proc->cpu_waiting_time
            );
        }

        int last_finish_time = 0;
        double total_turnaround = 0;
        double total_cpu_wait = 0;
        
        for (Process* proc : processes) {
            last_finish_time = std::max(last_finish_time, proc->finish_time);
            total_turnaround += (proc->finish_time - proc->arrival_time);
            total_cpu_wait += proc->cpu_waiting_time;
        }

        double cpu_util = (total_cpu_time * 100.0) / last_finish_time;
        double io_util = (total_io_time * 100.0) / last_finish_time;
        double avg_turnaround = total_turnaround / processes.size();
        double avg_cpu_wait = total_cpu_wait / processes.size();
        double throughput = (processes.size() * 100.0) / last_finish_time;

        printf("SUM: %d %.2lf %.2lf %.2lf %.2lf %.3lf\n",
            last_finish_time,
            cpu_util,
            io_util,
            avg_turnaround,
            avg_cpu_wait,
            throughput
        );
    }
};


void show_usage() {
    std::cout << "Usage: ./sched [-vh] [-t] [-e] [-p] [-s<schedspec>] inputfile randfile\n";
    std::cout << "  -v: verbose output\n";
    std::cout << "  -h: show this help\n";
    std::cout << "  -t: trace scheduler events\n";
    std::cout << "  -e: show eventQ before/after\n";
    std::cout << "  -p: show preemption decisions\n";
    std::cout << "  -s schedspec: scheduler specification\n";
    std::cout << "    F|FCFS : First Come First Served\n";
    std::cout << "    L|LCFS : Last Come First Served\n";
    std::cout << "    S|SRTF : Shortest Remaining Time First\n";
    std::cout << "    R<num> : Round Robin with quantum=num\n";
    std::cout << "    P<num>[:<maxprio>] : Priority Scheduler\n";
    std::cout << "    E<num>[:<maxprio>] : Preemptive Priority Scheduler\n";
    exit(1);
}

Scheduler* create_scheduler(const std::string& spec) {
    if (spec.empty()) {
        std::cerr << "Error: Scheduler specification required\n";
        exit(1);
    }

    if (spec == "F" || spec == "FCFS") {
        return new FCFSScheduler();
    } else if (spec == "L" || spec == "LCFS") {
        return new LCFSScheduler();
    } else if (spec == "S" || spec == "SRTF") {
        return new SRTFScheduler();
    } else if (spec[0] == 'R') {
        int quantum;
        if (sscanf(spec.c_str() + 1, "%d", &quantum) == 1 && quantum > 0) {
            return new RRScheduler(quantum);
        }
    } else if (spec[0] == 'P') {
        int quantum, maxprio = 4;
        char* ptr = const_cast<char*>(spec.c_str() + 1);
        quantum = atoi(ptr);
        ptr = strchr(ptr, ':');
        if (ptr) {
            maxprio = atoi(ptr + 1);
        }
        if (quantum > 0 && maxprio > 0) {
            return new PrioScheduler(quantum, maxprio);
        }
    } else if (spec[0] == 'E') {  // Preemptive Priority
        int quantum, maxprio = 4;
        char* ptr = const_cast<char*>(spec.c_str() + 1);
        quantum = atoi(ptr);
        ptr = strchr(ptr, ':');
        if (ptr) {
            maxprio = atoi(ptr + 1);
        }
        if (quantum > 0 && maxprio > 0) {
            return new PrePrioScheduler(quantum, maxprio);
        }
    }
    
    std::cerr << "Error: Invalid or unsupported scheduler specification: " << spec << std::endl;
    exit(1);
}

int main(int argc, char* argv[]) {
    bool verbose = false;
    std::string sched_spec;
    
    int c;
    opterr = 0; 
    while ((c = getopt(argc, argv, "vhteps:")) != -1) {
        switch (c) {
            case 'v':
                verbose = true;
                break;
            case 'h':
                show_usage();
                break;
            case 't':  
                break;
            case 'e':  
                break;
            case 'p':  
                break;
            case 's':
                sched_spec = optarg;
                break;
            case '?':
                if (optopt == 's')
                    std::cerr << "Option -s requires a scheduler specification.\n";
                else
                    std::cerr << "Unknown option: " << char(optopt) << std::endl;
                show_usage();
            default:
                show_usage();
        }
    }

    if (sched_spec.empty()) {
        std::cerr << "Error: Scheduler specification required (-s option)\n";
        show_usage();
    }

    if (argc - optind < 2) {
        std::cerr << "Error: Missing input and/or random file\n";
        show_usage();
    }

    std::string input_file = argv[optind];
    std::string rand_file = argv[optind + 1];

    try {
        DES_Layer des;
        
        Scheduler* scheduler = create_scheduler(sched_spec);
        des.set_scheduler(scheduler);
        
        des.set_verbose(verbose);
        
        try {
            des.read_rfile(rand_file);
        } catch (const std::exception& e) {
            std::cerr << "Error reading random number file: " << e.what() << std::endl;
            delete scheduler;
            exit(1);
        }
        
        try {
            des.read_input_file(input_file);
        } catch (const std::exception& e) {
            std::cerr << "Error reading input file: " << e.what() << std::endl;
            delete scheduler;
            exit(1);
        }
        
        des.run_simulation();
        
        des.print_statistics();
        
        delete scheduler;
        
    } catch (const std::exception& e) {
        std::cerr << "Error during simulation: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}