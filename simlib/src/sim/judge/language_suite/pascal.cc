#include <optional>
#include <simlib/file_info.hh>
#include <simlib/file_path.hh>
#include <simlib/merge.hh>
#include <simlib/sandbox/sandbox.hh>
#include <simlib/sandbox/seccomp/allow_common_safe_syscalls.hh>
#include <simlib/sandbox/seccomp/bpf_builder.hh>
#include <simlib/sim/judge/language_suite/fully_compiled_language.hh>
#include <simlib/sim/judge/language_suite/pascal.hh>
#include <simlib/slice.hh>
#include <string_view>
#include <sys/ioctl.h>
#include <vector>

using MountTmpfs = sandbox::RequestOptions::LinuxNamespaces::Mount::MountTmpfs;
using BindMount = sandbox::RequestOptions::LinuxNamespaces::Mount::BindMount;
using CreateDir = sandbox::RequestOptions::LinuxNamespaces::Mount::CreateDir;
using CreateFile = sandbox::RequestOptions::LinuxNamespaces::Mount::CreateFile;

namespace sim::judge::language_suite {

Pascal::Pascal()
: FullyCompiledLanguage{"/usr/bin/fpc", [] {
                            auto bpf = sandbox::seccomp::BpfBuilder{};
                            sandbox::seccomp::allow_common_safe_syscalls(bpf);
                            bpf.err_syscall(EPERM, SCMP_SYS(sysinfo));
                            return bpf.export_to_fd();
                        }()} {}

sandbox::Result Pascal::run_compiler(
    Slice<std::string_view> extra_args,
    std::optional<int> compilation_errors_fd,
    Slice<sandbox::RequestOptions::LinuxNamespaces::Mount::Operation> mount_ops,
    CompileOptions options
) {
    return sc
        .await_result(
            sc
                .send_request(
                    compiler_executable_path,
                    merge(std::vector<std::string_view>{"fpc", "-O2", "-XS", "-Xt"}, extra_args),
                    {
                        .stdout_fd = compilation_errors_fd,
                        .stderr_fd = compilation_errors_fd,
                        .env = {{"PATH=/usr/bin"}},
                        .linux_namespaces =
                            {
                                .user =
                                    {
                                        .inside_uid = 1000,
                                        .inside_gid = 1000,
                                    },
                                .mount =
                                    {
                                        .operations =
                                            merge(
                                                merge(
                                                    std::
                                                        vector<
                                                            sandbox::RequestOptions::
                                                                LinuxNamespaces::Mount::Operation>{
                                                            MountTmpfs{
                                                                .path = "/",
                                                                .max_total_size_of_files_in_bytes =
                                                                    options.max_file_size_in_bytes,
                                                                .inode_limit = 32,
                                                                .read_only = false,
                                                            },
                                                            CreateDir{.path = "/../etc"},
                                                            CreateDir{.path = "/../lib"},
                                                            CreateDir{.path = "/../lib64"},
                                                            CreateDir{.path = "/../tmp"},
                                                            CreateDir{.path = "/../usr"},
                                                            CreateDir{.path = "/../usr/bin"},
                                                            CreateDir{.path = "/../usr/lib"},
                                                            CreateDir{.path = "/../usr/lib64"},
                                                            CreateFile{.path = "/../etc/fpc.cfg"},
                                                            BindMount{
                                                                .source = "/lib",
                                                                .dest = "/../lib",
                                                                .no_exec = false,
                                                            },
                                                            BindMount{
                                                                .source = "/lib64",
                                                                .dest = "/../lib64",
                                                                .no_exec = false,
                                                            },
                                                            BindMount{
                                                                .source = "/usr/bin",
                                                                .dest = "/../usr/bin",
                                                                .no_exec = false,
                                                            },
                                                            BindMount{
                                                                .source = "/usr/lib",
                                                                .dest = "/../usr/lib",
                                                                .no_exec = false,
                                                            },
                                                            BindMount{
                                                                .source = "/usr/lib64",
                                                                .dest = "/../usr/lib64",
                                                                .no_exec = false,
                                                            },
                                                            BindMount{
                                                                .source = "/etc/fpc.cfg",
                                                                .dest = "/../etc/fpc.cfg",
                                                            },
                                                        },
                                                    path_exists("/etc/alternatives/") ? Slice<sandbox::RequestOptions::LinuxNamespaces::Mount::Operation>{{
                                                                                            CreateDir{
                                                                                                .path = "/../etc/alternatives"},
                                                                                            BindMount{
                                                                                                .source =
                                                                                                    "/etc/alternatives/",
                                                                                                .dest = "/../etc/alternatives",
                                                                                            },
                                                                                        }}
                                                                                      : Slice<sandbox::RequestOptions::LinuxNamespaces::Mount::Operation>{}
                                                ),
                                                mount_ops
                                            ),
                                        .new_root_mount_path = "/..",
                                    },
                            },
                        .cgroup =
                            {
                                .process_num_limit = 32,
                                .memory_limit_in_bytes = options.memory_limit_in_bytes,
                                .cpu_max_bandwidth =
                                    sandbox::RequestOptions::Cgroup::CpuMaxBandwidth{
                                        .max_usec = 10000,
                                        .period_usec = 10000,
                                    },
                            },
                        .prlimit =
                            {
                                .max_core_file_size_in_bytes = 0,
                                .max_file_size_in_bytes = options.max_file_size_in_bytes,
                            },
                        .time_limit = options.time_limit,
                        .cpu_time_limit = options.cpu_time_limit,
                        .seccomp_bpf_fd = compiler_seccomp_bpf_fd,
                    }
                )
        );
}

sandbox::Result Pascal::is_supported_impl(CompileOptions options) {
    return run_compiler({{"-iV"}}, std::nullopt, {}, std::move(options));
}

sandbox::Result Pascal::compile_impl(
    FilePath source, FilePath executable, int compilation_errors_fd, CompileOptions options
) {
    return run_compiler(
        {{"source.pas", "-oexe"}},
        compilation_errors_fd,
        {{
            CreateFile{.path = "/../exe"},
            CreateFile{.path = "/../source.pas"},
            BindMount{
                .source = std::string_view{executable},
                .dest = "/../exe",
                .read_only = false,
            },
            BindMount{
                .source = std::string_view{source},
                .dest = "/../source.pas",
            },
        }},
        std::move(options)
    );
}

} // namespace sim::judge::language_suite
