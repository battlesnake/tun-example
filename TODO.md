TODO
====

Wrap POSIX stuff in C++ classes.

Have a FileDescriptor base class, so we can have an EventIO class which
references a FileDescriptor and some callbacks.  A collection of EventIO
objects can then be passed to a generic wrapper for `poll`.
