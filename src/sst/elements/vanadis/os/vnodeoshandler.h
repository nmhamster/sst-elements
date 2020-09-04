
#ifndef _H_VANADIS_NODE_OS_CORE_HANDLER
#define _H_VANADIS_NODE_OS_CORE_HANDLER

#include <cstdint>
#include <cinttypes>

#include <sst/core/link.h>
#include <sst/core/interfaces/simpleMem.h>
#include <sst/core/output.h>

#include "os/callev/voscallall.h"
#include "os/voscallresp.h"
#include "os/voscallev.h"

#include "os/node/vnodeoshstate.h"
#include "os/node/vnodeosfd.h"
#include "os/node/vnodeoswritevh.h"
#include "os/node/vnodeosopenath.h"

using namespace SST::Interfaces;

namespace SST {
namespace Vanadis {

class VanadisNodeOSCoreHandler {
public:
	VanadisNodeOSCoreHandler( uint32_t verbosity, uint32_t core, const char* stdin_path,
		const char* stdout_path, const char* stderr_path ) :
		core_id(core), next_file_id(16) {

		core_link = nullptr;

		char* out_prefix = new char[64];
		snprintf( out_prefix, 64, "[node-os-core-%6" PRIu32 "]: ", core );
		output = new SST::Output( out_prefix, verbosity, 0, SST::Output::STDOUT );

		handler_state = nullptr;

		if( stdin_path != nullptr )
			file_descriptors.insert( std::pair< uint32_t, VanadisOSFileDescriptor*>( 0, new VanadisOSFileDescriptor( 0, stdin_path ) ) );
		if( stdout_path != nullptr )
			file_descriptors.insert( std::pair< uint32_t, VanadisOSFileDescriptor*>( 1, new VanadisOSFileDescriptor( 1, stdin_path ) ) );
		if( stderr_path != nullptr )
			file_descriptors.insert( std::pair< uint32_t, VanadisOSFileDescriptor*>( 2, new VanadisOSFileDescriptor( 2, stdin_path ) ) );

		resetSyscallNothing();
	}

	~VanadisNodeOSCoreHandler() {
		delete output;

		for( auto next_file = file_descriptors.begin(); next_file != file_descriptors.end(); next_file++ ) {
			delete next_file->second;
		}
	}

	void resetSyscallNothing() {
		current_syscall = std::bind( &VanadisNodeOSCoreHandler::processSyscallNothing, this );
		handler_state = nullptr;
	}

	void setLink( SST::Link* new_link ) {  core_link = new_link; }

	uint32_t getCoreID()   const { return core_id;   }

	void handleIncomingSyscall( VanadisSyscallEvent* sys_ev ) {

		switch( sys_ev->getOperation() ) {
		case SYSCALL_OP_UNAME:
			{
				output->verbose(CALL_INFO, 16, 0, "-> call is uname()\n");
				VanadisSyscallUnameEvent* uname_ev = dynamic_cast<VanadisSyscallUnameEvent*>( sys_ev );

				if( nullptr == uname_ev ) {
					output->fatal(CALL_INFO, -1, "-> error: unable to case syscall to a uname event.\n");
				}

				output->verbose(CALL_INFO, 16, 0, "---> uname struct is at address 0x%0llx\n", uname_ev->getUnameInfoAddress());
				output->verbose(CALL_INFO, 16, 0, "---> generating syscall memory updates...\n");

				std::vector<uint8_t> uname_payload;
				uname_payload.resize( 65 * 5, (uint8_t) '\0' );

				const char* uname_sysname = "Linux";
				const char* uname_node    = "sim.sstsimulator.org";
				const char* uname_release = "4.19.136";
				const char* uname_version = "#1 Wed Sep 2 23:59:59 MST 2020";
				const char* uname_machine = "mips";

				for( size_t i = 0; i < std::strlen( uname_sysname ); ++i ) {
					uname_payload[i] = uname_sysname[i];
				}

				for( size_t i = 0; i < std::strlen( uname_node ); ++i ) {
					uname_payload[65 + i] = uname_node[i];
				}

				for( size_t i = 0; i < std::strlen( uname_release ); ++i ) {
					uname_payload[65 + 65 +i] = uname_release[i];
				}

				for( size_t i = 0; i < std::strlen( uname_version ); ++i ) {
					uname_payload[65 + 65 + 65 +i] = uname_version[i];
				}

				for( size_t i = 0; i < std::strlen( uname_machine ); ++i ) {
					uname_payload[65 + 65 + 65 + 65 + i] = uname_machine[i];
				}

				sendBlockToMemory( uname_ev->getUnameInfoAddress(), uname_payload );
				current_syscall = std::bind( &VanadisNodeOSCoreHandler::processSyscallUname, this );
			}
			break;

		case SYSCALL_OP_OPENAT:
			{
				output->verbose(CALL_INFO, 16, 0, "-> call is openat()\n");
				VanadisSyscallOpenAtEvent* openat_ev = dynamic_cast< VanadisSyscallOpenAtEvent* >(sys_ev);

				if( nullptr == openat_ev ) {
					output->fatal(CALL_INFO, -1, "-> error unable ot cast syscall to an openat event.\n");
				}

				output->verbose(CALL_INFO, 16, 0, "-> call is openat( %" PRId64 ", 0x%0llx, %" PRId64 " )\n",
					openat_ev->getDirectoryFileDescriptor(),
					openat_ev->getPathPointer(), openat_ev->getFlags() );

				handler_state = new VanadisOpenAtHandlerState( openat_ev->getDirectoryFileDescriptor(),
					openat_ev->getPathPointer(), openat_ev->getFlags() );

				current_syscall = std::bind( &VanadisNodeOSCoreHandler::processSyscallOpenAt, this );
				const uint64_t start_read_len = (64 - (openat_ev->getPathPointer() % 64));

				SimpleMem::Request* openat_start_req = new SimpleMem::Request( SimpleMem::Request::Read,
					openat_ev->getPathPointer(), start_read_len );
				sendMemRequest( openat_start_req );
			}
			break;

		case SYSCALL_OP_READ:
			{
				output->verbose(CALL_INFO, 16, 0, "-> call is read()\n");
				VanadisSyscallReadEvent* read_ev = dynamic_cast< VanadisSyscallReadEvent* >(sys_ev);

				if( nullptr == read_ev ) {
					output->fatal(CALL_INFO, -1, "-> error unable to cast syscall to a read event.\n");
				}

				output->verbose(CALL_INFO, 16, 0, "-> call is read( %" PRId64 ", 0x%0llx, %" PRId64 " )\n",
					read_ev->getFileDescriptor(), read_ev->getBufferAddress(),
					read_ev->getCount() );

				for( auto next_file = file_descriptors.begin() ; next_file != file_descriptors.end();
					next_file++ ) {

					output->verbose(CALL_INFO, 16, 0, "---> file: %" PRIu32 " (path: %s)\n",
						next_file->first, next_file->second->getPath());
				}

				if( 0 == read_ev->getCount() ) {
					VanadisSyscallResponse* resp = new VanadisSyscallResponse( 0 );
		                        core_link->send( resp );

                		        resetSyscallNothing();
				} else {
					output->fatal(CALL_INFO, -1, "Non-zeros not implemented.\n");
				}
			}
			break;

		case SYSCALL_OP_WRITEV:
			{
				output->verbose(CALL_INFO, 16, 0, "-> call is writev()\n");
				VanadisSyscallWritevEvent* writev_ev = dynamic_cast< VanadisSyscallWritevEvent* >(sys_ev);

				if( nullptr == writev_ev ) {
					output->fatal(CALL_INFO, -1, "-> error unable to cast syscall to a writev event.\n");
				}

				output->verbose(CALL_INFO, 16, 0, "-> call is writev( %" PRId64 ", 0x%0llx, %" PRId64 " )\n",
					writev_ev->getFileDescriptor(), writev_ev->getIOVecAddress(),
					writev_ev->getIOVecCount() );

				auto file_des = file_descriptors.find( writev_ev->getFileDescriptor() );

				if( file_des == file_descriptors.end() ) {
					output->verbose(CALL_INFO, 16, 0, "-> file handle %" PRId64 " is not currently open, return an error code.\n",
						writev_ev->getFileDescriptor());

					// EINVAL = 22
					VanadisSyscallResponse* resp = new VanadisSyscallResponse(22 );
                                                core_link->send( resp );

                                       resetSyscallNothing();
				} else {
					if( writev_ev->getIOVecCount() > 0 ) {
						std::function<void(SimpleMem::Request*> send_req = std::bind(
								&VanadisNodeOSCoreHandler::sendMemRequest, this);
							);

						handler_state = new VanadisWritevHandlerState( writev_ev->getFileDescriptor(), writev_ev->getIOVecAddress(),
       		                                	writev_ev->getIOVecCount(), file_des->getFileHandle(), send_req);

						// Launch read of the initial iovec info
						sendMemRequest( new SimpleMem::Request( SimpleMem::Request::Read,
							writev_ev->getIOVecAddress(), 4);

						current_syscall = std::bind( &VanadisNodeOSCoreHandler::processSyscallWritev, this );
					} else if( writev_ev->getIOVecCount() == 0 ) {{
						VanadisSyscallResponse* resp = new VanadisSyscallResponse( 0 );
			                        core_link->send( resp );

	                		        resetSyscallNothing();
					} else {
						// EINVAL = 22
						VanadisSyscallResponse* resp = new VanadisSyscallResponse( 22 );
			                        core_link->send( resp );

	                		        resetSyscallNothing();
					}
				}
			}
			break;

		default:
			{
				// Send default response
				VanadisSyscallResponse* resp = new VanadisSyscallResponse();
				core_link->send( resp );
			}
			break;
		}

	}

	void sendBlockToMemory( const uint64_t start_address, std::vector<uint8_t>& data_block ) {
		uint64_t line_offset = 64 - (start_address % 64);

		std::vector<uint8_t> offset_payload;
		for( uint64_t i = 0; i < line_offset; ++i ) {
			offset_payload.push_back( data_block[i] );
		}

		sendMemRequest( new SimpleMem::Request( SimpleMem::Request::Write,
			start_address, offset_payload.size(), offset_payload) );

		uint64_t remainder = (data_block.size() - line_offset) % 64;
		uint64_t blocks    = (data_block.size() - line_offset - remainder) / 64;

		for( uint64_t i = 0; i < blocks; ++i ) {
			std::vector<uint8_t> block_payload;

			for( uint64_t j = 0; j < 64; ++j ) {
				block_payload.push_back( data_block[ line_offset + (i*64) + j] );
			}

			sendMemRequest( new SimpleMem::Request( SimpleMem::Request::Write,
				start_address + line_offset + (i*64), block_payload.size(), block_payload) );
		}

		std::vector<uint8_t> remainder_payload;
		for( uint64_t i = 0; i < remainder; ++i ) {
			remainder_payload.push_back( data_block[ line_offset + (blocks * 64) + i ] );
		}

		sendMemRequest( new SimpleMem::Request( SimpleMem::Request::Write,
			start_address + line_offset + (blocks * 64), remainder_payload.size(), remainder_payload) );
	}

	void handleIncomingMemory( SimpleMem::Request* ev ) {
		auto find_event = pending_mem.find(ev->id);

		if( find_event != pending_mem.end() ) {
			pending_mem.erase( find_event );
		}

		// if we have a state registered, pass off the event and payload
		if( nullptr != handler_state ) {
			handler_state->handleIncomingRequest( ev );
		}

		current_syscall();

		delete ev;
	}

	void setSendMemoryCallback( std::function<void( SimpleMem::Request*, uint32_t )> callback ) {
		output->verbose(CALL_INFO, 16, 0, "set new send memory callback\n");
		sendMemEventCallback = callback;
	}

	void sendMemRequest( SimpleMem::Request* req ) {
		output->verbose(CALL_INFO, 16, 0, "sending request: addr: %" PRIu64 " / size: %" PRIu64 "\n",
			req->addr, (uint64_t) req->size);

		pending_mem.insert( req->id );
		sendMemEventCallback( req, core_id );
	}

	void processSyscallUname() {
		output->verbose(CALL_INFO, 16, 0, "processing callback for uname, pending memory events: %" PRIu64 "\n",
			(uint64_t) pending_mem.size());

		if( 0 == pending_mem.size() ) {
			output->verbose(CALL_INFO, 16, 0, "completed, returning response to calling core.\n");
			VanadisSyscallResponse* resp = new VanadisSyscallResponse();
                	core_link->send( resp );

			resetSyscallNothing();
		}
	}

	void processSyscallOpenAt() {
		VanadisOpenAtHandlerState* openat_state = (VanadisOpenAtHandlerState*)( handler_state );

		if( openat_state->completedPathRead() ) {
			output->verbose(CALL_INFO, 16, 0, "openat->path read is completed %s, opening at file handle: %" PRIu32 "\n", openat_state->getOpenAtPath(),
				next_file_id);
			output->verbose(CALL_INFO, 16, 0, "call to openat() is complete, returning control to the calling core (core: %" PRIu32 ")\n", core_id);

			VanadisOSFileDescriptor* new_file = new VanadisOSFileDescriptor( next_file_id++, openat_state->getOpenAtPath() );
			file_descriptors.insert( std::pair< uint32_t, VanadisOSFileDescriptor* >( new_file->getHandle(), new_file ) );

			VanadisSyscallResponse* resp = new VanadisSyscallResponse( 0 );
                	core_link->send( resp );

			resetSyscallNothing();
		} else {
			SimpleMem::Request* new_req = new SimpleMem::Request( SimpleMem::Request::Read,
				openat_state->getPathPtr() + openat_state->getOpenAtPathLength(), 64 );
			sendMemRequest( new_req );
		}
	}

	void processSyscallWritev() {
/*
		VanadisWritevHandlerState* writev_state = ((VanadisWritevHandlerState*)( handler_state );

		if( writev_state->getCurrentIOVec() < write_state->getIOVecCount() ) {

		} else {
			VanadisSyscallResponse* resp = new VanadisSyscallResponse( writev_state->getTotalBytesWritten() );
                        core_link->send( resp );

                        resetSyscallNothing();
		}
*/
	}

	void processSyscallNothing() {

	}

protected:
	std::function<void( SimpleMem::Request*, uint32_t )> sendMemEventCallback;
	std::function<void()> current_syscall;
	std::unordered_set< SimpleMem::Request::id_t > pending_mem;
	std::unordered_map<uint32_t, VanadisOSFileDescriptor*> file_descriptors;

	VanadisHandlerState* handler_state;

	SST::Link* core_link;
	SST::Output* output;
	const uint32_t core_id;
	uint32_t next_file_id;
};

}
}

#endif
