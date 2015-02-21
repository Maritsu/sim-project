#pragma once

#include <string>

#ifdef DEBUG
#include <ostream>
#endif

namespace submissions_queue
{
	enum submission_status{OK, ERROR, C_ERROR, WAITING};

	inline std::string to_str(submission_status st)
	{
		switch(st)
		{
			case OK: return "ok";
			case ERROR: return "error";
			case C_ERROR: return "c_error";
			case WAITING: return "waiting";
		}
		return "error";
	}

	class submission
	{
	private:
		std::string _id, _problem_id;

	public:
		submission(const std::string& nid, const std::string& ntid): _id(nid), _problem_id(ntid)
		{}

		const std::string& id() const
		{return _id;}

		const std::string& problem_id() const
		{return _problem_id;}

		void set(submission_status st, long long points) const;
	};

	bool empty();
	void pop();
	submission extract();
	const submission& front();
}

#ifdef DEBUG
std::ostream& operator<<(std::ostream& os, const submissions_queue::submission& r);
#endif
