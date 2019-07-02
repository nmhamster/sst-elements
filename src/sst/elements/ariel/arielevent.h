// Copyright 2009-2019 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2019, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.


#ifndef _H_SST_ARIEL_EVENT
#define _H_SST_ARIEL_EVENT

#include <sst/core/event.h>
#include "ariel_shmem.h"

namespace SST {
namespace ArielComponent {

enum ArielEventType {
    READ_ADDRESS,
    WRITE_ADDRESS,
    START_DMA_TRANSFER,
    WAIT_ON_DMA_TRANSFER,
    CORE_EXIT,
    NOOP,
    MALLOC,
    MMAP,
    FREE,
    SWITCH_POOL,
    FLUSH,
    FENCE,
    GPU
};

class ArielEvent {

    public:
        ArielEvent();
        virtual ~ArielEvent();
        virtual ArielEventType getEventType() const = 0;

};


}

namespace GpgpusimComponent {

enum EventType {
   REQUEST,
   RESPONSE

};

// Thie defines a class for events of Gpgpusim
class GpgpusimEvent : public SST::Event {
   private:
      GpgpusimEvent() {hint = -1; } // For serialization
      EventType ev;
      long long int address;
      long long int paddress;
      int size;

   public:
      typedef std::vector<uint64_t> dataVec;
      dataVec payload;
      struct CudaArguments {
            union {
               char file_name[256];
               uint64_t free_address;
               struct {
                  unsigned fat_cubin_handle;
               } register_fatbin;
               struct {
                  unsigned fat_cubin_handle;
                  uint64_t host_fun;
                  char device_fun[512];
               } register_function;
               struct {
                  void** dev_ptr;
                  size_t size;
                  uint64_t ptr_address;
               } cuda_malloc;
               struct {
                  uint64_t dst;
                  uint64_t src;
                  size_t count;
                  cudaMemcpyKind kind;
               } cuda_memcpy;
               struct {
                  unsigned int gdx;
                  unsigned int gdy;
                  unsigned int gdz;
                  unsigned int bdx;
                  unsigned int bdy;
                  unsigned int bdz;
                  size_t sharedMem;
                  cudaStream_t stream;
               } cfg_call;
               struct {
                  uint64_t address;
                  uint8_t value[200];
                  size_t size;
                  size_t offset;
               } set_arg;
               struct {
                  uint64_t func;
               } cuda_launch;
               struct {
                  unsigned fatCubinHandle;
                  uint64_t hostVar; //pointer to...something
                  char deviceName[256]; //name of variable
                  int ext;
                  int size;
                  int constant;
                  int global;
               } register_var;
               struct {
                  int numBlock;
                  uint64_t hostFunc;
                  int blockSize;
                  size_t dynamicSMemSize;
                  int flags;
               } max_active_block;
            };
      };

      CudaArguments CA;
      GpuApi_t API;
      GpgpusimEvent(EventType y) : SST::Event(){ ev = y; }

      void setType(int ev1) { ev = static_cast<EventType>(ev1);}
      int getType() { return ev; }

      int hint;
      int fileID;
      void setResp(long long int add, long long int padd, int sz) { address = add; paddress = padd; size = sz;}
      long long int getAddress() { return address; }
      long long int getPaddress() { return paddress; }

      int getSize() { return size; }

      void serialize_order(SST::Core::Serialization::serializer &ser)  override {
            Event::serialize_order(ser);
            ser & payload;
      }

      ImplementSerializable(SST::GpgpusimComponent::GpgpusimEvent);

};


}
}

#endif
