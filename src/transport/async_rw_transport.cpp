#include "mcp/transport/async_rw_transport.hpp"

// AsyncRwTransport is a fully template-based class (parameterized on R, ReadStream,
// WriteStream). Its methods are defined in the header or must be explicitly
// instantiated per concrete stream type by the user. No non-template definitions
// are needed here.
//
// The helper functions make_pipe_transport and make_socket_transport are also
// templates that would need explicit instantiation for specific roles. Users
// should include the header directly for these.
