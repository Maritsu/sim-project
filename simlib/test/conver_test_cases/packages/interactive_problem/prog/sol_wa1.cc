// Krzysztof Małysa
#include <iostream>

using namespace std;

int main() {
	ios::sync_with_stdio(false);
	cin.tie(nullptr);

	int n;
	long long k;
	cin >> n >> k;
	k = (int)k;
	int beg = 0, end = n - 1;
	while (beg < end) {
		int mid = (beg + end) >> 1;
		cout << "? " << mid + 1 << '\n' << flush;
		long long x;
		cin >> x;
		if (x < k)
			beg = mid + 1;
		else
			end = mid;
	}

	cout << "! " << beg + 1 << '\n' << flush;
	return 0;
}
