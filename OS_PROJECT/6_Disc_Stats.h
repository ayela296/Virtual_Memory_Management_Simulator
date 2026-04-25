#pragma once
#include <iostream>
#include <queue>
#include <unordered_map>
#include <vector>
#include <iomanip>
#include <list>
#include <algorithm>
using namespace std;

// ============================================================================
// VIRTUAL DISK MODEL (Write-Back)
// ============================================================================
class VirtualDisk
{
private:
    uint32_t diskWriteCount;      // Number of dirty pages written to disk
    uint32_t diskReadCount;       // Number of pages read from disk

public:
    VirtualDisk() : diskWriteCount(0), diskReadCount(0) {}

    // Simulate writing a dirty page to disk (on eviction)
    void writeDirtyPage(uint32_t vpn)
    {
        diskWriteCount++;
    }

    // Simulate reading a page from disk (on page fault)
    void readPage(uint32_t vpn)
    {
        diskReadCount++;
    }

    // Getters
    uint32_t getWriteCount() const { return diskWriteCount; }
    uint32_t getReadCount() const { return diskReadCount; }

    void reset()
    {
        diskWriteCount = 0;
        diskReadCount = 0;
    }

    void printStats() const
    {
        cout << "    Disk Writes (Dirty evictions): " << diskWriteCount << endl;
        cout << "    Disk Reads (Page faults): " << diskReadCount << endl;
    }
};

class StatsEngine {
private:
    uint32_t totalReferences;
    uint32_t tlbHits;
    uint32_t tlbMisses;
    uint32_t pageHits;
    uint32_t pageFaults;
    uint32_t diskReads;
    uint32_t diskWrites;
    const double TLB_Latency;
    const double RAM_Latency;
    const double Disk_Latency;
    double totalTime;  // in nanoseconds

public:
    StatsEngine(double tlbLatency, double ramLatency, double diskLatency)
        : totalReferences(0), tlbHits(0), tlbMisses(0), pageHits(0), pageFaults(0), diskReads(0), diskWrites(0),
        TLB_Latency(tlbLatency), RAM_Latency(ramLatency), Disk_Latency(diskLatency), totalTime(0) {
    }

    void recordTLBHit() {
        tlbHits++;
        totalTime += TLB_Latency;
    }

    void recordTLBMiss() {
        tlbMisses++;
    }


    void recordPageHit() {
        pageHits++;
        totalTime += TLB_Latency + RAM_Latency;
    }

    void recordPageFault() {
        pageFaults++;
        diskReads++;
        totalTime += TLB_Latency + RAM_Latency + Disk_Latency;
    }

    void recordDirtyWrite() {   //check on eviction if the page is dirty, if so we write it back to disk    
        diskWrites++;
        totalTime += Disk_Latency;
    }

    void incrementReference() {
        totalReferences++;
    }
    double getTLBHitRate() const {
        return totalReferences > 0 ? (double)tlbHits / totalReferences * 100.0 : 0.0;
    }

    double getPageHitRate() const {
        return totalReferences > 0 ? (double)pageHits / totalReferences * 100.0 : 0.0;
    }

    double getPageFaultRate() const {
        return totalReferences > 0 ? (double)pageFaults / totalReferences * 100.0 : 0.0;
    }

    double getEAT() const {
        return totalReferences > 0 ? totalTime / totalReferences : 0.0;
    }

    double getAverageAccessTime() const {
        return getEAT();
    }

    uint32_t getTotalReferences() const { return totalReferences; }
    uint32_t getTLBHits() const { return tlbHits; }
    uint32_t getTLBMisses() const { return tlbMisses; }
    uint32_t getPageFaults() const { return pageFaults; }
    uint32_t getDiskReads() const { return diskReads; }
    uint32_t getDiskWrites() const { return diskWrites; }
    double getTotalTime() const { return totalTime; }

    void reset() {
        totalReferences = 0;
        tlbHits = 0;
        tlbMisses = 0;
        pageHits = 0;
        pageFaults = 0;
        diskReads = 0;
        diskWrites = 0;
        totalTime = 0;
    }

    void print() const {
        cout << "\n=== STATS ENGINE ===" << endl;
        cout << "Total References: " << totalReferences << endl;
        cout << "TLB Hits: " << tlbHits << endl;
        cout << "TLB Misses: " << tlbMisses << endl;
        cout << "TLB Hit Rate: " << fixed << setprecision(2) << getTLBHitRate() << "%" << endl;
        cout << "Page Hits: " << pageHits << endl;
        cout << "Page Faults: " << pageFaults << endl;
        cout << "Page Fault Rate: " << fixed << setprecision(2) << getPageFaultRate() << "%" << endl;
        cout << "Page Hit Rate: " << fixed << setprecision(2) << getPageHitRate() << "%" << endl;
        cout << "Disk Reads: " << diskReads << endl;
        cout << "Disk Writes (Dirty): " << diskWrites << endl;
        cout << "Total Simulated Time: " << totalTime << " ns   " << ( totalTime/1000000) << "ms" << endl;
        cout << "Average Access Time: " << getEAT() << "ns   " << ( getEAT() / 1000000) << "ms" << endl;
    }
};