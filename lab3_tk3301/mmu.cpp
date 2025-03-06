#include <iostream>
#include <fstream>
#include <vector>
#include <deque>
#include <cstring>
#include <sstream>
#include <unistd.h>
#include <getopt.h>
#include <utility>
#include <cstdio>
#include <climits>

using namespace std;

using std::cout;
using std::endl;

const int MAX_VPAGES = 64;       // Maximum virtual pages per process
const int MAX_FRAMES = 128;      // Maximum physical frames

class Process;  

std::vector<Process*> processes;
std::vector<int> random_numbers;
int rpos = 0;


int get_next_random() {
    if (rpos >= random_numbers.size()) {
        rpos = 0;
    }
    return random_numbers[rpos++];
}

struct pte_t {
    unsigned int present:1;     
    unsigned int referenced:1;  
    unsigned int modified:1;    
    unsigned int write_protect:1; 
    unsigned int pagedout:1;    
    unsigned int frame:7;       
    unsigned int file_mapped:1;
    unsigned int unused:19;     
};

struct frame_t {
    int pid = -1;        
    int vpage = -1;       
    unsigned int age = 0;  
    unsigned int last_used_time = 0;  
    bool mapped = false;
    int index;
};

frame_t frame_table[MAX_FRAMES];
std::deque<frame_t*> free_pool;

struct vma_t {
    int start_vpage;    
    int end_vpage;       
    bool write_protected; 
    bool file_mapped;    
};

struct pstats_t {
    unsigned long unmaps;
    unsigned long maps;
    unsigned long ins;
    unsigned long outs;
    unsigned long fins;
    unsigned long fouts;
    unsigned long zeros;
    unsigned long segv;
    unsigned long segprot;

    pstats_t() {
        memset(this, 0, sizeof(*this));
    }
};

// Process Control Block
class Process {
public:
    int pid;
    std::vector<vma_t> vmas;
    pte_t page_table[MAX_VPAGES];
    pstats_t stats;

    Process(int id) : pid(id) {
        // Initialize page table entries to 0
        memset(page_table, 0, sizeof(page_table));
    }

    // Check if virtual page is in any VMA
    bool is_valid_vpage(int vpage, bool& write_protected, bool& file_mapped) {
        for (const auto& vma : vmas) {
            if (vpage >= vma.start_vpage && vpage <= vma.end_vpage) {
                write_protected = vma.write_protected;
                file_mapped = vma.file_mapped;
                return true;
            }
        }
        return false;
    }
};

int current_process = 0;
unsigned long inst_count = 0;
unsigned long ctx_switches = 0;
unsigned long process_exits = 0;
unsigned long long cost = 0;


// Base Pager class
class Pager {
public:
    virtual frame_t* select_victim_frame() = 0;
    virtual void reset_age(frame_t* frame) {} 
    virtual ~Pager() {}
};

Pager* THE_PAGER;


class FIFOPager : public Pager {
    public:
    int hand = 0; 
    int num_frames;
    FIFOPager(int frames) : num_frames(frames) {}

    frame_t* select_victim_frame() override {
        frame_t* victim = &frame_table[hand];
        hand = (hand + 1) % num_frames;
        return victim;
    }
};


class RandomPager : public Pager {
private:
    int num_frames;

public:
    RandomPager(int frames) : num_frames(frames) {}

    frame_t* select_victim_frame() override {
        int victim_frame = get_next_random() % num_frames;
        return &frame_table[victim_frame];
    }
};


class ClockPager : public Pager {
private:
    int hand = 0;  // Points to the next frame to check
    int num_frames;

public:
    ClockPager(int frames) : num_frames(frames) {}

    frame_t* select_victim_frame() override {
        frame_t* victim = nullptr;
        
        while (true) {
            frame_t* frame = &frame_table[hand];
            Process* process = processes[frame->pid];
            pte_t* pte = &process->page_table[frame->vpage];

            if (!pte->referenced) {
                victim = frame;
                hand = (hand + 1) % num_frames;
                break;
            }

            pte->referenced = 0;  // Clear reference bit
            hand = (hand + 1) % num_frames;
        }

        return victim;
    }
};

class NRUPager : public Pager {
private:
    int hand = 0;
    int num_frames;
    unsigned int last_reset = 0;
    static const unsigned long RESET_INTERVAL = 48;

public:
    NRUPager(int frames) : num_frames(frames) {}

    frame_t* select_victim_frame() override {
        frame_t* victim = nullptr;
        int start_hand = hand;
        bool needs_reset = ((inst_count - last_reset) >= RESET_INTERVAL);
        
        // Array to store first frame found for each class
        frame_t* class_victims[4] = {nullptr, nullptr, nullptr, nullptr};

        do {
            frame_t* frame = &frame_table[hand];
            Process* process = processes[frame->pid];
            pte_t* pte = &process->page_table[frame->vpage];
            
            int class_num = (pte->referenced << 1) | pte->modified;
            
            // Remember first frame of each class
            if (class_victims[class_num] == nullptr) {
                class_victims[class_num] = frame;
                
                if (class_num == 0 && !needs_reset) {
                    victim = frame;
                    break;
                }
            }

            if (needs_reset) {
                pte->referenced = 0;
            }

            hand = (hand + 1) % num_frames;
        } while (hand != start_hand);

        if (victim == nullptr) {
            for (int i = 0; i < 4; i++) {
                if (class_victims[i] != nullptr) {
                    victim = class_victims[i];
                    break;
                }
            }
        }

        if (needs_reset) {
            last_reset = inst_count;
        }

        if (victim) {
            hand = ((victim - frame_table) + 1) % num_frames;
        }

        return victim;
    }
};


class AgingPager : public Pager {
private:
    int hand = 0;
    int num_frames;

public:
    AgingPager(int frames) : num_frames(frames) {}

    void reset_age(frame_t* frame) override {
        frame->age = 0;
    }

    frame_t* select_victim_frame() override {
        frame_t* victim = nullptr;
        unsigned int min_age = UINT_MAX;
        int start_hand = hand;
        
        int current = hand;
        do {
            frame_t* frame = &frame_table[current];
            Process* process = processes[frame->pid];
            pte_t* pte = &process->page_table[frame->vpage];

            frame->age = frame->age >> 1;
            if (pte->referenced) {
                frame->age = frame->age | 0x80000000;
                pte->referenced = 0;
            }

            current = (current + 1) % num_frames;
        } while (current != start_hand);

        current = hand;
        do {
            frame_t* frame = &frame_table[current];
            
            if (frame->age < min_age) {
                min_age = frame->age;
                victim = frame;
                hand = (current + 1) % num_frames;
            }

            current = (current + 1) % num_frames;
        } while (current != start_hand);

        if (!victim) {
            victim = &frame_table[hand];
            hand = (hand + 1) % num_frames;
        }
        
        return victim;
    }
};

class WorkingSetPager : public Pager {
private:
    int hand = 0;
    int num_frames;
    static const unsigned long TAU = 49;  
public:
    WorkingSetPager(int frames) : num_frames(frames) {}

    void reset_age(frame_t* frame) override {
        frame->last_used_time = inst_count;
    }

    frame_t* select_victim_frame() override {
        frame_t* victim = nullptr;
        int start_hand = hand;
        unsigned long max_age = 0;
        
        do {
            frame_t* frame = &frame_table[hand];
            Process* process = processes[frame->pid];
            pte_t* pte = &process->page_table[frame->vpage];
            
            unsigned long age = inst_count - frame->last_used_time;
            
            if (!pte->referenced && age > TAU) {
                victim = frame;
                hand = (hand + 1) % num_frames;
                break;
            }
            
            if (pte->referenced) {
                frame->last_used_time = inst_count;
                pte->referenced = 0;
            } else {
                if (age > max_age) {
                    max_age = age;
                    victim = frame;
                }
            }

            hand = (hand + 1) % num_frames;
        } while (hand != start_hand);

        if (!victim) {
            victim = &frame_table[start_hand];
            hand = (start_hand + 1) % num_frames;
        }

        hand = (victim - frame_table + 1) % num_frames;
        
        return victim;
    }
};

frame_t* allocate_frame_from_free_list();
frame_t* get_frame();
void read_input(const char* filename);
void read_random_file(const char* filename);
void simulate_instruction(char operation, int vpage);


void validate_frame_number(int num_frames) {
    if (num_frames <= 0 || num_frames > MAX_FRAMES) {
        std::cerr << "Invalid number of frames. Must be between 1 and " 
                  << MAX_FRAMES << std::endl;
        exit(1);
    }
}


Pager* create_pager(char algo, int num_frames) {
    switch (algo) {
        case 'f': 
            return new FIFOPager(num_frames);
        case 'r':
            return new RandomPager(num_frames);
        case 'c':
            return new ClockPager(num_frames);
        case 'e':
            return new NRUPager(num_frames);
        case 'a':
            return new AgingPager(num_frames);
        case 'w':
            return new WorkingSetPager(num_frames);
        default:
            std::cerr << "Unknown algorithm: " << algo << std::endl;
            exit(1);
    }
}




void handle_page_fault(Process* proc, pte_t* pte, int vpage, bool write_protected, bool file_mapped) {
    frame_t* newframe = get_frame();
    
    // If frame was in use, unmap it
    if (newframe->pid != -1) {
        Process* oldproc = processes[newframe->pid];
        pte_t* oldpte = &oldproc->page_table[newframe->vpage];
        
        std::cout << " UNMAP " << newframe->pid << ":" << newframe->vpage << std::endl;
        oldproc->stats.unmaps++;
        cost += 410;

        if (oldpte->modified) {
            bool old_write_protected, old_file_mapped;
            oldproc->is_valid_vpage(newframe->vpage, old_write_protected, old_file_mapped);
            
            if (old_file_mapped) {
                std::cout << " FOUT" << std::endl;
                oldproc->stats.fouts++;
                cost += 2800;
            } else {
                std::cout << " OUT" << std::endl;
                oldproc->stats.outs++;
                cost += 2750;
                oldpte->pagedout = 1;
            }
        }
        oldpte->present = 0;
        oldpte->referenced = 0;  // Clear reference bit when unmapping
    }

    // Reset PTE state before mapping
    pte->present = 0;
    pte->referenced = 0;
    pte->modified = 0;

    // Handle page content
    if (pte->pagedout) {
        std::cout << " IN" << std::endl;
        proc->stats.ins++;
        cost += 3200;
    } else if (file_mapped) {
        std::cout << " FIN" << std::endl;
        proc->stats.fins++;
        cost += 2350;
    } else {
        std::cout << " ZERO" << std::endl;
        proc->stats.zeros++;
        cost += 150;
    }

    std::cout << " MAP " << (int)(newframe - frame_table) << std::endl;
    proc->stats.maps++;
    cost += 350;

    // Update frame state
    newframe->pid = current_process;
    newframe->vpage = vpage;
    newframe->mapped = true;  // Set mapped flag

    // Update PTE state
    pte->frame = newframe - frame_table;
    pte->present = 1;
    pte->referenced = 1;  // Set reference bit for new mapping
    if (write_protected) pte->write_protect = 1;

    // Reset frame time/age
    THE_PAGER->reset_age(newframe);
}



void simulate_instruction(char operation, int vpage) {
    inst_count++;

    switch(operation) {
        case 'c': {  // Context switch
            current_process = vpage;
            ctx_switches++;
            cost += 130;  // Context switch cost
            break;
        }
        
        case 'e': {  // Process exit
            Process* proc = processes[current_process];
            process_exits++;
            cost += 1230;  // Process exit cost

            // Unmap all valid pages
            for (int i = 0; i < MAX_VPAGES; i++) {
                pte_t* pte = &proc->page_table[i];
                if (pte->present) {
                    // Handle frame cleanup
                    frame_t* frame = &frame_table[pte->frame];
                    
                    std::cout << " UNMAP " << frame->pid << ":" << frame->vpage << std::endl;
                    proc->stats.unmaps++;
                    cost += 410;  // Unmap cost

                    bool write_protected, file_mapped;
                    proc->is_valid_vpage(i, write_protected, file_mapped);
                    if (pte->modified && file_mapped) {
                        std::cout << " FOUT" << std::endl;
                        proc->stats.fouts++;
                        cost += 2800;  // FOUT cost
                    }

                    frame->pid = -1;
                    frame->vpage = -1;
                    
                    free_pool.push_back(frame);
                }
                pte->present = 0;
                pte->referenced = 0;
                pte->modified = 0;
                pte->pagedout = 0;
            }
            break;
        }

        case 'r':  
        case 'w': {  
            Process* proc = processes[current_process];
            pte_t* pte = &proc->page_table[vpage];
            cost += 1;  
            bool write_protected, file_mapped;
            if (!proc->is_valid_vpage(vpage, write_protected, file_mapped)) {
                std::cout << " SEGV" << std::endl;
                proc->stats.segv++;
                cost += 440;  
                break;
            }

            if (!pte->present) {
                handle_page_fault(proc, pte, vpage, write_protected, file_mapped);
            }

            pte->referenced = 1;

            if (operation == 'w') {
                if (write_protected) {
                    std::cout << " SEGPROT" << std::endl;
                    proc->stats.segprot++;
                    cost += 410;  
                } else {
                    pte->modified = 1;
                }
            }
            break;
        }

        default: {
            std::cerr << "Unknown operation: " << operation << std::endl;
            exit(1);
        }
    }
}


std::string get_pte_state(const pte_t& pte) {
    if (!pte.present && !pte.pagedout) {
        return "*";  
    }
    if (!pte.present && pte.pagedout) {
        return "#";  
    }
    
    std::string state;
    state += pte.referenced ? "R" : "-";
    state += pte.modified ? "M" : "-";
    state += pte.pagedout ? "S" : "-";
    return state;
}

void print_page_table() {
    for (size_t pid = 0; pid < processes.size(); pid++) {
        Process* proc = processes[pid];
        std::cout << "PT[" << pid << "]:";
        
        for (int i = 0; i < MAX_VPAGES; i++) {
            pte_t pte = proc->page_table[i];
            if (pte.present) {
                std::cout << " " << i << ":" << get_pte_state(pte);
            } else {
                std::cout << " " << get_pte_state(pte);
            }
        }
        std::cout << std::endl;
    }
}

void print_frame_table(int num_frames) {  
    std::cout << "FT:";
    for (int i = 0; i < num_frames; i++) {  
        frame_t* frame = &frame_table[i];
        if (frame->pid == -1) {
            std::cout << " *";
        } else {
           std::cout << " " << frame->pid << ":" << frame->vpage;
        }
    }
    std::cout << std::endl;
}

void print_statistics() {
    for (size_t i = 0; i < processes.size(); i++) {
        Process* p = processes[i];
        std::cout << "PROC[" << i << "]: U=" << p->stats.unmaps 
          << " M=" << p->stats.maps 
          << " I=" << p->stats.ins 
          << " O=" << p->stats.outs
          << " FI=" << p->stats.fins 
          << " FO=" << p->stats.fouts
          << " Z=" << p->stats.zeros 
          << " SV=" << p->stats.segv
          << " SP=" << p->stats.segprot << std::endl;
    }

    std::cout << "TOTALCOST " << inst_count << " " << ctx_switches << " " 
          << process_exits << " " << cost << " " << sizeof(pte_t) << std::endl;
}


void read_random_file(const char* filename) {
    std::ifstream infile(filename);
    if (!infile) {
        std::cerr << "Error: Cannot open random file: " << filename << std::endl;
        exit(1);
    }
    
    int num;
    if (!(infile >> num)) {
        std::cerr << "Error: Invalid random file format" << std::endl;
        exit(1);
    }
    
    while (infile >> num) {
        random_numbers.push_back(num);
    }
    
    if (random_numbers.empty()) {
        std::cerr << "Error: No random numbers read" << std::endl;
        exit(1);
    }
}

void read_input(const char* filename) {
    std::ifstream infile(filename);
    if (!infile) {
        std::cerr << "Error: Cannot open input file: " << filename << std::endl;
        exit(1);
    }
    std::string line;
    int num_processes;

    while (std::getline(infile, line)) {
        if (line[0] != '#') {
            num_processes = std::stoi(line);
            break;
        }
    }

    for (int i = 0; i < num_processes; i++) {
        Process* proc = new Process(i);
        
        int num_vmas;
        while (std::getline(infile, line)) {
            if (line[0] != '#') {
                num_vmas = std::stoi(line);
                break;
            }
        }

        for (int j = 0; j < num_vmas; j++) {
            vma_t vma;
            while (std::getline(infile, line)) {
                if (line[0] != '#') {
                    std::istringstream iss(line);
                    int write_prot, file_map;
                    iss >> vma.start_vpage >> vma.end_vpage 
                        >> write_prot >> file_map;
                    vma.write_protected = write_prot;
                    vma.file_mapped = file_map;
                    break;
                }
            }
            proc->vmas.push_back(vma);
        }
        processes.push_back(proc);
    }
}

frame_t* allocate_frame_from_free_list() {
    if (free_pool.empty()) {
        return nullptr;
    }
    frame_t* frame = free_pool.front();
    free_pool.pop_front();
    return frame;
}

frame_t* get_frame() {
    frame_t* frame = allocate_frame_from_free_list();
    if (frame == nullptr) {
        frame = THE_PAGER->select_victim_frame();
    }
    return frame;
}

int main(int argc, char* argv[]) {
    int c;
    int num_frames = 0;
    char algorithm = '\0';
    std::string options;
    
    while ((c = getopt(argc, argv, "f:a:o:")) != -1) {
        switch(c) {
            case 'f':
                num_frames = std::stoi(optarg);
                break;
            case 'a':
                algorithm = optarg[0];
                if (algorithm != 'f' && algorithm != 'r' && algorithm != 'c' && 
                    algorithm != 'e' && algorithm != 'a' && algorithm != 'w') {
                    std::cerr << "Currently only FIFO ('f'), Random ('r'), Clock ('c'), "
                             << "ESC ('e'), Aging ('a'), and Working Set ('w') are supported" << std::endl;
                    exit(1);
                }
                break;
            case 'o':
                options = optarg;
                break;
            default:
                std::cerr << "Usage: " << argv[0] 
                         << " -f<num_frames> -a<algo> [-o<options>] inputfile randomfile" 
                         << std::endl;
                exit(1);
        }
    }

    if (optind + 2 > argc) {
        std::cerr << "Missing input or random file" << std::endl;
        exit(1);
    }

    free_pool.clear();
    for (int i = 0; i < num_frames; i++) {
        frame_table[i].pid = -1;
        frame_table[i].vpage = -1;
        frame_table[i].mapped = false;
        frame_table[i].index = i;  
        frame_table[i].age = 0;    
        frame_table[i].last_used_time = 0;  
        free_pool.push_back(&frame_table[i]);
    }

    THE_PAGER = create_pager(algorithm, num_frames);

    // Read input files
    read_input(argv[optind]);
    read_random_file(argv[optind + 1]);

    // Read and process instructions
    std::string line;
    std::ifstream infile(argv[optind]);
    bool past_init = false;

    while (std::getline(infile, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        if (!past_init) {
            if (line[0] >= '0' && line[0] <= '9') continue;
            past_init = true;
        }

        char operation;
        int vpage;
        std::istringstream iss(line);
        iss >> operation >> vpage;
        
        if (options.find('O') != std::string::npos) {
            std::cout << inst_count << ": ==> " << operation << " " << vpage << std::endl;
        }
        
        simulate_instruction(operation, vpage);

        if (options.find('x') != std::string::npos) {
            print_page_table();
        }

        if (options.find('f') != std::string::npos) {
            print_frame_table(num_frames);
        }
    }

    if (options.find('P') != std::string::npos) {
        print_page_table();
    }

    if (options.find('F') != std::string::npos) {
        print_frame_table(num_frames);
    }

    if (options.find('S') != std::string::npos) {
        print_statistics();  
    }

    delete THE_PAGER;
    for (auto proc : processes) {
        delete proc;
    }

    return 0;
}
