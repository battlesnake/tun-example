#define KEEP_X_CONFIG
#include "Config.hpp"

namespace IpLink {

static bool strtobool(const std::string& s) {
	if (s == "true" || s == "on" || s == "yes") {
		return true;
	} else if (s == "false" || s == "off" || s == "no") {
		return false;
	} else {
		throw Config::parse_error("Invalid boolean: " + s);
	}
}

static std::string booltostr(bool b) {
	return b ? "yes" : "no";
}

static int strtonatural(const std::string& s)
{
	char *ep;
	int ret = strtoul(s.c_str(), &ep, 0);
	if (s.empty() || *ep) {
		throw Config::parse_error("Invalid natural number: <" + s + ">");
	}
	return ret;
}

void Config::set(const std::string& key, const std::string& value)
{
	if (0) {
	}
#define X(name, type, def, parse, format, help) else if (key == #name) { name = parse(value); }
	X_CONFIG
#undef X
	else {
		throw parse_error("Invalid parameter: " + key);
	}

}

void Config::parse_args(int argc, char *argv[], std::ostream& os)
{
	for (int i = 1; i < argc; ) {
		std::string key = argv[i++];
		if (key.substr(0, 2) != "--") {
			throw parse_error("Invalid argument: " + key);
		}
		key = key.substr(2);
		if (key == "help") {
			help(os);
		} else {
			if (i == argc) {
				throw parse_error("Missing parameter for argument: " + key);
			}
			std::string value = argv[i++];
			if (key == "config") {
				std::ifstream is(value);
				std::string str();
				parse_config({ std::istreambuf_iterator<char>(is), std::istreambuf_iterator<char>() });
			} else {
				set(key, value);
			}
		}
	}
}

void Config::parse_config(const std::string& config)
{
	using namespace std;
	const auto end = config.cend();
	auto it = config.cbegin();
	auto lf = it;
	int lineno = 1;
	regex assignment(R"(^\s*(?:(\w+)\s*=\s*([^\s#]*)\s*)?(?:#.*)?$)", regex_constants::ECMAScript);
	smatch match;
	for (; it < end; it = lf + 1, lineno++) {
		lf = find(it, end, '\n');
		try {
			if (!regex_match(it, lf, match, assignment)) {
				throw parse_error("Failed to parse configuration");
			}
			if (match.size() == 3 && match[1].length() > 0) {
				set(match[1].str(), match[2].str());
			}
		} catch (parse_error& e) {
			throw parse_error(e.what() + " at line #"s + to_string(lineno));
		}
	}
}

void Config::dump_var(std::ostream& os, const char *name, const char *type, const char *def, const char *help, const std::string& value, bool with_help)
{
	if (with_help) {
		os << "# " << name << " [" << type << "]: " << help << " (default: " << def << ")" << std::endl;
	}
	os << name << " = " << value << std::endl;
	if (with_help) {
		os << std::endl;
	}
}

void Config::dump(std::ostream& os, bool with_help) const
{
	using namespace std;
#define X(name, type, def, parse, format, help) dump_var(os, #name, #type, #def, help, format(name), with_help);
	X_CONFIG
#undef X
}

void Config::help(std::ostream& os)
{
	os << "# Arguments can be specified in config file as <arg = value> or on command line as <--arg value>" << std::endl;
	os << "# Special arguments" << std::endl;
	os << "#   --help        : to display help / dump config" << std::endl;
	os << "#   --config path : to load configuration from a config file" << std::endl;
	os << std::endl;
	dump(os, true);
	shown_help = true;
}

}
