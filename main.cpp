/*

Sean Jones
CPE 517 Final Project
Snooping Cache Coherency Simulator

Creating a custom memory structure for this program to fully simulate cache coherency
Setting a main memory size of 16 kB with 4-byte blocks
Fully Associative Cache of 256 bytes with a block size of 4 bytes
Cache will also be write through and write allocate
256/4 = 64 cache lines
Since fully associative, no index needed in cache address
Incoming address to the cache will be divided into bits for offset and tag
Offset corresponds to the bits used to determine the byte to be accessed from the cache line
Only 2 offset bits will be needed to address the 4 bytes of the block
Tag will use 12 bits
Valid bit will be at the beginning of the address
Will use a random replacement policy
There will be a global cache for all cores to access
Each core will also have a local cache
The local cache will have an additional shared bit in the cache address
There will be a bus object that will be continually monitored by all cores

*/

#include <iostream>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <mutex>

using namespace std;

// Define simulation values
#define NUM_CORES 4
#define CACHE_SIZE 64 // Number of  blocks
const int MEM_SIZE = 2 << 13; // 16 kB

// Create main memory and a way to read and write to it
uint8_t main_memory[MEM_SIZE];
// Read and write blocks of memory
// Using little endianess
uint32_t read_memory(int addr){
    if (addr < MEM_SIZE)
        return (main_memory[addr]) +
               (main_memory[addr+1] << 8) +
               (main_memory[addr+2] << 16) +
               (main_memory[addr+3] << 24);
    else {
        printf("Main Memory Read: Index out of range\n");
        return 0;
    }
}
void write_memory(int addr, uint32_t data){
    if (addr < MEM_SIZE) {
        main_memory[addr] = data & 0xff;
        main_memory[addr+1] = (data >> 8) & 0xff;
        main_memory[addr+2] = (data >> 16) & 0xff;
        main_memory[addr+3] = (data >> 24) & 0xff;
    }
    else
        printf("Main Memory Write: Index out of range\n");
}

class Global_Cache{
public:
    // Using block size of 64 bytes
    map<uint16_t, uint32_t> cache_memory;
    // Create a map iterator
    map<uint16_t, uint32_t>::iterator it;
    // Keep counts of read and writes (along with their misses)
    int readCnt;
    int readMissCnt;
    int writeCnt;
    int writeMissCnt;
    Global_Cache() {
        // Construct cache memory based off the given size
        for (int i=0; i<CACHE_SIZE; i++) {
            // bit 15: valid bit (start zero)
            // bits 14: unused for now
            // bits 13-2: tag
            // bits 1-0: offset
            uint16_t addr = i << 2; //random values for now
            cache_memory.insert(pair<uint16_t, uint32_t>(addr, 0));
        }
    }
    // Use random address to replace a block
    map<uint16_t, uint32_t>::iterator getRandomBlock() {
        // Choose a random index through the cache
        int j = rand() % CACHE_SIZE;
        int cnt = 0;
        map<uint16_t, uint32_t>::iterator temp;
        for (temp = cache_memory.begin(); temp != cache_memory.end(); temp++) {
            if (j == cnt) {
                return temp;
            }
            cnt++;
        }
    }
    bool isValid(uint16_t addr) {
        // bit 15 is the valid bit
        return addr & 0x8000;
    }
    uint16_t setValid(uint16_t addr) {
        // Set the valid bit
        return addr | 0x8000;
    }
    uint16_t getTag(uint16_t addr) {
        // Return bits 13-2
        return (addr & 0x3ffc) >> 2;
    }
    uint8_t getOffset(uint16_t addr) {
        // Return bits 1-0
        return addr & 0x0003;
    }
    uint8_t readByte(uint32_t data, uint8_t offset) {
        if (offset == 0b00) {
            return data & 0x000000ff;
        }
        else if (offset == 0b01) {
            return (data & 0x0000ff00) >> 8;
        }
        else if (offset == 0b10) {
            return (data & 0x00ff0000) >> 16;
        }
        else if (offset == 0b11) {
            return (data & 0xff000000) >> 24;
        }
        else {
            printf("Invlaid read offset value\n");
            return 0;
        }
    }
    uint32_t writeByte(uint32_t block, uint8_t data, uint8_t offset) {
        if (offset == 0b00) {
            return (block & 0xffffff00) + data;
        }
        else if (offset == 0b01) {
            return (block & 0xffff00ff) + (data << 8);
        }
        else if (offset == 0b10) {
            return (block & 0xff00ffff) + (data << 16);
        }
        else if (offset == 0b11) {
            return (block & 0x00ffffff) + (data << 24);
        }
        else {
            printf("Invlaid write offset value\n");
            return 0;
        }
    }
    void update_cache(uint16_t addr, uint32_t block) {
        // Read and Write method will handle all the checking for this
        // Assuming addr is not in the cache
        // Find an invalid line
        for (it = cache_memory.begin(); it != cache_memory.end(); it++) {
            if(!isValid(it->first)) {
                // Remove invalid line from cache
                cache_memory.erase(it);
                // Insert new line
                cache_memory.insert(pair<uint16_t, uint32_t>(setValid(addr), block));
                return;
            }
        }
        // Use random replacement if all slots are valid
        it = getRandomBlock();
        // Remove line from cache
        cache_memory.erase(it);
        // Insert new line
        cache_memory.insert(pair<uint16_t, uint32_t>(setValid(addr), block));
    }
    // Use offset in address to return a byte in the cache
    uint8_t read_cache(uint16_t addr) {
        readCnt++;
        // Check tags in cache
        for (it = cache_memory.begin(); it != cache_memory.end(); it++) {
            // Compare tag with address
            if(getTag(it->first) == getTag(addr)) {
                // Make sure it is valid
                if (isValid(it->first))
                    // Read the byte specified by the offset
                    return readByte(it->second, getOffset(addr));
                else
                    break;
            }
        }
        // This is a miss
        readMissCnt++;
        // Get the value from memory
        uint32_t block = read_memory(addr);
        // Populate the cache for next time
        update_cache(addr, block);
        // Return value
        return readByte(block, getOffset(addr));
    }
    // Return whole cache block
    uint32_t read_cache_block(uint16_t addr) {
        readCnt++;
        // Check tags in cache
        for (it = cache_memory.begin(); it != cache_memory.end(); it++) {
            // Compare tag with address
            if(getTag(it->first) == getTag(addr)) {
                // Make sure it is valid
                if (isValid(it->first))
                    // Read the byte specified by the offset
                    return it->second;
                else
                    break;
            }
        }
        // This is a miss
        readMissCnt++;
        // Get a block from memory
        uint32_t block = read_memory(getTag(addr));
        // Populate the cache for next time
        update_cache(addr, block);
        // Return value
        return block;
    }
    // Use offset in address to write a byte in a cache block
    void write_cache(uint16_t addr, uint8_t data) {
        writeCnt++;
        uint32_t block;
        // Check tags in cache
        for (it = cache_memory.begin(); it != cache_memory.end(); it++) {
            // Compare tag with address
            if(getTag(it->first) == getTag(addr)) {
                // See if it is valid
                if (isValid(it->first)) {
                    // Write the byte into the cache block
                    block = writeByte(it->second, data, getOffset(addr));
                    // Write new data
                    it->second = block;
                    // Also write through
                    write_memory(addr, block);
                    return;
                }
                else {
                    // The cache block needs to be updated before writing a byte to it
                    break;
                }
            }
        }
        // This is a write miss
        writeMissCnt++;
        // Get the block from memory first
        block = writeByte(read_memory(getTag(addr)), data, getOffset(addr));
        // Allocate data into the cache
        update_cache(addr, block);
        // Also write through
        write_memory(addr, block);
    }
    // Write whole cache block
    void write_cache_block(uint16_t addr, uint32_t data) {
        writeCnt++;
        uint32_t block;
        // Check tags in cache
        for (it = cache_memory.begin(); it != cache_memory.end(); it++) {
            // Compare tag with address
            if(getTag(it->first) == getTag(addr)) {
                it->second = data;
                // Also write through
                write_memory(addr, data);
                return;
            }
        }
        // This is a write miss
        writeMissCnt++;
        // Allocate data into the cache
        update_cache(addr, data);
        // Also write through
        write_memory(addr, data);
    }
};
Global_Cache globalCache; // Create a global cache that all cores can use

// Create objects used in snooping
class Bus; // Pre define the bus so local cache can store a reference to it
enum MessageType {
    ReadMiss,
    WriteMiss,
    Invalidate
};
class Message {
public:
    MessageType type;
    int core;
    uint16_t addr;
    uint8_t data;
    Message* next;
    Message() {
        // Set some default values
        type = ReadMiss;
        addr = 0;
        data = 0;
        next = NULL;
    }
    Message(const Message& mes) {
        // Set some default values
        type = mes.type;
        addr = mes.addr;
        data = mes.data;
        next = NULL;
    }
};

// Implement local cache that looks at a shared bit
// Writes will also be to upper level cache rather than main memory
class Local_Cache{
public:
    // Using block size of 64 bytes
    map<uint16_t, uint32_t> cache_memory;
    // Create a map iterator
    map<uint16_t, uint32_t>::iterator it;
    // Store a pointer to the bus object to send messages after read and writes
    Bus* bus;
    // Multiple places are writing to cache
    mutex mtx; // prevent race condition
    // Store core Number
    int cn;
    // Keep counts of read and writes (along with their misses)
    int readCnt;
    int readMissCnt;
    int writeCnt;
    int writeMissCnt;
    Local_Cache(Bus* coreBus, int coreNumber) {
        // Save the passed in bus
        bus = coreBus;
        // Save the passed in core number
        cn = coreNumber;
        // Construct cache memory based off the given size
        for (int i=0; i<CACHE_SIZE; i++) {
            // bit 15: valid bit (start zero)
            // bits 14: shared bit (start zero)
            // bits 13-2: tag
            // bits 1-0: offset
            uint16_t addr = i << 2; // random values for now
            cache_memory.insert(pair<uint16_t, uint32_t>(addr, 0));
        }
    }
    // Use random address to replace a block
    map<uint16_t, uint32_t>::iterator getRandomBlock() {
        // Choose a random index through the cache
        int j = rand() % CACHE_SIZE;
        int cnt = 0;
        map<uint16_t, uint32_t>::iterator temp;
        for (temp = cache_memory.begin(); temp != cache_memory.end(); temp++) {
            if (j == cnt) {
                return temp;
            }
            cnt++;
        }
    }
    bool isValid(uint16_t addr) {
        // bit 15 is the valid bit
        return addr & 0x8000;
    }
    uint16_t setValid(uint16_t addr) {
        // Set the valid bit
        return addr | 0x8000;
    }
    uint16_t invalidate(uint16_t addr) {
        // Invalidate address
        return addr & 0x7fff;
    }
    bool isShared(uint16_t addr) {
        // bit 14 is the shared bit
        return addr & 0x4000;
    }
    uint16_t setShared(uint16_t addr) {
        // Set the shared bit
        return addr | 0x4000;
    }
    uint16_t getTag(uint16_t addr) {
        // Return bits 13-2
        return (addr & 0x3ffc) >> 2;
    }
    uint8_t getOffset(uint16_t addr) {
        // Return bits 1-0
        return addr & 0x0003;
    }
    uint8_t readByte(uint32_t data, uint8_t offset) {
        if (offset == 0b00) {
            return data & 0x000000ff;
        }
        else if (offset == 0b01) {
            return (data & 0x0000ff00) >> 8;
        }
        else if (offset == 0b10) {
            return (data & 0x00ff0000) >> 16;
        }
        else if (offset == 0b11) {
            return (data & 0xff000000) >> 24;
        }
        else {
            printf("Invlaid read offset value\n");
            return 0;
        }
    }
    uint32_t writeByte(uint32_t block, uint8_t data, uint8_t offset) {
        if (offset == 0b00) {
            return (block & 0xffffff00) + data;
        }
        else if (offset == 0b01) {
            return (block & 0xffff00ff) + (data << 8);
        }
        else if (offset == 0b10) {
            return (block & 0xff00ffff) + (data << 16);
        }
        else if (offset == 0b11) {
            return (block & 0x00ffffff) + (data << 24);
        }
        else {
            printf("Invlaid write offset value\n");
            return 0;
        }
    }
    void update_cache(uint16_t addr, uint32_t block) {
        // Read and Write method will handle all the checking for this
        // Assuming addr is not in the cache
        // Find an invalid line
        for (it = cache_memory.begin(); it != cache_memory.end(); it++) {
            if(!isValid(it->first) && !isValid(it->first)) {
                // Remove invalid line from cache
                mtx.lock();
                cache_memory.erase(it);
                // Insert new line
                cache_memory.insert(pair<uint16_t, uint32_t>(setValid(addr), block));
                mtx.unlock();
                return;
            }
        }
        // Use random replacement if all slots are valid
        it = getRandomBlock();
        mtx.lock();
        // Remove line from cache
        cache_memory.erase(it);
        // Insert new line
        cache_memory.insert(pair<uint16_t, uint32_t>(setValid(addr), block));
        mtx.unlock();
    }
    // Implement these two functions after defining the bus because they will be using it
    uint8_t read_cache(uint16_t addr);
    void write_cache(uint16_t addr, uint8_t data);
};

class Bus{
public:
    // Use linked list to serialize requests
    Message* head;
    mutex mtx; // Used for taking control of the bus
    Bus() {
        head = NULL;
    }
    void addMessage(Message mes) {
        // Take control of the bus to write messages
        mtx.lock();
        // Want a copy so there is a unique message on all buses
        Message* newMes = new Message(mes);
        if (head == NULL) {
            head = newMes;
        }
        else {
            Message *temp;
            temp = head;
            while (temp->next != NULL) {
                temp = temp->next;
            }
            temp->next = newMes;
        }
        // Realease control of the bus
        mtx.unlock();
    }
    void processBus(Local_Cache* localCache) {
        // Take control of the bus to process messages
        mtx.lock();
        map<uint16_t, uint32_t>::iterator it;
        while(head != NULL) {
            if (head->type == ReadMiss) {
                // The addr on this message will go to the shared state
                // Check my cache for it and update
                for (it = localCache->cache_memory.begin(); it != localCache->cache_memory.end(); it++) {
                    // Compare tag with address
                    if(localCache->getTag(it->first) == localCache->getTag(head->addr)) {
                        localCache->mtx.lock();
                        // Remove existing line from cache
                        localCache->cache_memory.erase(it);
                        // Insert new shared line
                        uint32_t block = globalCache.read_cache_block(head->addr);
                        localCache->cache_memory.insert(pair<uint16_t, uint32_t>(localCache->setValid(localCache->setShared(it->first)), block));
                        localCache->mtx.unlock();
                        break;
                    }
                }
            }
            else if (head->type == WriteMiss) {
                for (it = localCache->cache_memory.begin(); it != localCache->cache_memory.end(); it++) {
                    // Compare tag with address
                    if(localCache->getTag(it->first) == localCache->getTag(head->addr)) {
                        // Write data that was sent with the message
                        localCache->mtx.lock();
                        uint32_t block = localCache->writeByte(it->second, head->data, localCache->getOffset(head->addr));
                        it->second = block;
                        localCache->mtx.unlock();
                        break;
                    }
                }
            }
            else if (head->type == Invalidate) {
                if (head->core != localCache->cn) {
                    // The addr on this message will go to the invalid state
                    // Check my cache for it and update
                    for (it = localCache->cache_memory.begin(); it != localCache->cache_memory.end(); it++) {
                        // Compare tag with address
                        if(localCache->getTag(it->first) == localCache->getTag(head->addr)) {
                            if (!localCache->isValid(it->first))
                                break;
                            localCache->mtx.lock();
                            // Remove existing line from cache
                            localCache->cache_memory.erase(it);
                            // Insert new invalidated line
                            localCache->cache_memory.insert(pair<uint16_t, uint32_t>(localCache->invalidate(it->first), it->second));
                            localCache->mtx.unlock();
                            break;
                        }
                    }
                }
            }
            else {
                printf("Unknown message type: %d\n", head->type);
            }
            // Finished with this message so get ready for the next one
            head = head->next;
        }
        // Finished processing messages
        head = NULL;
        mtx.unlock();
    }
};

// Give each core a bus to update
Bus* coreBuses[NUM_CORES];

void newMessage(Message mes) {
    for (int i=0; i<NUM_CORES; i++) {
        // Simulate serialization
        // Add new message to the end of bus messages
        coreBuses[i]->addMessage(mes);
    }
}

uint8_t Local_Cache::read_cache(uint16_t addr) {
    readCnt++;
    bool shared = false;
    // Check tags in cache
    for (it = cache_memory.begin(); it != cache_memory.end(); it++) {
        // Compare tag with address
        if(getTag(it->first) == getTag(addr)) {
            if (isShared(it->first)) {
                // We need to check for incoming bus messages if the block is shared
                // An invalidate could have been sent for this address
                bus->processBus(this);
                shared = true;
            }
            // Make sure it is valid
            if (isValid(it->first)) {
                // Normal read hit: read data in local cache
                // Read the byte specified by the offset
                return readByte(it->second, getOffset(addr));
            }
            else {
                // Place read miss on bus
                Message mes;
                mes.core = cn;
                mes.type = ReadMiss;
                mes.addr = addr;
                mes.next = NULL;
                newMessage(mes);
            }
        }
    }
    // This is a miss
    // block being addressed was either not in cache
    // or was invalid and not shared
    readMissCnt++;
    // Get the value from upper level cache
    uint32_t block = globalCache.read_cache_block(addr);
    // Populate the cache for next time
    if (!shared)
        update_cache(addr, block);
    // Return value
    return readByte(block, getOffset(addr));
}
void Local_Cache::write_cache(uint16_t addr, uint8_t data) {
    writeCnt++;
    uint32_t block;
    bool shared = false;
    // Check tags in cache
    for (it = cache_memory.begin(); it != cache_memory.end(); it++) {
        // Compare tag with address
        if(getTag(it->first) == getTag(addr)) {
            if(isShared(it->first)) {
                // We need to check for incoming bus messages if the block is shared
                // An invalidate could have been sent for this address
                bus->processBus(this);
                //shared = true;
            }
            if (isValid(it->first)) {
                mtx.lock();
                // Write hit, write data to local cache
                block = writeByte(it->second, data, getOffset(addr));
                it->second = block;
                mtx.unlock();
                // Also write through, so update upper level
                globalCache.write_cache_block(addr, block);
                if (isShared(it->first)) {
                    // The block was shared, invalidate on other cores
                    Message mes;
                    mes.core = cn;
                    mes.type = Invalidate;
                    mes.addr = addr;
                    mes.next = NULL;
                    newMessage(mes);
                }
                return;
            }
            // Place write miss on the bus
            Message mes;
            mes.core = cn;
            mes.type = WriteMiss;
            mes.addr = addr;
            mes.data = data;
            mes.next = NULL;
            // Place messge on the bus
            newMessage(mes);
        }
    }
    // This is a write miss
    writeMissCnt++;
    block = writeByte(globalCache.read_cache_block(addr), data, getOffset(addr));
    // Allocate data into the cache
    if (shared)
        update_cache(setShared(addr), block);
    else
        update_cache(addr, block);
    // Also write through
    globalCache.write_cache_block(addr, block);
}

// Create function for threads to run
mutex mtx; // Avoid deadlock when looking at coresRunning
bool coresRunning[NUM_CORES];
int readCnt[NUM_CORES];
int readMissCnt[NUM_CORES];
int writeCnt[NUM_CORES];
int writeMissCnt[NUM_CORES];

void *CoreExecution(void *coreNum) {
    uint32_t cn = (uint64_t)coreNum; // Do not need long percision (avoid warnings in print statements)
    printf("Setting Up Core %x\n", cn);
    mtx.lock();
    coresRunning[cn] = true;
    mtx.unlock();

    Bus bus;
    coreBuses[cn] = &bus;
    // Setup my local cache
    Local_Cache localCache(&bus, cn);

    // Give all cores a chance to finish setting up
    sleep(1);

    // Print values of interest in local cache before all cores start reading and writing
    // the same locations. All values should match here
    uint8_t a = localCache.read_cache(0);
    printf("\nInitial Value on Core %d: read %x at addr %x\n", cn, a, 0);

    sleep(1);

    printf("\nStarting Main Execution On Core %d\n\n", cn);
    // Simulate program execution inside the core
    for (int i=0; i<2; i++) {
        // Continually monitor the bus
        bus.processBus(&localCache);
        // Read and write data that will test cache and bus operations
        localCache.write_cache(0, 1+localCache.read_cache(0));
        a = localCache.read_cache(0);
        printf("Core %d: read %x at addr %x\n", cn, a, 0);
        sleep(1);
    }

    // Collect internal values for statistics
    readCnt[cn] = localCache.readCnt;
    readMissCnt[cn] = localCache.readMissCnt;
    writeCnt[cn] = localCache.writeCnt;
    writeMissCnt[cn] = localCache.writeMissCnt;
    mtx.lock();
    coresRunning[cn] = false;
    mtx.unlock();
    pthread_exit(NULL);
}

int main () {
    // Setup main memory
    // Put in a repeating pattern that is easy to visualize
    /*
    * mem[0] = 0x01
    * mem[1] = 0x02
    * mem[2] = 0x03
    * mem[3] = 0x04
    * ...
    * mem[MEM_SIZE-4] = 0x01
    * mem[MEM_SIZE-3] = 0x02
    * mem[MEM_SIZE-2] = 0x03
    * mem[MEM_SIZE-1] = 0x04
    */
    printf("\nSetting Up Main Memory:\n\n");
    for (int i=0; i<MEM_SIZE; i++) {
        uint8_t val = (i % 4) + 1;
        main_memory[i] = val;
    }
    printf("mem[0] = %x\n", main_memory[0]);
    printf("mem[1] = %x\n", main_memory[1]);
    printf("mem[2] = %x\n", main_memory[2]);
    printf("mem[3] = %x\n", main_memory[3]);
    printf("...\n");
    printf("mem[MEM_SIZE-4] = %x\n", main_memory[MEM_SIZE-4]);
    printf("mem[MEM_SIZE-3] = %x\n", main_memory[MEM_SIZE-3]);
    printf("mem[MEM_SIZE-2] = %x\n", main_memory[MEM_SIZE-2]);
    printf("mem[MEM_SIZE-1] = %x\n", main_memory[MEM_SIZE-1]);

    // Load some values into global cache
    // Choose address that I know will be accessed by the cores
    printf("\nPopulating Global Cache:\n\n");
    for (int i=0; i<NUM_CORES; i++) {
        uint8_t x = globalCache.read_cache(i);
        printf("Global Cache Addr: %x Read: %x\n", i, x);
    }

    // Create the cores
    printf("\nCreating Cores:\n\n");
    pthread_t threads[NUM_CORES];
    int rc;
    for(int i=0; i<NUM_CORES; i++) {
        printf("Creating Core %d\n", i);
        rc = pthread_create(&threads[i], NULL, CoreExecution, (void *)i);
        if (rc) {
            printf("Error: Unable To Create Core %d\n", rc);
            exit(-1);
        }
    }

    // Wait for core construction to finish
    sleep(1);

    // Wait here until all cores are done running
    bool allCoresRunning = true;
    while (allCoresRunning) {
        allCoresRunning = false;
        for (int i=0; i<NUM_CORES; i++) {
            // Check if cores are still running
            mtx.lock();
            allCoresRunning |= coresRunning[i];
            mtx.unlock();
        }
    }

    printf("\nCache Statistics:\n\n");
    // Print statistics
    int totalReadCnt = 0;
    int totalReadMissCnt = 0;
    int totalWriteCnt = 0;
    int totalWriteMissCnt = 0;
    float perc = 0.0f;
    for (int i=0; i<NUM_CORES; i++) {
        if (readCnt[i] == 0) {
            printf("No Local Cache Reads on Core %d\n", i);
        }
        else {
            perc = (float)readMissCnt[i]/(float)readCnt[i];
            printf("Core %d Local Cache Read Miss Rate = %f\n", i, perc);
            totalReadCnt += readCnt[i];
            totalReadMissCnt += readMissCnt[i];
        }

        if (writeCnt[i] == 0) {
            printf("No Local Cache Writes on Core %d\n", i);
        }
        else {
            perc = (float)writeMissCnt[i]/(float)writeCnt[i];
            printf("Core %d Local Cache Write Miss Rate = %f\n", i, perc);
            totalWriteCnt += writeCnt[i];
            totalWriteMissCnt += writeMissCnt[i];
        }
    }
    if (totalReadCnt == 0) {
        printf("No Local Cache Reads on Any Cores\n");
    }
    else {
        perc = (float)totalReadMissCnt/(float)totalReadCnt;
        printf("Combined Local Cache Read Miss Rate = %f\n", perc);
    }
    if (totalWriteCnt == 0) {
        printf("No Local Cache Writes on Any Cores\n");
    }
    else {
        perc = (float)totalWriteMissCnt/(float)totalWriteCnt;
        printf("Combined Local Cache Write Miss Rate = %f\n", perc);
    }
    if (globalCache.readCnt == 0) {
        printf("No Global Cache Reads Performed\n");
    }
    else {
        perc = (float)globalCache.readMissCnt/(float)globalCache.readCnt;
        printf("Global Cache Read Miss Rate = %f\n", perc);
    }
    if (globalCache.writeCnt == 0) {
        printf("No Global Cache Writes Performed\n");
    }
    else {
        perc = (float)globalCache.writeMissCnt/(float)globalCache.writeCnt;
        printf("Global Cache Write Miss Rate = %f\n", perc);
    }
}
