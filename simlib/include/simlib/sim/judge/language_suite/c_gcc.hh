#pragma once

#include <optional>
#include <simlib/file_path.hh>
#include <simlib/sandbox/sandbox.hh>
#include <simlib/sim/judge/language_suite/fully_compiled_language.hh>
#include <simlib/slice.hh>
#include <string_view>

namespace sim::judge::language_suite {

class C_GCC final : public FullyCompiledLanguage {
    std::string_view std_flag;

    [[nodiscard]] sandbox::Result run_compiler(
        Slice<std::string_view> extra_args,
        std::optional<int> compilation_errors_fd,
        Slice<sandbox::RequestOptions::LinuxNamespaces::Mount::Operation> mount_ops,
        CompileOptions options
    );

public:
    enum class Standard {
        C11,
        C17,
        Gnu11,
        Gnu17,
    };

    explicit C_GCC(Standard standard);

protected:
    sandbox::Result is_supported_impl(CompileOptions options) final;

    sandbox::Result compile_impl(
        FilePath source, FilePath executable, int compilation_errors_fd, CompileOptions options
    ) final;
};

} // namespace sim::judge::language_suite
