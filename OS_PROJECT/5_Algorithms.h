#pragma once
#include <iostream>
#include <queue>
#include <unordered_map>
#include <vector>
#include <iomanip>
#include <list>
#include <algorithm>
#include <string>

using namespace std;

// ============================================================================
// BASE REPLACEMENT ALGORITHM (Abstract Class)
// ============================================================================
class ReplacementAlgorithm
{
protected:
    uint32_t numFrames;                     // Maximum number of frames
    unordered_map<uint32_t, bool> inMemory; // Track which VPNs are in RAM
    uint32_t pageHits;
    uint32_t pageFaults;

public:
    ReplacementAlgorithm(uint32_t frames = 0) : numFrames(frames), pageHits(0), pageFaults(0) {}

    virtual ~ReplacementAlgorithm() {}

    // Called when a page is accessed (PAGE HIT)
    virtual void pageAccessed(uint32_t vpn) = 0;

    // Called when a page is loaded into RAM (PAGE FAULT)
    virtual void pageLoaded(uint32_t vpn) = 0;

    // Select victim for eviction
    virtual uint32_t selectVictim() = 0;

    // Called when a page is evicted
    virtual void pageEvicted(uint32_t vpn) = 0;

    // Check if a page is in memory(RAM)
    virtual bool isInMemory(uint32_t vpn) const
    {
        return inMemory.find(vpn) != inMemory.end();
    }

    // Get current memory size
    virtual size_t getMemorySize() const
    {
        return inMemory.size();
    }

    // Check if eviction is needed
    virtual bool needsEviction() const
    {
        return inMemory.size() >= numFrames;
    }

    // Reset the algorithm
    virtual void reset() = 0;

    // Print algorithm state
    virtual void printState() const = 0;

    // Get algorithm name
    virtual string getName() const = 0;

    //Get Page Hit and Page Fault counts and numFrames
    uint32_t getNumFrames() const { return numFrames; }
    uint32_t getPageHits() const { return pageHits; }
    uint32_t getPageFaults() const { return pageFaults; }
};

// ============================================================================
// FIFO REPLACEMENT ALGORITHM (Child Class)
// ============================================================================
class FIFOReplacement : public ReplacementAlgorithm
{
private:
    queue<uint32_t> fifoQueue;  // Queue of VPNs in order of loading

public:
    FIFOReplacement(uint32_t frames = 0) : ReplacementAlgorithm(frames) {}

    void pageAccessed(uint32_t vpn) override
    {
        pageHits++;   //RAM hit - no change in order for FIFO
    }

    void pageLoaded(uint32_t vpn) override
    {
        pageFaults++;     //RAM fault - needs eviction
        if (inMemory.find(vpn) == inMemory.end()) //if not already in memory
        {
            fifoQueue.push(vpn);
            inMemory[vpn] = true;
        }
    }

    uint32_t selectVictim() override
    {
        if (fifoQueue.empty()) {
            return 0;
        }
        //use the frame number of the victim page for eviction
        return fifoQueue.front();        //select the oldest page (front of queue)
    }

    void pageEvicted(uint32_t victimvpn) override
    {
        if (!fifoQueue.empty() && fifoQueue.front() == victimvpn)
        {
            fifoQueue.pop();
            inMemory.erase(victimvpn);
        }
    }

    void reset() override
    {
        while (!fifoQueue.empty()) fifoQueue.pop();
        inMemory.clear();
        pageHits = 0;
        pageFaults = 0;
    }

    void printState() const override
    {
        cout << "    FIFO Queue (oldest -> newest): ";
        if (fifoQueue.empty()) {
            cout << "Empty";
        }
        else {
            queue<uint32_t> temp = fifoQueue;
            while (!temp.empty()) {
                cout << "0x" << hex << temp.front() << dec << " ";
                temp.pop();
                if (!temp.empty()) cout << "-> ";
            }
        }
        cout << " | Size: " << inMemory.size() << "/" << numFrames << endl;
    }

    string getName() const override
    {
        return "FIFO";
    }
};

// ============================================================================
// LRU REPLACEMENT ALGORITHM (Child Class)
// ============================================================================
class LRUReplacement : public ReplacementAlgorithm
{
private:
    list<uint32_t> lruList;                                     // Most recent at front
    unordered_map<uint32_t, list<uint32_t>::iterator> lruMap;   // VPN -> iterator

public:
    LRUReplacement(uint32_t frames = 0) : ReplacementAlgorithm(frames) {}

    void pageAccessed(uint32_t vpn) override
    {
        pageHits++;
        auto it = lruMap.find(vpn);
        if (it != lruMap.end()) {
            lruList.erase(it->second);
            lruList.push_front(vpn);
            lruMap[vpn] = lruList.begin();
        }
    }

    void pageLoaded(uint32_t vpn) override
    {
        pageFaults++;
        if (inMemory.find(vpn) == inMemory.end()) {
            lruList.push_front(vpn);
            lruMap[vpn] = lruList.begin();
            inMemory[vpn] = true;
        }
    }

    uint32_t selectVictim() override        //select victim
    {
        if (lruList.empty()) {
            return 0;
        }

        uint32_t victim = lruList.back();
        return victim;
    }

    void pageEvicted(uint32_t vpn) override
    {
        auto it = lruMap.find(vpn);
        if (it != lruMap.end()) {
            lruList.erase(it->second);
            lruMap.erase(it);
            inMemory.erase(vpn);
        }
    }

    void reset() override
    {
        lruList.clear();
        lruMap.clear();
        inMemory.clear();
        pageHits = 0;
        pageFaults = 0;
    }

    void printState() const override
    {
        cout << "    LRU Order (most recent -> least recent): ";
        for (uint32_t vpn : lruList) {
            cout << "0x" << hex << vpn << dec << " ";
        }
        cout << endl;
    }

    string getName() const override
    {
        return "LRU";
    }
};

// ============================================================================
// OPT REPLACEMENT ALGORITHM (Child Class)
// ============================================================================
class OPTReplacement : public ReplacementAlgorithm
{
private:
    vector<uint32_t> futureReferences;   // Future reference string
    size_t currentPosition;              // Current position in reference string
    vector<uint32_t> currentPages;       // Pages currently in memory

    // Helper: Find when a page will be used next (returns -1 if never used again)
    int findNextUse(uint32_t vpn, size_t startPos) const
    {
        for (size_t pos = startPos + 1; pos < futureReferences.size(); pos++) {
            if (futureReferences[pos] == vpn) {
                return static_cast<int>(pos);
            }
        }
        return -1;  // Never used again - perfect victim candidate
    }
public:
    OPTReplacement(uint32_t frames = 0) : ReplacementAlgorithm(frames), currentPosition(0) {}

    // Set future references (call before simulation)
    void setFutureReferences(const vector<uint32_t>& references)
    {
        futureReferences = references;
    }

    void pageAccessed(uint32_t vpn) override
    {
        pageHits++;
    }

    void pageLoaded(uint32_t vpn) override
    {
        pageFaults++;
        if (inMemory.find(vpn) == inMemory.end())       //if not already in memory
        {
            inMemory[vpn] = true;
            currentPages.push_back(vpn);
        }
    }

    uint32_t selectVictim() override
    {
        if (currentPages.empty()) {
            return 0;
        }

        int victimIndex = -1;
        int farthestUse = -1;

        for (size_t i = 0; i < currentPages.size(); i++) {
            uint32_t candidate = currentPages[i];
            int nextUse = findNextUse(candidate, currentPosition);

            if (nextUse == -1) {
                // Never used again - perfect victim
                victimIndex = i;
                break;
            }

            if (nextUse > farthestUse) {
                farthestUse = nextUse;
                victimIndex = i;
            }
        }

        if (victimIndex != -1) {
            uint32_t victim = currentPages[victimIndex];
            return victim;
        }

        return 0;
    }

    void pageEvicted(uint32_t vpn) override
    {
        auto it = find(currentPages.begin(), currentPages.end(), vpn);
        if (it != currentPages.end()) {
            currentPages.erase(it);
            inMemory.erase(vpn);
        }
    }

    bool isInMemory(uint32_t vpn) const override
    {
        return find(currentPages.begin(), currentPages.end(), vpn) != currentPages.end();
    }

    void advancePosition()
    {
        currentPosition++;
    }

    void reset() override
    {
        currentPosition = 0;
        inMemory.clear();
        currentPages.clear();
        pageHits = 0;
        pageFaults = 0;
    }

    void printState() const override
    {
        cout << "    OPT Current pages in memory: ";
        if (currentPages.empty()) {
            cout << "(empty)";
        }
        else {
            for (uint32_t vpn : currentPages) {
                cout << "0x" << hex << vpn << dec << " ";
            }
        }
        cout << "| Position: " << currentPosition << "/" << futureReferences.size() << endl;
        cout << endl;
    }

    string getName() const override
    {
        return "OPT";
    }

    size_t getMemorySize() const override
    {
        return currentPages.size();
    }

    bool needsEviction() const override
    {
        return currentPages.size() >= numFrames;
    }
};
