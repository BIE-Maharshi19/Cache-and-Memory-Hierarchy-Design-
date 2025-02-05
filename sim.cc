#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <inttypes.h>
#include <math.h>
#include "sim.h"
#include <bitset>
#include <vector>
#include <iomanip>
#include <algorithm>

using namespace std;
// Global Output Flags 
uint32_t L1_reads, L1_writes, L2_reads, L2_writes = 0;
uint32_t L1_r_miss, L1_w_miss, L2_r_miss, L2_w_miss = 0;
uint32_t L1_wr_L2, L1_L2_wr_Mem, Mem_traffic = 0;
float L1_miss_rate, L2_miss_rate = 0;
uint32_t L1_prefetches, L2_prefetches = 0;
uint32_t L2_reads_prefetch ,L2_read_prefetch_misses =0;
uint32_t buffer_hit_index = 0;
uint32_t buffer_hit_addr = 0;
uint32_t buffer_index_n = 0;
uint32_t mem_fetch;

// Stream Buffer Structure
struct StreamBuffer {
   bool valid; 
   uint32_t head;
   uint32_t lru_counter;
   vector<uint32_t> blocks;

   StreamBuffer(uint32_t M) : valid(false), head(0), lru_counter(0), blocks(M, 0) {}
};

// Structure for each_block
struct block_info {
   bool valid ;
   uint32_t dirty ;
   uint32_t tag ;   
   uint32_t lru_counter ;
};

class cache{
   private:
   uint32_t blocksize, size, assoc; 
   uint32_t sets_nos;
   uint32_t index_nos;
   uint32_t block_nos;
   uint32_t tag_nos;
   uint32_t num_stream_buffers; // N
   uint32_t stream_buffer_depth;
   struct block_info** b;
   cache* next_cache_level;
   vector<StreamBuffer> stream_buffers; 
   bool buffer_active = false ;
   void init_set();

   public:
   uint32_t get_tag_from_addr(uint32_t address);
   uint32_t get_index_from_addr(uint32_t address);
   void LRU_update(uint32_t index, uint32_t HIT_OR_MISS, uint32_t current_counter);
   void read_request(uint32_t addr, bool is_initial_level,const char rw);
   void write_request(uint32_t addr, bool is_initial_level, const char rw);
   bool check_stream_buffer_hit_miss(uint32_t addr, uint32_t& hit_index, uint32_t& buffer_hit_addr, uint32_t& buffer_index_n);
   void handle_stream_buffer_miss(uint32_t addr, bool is_initial_level);
   void handle_stream_buffer_hit(uint32_t &hit_index, uint32_t stream_buffer_index, bool is_initial_level);
   void LRU_update_stream_buffer(uint32_t buffer_index, uint32_t HIT_OR_MISS);
   uint32_t find_lru_buffer();
   void calc_param(uint32_t block_size, uint32_t cache_size, uint32_t associativity);
   void print_cache_info();
   void print_cache_contents();
   void print_buffer_counters();
   uint32_t find_lru_block(uint32_t index);
   void print_buffer_contents();


   //constructor
   cache(uint32_t block_size, uint32_t cache_size, uint32_t associativity, cache* next_level, uint32_t N, uint32_t M){
      
      calc_param(block_size,cache_size,associativity);

      num_stream_buffers = N;
      stream_buffer_depth = M;
      if((N > 0) && (next_level == NULL)){
         buffer_active = true;
      }

      stream_buffers.resize(N, StreamBuffer(M));

      b = new block_info*[sets_nos];
      for (uint32_t i = 0; i < sets_nos; i++){
         b[i] = new block_info[assoc];
      }
      init_set();
      next_cache_level = next_level;
   }
};

//external class functions definations
void cache :: calc_param(uint32_t block_size, uint32_t cache_size, uint32_t associativity){
   size = cache_size;
   blocksize = block_size; 
   assoc = associativity;
   sets_nos = cache_size / (block_size * associativity);
   index_nos = log2(sets_nos);
   block_nos = log2(block_size);
   tag_nos = 32 - index_nos - block_nos;
}

void cache ::  init_set(){
   for (uint32_t i = 0; i < sets_nos; i++){  
      for (uint32_t j = 0; j < assoc; j++){
      b[i][j].valid = false; 
      b[i][j].dirty = 0;
      b[i][j].tag = 0;
      b[i][j].dirty = 0;
      b[i][j].lru_counter = j;
      }
   }

   for(uint32_t i = 0; i < num_stream_buffers ; i++){
      stream_buffers[i].lru_counter = i;
   }
}

void cache :: print_cache_info() { //change
        cout << "Cache Size: " << size << " bytes" <<endl;
        cout << "Block Size: " << blocksize << " bytes" <<endl;
        cout << "Associativity: " << assoc << "-way" <<endl;
        cout << "Number of Sets: " << sets_nos <<endl;
        cout << "Index Bits: " << index_nos <<endl;
        cout << "Block Offset Bits: " << block_nos <<endl;
        cout << "Tag Bits: " << tag_nos <<endl; 
        cout << endl; 
}

uint32_t cache :: get_tag_from_addr(uint32_t address){ // can be optimized for both returns
      
        //cout<<"Address :"<<hex<<address<<endl;
        
        uint32_t tag = (address >> (index_nos + block_nos) );
        return tag;
              
}

uint32_t cache :: get_index_from_addr(uint32_t address){

         uint32_t index_mask = (1 << index_nos) - 1;
         return (address >> block_nos) & index_mask;

}

void cache :: LRU_update_stream_buffer(uint32_t buffer_index, uint32_t HIT_OR_MISS){
    uint32_t current_counter = stream_buffers[buffer_index].lru_counter;
    
    for (uint32_t i = 0; i < num_stream_buffers; i++){
        if (i == buffer_index) {
            stream_buffers[i].lru_counter = 0;
        } else if (stream_buffers[i].lru_counter < current_counter) {
            stream_buffers[i].lru_counter++;
        }
    }
}

void cache :: LRU_update(uint32_t index, uint32_t HIT_OR_MISS, uint32_t column_index){ //HIT - 1 , MISS - 0

         
         if (HIT_OR_MISS == 1){
            uint32_t current_counter = b[index][column_index].lru_counter;
            for (uint32_t j = 0; j < assoc ; j++){
               if (b[index][j].lru_counter < current_counter) {
                        b[index][j].lru_counter++;   
                        
                    }    
                  }  
               b[index][column_index].lru_counter = 0;
           
         }

         else if (HIT_OR_MISS == 0){
            for (uint32_t j=0; j < assoc ; j++){
               if(j == column_index){
                  b[index][j].lru_counter = 0;   
               }
               else {
                  b[index][j].lru_counter++;
               }

            }

         }
         
}

uint32_t cache :: find_lru_buffer() {
    uint32_t lru_buffer_index = 0;
    uint32_t max_counter = 0;
    
    for (uint32_t i = 0; i < num_stream_buffers; i++) {
        if (stream_buffers[i].lru_counter > max_counter) {
            max_counter = stream_buffers[i].lru_counter;
            lru_buffer_index = i;
        }
    }
    return lru_buffer_index;
}

// uint32_t cache :: find_mru_buffer() { // you don't need this 
//     uint32_t mru_buffer_index = 0;
    
//     for (uint32_t i = 0; i < num_stream_buffers; i++) {
//         if (stream_buffers[i].lru_counter == 0) {
//             mru_buffer_index = i;
//         }
//     }
//     return mru_buffer_index;
// }

uint32_t cache :: find_lru_block(uint32_t index) {
    uint32_t max_counter = 0;
    uint32_t lru_block_index = 0;
    
    for (uint32_t j = 0; j < assoc; j++) {
        if (b[index][j].lru_counter > max_counter) {
            max_counter = b[index][j].lru_counter;
            lru_block_index = j;
        }
    }
    return lru_block_index;
}

bool cache :: check_stream_buffer_hit_miss(uint32_t addr, uint32_t& hit_index, uint32_t& buffer_hit_addr, uint32_t& buffer_index_n){
   uint32_t tag_search = get_tag_from_addr(addr);
   uint32_t index = get_index_from_addr(addr);
   uint32_t addr_b = (tag_search << index_nos) | index ;

   vector<uint32_t> buffer_indices(num_stream_buffers);

    for (uint32_t i = 0; i < num_stream_buffers; ++i) {
        buffer_indices[i] = i;
    }

    // Sort buffer indices based on LRU counter (MRU to LRU)
    sort(buffer_indices.begin(), buffer_indices.end(),
        [this](uint32_t a, uint32_t b) {
            return stream_buffers[a].lru_counter < stream_buffers[b].lru_counter;
        });

    for (uint32_t i : buffer_indices) {
        auto &buffer = stream_buffers[i];
        if (!buffer.valid) {
            continue;
        }

        for (uint32_t j = 0; j < stream_buffer_depth; ++j) {
            if (buffer.blocks[j] == addr_b) {
                hit_index = j;
                buffer_hit_addr = buffer.blocks[j];
                buffer_index_n = i;

                return true;  // Return true as soon as we find a hit
            }
        }
    }
    
   //  handle_stream_buffer_miss(addr);
    return false;  // If we've searched all buffers and found no hit, return false
    }

void cache :: handle_stream_buffer_miss(uint32_t addr, bool is_initial_level){
   uint32_t lru_buffer_index = find_lru_buffer();
    uint32_t tag_search = get_tag_from_addr(addr);
    uint32_t index = get_index_from_addr(addr);
    uint32_t addr_b = (tag_search << index_nos) | index;

    auto &buffer = stream_buffers[lru_buffer_index];
    buffer.valid = true;
    buffer.head = addr_b + 1; // Set head to the next block
    Mem_traffic++;
    // Prefetch M blocks ahead
    for (uint32_t i = 0; i < stream_buffer_depth; i++) {
        uint32_t prefetch_addr = addr_b + (i + 1);
        buffer.blocks[i] = prefetch_addr;
        Mem_traffic++;
        
        
        if (is_initial_level == true) {
            L1_prefetches++;
        }
        
        else {
            L2_prefetches++;
        }
    }

    LRU_update_stream_buffer(lru_buffer_index,0);


}

void cache :: handle_stream_buffer_hit(uint32_t &hit_index, uint32_t stream_buffer_index, bool is_initial_level){ //see the HIT logic
     
      auto &buffer = stream_buffers[stream_buffer_index];
    uint32_t current_head = buffer.head;

    
    for (uint32_t i = 0; i < stream_buffer_depth - hit_index - 1; i++) {
        buffer.blocks[i] = buffer.blocks[hit_index + 1 + i];
    }
    
    // Calculate the next block to prefetch
    uint32_t next_prefetch = buffer.blocks[stream_buffer_depth - 1] + 1;
    
    // Prefetch new blocks
    uint32_t new_blocks_count = hit_index + 1;

    for (uint32_t i = stream_buffer_depth - new_blocks_count; i < stream_buffer_depth; i++) {
        buffer.blocks[i] = next_prefetch;
        next_prefetch++;
        Mem_traffic ++;
        
    }
        // buffer.head = current_head + 1;
         buffer.head = buffer.blocks[0];

        if (is_initial_level == true) {
            L1_prefetches = L1_prefetches + (hit_index + 1) ;
            //Mem_traffic = Mem_traffic + (hit_index + 1);
        } else {
            L2_prefetches = L2_prefetches + (hit_index + 1);
            //Mem_traffic = Mem_traffic + (hit_index + 1); 
        }
    
    

}

void cache :: read_request(uint32_t addr, bool is_initial_level, const char rw){
   
   uint32_t hit = 0;
   uint32_t tag_search, index = 0;
   

   tag_search = get_tag_from_addr(addr);
   index = get_index_from_addr(addr);
  

    

   for (uint32_t j = 0; j < assoc ; j++) {     //search through blocks of the index row
      if( tag_search == b[index][j].tag && b[index][j].valid == true){
         hit = 1;
         if (check_stream_buffer_hit_miss(addr, buffer_hit_index, buffer_hit_addr, buffer_index_n)){
         handle_stream_buffer_hit(buffer_hit_index,buffer_index_n, is_initial_level);   
         LRU_update_stream_buffer(buffer_index_n,1);
         }
         LRU_update(index,1,j);    
         return;                                                    //HIT Logic
      }
   }          

   
   if (hit == 0){

   if(is_initial_level && buffer_active == false){
   L1_r_miss++;
   }
   
   uint32_t lru_block_index = find_lru_block(index);
   //writeback logic for eviction 

   if(b[index][lru_block_index].dirty == 1 ){
   // if(buffer_active){
   //    if(is_initial_level) L1_wr_L2 ++;
   //    else { L1_L2_wr_Mem ++;
   //       Mem_traffic ++;}
   // }
                                                          //evict first!!
   uint32_t evicted_address = (b[index][lru_block_index].tag << (index_nos + block_nos)) | (index << block_nos);
      if(next_cache_level){
         L1_wr_L2++;
         L2_writes++;
         next_cache_level->write_request(evicted_address,false,'w');
      }
      else{
         L1_L2_wr_Mem ++;
         Mem_traffic ++;
      }
      b[index][lru_block_index].dirty = 0; //note this clear here            
      
   }
    

   if(buffer_active){ // gets activated when next_level_cache is zero 
   if (check_stream_buffer_hit_miss(addr, buffer_hit_index, buffer_hit_addr, buffer_index_n)) {
      handle_stream_buffer_hit(buffer_hit_index,buffer_index_n, is_initial_level);
      LRU_update_stream_buffer(buffer_index_n,1); 
      
   }
   else{ //flow control to diff L1_r_miss with and without stream buffer
      handle_stream_buffer_miss(addr, is_initial_level);
      if(is_initial_level && next_cache_level == NULL){
      L1_r_miss++;
      }
      else{
      if(rw == 'r'){L2_r_miss++;}

      else {L2_w_miss++;}
         
      } 

      uint32_t mem_fetch = 1;
   }
   }


    b[index][lru_block_index].tag = tag_search;
    b[index][lru_block_index].valid = true;
    b[index][lru_block_index].dirty = 0;
    // Set the LRU counter for the newly inserted block to 0 (most recently used)
    LRU_update(index,0,lru_block_index);
   // 1 - Next Fetch L2, 0 - Next Fetch L1. 

   if(next_cache_level){ 
      // next_mem_unit = 0; // counter now points to Main Memory 
      L2_reads ++;
      next_cache_level->read_request(addr, false,'r'); // parse the address // 
   }
   else{ // fetch from the mem add
      if (buffer_active == false){
      if(is_initial_level == true){
         L2_r_miss = 0;
      }
      else if (next_cache_level == NULL && is_initial_level == false){
      if( rw == 'r'){
      L2_r_miss++;
      }
      else{ L2_w_miss ++;}   
      } 

      Mem_traffic++; 
   }
   if (mem_fetch == 1){
      Mem_traffic++;
   }
   }

}
}

void cache :: write_request(uint32_t addr, bool is_initial_level, const char rw){         // To replace it !! 
   uint32_t hit = 0;
   uint32_t tag_search, index = 0;


   tag_search = get_tag_from_addr(addr);
   index = get_index_from_addr(addr);

   //cout<<"Tag  : "<<hex<<tag_search<<" Index : "<<dec<<index<<endl;  

 

   for (uint32_t j = 0; j < assoc ; j++) {     //search through blocks of the index row
      if(b[index][j].valid && b[index][j].tag == tag_search){
         hit = 1;
         LRU_update(index,1,j); 
         b[index][j].dirty = 1;
         if (check_stream_buffer_hit_miss(addr, buffer_hit_index, buffer_hit_addr, buffer_index_n)){
         handle_stream_buffer_hit(buffer_hit_index,buffer_index_n, is_initial_level);   
         LRU_update_stream_buffer(buffer_index_n,1);
         } 
         return;                                                    //HIT Logic
      }
   }

   if(!hit){

   if (is_initial_level && buffer_active == false ){
   L1_w_miss++;
   }
   
   uint32_t lru_block_index = find_lru_block(index);

   if (b[index][lru_block_index].dirty) {
  

        uint32_t evicted_address = (b[index][lru_block_index].tag << (index_nos + block_nos)) | (index << block_nos);
        if (next_cache_level) {
            L1_wr_L2++;
            L2_writes++;
            next_cache_level->write_request(evicted_address, false,'w');
        } else {
            L1_L2_wr_Mem ++;
            Mem_traffic++;
        }
        b[index][lru_block_index].dirty = 0;  // Clean the block
    
   }

   if(buffer_active){
   if (check_stream_buffer_hit_miss(addr, buffer_hit_index, buffer_hit_addr, buffer_index_n)) {
      handle_stream_buffer_hit(buffer_hit_index,buffer_index_n, is_initial_level);
      LRU_update_stream_buffer(buffer_index_n,1); 
      
   }
   else{ //flow control to diff L1_r_miss with and without stream buffer
      handle_stream_buffer_miss(addr, is_initial_level);
      if(is_initial_level && next_cache_level == NULL){
      L1_w_miss++;
      }
      else {
      if(rw == 'w'){L2_w_miss++;}

      else {L2_r_miss++;}
      }
      uint32_t mem_fetch = 1; //read fetch
   }  
   }

    b[index][lru_block_index].tag = tag_search;
    b[index][lru_block_index].valid = true;
    // Set the LRU counter for the newly inserted block to 0 (most recently used)
    LRU_update(index,0,lru_block_index);
    b[index][lru_block_index].dirty = 1;

   // 1 - Next Fetch L2, 0 - Next Fetch L1. 
   if(next_cache_level){ 
      // next_mem_unit = 0; // counter now points to Main Memory 
      L2_reads ++;
      next_cache_level->read_request(addr,false,'r'); // parse the address // 
   }
   else{ // fetch from the mem add
      if(buffer_active == false){

      if(is_initial_level == true){
         L2_r_miss = 0;
      }
      else if (next_cache_level == NULL && is_initial_level == false){
      if( rw == 'r'){
      L2_r_miss++;
      }
      else{ L2_w_miss ++;}  
      }

      Mem_traffic++;  
   }
   if (mem_fetch == 1){
      Mem_traffic++;
   }
   }
}
}


void cache::print_cache_contents() {
    

    for (uint32_t i = 0; i < sets_nos; i++) {
        // Print the set index, ensuring proper formatting and alignment
        cout << "set " << setw(7) << dec << i << ": ";

        // Create a vector of pairs to store block index and LRU_counter
        vector<pair<uint32_t, uint32_t>> sorted_blocks;

        // Populate the vector with the block index and its corresponding LRU_counter value
        for (uint32_t j = 0; j < assoc; j++) {
                sorted_blocks.push_back(make_pair(j, b[i][j].lru_counter));    
        }

        // Sort the blocks by their LRU_counter (ascending, so MRU -> LRU)
        sort(sorted_blocks.begin(), sorted_blocks.end(),
            [&](const pair<uint32_t, uint32_t> &a, const pair<uint32_t, uint32_t> &b) {
                return a.second < b.second;
            });

        // Print the blocks in MRU -> LRU order
        for (const auto &block : sorted_blocks) {
            uint32_t j = block.first;
            cout << setw(8) << hex << b[i][j].tag; // Ensure tag alignment 
            if (b[i][j].dirty) {
                cout << " D ";  // Mark dirty blocks with 'D'
            } else {
                cout << "   ";  
            }
        }

        cout << endl;
    }
    cout << endl;
}

void cache::print_buffer_counters() {
    cout << "===== Buffer Counters =====" << endl;
    for (uint32_t i = 0; i < num_stream_buffers; i++) {
        cout << "Buffer " << dec << i << ": ";
        for (uint32_t j = 0; j < stream_buffer_depth; j++) {
            cout << dec << stream_buffers[i].lru_counter << " ";
        }
        cout << endl;
    }
   
    cout << endl;
}

void cache::print_buffer_contents() {
    cout << "===== Stream Buffer(s) contents =====" << endl;

    // Create a vector of pairs (buffer index, LRU counter)
    vector<pair<uint32_t, uint32_t>> buffer_order;
    for (uint32_t i = 0; i < num_stream_buffers; i++) {
        if (stream_buffers[i].valid) {
            buffer_order.push_back({i, stream_buffers[i].lru_counter});
        }
    }

    // Sort the vector based on LRU counter (ascending order, so MRU comes first)
    sort(buffer_order.begin(), buffer_order.end(),
         [](const pair<uint32_t, uint32_t>& a, const pair<uint32_t, uint32_t>& b) {
             return a.second < b.second;
         });

    // Print the buffer contents in MRU to LRU order
    for (const auto& buffer : buffer_order) {
        uint32_t i = buffer.first;
        for (uint32_t j = 0; j < stream_buffer_depth; j++) {
            cout << " " << hex << setw(8) << setfill(' ') << stream_buffers[i].blocks[j];
        }
        cout << endl;
    }

    cout << endl;
}


 int main (int argc, char *argv[]) {


   FILE *fp;			// File pointer.
   char *trace_file;		// This variable holds the trace file name.
   cache_params_t params;	// Look at the sim.h header file for the definition of struct cache_params_t.
   char rw;			// This variable holds the request's type (read or write) obtained from the trace.
   uint32_t addr;		// This variable holds the request's address obtained from the trace.
				// The header file <inttypes.h> above defines signed and unsigned integers of various sizes in a machine-agnostic way.  "uint32_t" is an unsigned integer of 32 bits.

   // Exit with an error if the number of command-line arguments is incorrect.
   if (argc != 9) { // change to 9 if trace file to be included !! 
      printf("Error: Expected 8 command-line arguments but was provided %d.\n", (argc - 1));
      exit(EXIT_FAILURE);
   }
    

   // "atoi()" (included by <stdlib.h>) converts a string (char *) to an integer (int).
   params.BLOCKSIZE = (uint32_t) atoi(argv[1]);
   params.L1_SIZE   = (uint32_t) atoi(argv[2]);
   params.L1_ASSOC  = (uint32_t) atoi(argv[3]);
   params.L2_SIZE   = (uint32_t) atoi(argv[4]);
   params.L2_ASSOC  = (uint32_t) atoi(argv[5]);
   params.PREF_N    = (uint32_t) atoi(argv[6]);
   params.PREF_M    = (uint32_t) atoi(argv[7]);
   trace_file       = argv[8];



   // Open the trace file for reading.
   fp = fopen(trace_file, "r");
   if (fp == (FILE *) NULL) {
      // Exit with an error if file open failed.
      printf("Error: Unable to open file %s\n", trace_file);
      exit(EXIT_FAILURE);
   }
    
   // Print simulator configuration.
   printf("===== Simulator configuration =====\n");
   printf("BLOCKSIZE:  %u\n", params.BLOCKSIZE); 
   printf("L1_SIZE:    %u\n", params.L1_SIZE);
   printf("L1_ASSOC:   %u\n", params.L1_ASSOC);
   printf("L2_SIZE:    %u\n", params.L2_SIZE);
   printf("L2_ASSOC:   %u\n", params.L2_ASSOC);
   printf("PREF_N:     %u\n", params.PREF_N);
   printf("PREF_M:     %u\n", params.PREF_M);
   printf("trace_file: %s\n", trace_file);
   printf("\n");
  
 
   cache* L2_cache = nullptr;
   //check if L2 is req or not
   if(params.L2_SIZE != 0){
   L2_cache = new cache (params.BLOCKSIZE, params.L2_SIZE, params.L2_ASSOC, nullptr, params.PREF_N, params.PREF_M);
   cache L1_cache(params.BLOCKSIZE, params.L1_SIZE, params.L1_ASSOC, L2_cache, params.PREF_N, params.PREF_M);
   //L2_cache->print_cache_info();
   }

   cache L1_cache(params.BLOCKSIZE, params.L1_SIZE, params.L1_ASSOC, L2_cache, params.PREF_N, params.PREF_M);
   //L1_cache.print_cache_info();

   // Read requests from the trace file and echo them back.
   while (fscanf(fp, "%c %x\n", &rw, &addr) == 2) { 
                // Stay in the loop if fscanf() successfully parsed two tokens as specified.
     // cout<<"address : "<<addr<<" "<<rw<<endl;
     // L1_cache.print_buffer_contents();
      //L1_cache.print_buffer_counters();

      if (rw == 'r'){
         //printf("r %x\n", addr);
         L1_reads++;
         L1_cache.read_request(addr,true,'r');
      }
         
      else if (rw == 'w'){
         //printf("w %x\n", addr);
         L1_writes++;
         L1_cache.write_request(addr,true,'w');
      }
      
      else {
         printf("Error: Unknown request type %c.\n", rw);
	 exit(EXIT_FAILURE);
      }

    }
    
    cout << "===== " << "L1" << " contents =====" << endl;
    L1_cache.print_cache_contents();
    if (L2_cache != nullptr) {
        cout << "===== " << "L2" << " contents =====" << endl;
        L2_cache->print_cache_contents();
    }

    if(params.PREF_N > 0){
      if(L2_cache != nullptr){
      L2_cache->print_buffer_contents();
      }
      else L1_cache.print_buffer_contents();
    }
      


    
        L1_miss_rate = (float) (L1_r_miss + L1_w_miss)/(L1_reads + L1_writes);
        L2_miss_rate = (float) (L2_r_miss)/ (L2_reads);
        
        if(params.L2_SIZE == 0){
          L1_wr_L2 = L1_L2_wr_Mem;
          L1_L2_wr_Mem = 0;
          L2_miss_rate = 0;
       }

    //Measurement Calculation
                cout << "===== Measurements =====" << endl;
   cout << left << setw(30) << "a. L1 reads:"                  << dec << L1_reads << endl;
   cout << left << setw(30) << "b. L1 read misses:"            << dec << L1_r_miss << endl;
   cout << left << setw(30) << "c. L1 writes:"                 << dec << L1_writes << endl;
   cout << left << setw(30) << "d. L1 write misses:"           << dec << L1_w_miss << endl;
   cout << left << setw(30) << "e. L1 miss rate:"              << fixed << setprecision(4) << L1_miss_rate << endl;
   cout << left << setw(30) << "f. L1 writebacks:"             << dec << L1_wr_L2 << endl;
   cout << left << setw(30) << "g. L1 prefetches:"             << dec << L1_prefetches << endl;
   cout << left << setw(30) << "h. L2 reads (demand):"         << dec << L2_reads << endl;
   cout << left << setw(30) << "i. L2 read misses (demand):"   << dec << L2_r_miss << endl;
   cout << left << setw(30) << "j. L2 reads (prefetch):"       << dec << L2_reads_prefetch << endl;
   cout << left << setw(30) << "k. L2 read misses (prefetch):" << dec << L2_read_prefetch_misses << endl;
   cout << left << setw(30) << "l. L2 writes:"                 << dec << L2_writes << endl;
   cout << left << setw(30) << "m. L2 write misses:"           << dec << L2_w_miss << endl;
   cout << left << setw(30) << "n. L2 miss rate:"              << fixed << setprecision(4) << float(L2_miss_rate) << endl;
   cout << left << setw(30) << "o. L2 writebacks:"             << dec << L1_L2_wr_Mem << endl;
   cout << left << setw(30) << "p. L2 prefetches:"             << dec << L2_prefetches << endl;
   cout << left << setw(30) << "q. memory traffic:"            << dec << Mem_traffic << endl;
  

    return(0);
}