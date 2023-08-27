#include "../../communication/client_supervisor.hh"

#include <array>
#include <memory>
#include <simlib/array_vec.hh>
#include <simlib/sandbox/sandbox.hh>
#include <simlib/slice.hh>
#include <string_view>

namespace sandbox::client::request {

struct SerializedReuest {
    ArrayVec<int, 4> fds;
    std::array<std::byte, sizeof(communication::client_supervisor::request::body_len_t)> header;
    std::unique_ptr<std::byte[]> body;
    size_t body_len;
};

SerializedReuest
serialize(int executable_fd, Slice<std::string_view> argv, const RequestOptions& options);

} // namespace sandbox::client::request
