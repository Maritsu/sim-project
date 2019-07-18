#pragma once

#include "debug.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

// File modes
constexpr int S_0600 = S_IRUSR | S_IWUSR;
constexpr int S_0644 = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
constexpr int S_0700 = S_IRWXU;
constexpr int S_0755 = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;

// Encapsulates file descriptor
class FileDescriptor {
	int fd_;

public:
	explicit FileDescriptor(int fd = -1) noexcept : fd_(fd) {}

	explicit FileDescriptor(FilePath filename, int flags,
	                        mode_t mode = S_0644) noexcept
	   : fd_(::open(filename, flags, mode)) {}

	FileDescriptor(const FileDescriptor&) = delete;

	FileDescriptor(FileDescriptor&& fd) noexcept : fd_(fd.release()) {}

	FileDescriptor& operator=(const FileDescriptor&) = delete;

	FileDescriptor& operator=(FileDescriptor&& fd) noexcept {
		reset(fd.release());
		return *this;
	}

	FileDescriptor& operator=(int fd) noexcept {
		reset(fd);
		return *this;
	}

	bool opened() const noexcept { return (fd_ >= 0); }

	operator int() const noexcept { return fd_; }

	int release() noexcept {
		int fd = fd_;
		fd_ = -1;
		return fd;
	}

	void reset(int fd) noexcept {
		if (fd_ >= 0)
			(void)::close(fd_);
		fd_ = fd;
	}

	int open(FilePath filename, int flags, int mode = S_0644) noexcept {
		return fd_ = ::open(filename, flags, mode);
	}

	int reopen(FilePath filename, int flags, int mode = S_0644) noexcept {
		reset(::open(filename, flags, mode));
		return fd_;
	}

	int close() noexcept {
		if (fd_ < 0)
			return 0;

		int rc = ::close(fd_);
		fd_ = -1;
		return rc;
	}

	~FileDescriptor() {
		if (fd_ >= 0)
			(void)::close(fd_);
	}
};

// Encapsulates directory object DIR
class Directory {
	DIR* dir_;

public:
	explicit Directory(DIR* dir = nullptr) noexcept : dir_(dir) {}

	explicit Directory(FilePath pathname) noexcept : dir_(opendir(pathname)) {}

	Directory(const Directory&) = delete;

	Directory(Directory&& d) noexcept : dir_(d.release()) {}

	Directory& operator=(const Directory&) = delete;

	Directory& operator=(Directory&& d) noexcept {
		reset(d.release());
		return *this;
	}

	Directory& operator=(DIR* d) noexcept {
		reset(d);
		return *this;
	}

	operator DIR*() const noexcept { return dir_; }

	DIR* release() noexcept {
		DIR* d = dir_;
		dir_ = nullptr;
		return d;
	}

	void reset(DIR* d) noexcept {
		if (dir_)
			(void)closedir(dir_);
		dir_ = d;
	}

	void close() noexcept {
		if (dir_) {
			(void)closedir(dir_);
			dir_ = nullptr;
		}
	}

	Directory& reopen(FilePath pathname) noexcept {
		reset(opendir(pathname));
		return *this;
	}

	~Directory() {
		if (dir_)
			(void)closedir(dir_);
	}
};

// The same as unlink(const char*)
inline int unlink(FilePath pathname) noexcept {
	return unlink(pathname.data());
}

// The same as remove(const char*)
inline int remove(FilePath pathname) noexcept {
	return remove(pathname.data());
}

/**
 * @brief Removes recursively file/directory @p pathname relative to the
 *   directory file descriptor @p dirfd
 *
 * @param dirfd directory file descriptor
 * @param pathname file/directory pathname (relative to @p dirfd)
 *
 * @return 0 on success, -1 on error
 *
 * @errors The same that occur for openat(2), unlinkat(2), fdopendir(3)
 */
int remove_rat(int dirfd, FilePath pathname) noexcept;

/**
 * @brief Removes recursively file/directory @p pathname
 * @details Uses remove_rat()
 *
 * @param pathname file/directory to remove
 *
 * @return 0 on success, -1 on error
 *
 * @errors The same that occur for remove_rat()
 */
inline int remove_r(FilePath pathname) noexcept {
	return remove_rat(AT_FDCWD, pathname);
}

/**
 * @brief Creates (and opens) unlinked temporary file
 * @details Uses open(2) if O_TMPFILE is defined, or mkostemp(3)
 *
 * @param flags flags which be ORed with O_TMPFILE | O_RDWR in open(2) or
 *   passed to mkostemp(3)
 *
 * @return file descriptor on success, -1 on error
 *
 * @errors The same that occur for open(2) (if O_TMPFILE is defined) or
 *   mkostemp(3)
 */
int openUnlinkedTmpFile(int flags = 0) noexcept;

class TemporaryDirectory {
private:
	std::string path_; // absolute path
	std::unique_ptr<char[]> name_;

public:
	TemporaryDirectory() = default; // Does NOT create a temporary directory

	explicit TemporaryDirectory(FilePath templ);

	TemporaryDirectory(const TemporaryDirectory&) = delete;
	TemporaryDirectory(TemporaryDirectory&&) noexcept = default;
	TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

	TemporaryDirectory& operator=(TemporaryDirectory&& td) {
		if (exist() && remove_r(path_) == -1)
			THROW("remove_r() failed", errmsg());

		path_ = std::move(td.path_);
		name_ = std::move(td.name_);
		return *this;
	}

	~TemporaryDirectory();

	// Returns true if object holds a real temporary directory
	bool exist() const noexcept { return (name_.get() != nullptr); }

	// Directory name (from constructor parameter) with trailing '/'
	const char* name() const noexcept { return name_.get(); }

	// Directory name (from constructor parameter) with trailing '/'
	std::string sname() const { return name_.get(); }

	// Directory absolute path with trailing '/'
	const std::string& path() const noexcept { return path_; }
};

// Create directory (not recursively) (mode: 0755/rwxr-xr-x)
inline int mkdir(FilePath pathname) noexcept { return mkdir(pathname, S_0755); }

// Create directories recursively (default mode: 0755/rwxr-xr-x)
int mkdir_r(std::string path, mode_t mode = S_0755) noexcept;

class TemporaryFile {
	std::string path_; // absolute path

public:
	/// Does NOT create a temporary file
	TemporaryFile() = default;

	/// The last six characters of template must be "XXXXXX" and these are
	/// replaced with a string that makes the filename unique.
	explicit TemporaryFile(std::string templ) {
		throw_assert(hasSuffix(templ, "XXXXXX") && "this is needed by mkstemp");
		FileDescriptor fd(mkstemp(templ.data()));
		if (not fd.opened())
			THROW("mkstemp() failed", errmsg());
		path_ = std::move(templ);
	}

	TemporaryFile(const TemporaryFile&) = delete;
	TemporaryFile& operator=(const TemporaryFile&) = delete;
	TemporaryFile(TemporaryFile&&) noexcept = default;

	TemporaryFile& operator=(TemporaryFile&& tf) noexcept {
		if (is_open())
			unlink(path_);

		path_ = std::move(tf.path_);
		return *this;
	}

	~TemporaryFile() {
		if (is_open())
			unlink(path_);
	}

	bool is_open() const noexcept { return not path_.empty(); }

	const std::string& path() const noexcept { return path_; }
};

class OpenedTemporaryFile {
	std::string path_; // absolute path
	FileDescriptor fd_;

public:
	/// Does NOT create a temporary file
	OpenedTemporaryFile() = default;

	/// The last six characters of template must be "XXXXXX" and these are
	/// replaced with a string that makes the filename unique.
	explicit OpenedTemporaryFile(std::string templ) {
		throw_assert(hasSuffix(templ, "XXXXXX") && "this is needed by mkstemp");
		fd_ = mkstemp(templ.data());
		if (not fd_.opened())
			THROW("mkstemp() failed", errmsg());
		path_ = std::move(templ);
	}

	OpenedTemporaryFile(const OpenedTemporaryFile&) = delete;
	OpenedTemporaryFile& operator=(const OpenedTemporaryFile&) = delete;
	OpenedTemporaryFile(OpenedTemporaryFile&&) noexcept = default;

	OpenedTemporaryFile& operator=(OpenedTemporaryFile&& tf) noexcept {
		if (is_open())
			unlink(path_);

		path_ = std::move(tf.path_);
		fd_ = std::move(tf.fd_);

		return *this;
	}

	~OpenedTemporaryFile() {
		if (is_open())
			unlink(path_);
	}

	operator const FileDescriptor&() const { return fd_; }

	operator int() const { return fd_; }

	bool is_open() const noexcept { return fd_.opened(); }

	const std::string& path() const noexcept { return path_; }
};

class DirectoryChanger {
	FileDescriptor old_cwd {open(".", O_RDONLY | O_CLOEXEC)};

public:
	DirectoryChanger(FilePath new_wd) {
		if (old_cwd == -1)
			THROW("open() failed", errmsg());

		if (chdir(new_wd.data()) == -1) {
			auto err = errno;
			old_cwd.close();
			THROW("chdir() failed", errmsg(err));
		}
	}

	DirectoryChanger(const DirectoryChanger&) = delete;
	DirectoryChanger(DirectoryChanger&&) noexcept = default;
	DirectoryChanger& operator=(const DirectoryChanger&) = delete;
	DirectoryChanger& operator=(DirectoryChanger&&) noexcept = default;

	~DirectoryChanger() {
		if (old_cwd >= 0)
			(void)fchdir(old_cwd);
	}
};

/**
 * @brief Calls @p func on every component of the @p dir other than "." and
 *   ".."
 *
 * @param dir directory object, readdir(3) is used on it so one may want to
 *   save its pos via telldir(3) and use seekdir(3) after the call or just
 *   rewinddir(3) after the call
 * @param func function to call on every component (other than "." and ".."),
 *   it should take one argument - dirent*, if it return sth convertible to
 *   false the lookup will break
 */
template <class Func, class ErrFunc>
void forEachDirComponent(DIR* dir, Func&& func, ErrFunc&& readdir_failed) {
	dirent* file;
	for (;;) {
		errno = 0;
		file = readdir(dir);
		if (file == nullptr) {
			if (errno == 0)
				return; // No more entries

			readdir_failed();
			return;
		}

		if (strcmp(file->d_name, ".") and strcmp(file->d_name, "..")) {
			if constexpr (std::is_constructible_v<decltype(func(file)), bool>) {
				if (not func(file))
					return;
			} else {
				func(file);
			}
		}
	}
}

/**
 * @brief Calls @p func on every component of the directory @p pathname other
 *   than "." and ".."
 *
 * @param pathname path of the directory to scan
 * @param func function to call on every component (other than "." and ".."),
 *   it should take one argument - dirent*, if it return sth convertible to
 *   false the lookup will break
 */
template <class Func, class ErrFunc>
void forEachDirComponent(FilePath pathname, Func&& func,
                         ErrFunc&& readdir_failed) {
	Directory dir {pathname};
	if (!dir)
		THROW("opendir()", errmsg());

	return forEachDirComponent(dir, std::forward<Func>(func),
	                           std::forward<ErrFunc>(readdir_failed));
}

template <class A, class Func>
auto forEachDirComponent(A&& a, Func&& func) {
	return forEachDirComponent(std::forward<A>(a), std::forward<Func>(func),
	                           [] { THROW("readdir()", errmsg()); });
}

/**
 * @brief Removes recursively all the contents of the directory @p pathname
 *   relative to the directory file descriptor @p dirfd
 * @details Uses remove_rat()
 *
 * @param dirfd directory file descriptor
 * @param pathname directory pathname (relative to @p dirfd)
 *
 * @return 0 on success, -1 on error
 *
 * @errors The same that occur for remove_rat()
 */
int removeDirContents_at(int dirfd, FilePath pathname) noexcept;

/**
 * @brief Removes recursively all the contents of the directory @p pathname
 * @details Uses remove_rat()
 *
 * @param pathname path to the directory
 *
 * @return 0 on success, -1 on error
 *
 * @errors The same that occur for remove_rat()
 */
inline int removeDirContents(FilePath pathname) noexcept {
	return removeDirContents_at(AT_FDCWD, pathname);
}

/// @brief Creates directories containing @p file if they don't exist
int create_subdirectories(StringView file) noexcept;

/**
 * @brief Fast copies file from @p infd to @p outfd
 * @details Reads from @p infd form it's offset and writes to @p outfd from its
 *   offset
 *
 * @param infd file descriptor from which data will be copied
 * @param outfd file descriptor to which data will be copied
 *
 * @return 0 on success, -1 on error
 *
 * @errors The same that occur for read(2), write(2)
 */
int blast(int infd, int outfd) noexcept;

/**
 * @brief Copies (overwrites) file from @p src to @p dest
 * @details Needs directory containing @p dest to exist
 *
 * @param src source file
 * @param dest destination file
 * @param mode access mode of the destination file (will be set iff the file is
 *   created)
 *
 * @return 0 on success, -1 on error
 *
 * @errors The same that occur for open(2), blast()
 */
int copy(FilePath src, FilePath dest, mode_t mode = S_0644) noexcept;

/**
 * @brief Copies (overrides) file @p src to @p dest relative to a directory
 *   file descriptor
 *
 * @param dirfd1 directory file descriptor
 * @param src source file (relative to @p dirfd1)
 * @param dirfd2 directory file descriptor
 * @param dest destination file (relative to @p dirfd2)
 *
 * @return 0 on success, -1 on error
 *
 * @errors The same that occur for openat(2), blast()
 */
int copyat(int dirfd1, FilePath src, int dirfd2, FilePath dest) noexcept;

/**
 * @brief Copies (overrides) file/directory @p src to @p dest relative to a
 *   directory file descriptor
 *
 * @param dirfd1 directory file descriptor
 * @param src source file/directory (relative to @p dirfd1)
 * @param dirfd2 directory file descriptor
 * @param dest destination file/directory (relative to @p dirfd2)
 *
 * @return 0 on success, -1 on error
 *
 * @errors The same that occur for fstat64(2), openat(2), fdopendir(3),
 *   mkdirat(2), copyat()
 */
int copy_rat(int dirfd1, FilePath src, int dirfd2, FilePath dest) noexcept;

/**
 * @brief Copies (overrides) recursively files and folders
 * @details Uses copy_rat()
 *
 * @param src source file/directory
 * @param dest destination file/directory
 * @param create_subdirs whether create subdirectories or not
 *
 * @return 0 on success, -1 on error
 *
 * @errors The same that occur for copy_rat()
 */
int copy_r(FilePath src, FilePath dest, bool create_subdirs = true) noexcept;

inline int access(FilePath pathname, int mode) noexcept {
	return access(pathname.data(), mode);
}

inline int rename(FilePath source, FilePath destination) noexcept {
	return rename(source.data(), destination.data());
}

/**
 * @brief Moves file from @p oldpath to @p newpath
 * @details First creates directory containing @p newpath
 *   (if @p create_subdirs is true) and then uses rename(2) to move
 *   file/directory or copy_r() and remove_r() if rename(2) fails with
 *   errno == EXDEV
 *
 * @param oldpath path to file/directory
 * @param newpath location
 * @param create_subdirs whether create @p newpath subdirectories or not
 *
 * @return Return value of rename(2) or copy_r() or remove_r()
 */
int move(FilePath oldpath, FilePath newpath,
         bool create_subdirs = true) noexcept;

/**
 * @brief Creates file pathname with access mode @p mode
 *
 * @param pathname pathname for a file
 * @param mode access mode
 *
 * @return 0 on success, -1 on error
 *
 * @errors The same that occur for creat(2), close(2)
 */
int createFile(FilePath pathname, mode_t mode = S_0644) noexcept;

/**
 * @brief Read @p count bytes to @p buff from @p fd
 * @details Uses read(2), but reads until it is unable to read
 *
 * @param fd file descriptor
 * @param buff where place read bytes
 * @param count number of bytes to read
 *
 * @return number of bytes read, if error occurs then errno is > 0
 *
 * @errors The same as for read(2) except EINTR
 */
size_t readAll(int fd, void* buff, size_t count) noexcept;

/**
 * @brief Write @p count bytes to @p fd from @p buff
 * @details Uses write(2), but writes until it is unable to write
 *
 * @param fd file descriptor
 * @param buff where write bytes from
 * @param count number of bytes to write
 *
 * @return number of bytes written, if error occurs then errno is > 0
 *
 * @errors The same as for write(2) except EINTR
 */
size_t writeAll(int fd, const void* buff, size_t count) noexcept;

/**
 * @brief Write @p count bytes to @p fd from @p str
 * @details Uses write(2), but writes until it is unable to write
 *
 * @param fd file descriptor
 * @param buff where write bytes from
 * @param count number of bytes to write
 *
 * @errors If any error occurs then an exception is thrown
 */
inline void writeAll_throw(int fd, const void* buff, size_t count) {
	if (writeAll(fd, buff, count) != count)
		THROW("write()", errmsg());
}

/**
 * @brief Write @p count bytes to @p fd from @p str
 * @details Uses write(2), but writes until it is unable to write
 *
 * @param fd file descriptor
 * @param str where write bytes from
 *
 * @return number of bytes written, if error occurs then errno is > 0
 *
 * @errors The same as for write(2) except EINTR
 */
inline size_t writeAll(int fd, StringView str) noexcept {
	return writeAll(fd, str.data(), str.size());
}

/**
 * @brief Write @p count bytes to @p fd from @p str
 * @details Uses write(2), but writes until it is unable to write
 *
 * @param fd file descriptor
 * @param str where write bytes from
 *
 * @errors If any error occurs then an exception is thrown
 */
inline void writeAll_throw(int fd, StringView str) {
	writeAll_throw(fd, str.data(), str.size());
}

/*
 *  Returns an absolute path that does not contain any . or .. components,
 *  nor any repeated path separators (/). @p curr_dir can be empty. If path
 * begins with / then @p curr_dir is ignored.
 */
std::string abspath(StringView path, size_t beg = 0,
                    size_t end = std::string::npos, std::string curr_dir = "/");

/*
 *  Returns an absolute path that does not contain any . or .. components,
 *  nor any repeated path separators (/). @p curr_dir can be empty. If path
 * begins with / then @p curr_dir is ignored.
 */
inline std::string abspath(StringView path, std::string curr_dir) {
	return abspath(path, 0, std::string::npos, std::move(curr_dir));
}

// Returns extension (without dot) e.g. "foo.cc" -> "cc", "bar" -> ""
inline StringView getExtension(StringView file) {
	size_t x = file.rfind('.');
	if (x == file.npos)
		return {}; // No extension

	return file.substr(x + 1);
}

/**
 * @brief Returns the filename of the path @p path
 * @details Examples:
 *   "/my/path/foo.bar" -> "foo.bar"
 *   "/my/path/" -> ""
 *   "/" -> ""
 *   "/my/path/." -> "."
 *   "/my/path/.." -> ".."
 *   "foo" -> "foo"
 *
 * @param path given path
 *
 * @return Extracted filename
 */
template <class T>
inline std::enable_if_t<std::is_convertible<T, CStringView>::value, CStringView>
filename(T&& path) {
	CStringView path_str(std::forward<T>(path));
	auto pos = path_str.rfind('/');
	return path_str.substr(pos == CStringView::npos ? 0 : pos + 1);
}

/**
 * @brief Returns the filename of the path @p path
 * @details Examples:
 *   "/my/path/foo.bar" -> "foo.bar"
 *   "/my/path/" -> ""
 *   "/" -> ""
 *   "/my/path/." -> "."
 *   "/my/path/.." -> ".."
 *   "foo" -> "foo"
 *
 * @param path given path
 *
 * @return Extracted filename
 */
template <class T>
inline std::enable_if_t<!std::is_convertible<T, CStringView>::value, StringView>
filename(T&& path) {
	StringView path_str(std::forward<T>(path));
	auto pos = path_str.rfind('/');
	return path_str.substr(pos == StringView::npos ? 0 : pos + 1);
}

/**
 * @brief Reads until end of file
 *
 * @param fd file descriptor to read from
 * @param bytes number of bytes to read
 *
 * @return read contents
 *
 * @errors If any error occurs an exception of type std::runtime_error is
 *   thrown (may happen if read(2) fails)
 */
std::string getFileContents(int fd, size_t bytes = -1);

/**
 * @brief Reads form @p fd from beg to end
 *
 * @param fd file descriptor to read from
 * @param beg begin offset (if negative, then is set to file_size + @p beg)
 * @param end end offset (@p end < 0 means size of file)
 *
 * @return read contents
 *
 * @errors If any error occurs an exception of type std::runtime_error is
 *   thrown (may happen if lseek64(3) or read(2) fails)
 */
std::string getFileContents(int fd, off64_t beg, off64_t end);

/**
 * @brief Reads until end of file
 *
 * @param file file to read from
 *
 * @return read contents
 *
 * @errors If any error occurs an exception of type std::runtime_error is
 *   thrown (may happen if open(2), read(2) or close(2) fails)
 */
std::string getFileContents(FilePath file);

/**
 * @brief Reads form @p file from beg to end
 *
 * @param file file to read from
 * @param beg begin offset (if negative, then is set to file_size + @p beg)
 * @param end end offset (@p end < 0 means size of file)
 *
 * @return read contents
 *
 * @errors If any error occurs an exception of type std::runtime_error is
 *   thrown (may happen if open(2), lseek64(3), read(2) or close(2) fails)
 */
std::string getFileContents(FilePath file, off64_t beg, off64_t end = -1);

constexpr int GFBL_IGNORE_NEW_LINES = 1; // Erase '\n' from each line
/**
 * @brief Get file contents by lines in range [first, last)
 *
 * @param file filename
 * @param flags if GFBL_IGNORE_NEW_LINES is set then '\n' is not appended to
 *   each line
 * @param first number of first line to fetch
 * @param last number of first line not to fetch
 *
 * @return vector<string> containing fetched lines
 */
std::vector<std::string> getFileByLines(FilePath file, int flags = 0,
                                        size_t first = 0, size_t last = -1);

/**
 * @brief Writes @p data to file @p file
 * @details Writes all data or nothing
 *
 * @param file file to write to
 * @param data data to write
 * @param mode access mode
 *
 * @errors If any error occurs an exception of type std::runtime_error is
 *   thrown (may happen if open(2) or write(2) fails)
 */
void putFileContents(FilePath file, const char* data, size_t len,
                     mode_t mode = S_0644);

inline void putFileContents(FilePath file, StringView data) {
	return putFileContents(file, data.data(), data.size());
}

// Closes file descriptor automatically
class FileDescriptorCloser {
	int fd_;

public:
	explicit FileDescriptorCloser(int fd) noexcept : fd_(fd) {}

	void cancel() noexcept { fd_ = -1; }

	/**
	 * @brief Closes file descriptor
	 *
	 * @return 0 on success, -1 on error
	 *
	 * @errors The same that occur to close()
	 */
	int close() noexcept {
		if (fd_ < 0)
			return 0;

		int rc = ::close(fd_);
		fd_ = -1;
		return rc;
	}

	~FileDescriptorCloser() noexcept {
		if (fd_ >= 0)
			(void)::close(fd_);
	}
};

template <int (*func)(FilePath)>
class RemoverBase {
	InplaceBuff<PATH_MAX> name;

	RemoverBase(const RemoverBase&) = delete;
	RemoverBase& operator=(const RemoverBase&) = delete;
	RemoverBase(const RemoverBase&&) = delete;
	RemoverBase& operator=(const RemoverBase&&) = delete;

public:
	RemoverBase() : name() {}

	explicit RemoverBase(FilePath str) : RemoverBase(str.data(), str.size()) {}

	/// If @p str is null then @p len is ignored
	RemoverBase(const char* str, size_t len) : name(len + 1) {
		if (len != 0)
			strncpy(name.data(), str, len + 1);
		name.size = len;
	}

	~RemoverBase() {
		if (name.size != 0)
			func(name);
	}

	void cancel() noexcept { name.size = 0; }

	void reset(FilePath str) { reset(str.data(), str.size()); }

	void reset(const char* str, size_t len) {
		cancel();
		if (len != 0) {
			name.lossy_resize(len + 1);
			strncpy(name.data(), str, len + 1);
			name.size = len;
		}
	}

	int removeTarget() noexcept {
		if (name.size == 0)
			return 0;

		int rc = 0;
		rc = func(name);
		cancel();
		return rc;
	}
};

typedef RemoverBase<unlink> FileRemover;
typedef RemoverBase<remove_r> DirectoryRemover;

/**
 * @brief Converts @p size, so that it human readable
 * @details It adds proper suffixes, for example:
 *   1 -> "1 byte"
 *   1023 -> "1023 bytes"
 *   1024 -> "1.0 KiB"
 *   129747 -> "127 KiB"
 *   97379112 -> "92.9 MiB"
 *
 * @param size size to humanize
 *
 * @return humanized file size
 */
std::string humanizeFileSize(uint64_t size);

/**
 * @brief Check whether @p file exists and is a regular file
 *
 * @param file path of the file to check (has to be null-terminated)
 *
 * @return true if @p file is a regular file, false otherwise. To distinguish
 *   other file type error from stat64(2) error set errno to 0 before calling
 *   this function, if stat64(2) fails, errno will have nonzero value
 */
inline bool isRegularFile(FilePath file) noexcept {
	struct stat64 st;
	return (stat64(file, &st) == 0 && S_ISREG(st.st_mode));
}

/**
 * @brief Check whether @p file exists and is a directory
 *
 * @param file path of the file to check (has to be null-terminated)
 *
 * @return true if @p file is a directory, false otherwise. To distinguish
 *   other file type error from stat64(2) error set errno to 0 before calling
 *   this function, if stat64(2) fails, errno will have nonzero value
 */
inline bool isDirectory(FilePath file) noexcept {
	struct stat64 st;
	return (stat64(file, &st) == 0 && S_ISDIR(st.st_mode));
}

inline uint64_t get_file_size(FilePath file) {
	struct stat64 st;
	if (stat64(file, &st))
		THROW("stat()", errmsg());

	return st.st_size;
}

// Returns file modification time (with second precision) as a time_point
inline std::chrono::system_clock::time_point
get_modification_time(const struct stat64& st) noexcept {
	return std::chrono::system_clock::from_time_t(st.st_mtime);
}

// Returns file modification time (with second precision) as a time_point
inline std::chrono::system_clock::time_point
get_modification_time(FilePath file) {
	struct stat64 st;
	if (stat64(file, &st))
		THROW("stat()", errmsg());

	return get_modification_time(st);
}

namespace directory_tree {

// Node represents a directory
class Node {
private:
	/**
	 * @brief Prints tree rooted in *this
	 *
	 * @param stream file to which write (cannot be NULL - does not check that)
	 * @param buff buffer used to printing tree structure
	 */
	void __print(FILE* stream, std::string buff = "") const;

	/// @brief Checks if path is valid in this directory, but path cannot
	///   contain "." and ".." parts
	bool __pathExists(StringView s) const noexcept;

public:
	std::string name_;
	std::vector<std::unique_ptr<Node>> dirs_;
	std::vector<std::string> files_;

	explicit Node(std::string new_name)
	   : name_(std::move(new_name)), dirs_(), files_() {}

	Node(const Node&) = delete;
	Node(Node&&) noexcept = default;
	Node& operator=(const Node&) = delete;
	Node& operator=(Node&&) = default;

	/**
	 * @brief Get subdirectory
	 *
	 * @param pathname name to search (cannot contain '/')
	 *
	 * @return pointer to subdirectory or nullptr if it does not exist
	 */
	Node* dir(StringView pathname) const;

	/// Removes directory in O(n) time (n = # of directories in this node),
	/// returns true if the removal occurred
	bool removeDir(StringView pathname);

	/// Removes file in O(n) time (n = # of files in this node), returns true
	/// if the removal occurred
	bool removeFile(StringView pathname);

	/**
	 * @brief Checks if file exists in this node
	 *
	 * @param pathname file to check (cannot contain '/')
	 *
	 * @return true if exists, false otherwise
	 */
	bool fileExists(StringView pathname) const noexcept {
		return std::binary_search(files_.begin(), files_.end(), pathname);
	}

	/**
	 * @brief Checks if path is valid in this directory
	 *
	 * @param path path to check, for empty one result is false
	 *
	 * @return true if path is valid, false otherwise
	 */
	bool pathExists(StringView path) const {
		return (path.size() and
		        __pathExists(intentionalUnsafeStringView(abspath(path))));
	}

	/**
	 * @brief Prints tree rooted in *this
	 *
	 * @param stream file to write to (if nullptr returns immediately)
	 */
	inline void print(FILE* stream) const {
		if (stream)
			return __print(stream);
	}
};

/**
 * @brief Dumps directory tree (rooted in @p path)
 *
 * @param path path to main directory
 *
 * @return pointer to root node
 */
std::unique_ptr<Node> dumpDirectoryTree(FilePath path);

/**
 * @brief Searches for files in @p dir for which @p func returns true
 *
 * @param dir directory tree to search in
 * @param func predicate function
 * @param path_prefix a string with which every returned path will be prefixed
 *
 * @return vector of paths (relative to @p dir) of files matched by @p func
 */
template <class UnaryPredicate>
std::vector<std::string> findFiles(directory_tree::Node* dir,
                                   UnaryPredicate&& func,
                                   std::string path_prefix = "") {
	if (!dir)
		return {};

	struct Helper {
		const UnaryPredicate& func;
		std::string path;
		std::vector<std::string> res;

		Helper(const UnaryPredicate& f, std::string&& pprefix)
		   : func(f), path(std::move(pprefix)) {}

		void find(directory_tree::Node* d) {
			// Files
			for (auto&& file : d->files_)
				if (func(file))
					res.emplace_back(concat_tostr(path, file));
			// Directories (recursively)
			for (auto&& x : d->dirs_) {
				path += x->name_;
				path += '/';
				find(x.get()); // Recursion
				path.erase(path.end() - x->name_.size() - 1, path.end());
			}
		}

	} foo(std::forward<UnaryPredicate>(func), std::move(path_prefix));

	foo.find(dir);
	return foo.res;
}

} // namespace directory_tree
