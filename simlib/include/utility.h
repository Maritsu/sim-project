#pragma once

#include <algorithm>
#include <string>
#include <vector>

template<class T>
class Appender {
public:
	T& ref;

	explicit Appender(T& reference) : ref(reference) {}
	~Appender() {}

	template<class A>
	Appender& operator()(const A& x) {
		ref.insert(ref.end(), x);
		return *this;
	}

	template<class A>
	Appender& operator<<(const A& x) {
		ref.insert(ref.end(), x);
		return *this;
	}
};

template<>
class Appender<std::string> {
public:
	std::string& ref;

	explicit Appender(std::string& reference) : ref(reference) {}
	~Appender() {}

	template<class A>
	Appender& operator()(const A& x) {
		ref += x;
		return *this;
	}

	template<class A>
	Appender& operator<<(const A& x) {
		ref += x;
		return *this;
	}
};

template<class T>
Appender<T> append(T& reference) { return Appender<T>(reference); }

#if __cplusplus > 201103L
# warning "Delete the class below (there is that feature in c++14)"
#endif
class less {
	template<class A, class B>
	bool operator()(const A& a, const B& b) { return a < b; }
};

template<class T, class C>
typename T::const_iterator binaryFind(const T& x, const C& val) noexcept {
	typename T::const_iterator beg = x.begin(), end = x.end(), mid;
	while (beg != end) {
		mid = beg + ((end - beg) >> 1);
		if (*mid < val)
			beg = ++mid;
		else
			end = mid;
	}
	return (beg != x.end() && *beg == val ? beg : x.end());
}

template<class T, class C, class Comp>
typename T::const_iterator binaryFind(const T& x, const C& val, Comp comp)
		noexcept {
	typename T::const_iterator beg = x.begin(), end = x.end(), mid;
	while (beg != end) {
		mid = beg + ((end - beg) >> 1);
		if (comp(*mid, val))
			beg = ++mid;
		else
			end = mid;
	}
	return (beg != x.end() && *beg == val ? beg : x.end());
}

template<class T, typename B, class C>
typename T::const_iterator binaryFindBy(const T& x, B T::value_type::*field,
		const C& val) noexcept {
	typename T::const_iterator beg = x.begin(), end = x.end(), mid;
	while (beg != end) {
		mid = beg + ((end - beg) >> 1);
		if ((*mid).*field < val)
			beg = ++mid;
		else
			end = mid;
	}
	return (beg != x.end() && (*beg).*field == val ? beg : x.end());
}

template<class T, typename B, class C, class Comp>
typename T::const_iterator binaryFindBy(const T& x, B T::value_type::*field,
		const C& val, Comp comp) noexcept {
	typename T::const_iterator beg = x.begin(), end = x.end(), mid;
	while (beg != end) {
		mid = beg + ((end - beg) >> 1);
		if (comp((*mid).*field, val))
			beg = ++mid;
		else
			end = mid;
	}
	return (beg != x.end() && (*beg).*field == val ? beg : x.end());
}

template<class A, class B>
inline bool binary_search(const A& a, const B& val) {
	return std::binary_search(a.begin(), a.end(), val);
}

template<class A, class B>
inline auto lower_bound(const A& a, const B& val) -> decltype(a.begin()) {
	return std::lower_bound(a.begin(), a.end(), val);
}

template<class A, class B>
inline auto upper_bound(const A& a, const B& val) -> decltype(a.begin()) {
	return std::upper_bound(a.begin(), a.end(), val);
}

template<class Func>
class CallInDtor {
	Func func;

public:
	explicit CallInDtor(Func f) : func(f) {}

	CallInDtor(const CallInDtor&) = delete;
	CallInDtor(CallInDtor&&) = delete;
	CallInDtor& operator=(const CallInDtor&) = delete;
	CallInDtor& operator=(CallInDtor&&) = delete;

	~CallInDtor() { func(); }
};
