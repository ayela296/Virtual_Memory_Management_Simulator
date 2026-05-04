#pragma once

#include <iostream>
#include <unordered_map>
#include <vector>
#include <iomanip>

using namespace std;

// ============================================================================
// STRUCT: Page Table Entry
// ============================================================================
struct PageTableEntry
{
    bool valid;         // Valid bit - is page in RAM?
    bool dirty;         // Dirty bit - has page been written                                      
    uint32_t frameNumber;

    PageTableEntry() : frameNumber(0) { valid = false; dirty = false; }
    PageTableEntry(uint32_t f, bool v, bool d) : frameNumber(f), valid(v), dirty(d) {}

    void print() const {
        cout << "Frame: " << frameNumber
            << ", Valid: " << valid
            << ", Dirty: " << dirty
            << endl;
    }
};

// ============================================================================
// CLASS 5: Page Table
// ============================================================================
class PageTable
{
private:
    unordered_map<uint32_t, PageTableEntry> pageTable;    // VPN -> PTE mapping
    uint32_t pagesize;
    uint32_t pageHits;
    uint32_t pageFaults;

public:
    PageTable(uint32_t pageSize) : pagesize(pageSize), pageHits(0), pageFaults(0) {}

    // Look up page table for VPN, return true if valid and set frameNumber and dirty flag
    bool pageTableLookup(uint32_t vpn, uint32_t& frameNumber, bool& valid, bool& dirty)
    {
        auto it = pageTable.find(vpn);

        if (it != pageTable.end() && it->second.valid) {
            frameNumber = it->second.frameNumber;
            valid = it->second.valid;
            dirty = it->second.dirty;
            pageHits++;                 //Page table hit
            return true;
        }

        // Page fault - page not in RAM
        pageFaults++;
        return false;
    }

    // Add or update a page table entry (on page load or after eviction)
    void insertPage(uint32_t vpn, uint32_t frameNumber, bool valid, bool isWrite)
    {
        auto it = pageTable.find(vpn);

        if (it != pageTable.end()) {
            // Update existing entry 
            it->second.valid = valid;
            it->second.frameNumber = frameNumber;
            it->second.dirty = isWrite;  // Dirty bit = isWrite from trace
        }
        else {
            // Create new entry
            PageTableEntry entry(frameNumber, valid, isWrite); // valid=valid, dirty=isWrite
            pageTable[vpn] = entry;
        }
    }

    // Invalidate a page (mark as not in RAM, but keep entry for future reference)
    void invalidatePage(uint32_t vpn)
    {
        auto it = pageTable.find(vpn);
        if (it != pageTable.end()) {
            it->second.valid = false;
        }
    }

    // Get frame number for a VPN (only if valid)
    uint32_t getFrameNumber(uint32_t vpn) const
    {
        auto it = pageTable.find(vpn);
        if (it != pageTable.end() && it->second.valid) {
            return it->second.frameNumber;
        }
        return 0xFFFFFFFF;  // Invalid frame number
    }

    // Check if a page is dirty (was written to)
    bool isDirty(uint32_t vpn) const
    {
        auto it = pageTable.find(vpn);
        if (it != pageTable.end() && it->second.valid) {
            return it->second.dirty;
        }
        return false;
    }

    // Check if a page is loaded in memory (valid bit = true)
    bool isPageLoaded(uint32_t vpn) const
    {
        auto it = pageTable.find(vpn);
        return (it != pageTable.end() && it->second.valid);
    }

    // ====== Statistics and Information ======

    size_t getSize() const {
        return pageTable.size();
    }

    // Get number of dirty entries (pages that were written to)
    size_t getDirtyEntryCount() const {
        size_t count = 0;
        for (const auto& entry : pageTable) {
            if (entry.second.dirty) count++;
        }
        return count;
    }

    // Get number of valid entries (pages currently in RAM)
    size_t getValidEntryCount() const {
        size_t count = 0;
        for (const auto& entry : pageTable) {
            if (entry.second.valid) count++;
        }
        return count;
    }

    // Get page fault count - FIXED: use pageFaults (not pageFaultCount)
    uint32_t getPageFaultCount() const { return pageFaults; }

    // Get page hit count - FIXED: use pageHits (not pageHitCount)
    uint32_t getPageHitCount() const { return pageHits; }

    // Get total page table accesses
    uint32_t getTotalAccesses() const { return pageFaults + pageHits; }

    // Get page hit rate
    double getHitRate() const {
        return (pageFaults + pageHits) > 0 ?
            (100.0 * pageHits / (pageFaults + pageHits)) : 0.0;
    }

    // Get page fault rate
    double getFaultRate() const {
        return (pageFaults + pageHits) > 0 ?
            (100.0 * pageFaults / (pageFaults + pageHits)) : 0.0;
    }

    // ====== Printing Functions ======

    // Print all page table entries
    void printPT() const {
        if (pageTable.empty()) {
            cout << "\n=== PAGE TABLE IS EMPTY ===" << endl;
            return;
        }

        cout << "\n=== PAGE TABLE CONTENTS ===" << endl;
        cout << left << setw(12) << "VPN"
            << setw(10) << "Frame"
            << setw(8) << "Valid"
            << setw(8) << "Dirty"
            << "   Source" << endl;
        cout << string(55, '-') << endl;

        for (const auto& entry : pageTable) {
            cout << "0x" << hex << setw(8) << entry.first << dec
                << setw(10) << entry.second.frameNumber
                << setw(8) << (entry.second.valid ? "Yes" : "No")
                << setw(8) << (entry.second.dirty ? "Yes" : "No")
                << "   " << (entry.second.dirty ? "from WRITE" : "from READ") << endl;
        }
        cout << "============================" << endl;
    }

    // Print only valid entries (pages currently in RAM)
    void printValidEntries() const {
        cout << "\n=== PAGES CURRENTLY IN RAM ===" << endl;
        for (const auto& entry : pageTable) {
            if (entry.second.valid) {
                cout << "VPN 0x" << hex << entry.first << dec
                    << " -> Frame " << entry.second.frameNumber;
                if (entry.second.dirty) {
                    cout << " [DIRTY - was WRITTEN to]";
                }
                else {
                    cout << " [CLEAN - only READ]";
                }
                cout << endl;
            }
        }
    }

    // Print page table statistics
    void printPTStats() const {
        cout << "\n=== PAGE TABLE STATISTICS ===" << endl;
        cout << "Total Entries: " << getSize() << endl;
        cout << "Valid Entries (in Page Table): " << getValidEntryCount() << endl;
        cout << "Valid Dirty Entries (were WRITTEN): " << getDirtyEntryCount() << endl;
        cout << "Valid Clean Entries (only READ): " << (getValidEntryCount() - getDirtyEntryCount()) << endl;
        cout << endl;
        cout << "Total Page Table Walks: " << getTotalAccesses() << endl;
        cout << "Page Hits: " << pageHits << endl;
        cout << "Page Faults: " << pageFaults << endl;
        cout << "Page Hit Rate: " << fixed << setprecision(2) << getHitRate() << "%" << endl;
        cout << "Page Fault Rate: " << fixed << setprecision(2) << getFaultRate() << "%" << endl;
        cout << "==============================" << endl;
    }

    // Print summary (compact view)
    void printSummary() const {
        cout << "\n[PageTable] Entries: " << getSize()
            << " | Dirty: " << getDirtyEntryCount()
            << " | Faults: " << pageFaults
            << " | Hits: " << pageHits
            << " | Hit Rate: " << fixed << setprecision(2) << getHitRate() << "%" << endl;
    }

    // ===== Utility Functions ======

    // Clear the entire page table
    void clear() {
        pageTable.clear();
        pageFaults = 0;
        pageHits = 0;
    }

    // Check if a VPN exists in page table (valid or invalid)
    bool exists(uint32_t vpn) const {
        return pageTable.find(vpn) != pageTable.end();
    }
};