#include <cmath>
#include <sstream>
#include <iomanip>

#include "format_si.hpp"

using namespace std;

const string prefixes = "yzafpnum kMGTPEZY";

string format_si(float value, const string& base_unit, int digits)
{
	int l1000 = value == 0 ? 0 : floor(log10(abs(value)) / 3);
	int iprefix = max(min(l1000 + 8, (int) prefixes.length() - 1), 0);
	value *= pow(1000, -(iprefix - 8));
	char prefix = prefixes[iprefix];
	ostringstream oss;
	if (abs(value) < 1 && value != 0) {
		oss.setf(ios::scientific, ios::floatfield);
		oss.precision(digits);
	} else {
		oss.setf(ios::fixed, ios::floatfield);
		int places = digits - (value == 0 ? 0 : floor(log10(abs(value)))) - 1;
		oss.precision(places);
	}
	oss << value;
	if (prefix != ' ') {
		oss << prefix;
	}
	oss << base_unit;
	return oss.str();
}

__attribute__((__unused__))
	static void test(ostream& os)
{
#define test(value, base, digits) \
	os << "format_si(" << value << ", " << base << ", " << digits << ") ==> " << format_si(value, base, digits) << std::endl;

	os.precision(6);
	os.setf(std::ios::fixed, std::ios::floatfield);

	test(0, "Ω", 6);
	test(0, "Ω", 3);
	test(0, "Ω", 1);

	test(1, "Ω", 6);
	test(1, "Ω", 3);
	test(1, "Ω", 1);

	test(-1, "Ω", 6);
	test(-1, "Ω", 3);
	test(-1, "Ω", 1);

	test(0.00001234, "Ω", 3);
	test(0.0001234, "Ω", 3);
	test(0.001234, "Ω", 3);
	test(0.01234, "Ω", 3);
	test(0.1234, "Ω", 3);
	test(1.234, "Ω", 3);
	test(12.34, "Ω", 3);
	test(123.4, "Ω", 3);
	test(1234, "Ω", 3);
	test(12340, "Ω", 3);
	test(123400, "Ω", 3);
	test(1234000, "Ω", 3);
	test(12340000, "Ω", 3);
	test(123400000, "Ω", 3);
}
