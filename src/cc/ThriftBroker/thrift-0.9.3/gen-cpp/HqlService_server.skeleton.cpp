// This autogenerated skeleton file illustrates how to build a server.
// You should copy it to another filename to avoid overwriting it.

#include "HqlService.h"
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

using boost::shared_ptr;

using namespace  ::Hypertable::ThriftGen;

class HqlServiceHandler : virtual public HqlServiceIf {
 public:
  HqlServiceHandler() {
    // Your initialization goes here
  }

  /**
   * Execute an HQL command
   * 
   * @param ns - Namespace id
   * 
   * @param command - HQL command
   * 
   * @param noflush - Do not auto commit any modifications (return a mutator)
   * 
   * @param unbuffered - return a scanner instead of buffered results
   * 
   * @param ns
   * @param command
   * @param noflush
   * @param unbuffered
   */
  void hql_exec(HqlResult& _return, const int64_t ns, const std::string& command, const bool noflush, const bool unbuffered) {
    // Your implementation goes here
    printf("hql_exec\n");
  }

  /**
   * Convenience method for executing an buffered and flushed query
   * 
   * because thrift doesn't (and probably won't) support default argument values
   * 
   * @param ns - Namespace
   * 
   * @param command - HQL command
   * 
   * @param ns
   * @param command
   */
  void hql_query(HqlResult& _return, const int64_t ns, const std::string& command) {
    // Your implementation goes here
    printf("hql_query\n");
  }

  /**
   * @see hql_exec
   * 
   * @param ns
   * @param command
   * @param noflush
   * @param unbuffered
   */
  void hql_exec_as_arrays(HqlResultAsArrays& _return, const int64_t ns, const std::string& command, const bool noflush, const bool unbuffered) {
    // Your implementation goes here
    printf("hql_exec_as_arrays\n");
  }

  void hql_exec2(HqlResult2& _return, const int64_t ns, const std::string& command, const bool noflush, const bool unbuffered) {
    // Your implementation goes here
    printf("hql_exec2\n");
  }

  /**
   * @see hql_query
   * 
   * @param ns
   * @param command
   */
  void hql_query_as_arrays(HqlResultAsArrays& _return, const int64_t ns, const std::string& command) {
    // Your implementation goes here
    printf("hql_query_as_arrays\n");
  }

  void hql_query2(HqlResult2& _return, const int64_t ns, const std::string& command) {
    // Your implementation goes here
    printf("hql_query2\n");
  }

};

int main(int argc, char **argv) {
  int port = 9090;
  shared_ptr<HqlServiceHandler> handler(new HqlServiceHandler());
  shared_ptr<TProcessor> processor(new HqlServiceProcessor(handler));
  shared_ptr<TServerTransport> serverTransport(new TServerSocket(port));
  shared_ptr<TTransportFactory> transportFactory(new TBufferedTransportFactory());
  shared_ptr<TProtocolFactory> protocolFactory(new TBinaryProtocolFactory());

  TSimpleServer server(processor, serverTransport, transportFactory, protocolFactory);
  server.serve();
  return 0;
}

