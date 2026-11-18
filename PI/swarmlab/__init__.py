"""Shared code for everything that talks to the boards or reads a capture.

    ports      which serial ports exist, and what a STATUS reply says
    board      BoardLink: one board's serial port
    event      Event: one line of a capture, as written and as read back
    recorder   SwarmLogger: every board's output into one CSV, on one clock
    capture    Capture: a recorded run, loaded for analysis
    scenario   Scenario: drive the boards, record, then grade the capture

The scripts in PI/ and PI/scenarios/ are thin: each is a command line around
one of these. Import the submodule you need. Nothing is imported here, so
grading a capture does not require pyserial.
"""
