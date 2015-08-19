#pragma once

/**
 * @brief Compiles C++ @p source to @p exec using g++ via PRoot
 * @details If compilation is not successful then errors are placed if c_errors
 * (if  c_errors is not NULL)
 *
 * @param source C++ source filename
 * @param exec output executable filename
 * @param verbosity verbosity level: 0 - quiet (no output), 1 - (errors only),
 * 2 or more - verbose mode
 * @param c_errors pointer to string in which compilation errors will be placed
 * @param c_errors_max_len maximum c_errors length
 * @param proot_path path to PRoot executable (to pass to spawn())
 * @return 0 on success, non-zero value on error
 */
int compile(const std::string& source, const std::string& exec,
	unsigned verbosity, std::string* c_errors = NULL,
	size_t c_errors_max_len = -1, const std::string& proot_path = "proot");
