Simple program to transmit CW across the internet.

Very simple UDP protocol

First byte is an uint8_t which increases for every transmitted packed

Second byte is one of the following:
0: Transition to off (unkeyed)
1: Transition to on (keyed)
2: Notification that the key is currently off
3: Notification that the key is currently on
4: Request notification from the other end.

Third and fourth byte are a uint16_t in network byte order that measures
the milliseconds after the last transition that this transition occurs.
For types 2, 3, and 4, this value is zero.

The key is expected to be connected between the DSR and DTR lines of a serial
port.  For DE-9 connectors, this is pins 6 and 4.

