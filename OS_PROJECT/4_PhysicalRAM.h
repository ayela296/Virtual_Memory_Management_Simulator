#pragma once
#include <iostream>
#include <queue>
#include <vector>
#include <iomanip>
using namespace std;

class PhysicalRAM
{
private:
    uint32_t numFrames;
    uint32_t allocatedFramesCount;
    queue<uint32_t> freeFrames; // true = free, false = occupied
    vector<bool> allocatedFrames; // true = allocated at some point, false = never used (for statistics)
    vector<bool> dirtyFrames;     // true = frame has been written to, false = only read (for statistics)
public:
    // Constructor - initializes RAM with given number of frames
    PhysicalRAM(uint32_t frameCount) : numFrames(frameCount), allocatedFramesCount(0)
    {
        allocatedFrames.resize(numFrames, false);
        dirtyFrames.resize(numFrames, false);

        // Initialize free frames queue (all frames are free initially)
        for (uint32_t i = 0; i < numFrames; i++) {
            freeFrames.push(i);
        }
    }

    // Allocate a free frame - returns true and sets frameNumber if successful
    bool allocateFreeFrame(uint32_t& frameNumber, bool Dirty)
    {
        if (freeFrames.empty()) {
            return false;
        }

        frameNumber = freeFrames.front();
        freeFrames.pop();
        allocatedFrames[frameNumber] = true;
        dirtyFrames[frameNumber] = Dirty;  // New frame starts with the specified dirty status
        allocatedFramesCount++;
        return true;
    }

    // Allocate a specific frame (used when reusing evicted frame)
    bool allocateSpecificFrame(uint32_t frameNumber, bool Dirty)
    {
        if (frameNumber >= numFrames) {return false;}

        if (allocatedFrames[frameNumber]) {return false;}

        //// Rebuild free queue, skipping frameNumber once
        //queue<uint32_t> temp;
        //bool found = false;
        //while (!freeFrames.empty()) {
        //    uint32_t f = freeFrames.front();
        //    freeFrames.pop();
        //    if (f == frameNumber && !found) {
        //        found = true;   // drop it - this is the one we are allocating
        //    }
        //    else {
        //        temp.push(f);
        //    }
        //}
        //freeFrames = temp;

        //if (!found) return false;   // frame was not actually free

        //allocatedFrames[frameNumber] = true;
        //dirtyFrames[frameNumber] = Dirty;
        //allocatedFramesCount++;
        //return true;

        //Fault: Blindly removes one frame which could be incorrect if 
        // queue front is not same frame number
        allocatedFrames[frameNumber] = true;
        dirtyFrames[frameNumber] = Dirty;  
        //Set dirty status based on whether this frame is being reused after eviction
        allocatedFramesCount++;
        freeFrames.pop(); 
        return true;
    }

    // Free a frame (make it available again)
    bool freeFrame(uint32_t frameNumber)
    {
        if (frameNumber >= numFrames) {
            return false;
        }

        if (!allocatedFrames[frameNumber]) {
            return false;
        }

        allocatedFrames[frameNumber] = false;
        dirtyFrames[frameNumber] = false;  // Reset dirty bit
        freeFrames.push(frameNumber);
        allocatedFramesCount--;
        return true;
    }

    // Check if a frame is allocated
    bool isFrameAllocated(uint32_t frameNumber) const
    {
        return (frameNumber < numFrames && allocatedFrames[frameNumber]);
    }

    // Check if there are free frames
    bool hasFreeFrames() const
    {
        return !freeFrames.empty();
    }

    // Get number of free frames
    uint32_t getFreeFrameCount() const
    {
        return freeFrames.size();
    }

    // Get number of allocated frames
    uint32_t getAllocatedFrameCount() const
    {
        return allocatedFramesCount;
    }

    // Get total number of frames
    uint32_t getTotalFrames() const
    {
        return numFrames;
    }

    // Get number of dirty frames
    uint32_t getDirtyFrameCount() const
    {
        uint32_t count = 0;
        for (uint32_t i = 0; i < numFrames; i++) {
            if (allocatedFrames[i] && dirtyFrames[i]) {
                count++;
            }
        }
        return count;
    }

    // Mark a frame as dirty (when write occurs on existing page)
    void markDirtyBit(uint32_t frameNumber, bool isWrite)
    {
        if (frameNumber < numFrames && allocatedFrames[frameNumber]) {
            dirtyFrames[frameNumber] = isWrite;
        }
    }

    bool isDirty(uint32_t frameNumber) const
    {
        if (frameNumber < numFrames && allocatedFrames[frameNumber]) {
            return dirtyFrames[frameNumber];
        }
        return false;
    }

    uint32_t getnumFrames() const
    {
        return numFrames;
    }

    void reset()
    {
        // Reset all frames
        for (uint32_t i = 0; i < numFrames; i++) {
            allocatedFrames[i] = false;
            dirtyFrames[i] = false;
        }

        // Rebuild free frames queue
        while (!freeFrames.empty()) {
            freeFrames.pop();
        }
        for (uint32_t i = 0; i < numFrames; i++) {
            freeFrames.push(i);
        }

        allocatedFramesCount = 0;
        cout << "PhysicalRAM: Reset - all frames freed" << endl;
    }

    // Print RAM status
    void printRAMDetails() const
    {
        cout << "\n=== PHYSICAL RAM STATUS ===" << endl;
        cout << "Total Frames: " << numFrames << endl;
        cout << "Allocated Frames: " << allocatedFramesCount << endl;
        cout << "Free Frames: " << freeFrames.size() << endl;
        cout << "Dirty Frames: " << getDirtyFrameCount() << endl;
        cout << "===========================" << endl;
    }

    // Print detailed frame information
    void printRAM() const
    {
        cout << "\n=== PHYSICAL RAM DETAILS ===" << endl;
        cout << left << setw(8) << "Frame"
            << setw(12) << "Allocated"
            << setw(8) << "Dirty" << endl;
        cout << string(28, '-') << endl;

        for (uint32_t i = 0; i < numFrames; i++) {
            cout << left << setw(8) << i
                << setw(12) << (allocatedFrames[i] ? "Yes" : "No")
                << setw(8) << (dirtyFrames[i] ? "Yes" : "No") << endl;
        }
        cout << "===========================" << endl;
    }

    // Print allocated frames only
    void printAllocatedFrames() const
    {
        cout << "\n=== ALLOCATED FRAMES ===" << endl;
        for (uint32_t i = 0; i < numFrames; i++) {
            if (allocatedFrames[i]) {
                cout << "Frame " << i;
                if (dirtyFrames[i]) {
                    cout << " [DIRTY]";
                }
                cout << endl;
            }
        }
        if (allocatedFramesCount == 0) {
            cout << "No frames allocated" << endl;
        }
    }

    // Print free frames list
    void printFreeFrames() const
    {
        cout << "\n=== FREE FRAMES ===" << endl;
        queue<uint32_t> temp = freeFrames;
        if (temp.empty()) {
            cout << "No free frames available" << endl;
        }
        else {
            cout << "Free frames: ";
            while (!temp.empty()) {
                cout << temp.front() << " ";
                temp.pop();
            }
            cout << endl;
        }
    }
};