#include "1_Config.h"
#include "2_TLB.h"
#include "3_Pagetable.h"
#include "4_PhysicalRAM.h"
#include "5_Algorithms.h"
#include "6_Disc_Stats.h"

#include <iostream>
using namespace std;

class Simulator
{
private:
    // Core components
    ConfigParser configParser;
    TraceParser traceParser;
    AddressGenerator addrGenerator;
    PageTable pageTable;
    PhysicalRAM physicalRAM;
    TLB tlb;
    VirtualDisk disk;
    StatsEngine stats;
    vector<uint32_t> PhysicalAddresses;
    vector<bool> PhysicalisWrite;

    // Replacement algorithm
    ReplacementAlgorithm* replacementAlgo;

    // Configuration
    uint32_t numFrames;
    uint32_t pageSize;
    uint32_t tlbSize;
public:
    Simulator() : addrGenerator(traceParser, configParser),
        pageTable(configParser.getPageSize()),
        physicalRAM(configParser.getNumFrames()),
        tlb(configParser.getTLBSize()),
        stats(configParser.getTLBLatency(),
            configParser.getRAMLatency(),
            configParser.getDiskLatency())
    {
        numFrames = configParser.getNumFrames();
        pageSize = configParser.getPageSize();
        tlbSize = configParser.getTLBSize();
        replacementAlgo = nullptr;
    }

    ~Simulator()
    {
        if (replacementAlgo) {
            delete replacementAlgo;
        }
    }

    // Load configuration from file
    bool loadConfiguration(const string& configFile)
    {   
        configParser.loadFromFile(configFile);
        configParser.printConfig();
        cout << endl;

        // Re-initialize components with new config
        numFrames = configParser.getNumFrames();
        pageSize = configParser.getPageSize();
        tlbSize = configParser.getTLBSize();

        // Reinitialize components that depend on config
        physicalRAM = PhysicalRAM(numFrames);
        tlb = TLB(tlbSize);
        return true;
    }

    // Load trace file
    bool loadTraceFile(const string& traceFile)
    {
        traceParser.loadFromFile(traceFile);

        if (traceParser.size() == 0) {
            cerr << "Error: No valid references in trace file!" << endl;
            return false;
        }

        traceParser.printStats();
        return true;
    }

    // Set replacement algorithm
    void setReplacementAlgorithm(const string& algorithm)
    {
        if (replacementAlgo) {
            delete replacementAlgo;
        }

        if (algorithm == "FIFO" || algorithm == "fifo") {
            replacementAlgo = new FIFOReplacement(numFrames);
            cout << "\n[ALGORITHM] Using FIFO Replacement Algorithm" << endl;
        }
        else if (algorithm == "LRU" || algorithm == "lru") {
            replacementAlgo = new LRUReplacement(numFrames);
            cout << "\n[ALGORITHM] Using LRU Replacement Algorithm" << endl;
        }
        else if (algorithm == "OPT" || algorithm == "opt") {
            replacementAlgo = new OPTReplacement(numFrames);
            cout << "\n[ALGORITHM] Using OPT Replacement Algorithm" << endl;
        }
        else {
            cout << "\n[WARNING] Unknown algorithm: " << algorithm << ". Using LRU as default." << endl;
            replacementAlgo = new LRUReplacement(numFrames);
        }
    }

    // Precompute VPNs and offsets
    void precomputeAddresses()
    {
        addrGenerator.precomputeVPNsAndOffsets();
    }

    // Get simulated physical address (for page hit)
    uint32_t recordPhysicalAddress(uint32_t frameNumber, uint32_t offset)
    {
        uint32_t PhysicalAdd = (frameNumber * pageSize) + offset;
        PhysicalAddresses.push_back(PhysicalAdd);
        return PhysicalAdd;
    }

    void recordPhysicalAddOperation(bool isWrite)
    {
        PhysicalisWrite.push_back(isWrite);
        return;
    }

    // Handle page fault - load page from disk
    void handlePageFault(uint32_t vpn, bool isWrite)
    {
        uint32_t victimFrame;       //KEEP RECORD OF THE FRAME ALLOCATED  
        uint32_t victimVPN = 0;
        bool needEviction = replacementAlgo->needsEviction();

        if (needEviction) {
            // Select victim using replacement algorithm
            victimVPN = replacementAlgo->selectVictim();

            // Get victim's frame number from page table
            uint32_t victimFrameNum;
            bool victimValid, victimDirty;
            victimFrameNum = pageTable.getFrameNumber(victimVPN);
            victimDirty = pageTable.isDirty(victimVPN);
            victimValid = pageTable.isPageLoaded(victimVPN);

            // Check if victim is dirty - write back to disk
            if (pageTable.isDirty(victimVPN)) {
                stats.recordDirtyWrite();
                disk.writeDirtyPage(victimVPN);
            }

            // Invalidate victim in TLB and Page Table
            tlb.invalidate(victimVPN);
            pageTable.invalidatePage(victimVPN);
            physicalRAM.freeFrame(victimFrameNum);

            // Notify replacement algorithm about eviction
            replacementAlgo->pageEvicted(victimVPN);

            victimFrame = victimFrameNum;
        }
        else {
            // Allocate new free frame
            if (!physicalRAM.allocateFreeFrame(victimFrame, isWrite)) {
                return;
            }
        }

        // Load page into allocated frame
        if (needEviction) {
            physicalRAM.allocateSpecificFrame(victimFrame, isWrite);
        }

        // Update page table
        pageTable.insertPage(vpn, victimFrame, true, isWrite);

        // Update TLB
        tlb.insert(vpn, victimFrame, true, isWrite);

        // Notify replacement algorithm about page load
        replacementAlgo->pageLoaded(vpn);

    }

    // Process a single memory reference
    void processReference(uint32_t vpn, uint32_t offset, bool isWrite, bool addressValid, size_t stepNum)
    {
        stats.incrementReference();

        // Step 1: TLB Lookup
        uint32_t frameNumber;
        bool valid, dirty;

        if (tlb.TLBlookup(vpn, frameNumber, valid, dirty)) {
            // TLB HIT
            stats.recordTLBHit();

            replacementAlgo->pageAccessed(vpn);
                                        //in case if address is same but different operation
            physicalRAM.markDirtyBit(frameNumber, isWrite);
            pageTable.insertPage(vpn, frameNumber, true, isWrite);  // dirty=true
            tlb.insert(vpn, frameNumber, true, isWrite);            // Update TLB with dirty
           

            uint32_t physAddr = recordPhysicalAddress(frameNumber, offset);
            recordPhysicalAddOperation(isWrite);

        }
        else {
            // TLB MISS - Need to check page table
            stats.recordTLBMiss();

            // Step 2: Page Table Lookup
            if (pageTable.pageTableLookup(vpn, frameNumber, valid, dirty)) {
                // Page HIT - Page is in RAM
                stats.recordPageHit();
                replacementAlgo->pageAccessed(vpn);

                // Update dirty bit 
                physicalRAM.markDirtyBit(frameNumber, isWrite);
                pageTable.insertPage(vpn, frameNumber, true, isWrite);

            // Insert into TLB
                tlb.insert(vpn, frameNumber, true, isWrite);

                // Generate physical address
                uint32_t physAddr = recordPhysicalAddress(frameNumber, offset);
                recordPhysicalAddOperation(isWrite);
            }
            else {
                // Page FAULT - Page not in RAM
                stats.recordPageFault();
                disk.readPage(vpn);

                // Handle page fault (load from disk, evict if needed)
                handlePageFault(vpn, isWrite);

                // After loading, update TLB with new mapping
                uint32_t newFrame;
                bool newValid, newDirty;

                newFrame = pageTable.getFrameNumber(vpn);
                newDirty = pageTable.isDirty(vpn);
                newValid = pageTable.isPageLoaded(vpn);
                tlb.insert(vpn, newFrame, true, isWrite);

                // stats.recordRAMAccess();
                // Generate physical address
                uint32_t physAddr = recordPhysicalAddress(newFrame, offset);
                recordPhysicalAddOperation(isWrite);
            }
        }
    }

    // Run full simulation
    void runSimulation(const string& algorithm)
    {
        cout << "\n" << string(70, '=') << endl;
        cout << "STARTING VIRTUAL MEMORY SIMULATION" << endl;
        cout << string(70, '=') << endl;

        // Precompute addresses
        precomputeAddresses();

        // Set replacement algorithm
        setReplacementAlgorithm(algorithm);

        // Get precomputed data
        const auto& vpns = addrGenerator.getVPNs();
        const auto& offsets = addrGenerator.getOffsets();
        const auto& validity = addrGenerator.getValidity();
        const auto& writeFlags = addrGenerator.getWriteFlags();
        const auto& ValidAddresses = addrGenerator.getValidAddresses();

        cout << "\nStarting simulation with " << vpns.size() << " references..." << endl;
        cout << string(70, '-') << endl;

        // OPT special handling: set future references
        OPTReplacement* opt = dynamic_cast<OPTReplacement*>(replacementAlgo);
        if (opt) {
            opt->setFutureReferences(vpns);
        }
        cout << endl;
        // Process each reference
        for (size_t i = 0; i < vpns.size(); i++) {
            //incase of opt current position = 0
            processReference(vpns[i], offsets[i], writeFlags[i], validity[i], i + 1);

            //Advance OPT position AFTER processing
            if (opt) {
                opt->advancePosition();
            }
        }

        // Print final results
        printFinalResults();
    }

    // Run comparison of all algorithms
    void runComparison()
    {
        cout << "\n" << string(70, '=') << endl;
        cout << "RUNNING ALGORITHM COMPARISON" << endl;
        cout << string(70, '=') << endl;

        vector<string> algorithms = { "FIFO", "LRU", "OPT" };

        for (const string& algo : algorithms) {
            // Reset all components
            reset();

            // Run simulation with this algorithm
            runSimulation(algo);

            cout << "\n" << string(70, '-') << endl;
            cout << "Press Enter to continue to next algorithm...";
            cin.get();
        }
    }

    // Reset simulator state
    void reset()
    {
        pageTable.clear();
        physicalRAM.reset();
        tlb.reset();
        disk.reset();
        stats.reset();
        addrGenerator.reset();

        if (replacementAlgo) {
            replacementAlgo->reset();
        }

        PhysicalAddresses.clear();
        PhysicalisWrite.clear();
        replacementAlgo->reset();
    }

    // Print final simulation results
    void printFinalResults()
    {
        cout << "\n" << string(70, '=') << endl;
        cout << "FINAL SIMULATION RESULTS FOR ALGORITHM " << replacementAlgo->getName() << endl;
        cout << string(70, '=') << endl;

        // Performance Statistics   
        stats.print();

        // Effective Access Time
        cout << "\n[EFFECTIVE ACCESS TIME (EAT)]:" << endl;
        cout << "  EAT = " << fixed << setprecision(2) << stats.getEAT() << " ns" << endl;

        cout << "\n" << string(70, '=') << endl;
        cout << "SIMULATION COMPLETED FOR ALGORITHM " << replacementAlgo->getName() << endl;
        cout << string(70, '=') << endl;
    }
};

// ============================================================================
// MAIN FUNCTION
// ============================================================================
int main(int argc, char* argv[])
{
    cout << "\n";
    cout << "+=====================================================+" << endl;
    cout << "|     VIRTUAL MEMORY MANAGEMENT SIMULATOR             |" << endl;
    cout << "|     TLB | Page Table | Physical RAM | Disk (WB)     |" << endl;
    cout << "+=====================================================+" << endl;

    Simulator sim;

    // Default file paths
    string configFile = "Config.txt";
    string traceFile = "Trace_Memory_Check.txt";
    string algorithm = "LRU";
    string algorithm1 = "FIFO";
    string algorithm2 = "OPT";

    // Load configuration
    if (!sim.loadConfiguration(configFile)) {
        cerr << "Failed to load configuration!" << endl;
        return 1;
    }

    // Load trace file
    if (!sim.loadTraceFile(traceFile)) {
        cerr << "Failed to load trace file!" << endl;
        return 1;
    }

    // Run simulation
    sim.runSimulation(algorithm);
    system("pause");
    sim.reset();
    sim.runSimulation(algorithm1);
    system("pause");
    sim.reset();
    sim.runSimulation(algorithm2);

    // // Uncomment to run comparison of all algorithms
    // sim.runComparison();

    return 0;
}