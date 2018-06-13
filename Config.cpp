#define KEEP_X_CONFIG
#include "Config.hpp"

namespace IpLink {

using string = std::string;

static bool strtobool(const string& s) {
	if (s == "true" || s == "on" || s == "yes") {
		return true;
	} else if (s == "false" || s == "off" || s == "no") {
		return false;
	} else {
		throw Config::parse_error("Invalid boolean: " + s);
	}
}

static string booltostr(bool b) {
	return b ? "yes" : "no";
}

static int strtonatural(const string& s)
{
	char *ep;
	int ret = strtoul(s.c_str(), &ep, 0);
	if (s.empty() || *ep) {
		throw Config::parse_error("Invalid natural number: <" + s + ">");
	}
	return ret;
}

void Config::set(const string& key, const string& value)
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
		string key = argv[i++];
		if (key.substr(0, 2) != "--") {
			throw parse_error("Invalid argument: " + key);
		}
		key = key.substr(2);
		if (key == "help") {
			help(os);
		} else if (key == "dump") {
			dump(os, false);
			shown_help = true;
		} else {
			auto eq = key.find('=');
			string value;
			if (eq == string::npos) {
				if (i == argc) {
					throw parse_error("Missing parameter for argument: " + key);
				}
				value = argv[i++];
			} else {
				value = key.substr(eq + 1);
				key = key.substr(0, eq);
			}
			if (key == "config") {
				std::ifstream is(value);
				string str();
				parse_config({ std::istreambuf_iterator<char>(is), std::istreambuf_iterator<char>() });
			} else {
				set(key, value);
			}
		}
	}
}

void Config::parse_config(const string& config)
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

void Config::dump_var(std::ostream& os, const char *name, const char *type, const char *def, const char *help, const string& value, bool with_help)
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
	os << "# Arguments can be specified in config file as <arg = value> or on command line as <--arg value> or <--arg=value>" << std::endl;
	os << "# Special arguments" << std::endl;
	os << "#   --help        : to display help and annotated config" << std::endl;
	os << "#   --dump        : to dump config to standard output" << std::endl;
	os << "#   --config path : to load configuration from a config file" << std::endl;
	os << std::endl;
	dump(os, true);
	shown_help = true;
}

void Config::validate() const
{
	if (mtu < 64) {
		throw std::runtime_error("MTU is too small");
	}
	if (keepalive_interval > 0 && keepalive_limit <= 1) {
		throw std::runtime_error("Invalid arguments: To enable keep-alives, the limit must be greater than one");
	}
	if (updown && keepalive_interval <= 0) {
		throw std::runtime_error("Invalid arguments: \"updown\" requires keepalives to be enabled");
	}
}

}
