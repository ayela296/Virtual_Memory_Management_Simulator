#pragma once

#include<iostream>
#include<fstream>
#include<string>
#include<vector>
#include<sstream>

using namespace std;

// ============================================================================
// STRUCT: Configuration (from .TXT file)
// ============================================================================
struct system_config
{
    uint32_t physicalRAMSize;
    uint32_t pageSize;
    uint32_t tlbSize;

    double tlbLatency;
    double ramLatency;
    double diskLatency;

    //Memory Protection Bounds
    uint32_t minValidAddress;
    uint32_t maxValidAddress;

    //Derived values 
    uint32_t numFrames;
    uint32_t shiftAmount;
    uint32_t offsetMask;
    uint32_t vpnMask;

    void CalculateDerived()
    {
        //calculate number of frames in physical RAM
        numFrames = physicalRAMSize / pageSize;

        //Calculate shift amount   (e.g. if page size is 4096, shift amount is 12)
        shiftAmount = 0;
        uint32_t temp = pageSize;
        while (temp > 1)
        {
            temp >>= 1;
            shiftAmount++;
        }

        //Mask for offset   (e.g. if page size is 4096, offset mask is 0xFFF)
        offsetMask = pageSize - 1;

        //Mask for VPN     (e.g. if shift amount is 12, vpn mask is 0xFFFFF(top 20 bits))
        vpnMask = 0xFFFFFFFF >> shiftAmount;

        if (numFrames > 1048576) {  // > 1M frames
            cout << "Warning: Large number of frames: " << numFrames << endl;
        }

    }
};

// ============================================================================
// CLASS 1: CONFIG PARSER
// ============================================================================
class ConfigParser {
private:
    system_config config;

public:
    ConfigParser() {
        // Initialize with defaults (256 KB RAM, 4KB pages, 16 TLB entries)
        config.physicalRAMSize = 256 * 1024;  // 256 KB
        config.pageSize = 4096;                // 4 KB
        config.tlbSize = 16;                   // 16 entries
        config.tlbLatency = 1.0;               // 1 ns
        config.ramLatency = 100.0;             // 100 ns
        config.diskLatency = 10000000.0;       // 10 ms
        config.CalculateDerived();
    }

    uint32_t getshiftAmount() const { return config.shiftAmount; }
    uint32_t getoffsetMask() const { return config.offsetMask; }
    uint32_t getvpnMask() const { return config.vpnMask; }
    const system_config& getConfig() const { return config; }
    uint32_t getNumFrames() const { return config.numFrames; }
    uint32_t getPageSize() const { return config.pageSize; }
    uint32_t getTLBSize() const { return config.tlbSize; }
    double getTLBLatency() const { return config.tlbLatency; }
    double getRAMLatency() const { return config.ramLatency; }
    double getDiskLatency() const { return config.diskLatency; }

    void loadFromFile(const string& filename) {
        ifstream file(filename);
        if (!file.is_open()) {
            cerr << "Warning: Using default configuration" << endl;
            return;
        }

        string key, equals, value;

        // Simple line-by-line parsing
        while (file >> key >> equals >> value) {
            if (equals != "=") continue;

            if (key == "PhysicalRAMSize")
                config.physicalRAMSize = stoul(value);    //string to unsigned long(32 bits)
            else if (key == "PageSize")
                config.pageSize = stoul(value);
            else if (key == "TLBSize")
                config.tlbSize = stoul(value);
            else if (key == "TLBLatency")
                config.tlbLatency = stod(value);    //string to double
            else if (key == "RAMLatency")
                config.ramLatency = stod(value);
            else if (key == "DiskLatency")
                config.diskLatency = stod(value);
            else if (key == "MinValidAddress")
                config.minValidAddress = stoul(value, nullptr, 16);
            else if (key == "MaxValidAddress")
                config.maxValidAddress = stoul(value, nullptr, 16);
        }

        config.CalculateDerived();
        return;
    }

    void printConfig() const {
        cout << "\n=== MMU CONFIGURATION ===" << endl;
        cout << "Physical RAM: " << config.physicalRAMSize << " bytes";

        // Convert to KB/MB for readability
        if (config.physicalRAMSize >= 1024 * 1024)
            cout << " (" << config.physicalRAMSize / (1024 * 1024) << " MB)";
        else if (config.physicalRAMSize >= 1024)
            cout << " (" << config.physicalRAMSize / 1024 << " KB)";
        cout << endl;

        cout << "Page Size: " << config.pageSize << " bytes";
        if (config.pageSize >= 1024)
            cout << " (" << config.pageSize / 1024 << " KB)";
        cout << endl;

        cout << "Number of Frames: " << config.numFrames << endl;
        cout << "TLB Size: " << config.tlbSize << " entries" << endl;
        cout << "TLB Latency: " << config.tlbLatency << " ns" << endl;
        cout << "RAM Latency: " << config.ramLatency << " ns" << endl;
        cout << "Disk Latency: " << config.diskLatency << " ns";

        // Convert to ms for readability
        if (config.diskLatency >= 1000000)
            cout << " (" << config.diskLatency / 1000000 << " ms)";
        cout << endl;

        cout << "Memory Protection Bounds: 0x" << hex << config.minValidAddress
            << " - 0x" << config.maxValidAddress << dec << endl;

        cout << "Address Translation: " << config.shiftAmount
            << " bits for offset, " << (32 - config.shiftAmount)
            << " bits for VPN" << endl;
        cout << "=========================" << endl;
    }
};

// ============================================================================
// CLASS 2: TRACE PARSER
// ============================================================================

class TraceParser {
private:
    vector<uint32_t> virtualAddresses;
    vector<bool> isWrite;  // true = WRITE, false = READ

public:
    void loadFromFile(const string& filename) {
        ifstream file(filename);
        if (!file.is_open()) {
            cerr << "Error: Could not open trace file: " << filename << endl;
            cerr << "Failed to load trace file." << endl;
            return;
        }

        string line;
        int lineNum = 0;
        int validlines = 0;
        int invalidlines = 0;

        while (getline(file, line)) {
            lineNum++;

            // Trim
            size_t start = line.find_first_not_of(" \t\r");
            if (start == string::npos) continue;  // Empty line
            line = line.substr(start);

            size_t end = line.find_last_not_of(" \t\r");
            if (end != string::npos) {
                line = line.substr(0, end + 1);
            }

            if (line.empty()) continue;

            // IGNORE COMMENTS - lines starting with '#'
            if (line[0] == '#') {
                continue;
            }

            // Parse: "R 0x1234" or "W 0x5678"
            char operation;
            string addressStr;
            stringstream ss(line);
            ss >> operation >> addressStr;

            // Validate operation
            if (operation != 'R' && operation != 'W') {
                cerr << "Trace Validator: Invalid operation at line " << lineNum << endl;
                invalidlines++;
                continue;
            }

            // Validate and parse hex address
            if (addressStr.length() >= 2 && (addressStr.substr(0, 2) == "0x"
                || addressStr.substr(0, 2) == "0X")) {
                addressStr = addressStr.substr(2);
            }

            // Check if hex string is valid
            bool validHex = true;
            for (char c : addressStr) {
                if (!isxdigit(c)) {
                    validHex = false;
                    break;
                }
            }

            if (!validHex) {
                cerr << "Trace Validator: Invalid hex address at line " << lineNum << endl;
                continue;
            }

            uint32_t address;
            stringstream(addressStr) >> hex >> address;

            virtualAddresses.push_back(address);
            isWrite.push_back(operation == 'W');    // true for WRITE, false for READ
            validlines++;
        }

        cout << "Trace Validator: Loaded " << virtualAddresses.size() << " valid references" << endl;
        return;
    }

    bool isWriteOperation(size_t index) const
    {
        if (index >= isWrite.size()) return false;
        return isWrite[index];
    }

    void printStats() const
    {
        if (virtualAddresses.empty()) {
            cout << "No trace data loaded." << endl;
            return;
        }

        size_t reads = 0, writes = 0;
        for (bool write : isWrite) {
            if (write) writes++;
            else reads++;
        }

        cout << "\n=== TRACE STATISTICS ===" << endl;
        cout << "Total references: " << virtualAddresses.size() << endl;
        cout << "Read operations:  " << reads << " ("
            << (reads * 100.0 / virtualAddresses.size()) << "%)" << endl;
        cout << "Write operations: " << writes << " ("
            << (writes * 100.0 / virtualAddresses.size()) << "%)" << endl;
    }

    const vector<uint32_t>& getVirtualAddresses() const { return virtualAddresses; }
    const vector<bool>& getWriteFlags() const { return isWrite; }
    size_t size() const { return virtualAddresses.size(); }
};

// ============================================================================
// CLASS 3: ADDRESS GENERATOR 
// ============================================================================
class AddressGenerator
{
private:
    vector<uint32_t> vpns;  // Precomputed VPNs
    vector<uint32_t> offsets; // Precomputed offsets 
    vector<bool> isValid;     //Check if address is out of bounds (e.g. if VPN exceeds vpnMask)
    vector<uint32_t> validAddresses; //Store valid addresses for statistics
    vector<bool> writeFlags; //Store write flags for valid addresses

    const TraceParser& trace;
    const ConfigParser& config;
    int validAddressCount;
    int invalidAddressCount;

    uint32_t MaxAddr, MinAddr;
public:
    AddressGenerator(const TraceParser& t, const ConfigParser& c) : trace(t), config(c), validAddressCount(0), invalidAddressCount(0) {}

    void precomputeVPNsAndOffsets()
    {
        vpns.clear();
        offsets.clear();
        isValid.clear();
        validAddresses.clear();
        writeFlags.clear();

        uint32_t shiftAmount = config.getshiftAmount();   //get shift amount from config
        uint32_t offsetMask = config.getoffsetMask();     //get offset mask from config
        uint32_t minValidAddress = config.getConfig().minValidAddress;
        uint32_t maxValidAddress = config.getConfig().maxValidAddress;

        for (size_t i = 0; i < trace.size(); i++) {
            uint32_t addr = trace.getVirtualAddresses()[i];
            uint32_t vpn = addr >> shiftAmount;  // Extract VPN
            uint32_t offset = addr & offsetMask;  // Extract offset

            bool valid = (addr >= minValidAddress && addr <= maxValidAddress);   // Check if address is within valid bounds

            if (!valid)
            {
                invalidAddressCount++;
            }
            else {                  //only push valid addresses to the vectors
                validAddressCount++;
                vpns.push_back(vpn);
                offsets.push_back(offset);
                isValid.push_back(valid);
                validAddresses.push_back(addr);
                writeFlags.push_back(trace.getWriteFlags()[i]);
            }
        }
        MaxAddr = maxValidAddress;
        MinAddr = minValidAddress;
    }

    void printAddrStats()
    {
        cout << "\n=== ADDRESS GENERATOR SUMMARY ===" << endl;
        cout << "Total addresses processed: " << trace.size() << endl;
        cout << "Valid address range: 0x" << hex << MaxAddr
            << " - 0x" << MinAddr << dec << endl;
        cout << "Valid addresses: " << validAddressCount << endl;
        cout << "Out of bounds addresses: " << invalidAddressCount << endl << endl;
    }

    const vector<uint32_t>& getVPNs() const { return vpns; }
    const vector<uint32_t>& getOffsets() const { return offsets; }
    const vector<bool>& getValidity() const { return isValid; }
    const vector<bool>& getWriteFlags() const { return writeFlags; }
    const vector<uint32_t>& getValidAddresses() const { return validAddresses; }

    const size_t size() const { return vpns.size(); }
    const int getTotalAddresses() const { return validAddressCount + invalidAddressCount; }
    const int getValidAddressCount() const { return validAddressCount; }
    const int getInvalidAddressCount() const { return invalidAddressCount; }

    void reset() {
        vpns.clear();
        offsets.clear();
        isValid.clear();
        writeFlags.clear();
        validAddresses.clear();
        validAddressCount = 0;
        invalidAddressCount = 0;
    }
};
