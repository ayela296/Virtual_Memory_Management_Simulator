#include "1_Config.h"
#include "2_TLB.h"
#include "3_PageTable.h"      
#include "4_PhysicalRAM.h"
#include "5_Algorithms.h"
#include "6_Disc_Stats.h"

#include <iostream>
#include <iomanip>
using namespace std;

class Simulator
{
private:
    ConfigParser     configParser;
    TraceParser      traceParser;
    AddressGenerator addrGenerator;
    PageTable        pageTable;
    PhysicalRAM      physicalRAM;
    TLB              tlb;
    VirtualDisk      disk;
    StatsEngine      stats;

    vector<uint32_t> PhysicalAddresses;
    vector<bool>     PhysicalisWrite;

    ReplacementAlgorithm* replacementAlgo;

    uint32_t numFrames;
    uint32_t pageSize;
    uint32_t tlbSize;

    bool debugMode;

public:
    Simulator() : addrGenerator(traceParser, configParser),
        pageTable(configParser.getPageSize()),
        physicalRAM(configParser.getNumFrames()),
        tlb(configParser.getTLBSize()),
        stats(configParser.getTLBLatency(),
            configParser.getRAMLatency(),
            configParser.getDiskLatency()),
        debugMode(false)
    {
        numFrames = configParser.getNumFrames();
        pageSize = configParser.getPageSize();
        tlbSize = configParser.getTLBSize();
        replacementAlgo = nullptr;
    }

    ~Simulator()
    {
        if (replacementAlgo) delete replacementAlgo;
    }

    void setDebugMode(bool on) { debugMode = on; }

    bool loadConfiguration(const string& configFile)
    {
        configParser.loadFromFile(configFile);
        configParser.printConfig();
        cout << endl;

        numFrames = configParser.getNumFrames();
        pageSize = configParser.getPageSize();
        tlbSize = configParser.getTLBSize();

        physicalRAM = PhysicalRAM(numFrames);
        tlb = TLB(tlbSize);
        return true;
    }

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

    void setReplacementAlgorithm(const string& algorithm)
    {
        if (replacementAlgo) delete replacementAlgo;

        if (algorithm == "FIFO" || algorithm == "fifo") {
            replacementAlgo = new FIFOReplacement(numFrames);
        }
        else if (algorithm == "LRU" || algorithm == "lru") {
            replacementAlgo = new LRUReplacement(numFrames);
        }
        else if (algorithm == "OPT" || algorithm == "opt") {
            replacementAlgo = new OPTReplacement(numFrames);
        }
        else {
            cout << "\n[WARNING] Unknown algorithm '" << algorithm << "'. Using LRU." << endl;
            replacementAlgo = new LRUReplacement(numFrames);
        }
    }

    void precomputeAddresses()
    {
        addrGenerator.precomputeVPNsAndOffsets();
        addrGenerator.printAddrStats();
    }

    uint32_t recordPhysicalAddress(uint32_t frameNumber, uint32_t offset)
    {
        uint32_t physAddr = (frameNumber * pageSize) + offset;
        PhysicalAddresses.push_back(physAddr);
        return physAddr;
    }

    void recordPhysicalAddOperation(bool isWrite)
    {
        PhysicalisWrite.push_back(isWrite);
    }

    void handlePageFault(uint32_t vpn, bool isWrite)
    {
        uint32_t frameToUse;
        bool needEviction = replacementAlgo->needsEviction();

        if (needEviction) {
            // Select victim and get its frame
            uint32_t victimVPN = replacementAlgo->selectVictim();
            uint32_t victimFrameNum = pageTable.getFrameNumber(victimVPN);

            // Write dirty victim back to disk
            if (pageTable.isDirty(victimVPN)) {
                stats.recordDirtyWrite();
                disk.writeDirtyPage(victimVPN);
            }

            // Remove victim from TLB and page table
            tlb.invalidate(victimVPN);
            pageTable.invalidatePage(victimVPN);
            replacementAlgo->pageEvicted(victimVPN);

            //use victim frame
            frameToUse = victimFrameNum;
            physicalRAM.markDirtyBit(frameToUse, isWrite);
        }
        else {
            // Free frames available
            if (!physicalRAM.allocateFreeFrame(frameToUse, isWrite)) {
                cerr << "ERROR: allocateFreeFrame failed unexpectedly" << endl;
                return;
            }
        }

        // Load new page into chosen frame
        pageTable.insertPage(vpn, frameToUse, true, isWrite);
        tlb.insert(vpn, frameToUse, true, isWrite);
        replacementAlgo->pageLoaded(vpn);
    }

    void processReference(uint32_t vpn, uint32_t offset, bool isWrite,
        bool addressValid, size_t stepNum)
    {
        stats.incrementReference();

        if (debugMode) {
            cout << "\n--- Step " << stepNum << " ---" << endl;
            cout << "  VPN=0x" << hex << vpn
                << "  Offset=0x" << offset << dec
                << "  Op=";
            if (isWrite) { cout << "WRITE"; }
            else { cout << "READ"; }
            cout << endl;
        }

        uint32_t frameNumber;
        bool valid, dirty;

        if (tlb.TLBlookup(vpn, frameNumber, valid, dirty)) {
            // TLB HIT
            stats.recordTLBHit();
            replacementAlgo->pageAccessed(vpn);
            physicalRAM.markDirtyBit(frameNumber, isWrite);
            pageTable.insertPage(vpn, frameNumber, true, isWrite);
            tlb.insert(vpn, frameNumber, true, isWrite);

            uint32_t physAddr = recordPhysicalAddress(frameNumber, offset);
            recordPhysicalAddOperation(isWrite);

            if (debugMode) {
                cout << "  RESULT  : TLB HIT" << endl;
                cout << "  Frame   : " << frameNumber << endl;
                cout << "  PhysAddr: 0x" << hex << physAddr << dec << endl;
                cout << "  Cost    : " << configParser.getTLBLatency()
                    << " ns  (TLB only)" << endl;
            }
        }
        else {
            stats.recordTLBMiss();

            if (debugMode) cout << "  TLB MISS -> checking page table" << endl;

            if (pageTable.pageTableLookup(vpn, frameNumber, valid, dirty)) {
                // PAGE HIT
                stats.recordPageHit();
                replacementAlgo->pageAccessed(vpn);
                physicalRAM.markDirtyBit(frameNumber, isWrite);
                pageTable.insertPage(vpn, frameNumber, true, isWrite);
                tlb.insert(vpn, frameNumber, true, isWrite);

                uint32_t physAddr = recordPhysicalAddress(frameNumber, offset);
                recordPhysicalAddOperation(isWrite);

                if (debugMode) {
                    cout << "  RESULT  : PAGE HIT (TLB miss, RAM hit)" << endl;
                    cout << "  Frame   : " << frameNumber << endl;
                    cout << "  PhysAddr: 0x" << hex << physAddr << dec << endl;
                    cout << "  Cost    : "
                        << configParser.getTLBLatency() + configParser.getRAMLatency()
                        << " ns  (TLB + RAM)" << endl;
                }
            }
            else {
                // PAGE FAULT
                stats.recordPageFault();
                disk.readPage(vpn);

                if (debugMode) cout << "  PAGE FAULT -> loading from disk..." << endl;

                handlePageFault(vpn, isWrite);

                uint32_t newFrame = pageTable.getFrameNumber(vpn);
                tlb.insert(vpn, newFrame, true, isWrite);

                uint32_t physAddr = recordPhysicalAddress(newFrame, offset);
                recordPhysicalAddOperation(isWrite);

                if (debugMode) {
                    cout << "  RESULT  : PAGE FAULT" << endl;
                    cout << "  Frame   : " << newFrame << endl;
                    cout << "  PhysAddr: 0x" << hex << physAddr << dec << endl;
                    cout << "  Cost    : "
                        << configParser.getTLBLatency()
                        + configParser.getRAMLatency()
                        + configParser.getDiskLatency()
                        << " ns  (TLB + RAM + Disk)" << endl;
                }
            }
        }
    }
    void reset()
    {
        pageTable.clear();

        // Full reconstruction
        physicalRAM = PhysicalRAM(numFrames);
        tlb = TLB(tlbSize);

        disk.reset();
        stats.reset();
        addrGenerator.reset();     
        PhysicalAddresses.clear();
        PhysicalisWrite.clear();

        if (replacementAlgo) {
            replacementAlgo->reset(); 
        }
    }

    void runSimulation(const string& algorithm)
    {
        precomputeAddresses();          //addresses reloaded each time
        setReplacementAlgorithm(algorithm);

        const auto& vpns = addrGenerator.getVPNs();
        const auto& offsets = addrGenerator.getOffsets();
        const auto& validity = addrGenerator.getValidity();
        const auto& writeFlags = addrGenerator.getWriteFlags();

        cout << string(70, '-') << endl;

        OPTReplacement* opt = dynamic_cast<OPTReplacement*>(replacementAlgo);
        if (opt) opt->setFutureReferences(vpns);

        for (size_t i = 0; i < vpns.size(); i++) {
            processReference(vpns[i], offsets[i], writeFlags[i], validity[i], i + 1);
            if (opt) opt->advancePosition();
        }

        printFinalResults();
    }

    void runComparison()
    {
        cout << "\n";
        cout << "ALGORITHM PERFORMANCE COMPARISON" << endl;
        cout << endl;

        // Store results for each algorithm
        struct Results {
            string name;
            uint32_t pageFaults, pageHits;
            double faultRate, HitRate;
            uint32_t tlbHits;
            double tlbHitRate;
            uint32_t diskWrites;
            double eat;
        };

        vector<Results> results;
        vector<string> algorithms = { "FIFO", "LRU", "OPT" };

        for (const string& algo : algorithms) {
            // Reset everything before each run
            reset();

            // Set up for this algorithm
            setReplacementAlgorithm(algo);
            addrGenerator.precomputeVPNsAndOffsets();

            const auto& vpns = addrGenerator.getVPNs();
            const auto& offsets = addrGenerator.getOffsets();
            const auto& validity = addrGenerator.getValidity();
            const auto& writeFlags = addrGenerator.getWriteFlags();

            // OPT needs future references
            OPTReplacement* opt = dynamic_cast<OPTReplacement*>(replacementAlgo);
            if (opt) opt->setFutureReferences(vpns);

            // Run simulation
            for (size_t i = 0; i < vpns.size(); i++) {
                processReference(vpns[i], offsets[i], writeFlags[i], validity[i], i + 1);
                if (opt) opt->advancePosition();
            }

            // Store results
            Results r;
            r.name = algo;
            r.pageFaults = stats.getPageFaults();
            r.faultRate = stats.getPageFaultRate();
            r.pageHits = stats.getPageHits();
            r.HitRate = stats.getPageHitRate();
            r.tlbHits = stats.getTLBHits();
            r.tlbHitRate = stats.getTLBHitRate();
            r.diskWrites = stats.getDiskWrites();
            r.eat = stats.getEAT();
            results.push_back(r);
        }

        // Print header
        cout << left
            << setw(12) << "Algorithm"
            << setw(14) << "Page Faults"
            << setw(12) << "Fault %"
            << setw(14) << "Page Hits"
            << setw(12) << "Hit %"
            << setw(12) << "TLB Hits"
            << setw(12) << "TLB Hit %"
            << setw(14) << "Disk Writes"
            << setw(14) << "EAT (ns)" << endl;

        // Print each result
        for (const auto& r : results) {
            cout << left
                << setw(12) << r.name
                << setw(14) << r.pageFaults
                << setw(12) << fixed << setprecision(2) << r.HitRate
                << setw(14) << r.pageHits
                << setw(12) << fixed << setprecision(2) << r.faultRate
                << setw(12) << r.tlbHits
                << setw(12) << fixed << setprecision(2) << r.tlbHitRate
                << setw(14) << r.diskWrites
                << setw(14) << fixed << setprecision(2) << r.eat << endl;
        }

        cout << endl;

        // Find best algorithm
        auto bestEAT = min_element(results.begin(), results.end(),
            [](const Results& a, const Results& b) { return a.eat < b.eat; });

        auto bestFault = min_element(results.begin(), results.end(),
            [](const Results& a, const Results& b) { return a.pageFaults < b.pageFaults; });

        cout << "Summary:" << endl;
        cout << "  Best EAT:         " << bestEAT->name << " (" << fixed << setprecision(2) << bestEAT->eat << " ns)" << endl;
        cout << "  Best Fault Rate:  " << bestFault->name << " (" << fixed << setprecision(2) << bestFault->faultRate << "%)" << endl;
    }

    void runBeladyTest(uint32_t maxFrames = 8)
    {
        cout << "\n";
        cout << "BELADY'S ANOMALY TEST (FIFO)" << endl;
        cout << endl;

        cout << left
            << setw(10) << "Frames"
            << setw(15) << "Page Faults"
            << setw(12) << "Page Hits"
            << setw(10) << "TLB Hits"
            << setw(16) << "EAT (ns)"
            << "Anomaly?" << endl;

        uint32_t prevFaults = UINT32_MAX;

        for (uint32_t frames = 1; frames <= maxFrames; frames++) {

            // Full reconstruction
            pageTable.clear();
            physicalRAM = PhysicalRAM(frames);
            tlb = TLB(tlbSize);
            disk.reset();
            stats.reset();
            addrGenerator.reset();
            PhysicalAddresses.clear();
            PhysicalisWrite.clear();

            if (replacementAlgo) delete replacementAlgo;
            replacementAlgo = new FIFOReplacement(frames);

            addrGenerator.precomputeVPNsAndOffsets();

            const auto& vpns = addrGenerator.getVPNs();
            const auto& offsets = addrGenerator.getOffsets();
            const auto& validity = addrGenerator.getValidity();
            const auto& writeFlags = addrGenerator.getWriteFlags();

            for (size_t i = 0; i < vpns.size(); i++)
                processReference(vpns[i], offsets[i], writeFlags[i], validity[i], i + 1);

            uint32_t faults = stats.getPageFaults();
            bool anomaly = (prevFaults != UINT32_MAX && faults > prevFaults);

            cout << left
                << setw(10) << frames
                << setw(15) << faults
                << setw(12) << stats.getPageHits()
                << setw(10) << stats.getTLBHits()
                << setw(16) << fixed << setprecision(2) << stats.getEAT()
                << (anomaly ? "YES <--" : "NO") << endl;

            prevFaults = faults;
        }

        cout << endl;
        cout << "Note: Belady's Anomaly occurs when more frames cause MORE page faults." << endl;
    }

    void printFinalResults()
    {
        cout << "\n" << string(70, '=') << endl;
        cout << "FINAL RESULTS  [" << replacementAlgo->getName() << "]" << endl;
        cout << string(70, '=') << endl;

        stats.print();

        cout << "\n[EFFECTIVE ACCESS TIME]" << endl;
        cout << "  EAT = " << fixed << setprecision(2) << stats.getEAT() << " ns" << endl;

        cout << "\n" << string(70, '=') << endl;
        cout << "SIMULATION COMPLETE  [" << replacementAlgo->getName() << "]" << endl;
        cout << string(70, '=') << endl;
    }

};

// ============================================================================
// MAIN
// ============================================================================
int main()
{
    cout << "\n";
    cout << "+=====================================================+" << endl;
    cout << "|     VIRTUAL MEMORY MANAGEMENT SIMULATOR             |" << endl;
    cout << "|     TLB | Page Table | Physical RAM | Disk (WB)     |" << endl;
    cout << "+=====================================================+" << endl;

    Simulator sim;

    string configFile = "Config.txt";
    string traceFile = "ComparisonTrace.txt";

    if (!sim.loadConfiguration(configFile)) { cerr << "Failed to load config!" << endl; return 1; }
    if (!sim.loadTraceFile(traceFile)) { cerr << "Failed to load trace!" << endl;  return 1; }

    // Normal runs
    sim.runSimulation("LRU");
    system("pause");
    sim.reset();

    sim.runSimulation("FIFO");
    system("pause");
    sim.reset();

    sim.runSimulation("OPT");
    system("pause");

    // reset() before debug run
    sim.reset();

    // algorithm performance comparison
    sim.runComparison();
    system("pause");

    // Debug mode
    cout << "\n\n=== DEBUG MODE (step-by-step trace) ===" << endl;
    sim.setDebugMode(false);
    //sim.runSimulation("LRU");
    sim.setDebugMode(false);
    system("pause");

    // Belady's Anomaly comparison table
    sim.reset();
    sim.runBeladyTest(4);

    return 0;
}