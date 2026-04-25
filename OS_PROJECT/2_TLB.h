#pragma once

#include <iostream>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <queue>
#include <list>
#include <iomanip>
using namespace std;

// ============================================================================
// STRUCT: TLB Entry
// ============================================================================
struct TLBEntry
{
    uint32_t vpn;
    uint32_t frameNumber;
    bool valid;
    bool dirty;

    TLBEntry() : vpn(0), frameNumber(0), valid(false), dirty(false) {}
    TLBEntry(uint32_t v, uint32_t f, bool valid, bool dirty) : vpn(v), frameNumber(f), valid(valid), dirty(dirty) {}

    void print() const {
        cout << "VPN: " << vpn
            << ", Frame: " << frameNumber
            << ", Valid: " << valid
            << ", Dirty: " << dirty
            << endl;
    }
};

class TLB
{
private:
    vector<TLBEntry> tlbEntries;
    uint32_t tlbsize;
    uint32_t hitcount;
    uint32_t misscount;

    //for lru replacement - maintain a list of VPNs in order of access (LRU Algorithm)
    list<uint32_t> lruList;  // Most recently used at front
    unordered_map<uint32_t, list<uint32_t>::iterator> lruMap; // VPN -> iterator in lruList

    //HELPER FUNCTIONS FOR LRU REPLACEMENT
    // update LRU order on access
    void updateLRU(uint32_t vpn)
    {
        auto it = lruMap.find(vpn);
        if (it != lruMap.end()) {
            // Remove from current position
            lruList.erase(it->second);
            // Move to front (most recently used)
            lruList.push_front(vpn);
            lruMap[vpn] = lruList.begin();
        }
    }

    // Add a new VPN to the LRU list
    void addToLRU(uint32_t vpn)
    {
        lruList.push_front(vpn);
        lruMap[vpn] = lruList.begin();
    }

public:
    TLB(uint32_t tlbsize) : tlbsize(tlbsize), hitcount(0), misscount(0)
    {
        tlbEntries.resize(tlbsize);
    }

    //look up TLB for VPN, return true if hit and set frameNumber and dirty flag
    bool TLBlookup(uint32_t vpn, uint32_t& frameNumber, bool& valid, bool& dirty)
    {
        for (const auto& entry : tlbEntries) {
            if (entry.valid && entry.vpn == vpn) {
                frameNumber = entry.frameNumber;
                valid = entry.valid;
                dirty = entry.dirty;
                hitcount++;             //TLB hit
                updateLRU(vpn);
                return true;
            }
        }
        misscount++;                    //TLB miss     
        return false;
    }

    // Invalidate a specific VPN from TLB (when page is evicted from RAM)
    void invalidate(uint32_t vpn)
    {
        for (uint32_t i = 0; i < tlbsize; i++) {
            if (tlbEntries[i].valid && tlbEntries[i].vpn == vpn)
            {
                tlbEntries[i].valid = false;
                // Also remove from LRU list
                auto it = lruMap.find(vpn);
                if (it != lruMap.end()) {
                    lruList.erase(it->second);
                    lruMap.erase(it);
                }
                break;
            }
        }
    }

    // Insert or update a TLB entry (called on page load or after eviction)
    void insert(uint32_t vpn, uint32_t frameNumber, bool valid, bool dirty)
    {
        // First, check if this VPN already exists in TLB (valid or invalid)
        for (uint32_t i = 0; i < tlbsize; i++) {
            if (tlbEntries[i].valid && tlbEntries[i].vpn == vpn) {
                // Update existing entry
                tlbEntries[i].frameNumber = frameNumber;
                tlbEntries[i].valid = valid;
                tlbEntries[i].dirty = dirty;
                updateLRU(vpn);
                return;
            }
        }
        // Find an invalid slot first
        for (uint32_t i = 0; i < tlbsize; i++) {
            if (!tlbEntries[i].valid) {
                tlbEntries[i] = TLBEntry(vpn, frameNumber, valid, dirty);
                addToLRU(vpn);
                return;
            }
        }
        // No invalid slots - need to evict least recently used entry
        uint32_t victimVPN = lruList.back();
        lruList.pop_back();
        lruMap.erase(victimVPN);

        // Find and replace the victim entry
        for (uint32_t i = 0; i < tlbsize; i++)
        {
            if (tlbEntries[i].vpn == victimVPN)
            {
                tlbEntries[i] = TLBEntry(vpn, frameNumber, valid, dirty);
                addToLRU(vpn);
                return;
            }
        }
    }

    // Get the current LRU order (for debugging)
    void printTLBOrder() const
    {
        cout << " TLB Order (LRU)(most recent -> least recent): ";
        if (lruList.empty()) {
            cout << "(empty)";
        }
        else {
            for (uint32_t vpn : lruList) {
                cout << "0x" << hex << vpn << dec << " ";
            }
            cout << endl;
        }
    }

    // Print TLB contents
    void printTLB() const {
        cout << "\n=== TLB CONTENTS (LRU Replacement) ===" << endl;
        cout << left << setw(8) << "Slot"
            << setw(14) << "VPN"
            << setw(10) << "Frame"
            << setw(8) << "Valid"
            << setw(8) << "Dirty" << endl;
        cout << string(48, '-') << endl;

        for (uint32_t i = 0; i < tlbsize; i++) {
            cout << left << setw(8) << i
                << "0x" << hex << setw(10) << tlbEntries[i].vpn << dec
                << setw(10) << tlbEntries[i].frameNumber
                << setw(8) << (tlbEntries[i].valid ? "Yes" : "No")
                << setw(8) << (tlbEntries[i].dirty ? "Yes" : "No") << endl;
        }
        cout << "================================" << endl;

        printTLBOrder();
    }

    // Get statistics
    uint32_t gettlbsize() const { return tlbsize; }
    uint32_t getHitCount() const { return hitcount; }
    uint32_t getMissCount() const { return misscount; }
    uint32_t getTotalAccesses() const { return hitcount + misscount; }
    double getHitRate() const {
        return (hitcount + misscount) > 0 ?
            (100.0 * hitcount / (hitcount + misscount)) : 0.0;
    }

    void printStats() const {
        cout << "\n=== TLB LRU STATISTICS ===" << endl;
        cout << "TLB Size: " << tlbsize << " entries" << endl;
        cout << "Total Accesses: " << getTotalAccesses() << endl;
        cout << "Hits: " << getHitCount() << endl;
        cout << "Misses: " << getMissCount() << endl;
        cout << "Hit Rate: " << fixed << setprecision(2) << getHitRate() << "%" << endl;
        cout << "==========================" << endl;
    }

    void reset() {
        for (uint32_t i = 0; i < tlbsize; i++)
        {
            tlbEntries[i].valid = false;
            tlbEntries[i].vpn = 0;
            tlbEntries[i].frameNumber = 0;
            tlbEntries[i].dirty = false;
        }                       //keep the same size, just invalidate all entries
        lruList.clear();
        lruMap.clear();
        hitcount = 0;
        misscount = 0;
        cout << "TLB: Cleared all entries." << endl;
    }
};