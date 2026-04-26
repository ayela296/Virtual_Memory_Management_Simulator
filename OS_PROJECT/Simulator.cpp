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
            cout << "\n[WARNING] Unknown algorithm '" << algorithm << "'. Using LRU." << endl;
            replacementAlgo = new LRUReplacement(numFrames);
        }
    }

    void precomputeAddresses()
    {
        addrGenerator.precomputeVPNsAndOffsets();
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
        cout << "\n" << string(70, '=') << endl;
        cout << "STARTING SIMULATION  [" << algorithm << "]" << endl;
        cout << string(70, '=') << endl;

        precomputeAddresses();
        setReplacementAlgorithm(algorithm);

        const auto& vpns = addrGenerator.getVPNs();
        const auto& offsets = addrGenerator.getOffsets();
        const auto& validity = addrGenerator.getValidity();
        const auto& writeFlags = addrGenerator.getWriteFlags();

        cout << "\nSimulating " << vpns.size() << " references..." << endl;
        cout << string(70, '-') << endl;

        OPTReplacement* opt = dynamic_cast<OPTReplacement*>(replacementAlgo);
        if (opt) opt->setFutureReferences(vpns);

        cout << endl;

        for (size_t i = 0; i < vpns.size(); i++) {
            processReference(vpns[i], offsets[i], writeFlags[i], validity[i], i + 1);
            if (opt) opt->advancePosition();
        }

        printFinalResults();
    }

    void runBeladyTest(uint32_t maxFrames = 8)
    {
        cout << "\n" << string(70, '=') << endl;
        cout << "BELADY'S ANOMALY TEST  (FIFO, frames 1.." << maxFrames << ")" << endl;
        cout << string(70, '-') << endl;
        cout << left
            << setw(14) << "Frames"
            << setw(15) << "Page Faults"
            << setw(12) << "TLB Hits"
            << setw(16) << "EAT (ns)"
            << "Belady?" << endl;
        cout << string(70, '-') << endl;

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
                << setw(14) << frames
                << setw(15) << faults
                << setw(12) << stats.getTLBHits()
                << setw(16) << fixed << setprecision(2) << stats.getEAT()
                << (anomaly ? "<-- BELADY'S ANOMALY!" : "") << endl;

            prevFaults = faults;
        }

        cout << string(70, '=') << endl;
        cout << "Note: Belady's Anomaly only occurs with FIFO, never with LRU or OPT." << endl;

        //original frame count
        physicalRAM = PhysicalRAM(numFrames);
        tlb = TLB(tlbSize);
        if (replacementAlgo) { delete replacementAlgo; replacementAlgo = nullptr; }
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
    string traceFile = "Trace_Memory_Check.txt";

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

    // Debug mode
    cout << "\n\n=== DEBUG MODE (step-by-step trace) ===" << endl;
    sim.setDebugMode(true);
    sim.runSimulation("LRU");
    sim.setDebugMode(false);
    system("pause");

    // Belady's Anomaly comparison table
    sim.reset();
    sim.runBeladyTest(8);

    return 0;
}