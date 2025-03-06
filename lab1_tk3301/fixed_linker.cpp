#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <cstring>

const int MACHINE_SIZE = 512;
const int MAX_SYMBOL_TABLE_SIZE = 256;
const int MAX_MODULE_TABLE_SIZE = 128;

int linenum = 0;  
int lineoffset = 0;
int last_valid_offset = 0;
int totalInstructions = 0;
std::ifstream input_file;
std::string current_line;
size_t current_pos = 0;

struct Symbol {
    std::string name;
    int value;
    bool isDefined;
    bool isUsed;
    int definingModule;
    std::string errorMessage;
};

std::map<std::string, Symbol> symbolTable;
std::vector<int> moduleBaseAddresses;
std::vector<int> memoryMap;
std::vector<std::string> warnings;

void __parseerror(int errcode) {
    static const char* errstr[] = {
        "NUM_EXPECTED",        // 0
        "SYM_EXPECTED",        // 1
        "ADDR_EXPECTED",       // 2
        "SYM_TOO_LONG",        // 3
        "TOO_MANY_DEF_IN_MODULE", // 4
        "TOO_MANY_USE_IN_MODULE", // 5
        "TOO_MANY_INSTR",      // 6
        "MARIE_EXPECTED"       // 7
    };
    printf("Parse Error line %d offset %d: %s\n", linenum, lineoffset, errstr[errcode]);
    exit(1);
}

std::string getToken() {
    while (true) {
        // If we're at the end of the current line or haven't read a line yet
        if (current_pos >= current_line.length()) {
            if (!std::getline(input_file, current_line)) {
                return ""; // EOF
            }
            current_line += '\n'; // Add newline for consistent processing
            linenum++;
            current_pos = 0;
            lineoffset = 1; // Reset offset to start of the new line
        }

        
        while (current_pos < current_line.length() && std::isspace(current_line[current_pos])) {
            if (current_line[current_pos] == '\n') {
                //linenum++;  // Increment the line number
                lineoffset = current_line.length();
                current_pos++;
                //lineoffset = 1; // Reset offset to 1 at the start of the new line
                continue;
           }
            current_pos++;
            lineoffset++;
        }

        size_t token_start_pos = current_pos;

        while (current_pos < current_line.length() && !std::isspace(current_line[current_pos])) {
            current_pos++;
            
        }

        if (token_start_pos == current_pos) {
            continue;
        }

        // Extract the token
        std::string token = current_line.substr(token_start_pos, current_pos - token_start_pos);

        //last_valid_offset = lineoffset;
        //lineoffset = current_pos + 1;
        //std::cout<<lineoffset<<std::endl;
        return token;
    }
}

int readInt() {
    std::string token = getToken();
    if (token.empty()) throw std::runtime_error("EOF");
    try {
        size_t pos = 0;
        int value = std::stoi(token, &pos);
        if (pos != token.length()) __parseerror(0);
        
        return value;
    } catch (const std::invalid_argument&) {
        __parseerror(0);
    } catch (const std::out_of_range&) {
        __parseerror(0);
    }
    
    return 0;
}

std::string readSymbol() {
    std::string token = getToken();
    if (token.empty() || !std::isalpha(token[0])) __parseerror(1);
    if (token.length() > 16) __parseerror(3);
    for (char c : token) {
        if (!std::isalnum(c)) __parseerror(1);
    }
    lineoffset = current_pos + 1;
    return token;
}

char readMARIE() {
    std::string token = getToken();
    if (token.empty() || token.length() != 1 || strchr("MARIE", token[0]) == NULL) {
        __parseerror(7); // MARIE_EXPECTED
    }
    lineoffset = current_pos + 1;
    return token[0];
}

void pass1() {
    int moduleCount = 0;
    totalInstructions = 0;

    while (true) {
        try {
            int defCount = readInt();
            if (defCount > 16) __parseerror(4);

            for (int i = 0; i < defCount; i++) {
                std::string symbol = readSymbol();
                int value = readInt();

                if (symbolTable.count(symbol) > 0) {
                    if (symbolTable[symbol].errorMessage.empty()) {
                        symbolTable[symbol].errorMessage = "Error: This variable is multiple times defined; first value used";
                        warnings.push_back("Warning: Module " + std::to_string(moduleCount) + ": " + symbol + " redefinition ignored");
                    }
                    continue;
                }

                if (symbolTable.size() >= MAX_SYMBOL_TABLE_SIZE) {
                    __parseerror(4);
                }

                symbolTable[symbol] = {symbol, value + totalInstructions, true, false, moduleCount, ""};
            }

            int useCount = readInt();
            if (useCount > 16) __parseerror(5);
            for (int i = 0; i < useCount; i++) readSymbol();

            int instructionCount = readInt();
            if (totalInstructions + instructionCount > MACHINE_SIZE) __parseerror(6);

            for (int i = 0; i < instructionCount; i++) {
                readMARIE();
                readInt();
            }

            for (auto& pair : symbolTable) {
                if (pair.second.definingModule == moduleCount) {
                    if (pair.second.value - totalInstructions >= instructionCount) {
                        warnings.push_back("Warning: Module " + std::to_string(moduleCount) + ": " + pair.first + 
                                           "=" + std::to_string(pair.second.value - totalInstructions) + 
                                           " valid=[0.." + std::to_string(instructionCount-1) + "] assume zero relative");
                        pair.second.value = totalInstructions;
                    }
                }
            }

            moduleBaseAddresses.push_back(totalInstructions);
            totalInstructions += instructionCount;
            moduleCount++;

            if (moduleCount > MAX_MODULE_TABLE_SIZE) {
                __parseerror(6);
            }
        } catch (const std::runtime_error& e) {
            if (std::string(e.what()) == "EOF") break;
            throw;
        }
    }
}

void pass2() {
    input_file.clear();
    input_file.seekg(0);
    linenum = 1;
    lineoffset = 0;
    current_pos = 0;
    current_line = "";

    int moduleCount = 0;
    int currentAddress = 0;

    while (true) {
        try {
            int defCount = readInt();

            for (int i = 0; i < defCount; i++) {
                readSymbol();
                readInt();
            }

            int useCount = readInt();
            std::vector<std::string> useList;
            std::vector<bool> usedSymbols(useCount, false);
            for (int i = 0; i < useCount; i++) {
                useList.push_back(readSymbol());
            }

            int instructionCount = readInt();
            for (int i = 0; i < instructionCount; i++) {
                char addressMode = readMARIE();
                int instruction = readInt();
                int opcode = instruction / 1000;
                int operand = instruction % 1000;

                std::stringstream output;
                output << std::setfill('0') << std::setw(3) << currentAddress << ": ";

                if (opcode >= 10) {
                    output << "9999 Error: Illegal opcode; treated as 9999";
                    memoryMap.push_back(9999);
                } else {
                    switch (addressMode) {
                        case 'I':
                            if (operand >= 900) {
                                output << std::setw(4) << (opcode * 1000 + 999) << " Error: Illegal immediate operand; treated as 999";
                                memoryMap.push_back(opcode * 1000 + 999);
                            } else {
                                output << std::setw(4) << instruction;
                                memoryMap.push_back(instruction);
                            }
                            break;
                        case 'A':
                            if (operand >= MACHINE_SIZE) {
                                output << std::setw(4) << (opcode * 1000) << " Error: Absolute address exceeds machine size; zero used";
                                memoryMap.push_back(opcode * 1000);
                            } else {
                                output << std::setw(4) << instruction;
                                memoryMap.push_back(instruction);
                            }
                            break;
                        case 'R':
                            if (operand >= instructionCount) {
                                output << std::setw(4) << (opcode * 1000 + moduleBaseAddresses[moduleCount])
                                       << " Error: Relative address exceeds module size; relative zero used";
                                memoryMap.push_back(opcode * 1000 + moduleBaseAddresses[moduleCount]);
                            } else {
                                int absoluteAddress = opcode * 1000 + operand + moduleBaseAddresses[moduleCount];
                                output << std::setw(4) << absoluteAddress;
                                memoryMap.push_back(absoluteAddress);
                            }
                            break;
                        case 'E':
                            if (operand >= useCount) {
                                output << std::setw(4) << (opcode * 1000)
                                       << " Error: External operand exceeds length of uselist; treated as relative=0";
                                memoryMap.push_back(opcode * 1000);
                            } else {
                                std::string symbol = useList[operand];
                                usedSymbols[operand] = true;
                                if (symbolTable.count(symbol) == 0) {
                                    output << std::setw(4) << (opcode * 1000)
                                           << " Error: " << symbol << " is not defined; zero used";
                                    memoryMap.push_back(opcode * 1000);
                                } else {
                                    symbolTable[symbol].isUsed = true;
                                    int absoluteAddress = opcode * 1000 + symbolTable[symbol].value;
                                    output << std::setw(4) << absoluteAddress;
                                    memoryMap.push_back(absoluteAddress);
                                }
                            }
                            break;
                        case 'M':
                            if (operand >= moduleBaseAddresses.size()) {
                                output << std::setw(4) << (opcode * 1000)
                                       << " Error: Illegal module operand ; treated as module=0";
                                memoryMap.push_back(opcode * 1000);
                            } else {
                                int absoluteAddress = opcode * 1000 + moduleBaseAddresses[operand];
                                output << std::setw(4) << absoluteAddress;
                                memoryMap.push_back(absoluteAddress);
                            }
                            break;
                    }
                }
                std::cout << output.str() << "\n";
                currentAddress++;
            }

            for (size_t i = 0; i < useList.size(); i++) {
                if (!usedSymbols[i]) {
                    std::cout << "Warning: Module " << moduleCount << ": uselist[" << i << "]=" << useList[i] << " was not used\n";
                }
            }

            moduleCount++;
        } catch (const std::runtime_error& e) {
            if (std::string(e.what()) == "EOF") break;
            throw;
        }
    }

    std::cout << std::endl;  // Add a blank line after Memory Map

    for (const auto& pair : symbolTable) {
        if (pair.second.isDefined && !pair.second.isUsed) {
            warnings.push_back("Warning: Module " + std::to_string(pair.second.definingModule) + ": " + pair.first + " was defined but never used");
        }
    }
}

void printSymbolTable() {
    std::cout << "Symbol Table\n";
    for (const auto& pair : symbolTable) {
        std::cout << pair.first << "=" << pair.second.value;
        if (!pair.second.errorMessage.empty()) {
            std::cout << " " << pair.second.errorMessage;
        }
        std::cout << "\n";
    }
    std::cout << std::endl;  // Add a blank line after Symbol Table
}

void printWarnings() {
    for (const auto& warning : warnings) {
        std::cout << warning << "\n";
    }
    std::cout << std::endl;  // Add a blank line after warnings
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <input_file>\n";
        return 1;
    }

    input_file.open(argv[1]);
    if (!input_file) {
        std::cerr << "Error opening file: " << argv[1] << "\n";
        return 1;
    }

    try {
        pass1();
        printWarnings();
        printSymbolTable();

        warnings.clear();

        std::cout << "Memory Map\n";
        pass2();
        printWarnings();

        // No need for additional std::cout << std::endl; here, as it's already added in pass2() and printWarnings()
    } catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
        return 1;
    }

    input_file.close();
    return 0;
}