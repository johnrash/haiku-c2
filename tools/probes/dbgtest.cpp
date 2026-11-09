/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * dbgtest <team> <c2_target address> <gCounter address>: tests the kernel's
 * userland debugging support on a running dbgtarget through the debug nub:
 * a hardware breakpoint, single steps, eight breakpoints (more than the six
 * hardware ones: software breakpoints too), a watchpoint, a changed
 * register, and a vector register written and read back. Detaches at the
 * end; dbgtarget must go on running.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <OS.h>
#include <debugger.h>


static port_id sDebuggerPort;
static port_id sNubPort;
static port_id sReplyPort;
static int sFailures = 0;


#define CHECK(condition, ...)							\
	do {												\
		if (!(condition)) {								\
			printf("FAIL: " __VA_ARGS__);				\
			printf("\n");								\
			sFailures++;								\
		}												\
	} while (0)


static status_t
nub_call(int32 code, const void* message, size_t size)
{
	if (write_port(sNubPort, code, message, size) != B_OK)
		return B_ERROR;
	status_t error;
	int32 replyCode;
	if (read_port_etc(sReplyPort, &replyCode, &error, sizeof(error),
			B_RELATIVE_TIMEOUT, 2000000) < 0) {
		return B_TIMED_OUT;
	}
	return error;
}


static status_t
set_breakpoint(addr_t address)
{
	debug_nub_set_breakpoint message;
	message.reply_port = sReplyPort;
	message.address = (void*)address;
	return nub_call(B_DEBUG_MESSAGE_SET_BREAKPOINT, &message, sizeof(message));
}


static void
clear_breakpoint(addr_t address)
{
	debug_nub_clear_breakpoint message;
	message.address = (void*)address;
	write_port(sNubPort, B_DEBUG_MESSAGE_CLEAR_BREAKPOINT, &message,
		sizeof(message));
}


static void
continue_thread(thread_id thread, bool singleStep)
{
	debug_nub_continue_thread message;
	message.thread = thread;
	message.handle_event = B_THREAD_DEBUG_HANDLE_EVENT;
	message.single_step = singleStep;
	write_port(sNubPort, B_DEBUG_MESSAGE_CONTINUE_THREAD, &message,
		sizeof(message));
}


/*!	Waits for a breakpoint, watchpoint or single step event; other events are
	continued. Returns the event code, or -1 on a timeout.
*/
static int32
wait_event(debug_debugger_message_data& data, debug_cpu_state*& state)
{
	while (true) {
		int32 code;
		if (read_port_etc(sDebuggerPort, &code, &data, sizeof(data),
				B_RELATIVE_TIMEOUT, 3000000) < 0) {
			return -1;
		}
		switch (code) {
			case B_DEBUGGER_MESSAGE_BREAKPOINT_HIT:
				state = &data.breakpoint_hit.cpu_state;
				return code;
			case B_DEBUGGER_MESSAGE_WATCHPOINT_HIT:
				state = &data.watchpoint_hit.cpu_state;
				return code;
			case B_DEBUGGER_MESSAGE_SINGLE_STEP:
				state = &data.single_step.cpu_state;
				return code;
			default:
				printf("  (event %" B_PRId32 ", continuing)\n", code);
				continue_thread(data.origin.thread, false);
				break;
		}
	}
}


static const char*
event_name(int32 code)
{
	switch (code) {
		case B_DEBUGGER_MESSAGE_BREAKPOINT_HIT: return "breakpoint";
		case B_DEBUGGER_MESSAGE_WATCHPOINT_HIT: return "watchpoint";
		case B_DEBUGGER_MESSAGE_SINGLE_STEP: return "single step";
		case -1: return "timeout";
	}
	return "?";
}


int
main(int argc, char** argv)
{
	if (argc != 4) {
		fprintf(stderr, "usage: dbgtest <team> <c2_target> <gCounter>\n");
		return 2;
	}
	team_id team = strtol(argv[1], NULL, 0);
	addr_t function = strtoul(argv[2], NULL, 0);
	addr_t counter = strtoul(argv[3], NULL, 0);

	sDebuggerPort = create_port(10, "dbgtest debugger");
	sReplyPort = create_port(10, "dbgtest reply");
	sNubPort = install_team_debugger(team, sDebuggerPort);
	if (sNubPort < 0) {
		printf("install_team_debugger: %s\n", strerror(sNubPort));
		return 1;
	}

	debug_debugger_message_data data;
	debug_cpu_state* state;
	int32 code;
	thread_id thread;

	// 1. a (hardware) breakpoint and single steps
	printf("1. breakpoint at %#lx, two single steps\n", function);
	CHECK(set_breakpoint(function) == B_OK, "set breakpoint");
	code = wait_event(data, state);
	CHECK(code == B_DEBUGGER_MESSAGE_BREAKPOINT_HIT
		&& state->elr == function, "%s at %#lx", event_name(code),
		code >= 0 ? (addr_t)state->elr : 0);
	if (code < 0)
		return 1;
	thread = data.origin.thread;
	long argument = state->x[0];
	printf("  hit: thread %" B_PRId32 ", x0 %ld, sp %#lx, lr %#lx\n", thread,
		argument, (addr_t)state->sp, (addr_t)state->lr);
	for (int i = 1; i <= 2; i++) {
		continue_thread(thread, true);
		code = wait_event(data, state);
		CHECK(code == B_DEBUGGER_MESSAGE_SINGLE_STEP
			&& state->elr == function + 4 * i, "step %d: %s at %#lx", i,
			event_name(code), code >= 0 ? (addr_t)state->elr : 0);
		if (code >= 0)
			printf("  step %d: %s at %#lx\n", i, event_name(code),
				(addr_t)state->elr);
	}
	continue_thread(thread, false);
	code = wait_event(data, state);
	CHECK(code == B_DEBUGGER_MESSAGE_BREAKPOINT_HIT && state->elr == function
		&& (long)state->x[0] == argument + 1, "next call: %s, x0 %ld",
		event_name(code), code >= 0 ? (long)state->x[0] : 0);
	printf("  next call: %s, x0 %ld\n", event_name(code),
		code >= 0 ? (long)state->x[0] : 0);
	clear_breakpoint(function);

	// 2. eight breakpoints: six hardware, two software ones
	printf("2. eight breakpoints at %#lx..%#lx\n", function, function + 28);
	for (int i = 1; i < 8; i++)
		CHECK(set_breakpoint(function + 4 * i) == B_OK, "set breakpoint %d", i);
	for (int i = 1; i < 8; i++) {
		continue_thread(thread, false);
		code = wait_event(data, state);
		CHECK(code == B_DEBUGGER_MESSAGE_BREAKPOINT_HIT
			&& state->elr == function + 4 * i, "hit %d: %s at %#lx", i,
			event_name(code), code >= 0 ? (addr_t)state->elr : 0);
	}
	for (int i = 1; i < 8; i++)
		clear_breakpoint(function + 4 * i);
	printf("  hits in order: %s\n", sFailures == 0 ? "yes" : "no");

	// 3. a write watchpoint on gCounter
	printf("3. watchpoint on %#lx\n", counter);
	debug_nub_set_watchpoint watch;
	watch.reply_port = sReplyPort;
	watch.address = (void*)counter;
	watch.type = B_DATA_WRITE_WATCHPOINT;
	watch.length = 8;
	CHECK(nub_call(B_DEBUG_MESSAGE_SET_WATCHPOINT, &watch, sizeof(watch))
		== B_OK, "set watchpoint");
	addr_t watchIP = 0;
	bigtime_t last = 0;
	for (int i = 0; i < 3; i++) {
		continue_thread(thread, false);
		code = wait_event(data, state);
		bigtime_t now = system_time();
		CHECK(code == B_DEBUGGER_MESSAGE_WATCHPOINT_HIT
			&& state->elr > function && state->elr < function + 256
			&& (i == 0 || state->elr == watchIP), "watch %d: %s at %#lx", i,
			event_name(code), code >= 0 ? (addr_t)state->elr : 0);
		if (code >= 0)
			watchIP = state->elr;
		if (i > 0) {
			// once per call, not again right after continuing
			CHECK(now - last > 100000, "watch %d after %lld us", i,
				(long long)(now - last));
		}
		printf("  %s at %#lx after %lld us\n", event_name(code), watchIP,
			(long long)(i > 0 ? now - last : 0));
		last = now;
	}
	debug_nub_clear_watchpoint unwatch;
	unwatch.address = (void*)counter;
	write_port(sNubPort, B_DEBUG_MESSAGE_CLEAR_WATCHPOINT, &unwatch,
		sizeof(unwatch));

	// 4. change x0 at the function's start: c2_target(1000) = 6001
	printf("4. x0 = 1000 at the next call\n");
	CHECK(set_breakpoint(function) == B_OK, "set breakpoint");
	continue_thread(thread, false);
	code = wait_event(data, state);
	CHECK(code == B_DEBUGGER_MESSAGE_BREAKPOINT_HIT && state->elr == function,
		"%s", event_name(code));
	if (code >= 0) {
		debug_nub_set_cpu_state set;
		set.thread = thread;
		set.cpu_state = *state;
		set.cpu_state.x[0] = 1000;
		write_port(sNubPort, B_DEBUG_MESSAGE_SET_CPU_STATE, &set, sizeof(set));
	}
	clear_breakpoint(function);

	// 5. a vector register (v31, which the target never uses) set and read
	// back across a single step
	printf("5. v31 written, single step, read back\n");
	continue_thread(thread, true);
	code = wait_event(data, state);
	CHECK(code == B_DEBUGGER_MESSAGE_SINGLE_STEP, "%s", event_name(code));
	if (code >= 0) {
		debug_nub_set_cpu_state set;
		set.thread = thread;
		set.cpu_state = *state;
		set.cpu_state.v[31].value[0] = 0x1122334455667788UL;
		set.cpu_state.v[31].value[1] = 0x99aabbccddeeff00UL;
		write_port(sNubPort, B_DEBUG_MESSAGE_SET_CPU_STATE, &set, sizeof(set));
		continue_thread(thread, true);
		code = wait_event(data, state);
		CHECK(code == B_DEBUGGER_MESSAGE_SINGLE_STEP
			&& state->v[31].value[0] == 0x1122334455667788UL
			&& state->v[31].value[1] == 0x99aabbccddeeff00UL,
			"v31 %#lx %#lx", code >= 0 ? state->v[31].value[0] : 0,
			code >= 0 ? state->v[31].value[1] : 0);
		printf("  v31 %#lx %#lx, fpcr %#x\n", state->v[31].value[0],
			state->v[31].value[1], state->fpcr);
	}
	continue_thread(thread, false);

	snooze(100000);
	remove_team_debugger(team);
	printf("%s (%d failures); dbgtarget should print \"-> 6001\" once and "
		"then go on\n", sFailures == 0 ? "PASS" : "FAIL", sFailures);
	return sFailures == 0 ? 0 : 1;
}
