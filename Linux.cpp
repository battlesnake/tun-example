#include "Linux.hpp"

namespace Linux {

const SignalSet SignalSet::empty{false};
const SignalSet SignalSet::full{true};

CurrentThread current_thread;

}
