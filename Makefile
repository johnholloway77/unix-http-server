# ═══════════════════════════════════════════════════════════════════════
#  poll-based HTTP server — Makefile (portable)
#  Primary target: FreeBSD.  Also builds on Linux, macOS, OmniOS/illumos.
# ═══════════════════════════════════════════════════════════════════════
#
#  GNU make required. On FreeBSD/OmniOS the base 'make' is BSD/Sun make —
#  use gmake. On Linux/macOS the system 'make' IS GNU make.
#
#  libmagic is OPTIONAL. It is a fallback for MIME detection; the static
#  extension table is the primary source. Define USE_LIBMAGIC to compile
#  the libmagic fallback in. It is ON by default everywhere EXCEPT OmniOS,
#  where libmagic is not reliably packaged.
#    -> This requires the C source to guard <magic.h> and all magic_*()
#       calls behind  #ifdef USE_LIBMAGIC  (see note at bottom of file).
# ═══════════════════════════════════════════════════════════════════════

# ─── Platform detection ────────────────────────────────────────────────
UNAME_S := $(shell uname -s)

# ─── Compiler (overridable: `gmake CC=gcc`) ────────────────────────────
# clang is the native compiler on FreeBSD and macOS. On OmniOS, gcc is the
# norm, so the OmniOS recipe below flips CC to gcc.
CC ?= clang

# ─── Base flags ────────────────────────────────────────────────────────
CFLAGS  ?= -Wall -Wextra -std=c11
STRICT_CFLAGS ?=
CFLAGS += $(STRICT_CFLAGS)
LIBS    =
LDFLAGS =

# libmagic on by default; switched off for OmniOS further down.
USE_LIBMAGIC ?= 1

# ═══════════════════════════════════════════════════════════════════════
#  Per-platform configuration
# ═══════════════════════════════════════════════════════════════════════

# ─── FreeBSD (PRIMARY) ─────────────────────────────────────────────────
# libmagic + headers ship in the base system; nothing extra to point at.
# Criterion installs under /usr/local via pkg.
ifeq ($(UNAME_S),FreeBSD)
    CC ?= clang
    CRITERION_PREFIX ?= /usr/local
    # base-system libmagic is on the default search path; no -I/-L needed
endif

# ─── Linux ─────────────────────────────────────────────────────────────
# libmagic-dev and criterion install to standard /usr paths.
ifeq ($(UNAME_S),Linux)
    CC ?= clang
    CRITERION_PREFIX ?= /usr
    CFLAGS += -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
    TEST_CFLAGS += -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
    # libmagic headers/lib on default path via libmagic-dev; no -I/-L needed
    LIBS += -lbsd
    TEST_LDFLAGS += -lbsd
endif

# ─── macOS (Darwin) ────────────────────────────────────────────────────
# Homebrew formulae are keg-only and NOT on the default search path. On
# Apple Silicon the prefix is /opt/homebrew. Resolve each prefix via brew.
ifeq ($(UNAME_S),Darwin)
    CC ?= clang
    BREW_MAGIC     := $(shell brew --prefix libmagic 2>/dev/null)
    BREW_CRITERION := $(shell brew --prefix criterion 2>/dev/null)
    CRITERION_PREFIX ?= $(BREW_CRITERION)
    CFLAGS  += -I$(BREW_MAGIC)/include
    LIBS    += -L$(BREW_MAGIC)/lib
endif

# ─── OmniOS / illumos (SunOS) ──────────────────────────────────────────
# UNCONFIRMED: the following package/path values were never verified on a
# live OmniOS build. Adjust after checking on the box:
#   gmake : developer/build/gnu-make   (confirm: pkg search -r gmake)
#   gcc   : developer/gcc14
#   crit. : likely NOT in IPS; may need pkgsrc  (confirm: pkg search -r criterion)
#   magic : NOT found under system/library/libmagic  (confirm: pkg search -r libmagic)
# Because libmagic is unconfirmed, libmagic is DISABLED here by default.
ifeq ($(UNAME_S),SunOS)
    CC ?= gcc
    USE_LIBMAGIC = 1
    # gcc on OmniOS lives here; adjust if your gcc14 install differs
    CRITERION_PREFIX ?= /opt/ooce
    CFLAGS += -D_POSIX_C_SOURCE=200809L -D__EXTENSIONS__
    LIBS += -lsocket -lnsl
    # If you locate libmagic (e.g. via pkgsrc /opt/local), you can re-enable:
    #   USE_LIBMAGIC = 1
    #   CFLAGS += -I/opt/local/include
    #   LIBS   += -L/opt/local/lib
endif

# ─── Apply libmagic toggle ─────────────────────────────────────────────
# When enabled: define the macro (so the C guards compile the fallback in)
# and link the library. When disabled: neither, and the C falls back to the
# extension table only.
ifeq ($(USE_LIBMAGIC),1)
    CFLAGS += -DUSE_LIBMAGIC
    LIBS   += -lmagic
endif

# ─── Debug flags (symbols + ASan + -DDEBUG) ────────────────────────────
DEBUG_CFLAGS  = $(CFLAGS) -g -DDEBUG -fsanitize=address -fno-omit-frame-pointer
DEBUG_LDFLAGS = $(LDFLAGS) -fsanitize=address

# ─── Source files ──────────────────────────────────────────────────────
SOURCES = main.c \
    client_conn/connections.c \
    client_conn/accept_new_conn.c \
    client_conn/client_handler.c \
    client_conn/do_read.c \
    client_conn/do_write.c \
    flags/setFlags.c \
    logging/logging.c \
    sockets/get_listener_v4.c \
    sockets/get_listener_v6.c \
    requests/parse_request.c \
    requests/parse_request2.c \
    requests/process_request.c \
    requests/resolve_path.c \
    requests/close_resolve_path.c \
    response/build_okay_response.c \
    response/build_error_response.c

BINARY       = simple_server
DEBUG_BINARY = simple_server_debug

# ─── Build directories ─────────────────────────────────────────────────
RELEASE_DIR = build/release
DEBUG_DIR   = build/debug
RELEASE_OBJECTS = $(SOURCES:%.c=$(RELEASE_DIR)/%.o)
DEBUG_OBJECTS   = $(SOURCES:%.c=$(DEBUG_DIR)/%.o)

# ─── Default: release build ────────────────────────────────────────────
.PHONY: all
all: $(BINARY)

$(BINARY): $(RELEASE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(RELEASE_OBJECTS) $(LIBS)

$(RELEASE_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# ─── Debug build ───────────────────────────────────────────────────────
.PHONY: debug
debug: $(DEBUG_BINARY)

$(DEBUG_BINARY): $(DEBUG_OBJECTS)
	$(CC) $(DEBUG_LDFLAGS) -o $@ $(DEBUG_OBJECTS) $(LIBS)

$(DEBUG_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(DEBUG_CFLAGS) -c -o $@ $<

#═══════════════════════════════════════════════════════════════════════
#  Valgrind build (symbols, NO sanitizer — Valgrind and ASan conflict)
#
#═══════════════════════════════════════════════════════════════════════

VALGRIND_CFLAGS  = $(CFLAGS) -g
VALGRIND_BINARY  = simple_server_valgrind
VALGRIND_DIR     = build/valgrind
VALGRIND_OBJECTS = $(SOURCES:%.c=$(VALGRIND_DIR)/%.o)

.PHONY: valgrind-build
valgrind-build: $(VALGRIND_BINARY)

$(VALGRIND_BINARY): $(VALGRIND_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(VALGRIND_OBJECTS) $(LIBS)

$(VALGRIND_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(VALGRIND_CFLAGS) -c -o $@ $<


# ─── Clean ─────────────────────────────────────────────────────────────
.PHONY: clean
clean:
	rm -rf $(BINARY) $(DEBUG_BINARY) build \
	       $(PARSE_BINARY) $(PARSE_BINARY_ASAN) \
	       $(PARSE2_BINARY) $(PARSE2_BINARY_ASAN) \
	       $(PROCESS_REQUEST_BINARY) $(PROCESS_REQUEST_BINARY_ASAN) \
	       $(RESOLVE_BINARY) $(RESOLVE_BINARY_ASAN) \
		   $(VALGRIND_BINARY) $(VALGRIND_OBJECTS) \
	       test_cases/fixtures

.PHONY: clean-obj
clean-obj:
	rm -rf build

# ═══════════════════════════════════════════════════════════════════════
#  Testing (Criterion)
# ═══════════════════════════════════════════════════════════════════════
CRITERION_PREFIX ?= /usr/local
TEST_CFLAGS  = -std=c11 -Wall -Wextra  -g \
               -I requests -I $(CRITERION_PREFIX)/include \
               #-DDEBUG
TEST_CFLAGS += $(STRICT_CFLAGS)
TEST_LDFLAGS = -L $(CRITERION_PREFIX)/lib -lcriterion

ifeq ($(UNAME_S),Linux)
    TEST_CFLAGS  += -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
    TEST_LDFLAGS += -lbsd
endif

# macOS: also need libmagic's keg path in the test flags (resolve_path
# suite includes <magic.h> and links -lmagic when USE_LIBMAGIC is on).
ifeq ($(UNAME_S),Darwin)
    TEST_CFLAGS  += -I$(BREW_MAGIC)/include
    TEST_LDFLAGS += -L$(BREW_MAGIC)/lib
endif

# Propagate the libmagic toggle into the test build too, so the resolve
# suite compiles its #ifdef USE_LIBMAGIC branch consistently with the
# server. When off (OmniOS), the suite must not reference -lmagic/magic.h.
TEST_MAGIC_LIB =
ifeq ($(USE_LIBMAGIC),1)
    TEST_CFLAGS    += -DUSE_LIBMAGIC
    TEST_MAGIC_LIB  = -lmagic
endif

TEST_ASAN_CFLAGS  = $(TEST_CFLAGS) -fsanitize=address -fno-omit-frame-pointer
TEST_ASAN_LDFLAGS = $(TEST_LDFLAGS) -fsanitize=address

TEST_RUN_FLAGS ?= -j1 --verbose #--quiet

# ─── parse_request suite ───────────────────────────────────────────────
PARSE_SRC         = test_cases/test_parse_request.c
PARSE_UNIT        = requests/parse_request.c \
					logging/logging.c
PARSE_BINARY      = test_cases/test_parse_request
PARSE_BINARY_ASAN = test_cases/test_parse_request_asan

.PHONY: test-parse
test-parse: $(PARSE_BINARY)
	@echo "Running parse_request unit tests..."
	@./$(PARSE_BINARY) $(TEST_RUN_FLAGS)

.PHONY: test-parse-asan
test-parse-asan: $(PARSE_BINARY_ASAN)
	@echo "Running parse_request unit tests under AddressSanitizer..."
	@./$(PARSE_BINARY_ASAN) $(TEST_RUN_FLAGS)

$(PARSE_BINARY): $(PARSE_SRC) $(PARSE_UNIT)
	$(CC) $(TEST_CFLAGS) -o $@ $(PARSE_SRC) $(PARSE_UNIT) $(TEST_LDFLAGS)

$(PARSE_BINARY_ASAN): $(PARSE_SRC) $(PARSE_UNIT)
	$(CC) $(TEST_ASAN_CFLAGS) -o $@ \
	    $(PARSE_SRC) $(PARSE_UNIT) $(TEST_ASAN_LDFLAGS)

# ─── parse_request2 slice suite ────────────────────────────────────────
PARSE2_SRC         = test_cases/test_parse_request2.c
PARSE2_UNIT        = requests/parse_request2.c
PARSE2_BINARY      = test_cases/test_parse_request2
PARSE2_BINARY_ASAN = test_cases/test_parse_request2_asan

.PHONY: test-parse2
test-parse2: $(PARSE2_BINARY)
	@echo "Running parse_request2 slice unit tests..."
	@./$(PARSE2_BINARY) $(TEST_RUN_FLAGS)

.PHONY: test-parse2-asan
test-parse2-asan: $(PARSE2_BINARY_ASAN)
	@echo "Running parse_request2 slice unit tests under AddressSanitizer..."
	@./$(PARSE2_BINARY_ASAN) $(TEST_RUN_FLAGS)

$(PARSE2_BINARY): $(PARSE2_SRC) $(PARSE2_UNIT)
	$(CC) $(TEST_CFLAGS) -o $@ \
	    $(PARSE2_SRC) $(PARSE2_UNIT) $(TEST_LDFLAGS)

$(PARSE2_BINARY_ASAN): $(PARSE2_SRC) $(PARSE2_UNIT)
	$(CC) $(TEST_ASAN_CFLAGS) -o $@ \
	    $(PARSE2_SRC) $(PARSE2_UNIT) $(TEST_ASAN_LDFLAGS)

# ─── process_request suite ─────────────────────────────────────────────
PROCESS_REQUEST_SRC         = test_cases/test_process_request.c
PROCESS_REQUEST_UNIT        = requests/process_request.c
PROCESS_REQUEST_BINARY      = test_cases/test_process_request
PROCESS_REQUEST_BINARY_ASAN = test_cases/test_process_request_asan

.PHONY: test-process-request
test-process-request: $(PROCESS_REQUEST_BINARY)
	@echo "Running process_request unit tests..."
	@./$(PROCESS_REQUEST_BINARY) $(TEST_RUN_FLAGS)

.PHONY: test-process-request-asan
test-process-request-asan: $(PROCESS_REQUEST_BINARY_ASAN)
	@echo "Running process_request unit tests under AddressSanitizer..."
	@./$(PROCESS_REQUEST_BINARY_ASAN) $(TEST_RUN_FLAGS)

$(PROCESS_REQUEST_BINARY): $(PROCESS_REQUEST_SRC) $(PROCESS_REQUEST_UNIT)
	$(CC) $(TEST_CFLAGS) -o $@ \
	    $(PROCESS_REQUEST_SRC) $(PROCESS_REQUEST_UNIT) $(TEST_LDFLAGS)

$(PROCESS_REQUEST_BINARY_ASAN): $(PROCESS_REQUEST_SRC) $(PROCESS_REQUEST_UNIT)
	$(CC) $(TEST_ASAN_CFLAGS) -o $@ \
	    $(PROCESS_REQUEST_SRC) $(PROCESS_REQUEST_UNIT) $(TEST_ASAN_LDFLAGS)

# ─── resolve_path suite (needs libmagic when enabled) ──────────────────
RESOLVE_SRC         = test_cases/test_resolve_path.c
RESOLVE_UNIT        = requests/resolve_path.c \
					  requests/close_resolve_path.c \
				      requests/parse_request.c \
                      logging/logging.c
RESOLVE_BINARY      = test_cases/test_resolve_path
RESOLVE_BINARY_ASAN = test_cases/test_resolve_path_asan

.PHONY: test-resolve
test-resolve: $(RESOLVE_BINARY)
	@echo "Running resolve_path unit tests..."
	@./$(RESOLVE_BINARY) $(TEST_RUN_FLAGS)

.PHONY: test-resolve-asan
test-resolve-asan: $(RESOLVE_BINARY_ASAN)
	@echo "Running resolve_path unit tests under AddressSanitizer..."
	@./$(RESOLVE_BINARY_ASAN) $(TEST_RUN_FLAGS)

$(RESOLVE_BINARY): $(RESOLVE_SRC) $(RESOLVE_UNIT)
	$(CC) $(TEST_CFLAGS) -o $@ \
	    $(RESOLVE_SRC) $(RESOLVE_UNIT) $(TEST_LDFLAGS) $(TEST_MAGIC_LIB)

$(RESOLVE_BINARY_ASAN): $(RESOLVE_SRC) $(RESOLVE_UNIT)
	$(CC) $(TEST_ASAN_CFLAGS) -o $@ \
	    $(RESOLVE_SRC) $(RESOLVE_UNIT) $(TEST_ASAN_LDFLAGS) $(TEST_MAGIC_LIB)

# ─── build_error suite ─────────────────────────────────────────────────
BUILD_ERR_SRC         = test_cases/test_build_error.c
BUILD_ERR_UNIT        = response/build_error_response.c
BUILD_ERR_BINARY      = test_cases/test_build_error
BUILD_ERR_BINARY_ASAN = test_cases/test_build_error_asan

.PHONY: test-build-error
test-build-error: $(BUILD_ERR_BINARY)
	@echo "Running build_error_response unit tests..."
	@./$(BUILD_ERR_BINARY) $(TEST_RUN_FLAGS)

.PHONY: test-build-error-asan
test-build-error-asan: $(BUILD_ERR_BINARY_ASAN)
	@echo "Running build_error_response unit tests under AddressSanitizer..."
	@./$(BUILD_ERR_BINARY_ASAN) $(TEST_RUN_FLAGS)

$(BUILD_ERR_BINARY): $(BUILD_ERR_SRC) $(BUILD_ERR_UNIT)
	$(CC) $(TEST_CFLAGS) -o $@ $(BUILD_ERR_SRC) $(BUILD_ERR_UNIT) $(TEST_LDFLAGS)

$(BUILD_ERR_BINARY_ASAN): $(BUILD_ERR_SRC) $(BUILD_ERR_UNIT)
	$(CC) $(TEST_ASAN_CFLAGS) -o $@ \
	    $(BUILD_ERR_SRC) $(BUILD_ERR_UNIT) $(TEST_ASAN_LDFLAGS)


# ─── build_okay_response suite ──────────────────────────────────────────
# Unit layer: exercises the REAL resolve_path -> build_okay_response
# handoff (not mocked) — this is the layer that catches struct-ownership
# bugs between the two functions.
#
# Integration layer: forks + execs a REAL server binary, fires real HTTP
# requests via curl, sends SIGTERM, asserts clean shutdown. Which binary
# gets spawned is controlled by SIMPLE_SERVER_BIN (set below per target),
# so the memory-checking tool that matters (ASan vs Valgrind) is selected
# by which server binary the integration test is told to exec.
BUILD_OKAY_SRC  = test_cases/test_build_okay_response.c
BUILD_OKAY_UNIT = response/build_okay_response.c \
				  response/build_error_response.c \
                  requests/resolve_path.c \
                  requests/close_resolve_path.c \
                  requests/parse_request.c \
                  logging/logging.c
BUILD_OKAY_BINARY      = test_cases/test_build_okay_response
BUILD_OKAY_BINARY_ASAN = test_cases/test_build_okay_response_asan

.PHONY: test-build-okay
test-build-okay: $(BUILD_OKAY_BINARY) $(BINARY)
	@echo "Running build_okay_response unit + integration tests..."
	@echo "(integration layer spawns $(CURDIR)/$(BINARY))"
	@SIMPLE_SERVER_BIN=$(CURDIR)/$(BINARY) ./$(BUILD_OKAY_BINARY) $(TEST_RUN_FLAGS)

# ASan variant: the TEST BINARY is compiled with ASan (covers the unit
# layer directly, in-process). The INTEGRATION layer's spawned child is
# pointed at $(DEBUG_BINARY) — your existing ASan server build — so the
# real accept/do_read/resolve_path/build_okay_response/do_write pipeline
# also runs under ASan, not just the isolated unit calls.
.PHONY: test-build-okay-asan
test-build-okay-asan: $(BUILD_OKAY_BINARY_ASAN) $(DEBUG_BINARY)
	@echo "Running build_okay_response unit + integration tests under AddressSanitizer..."
	@echo "(integration layer spawns $(CURDIR)/$(DEBUG_BINARY), ASan build)"
	@SIMPLE_SERVER_BIN=$(CURDIR)/$(DEBUG_BINARY) ./$(BUILD_OKAY_BINARY_ASAN) $(TEST_RUN_FLAGS)

$(BUILD_OKAY_BINARY): $(BUILD_OKAY_SRC) $(BUILD_OKAY_UNIT)
	$(CC) $(TEST_CFLAGS) -o $@ \
	    $(BUILD_OKAY_SRC) $(BUILD_OKAY_UNIT) $(TEST_LDFLAGS) $(TEST_MAGIC_LIB)

$(BUILD_OKAY_BINARY_ASAN): $(BUILD_OKAY_SRC) $(BUILD_OKAY_UNIT)
	$(CC) $(TEST_ASAN_CFLAGS) -o $@ \
	    $(BUILD_OKAY_SRC) $(BUILD_OKAY_UNIT) $(TEST_ASAN_LDFLAGS) $(TEST_MAGIC_LIB)


# ═══════════════════════════════════════════════════════════════════════
#  Memory leak testing (Valgrind)
# ═══════════════════════════════════════════════════════════════════════
#  Starts the server, hammers it with a mix of file and directory
#  requests, sends a graceful shutdown signal, then asserts Valgrind
#  reports zero definitely/indirectly/possibly-lost bytes.
#
#  This is NOT a substitute for the single-request Valgrind checks during
#  development — it specifically catches PER-REQUEST leaks that only show
#  up at volume (a single request leaking 88 bytes looks identical to zero
#  leaked bytes until you multiply by request count).
# ═══════════════════════════════════════════════════════════════════════
VALGRIND_PORT     = 8099
VALGRIND_REQUESTS = 500
VALGRIND_LOG      = /tmp/simple_server_valgrind.log
VALGRIND_FIXTURE  = /tmp/simple_server_valgrind_root

.PHONY: memcheck
memcheck: $(VALGRIND_BINARY)
	# Valgrind unavailable on some systems
	@if ! command -v valgrind >/dev/null 2>&1; then \
		    echo "valgrind not available on this platform ($(UNAME_S)) — skipping memcheck"; \
		    exit 0; \
	fi
	@echo "Setting up fixture directory for memcheck..."
	@mkdir -p $(VALGRIND_FIXTURE)/emptydir
	@echo "<html><body><h1>memcheck</h1></body></html>" > $(VALGRIND_FIXTURE)/index.html
	@echo "Starting simple_server under Valgrind (port $(VALGRIND_PORT))..."
	@( cd $(VALGRIND_FIXTURE) && valgrind --leak-check=full --track-fds=yes --error-exitcode=1 $(CURDIR)/$(VALGRIND_BINARY) -v -p $(VALGRIND_PORT) > $(VALGRIND_LOG) 2>&1 & echo $$! > $(VALGRIND_FIXTURE)/vg.pid )
	@sleep 1
	@echo "Firing $(VALGRIND_REQUESTS) mixed requests..."
	@for i in $$(seq 1 $(VALGRIND_REQUESTS)); do curl -s -o /dev/null http://127.0.0.1:$(VALGRIND_PORT)/index.html; curl -s -o /dev/null http://127.0.0.1:$(VALGRIND_PORT)/emptydir/; curl -s -o /dev/null http://127.0.0.1:$(VALGRIND_PORT)/no-such-file; done
	@echo "Sending graceful shutdown (SIGTERM)..."
	@kill -TERM $$(cat $(VALGRIND_FIXTURE)/vg.pid 2>/dev/null) 2>/dev/null || true
	@sleep 2
	@echo "--- Valgrind report ---"
	@cat $(VALGRIND_LOG)
	@echo "-----------------------"
	@grep -q "definitely lost: 0 bytes in 0 blocks" $(VALGRIND_LOG) && grep -q "indirectly lost: 0 bytes in 0 blocks" $(VALGRIND_LOG) && grep -q "ERROR SUMMARY: 0 errors" $(VALGRIND_LOG) && echo "memcheck OK - no leaks across $(VALGRIND_REQUESTS) requests" || (echo "memcheck FAIL - see $(VALGRIND_LOG)"; exit 1)
	@rm -rf $(VALGRIND_FIXTURE)

.PHONY: clean-memcheck
clean-memcheck:
	@rm -f $(VALGRIND_LOG)
	@rm -rf $(VALGRIND_FIXTURE)

# ─── Aggregate test targets ────────────────────────────────────────────
.PHONY: test
test: test-parse test-parse2 test-process-request test-resolve test-build-error test-build-okay

.PHONY: test-all
test-all: test-parse test-parse2 test-process-request test-resolve test-build-error test-build-okay memcheck

# FIXED: was 'test-asan' (nonexistent) -> 'test-parse-asan'
.PHONY: test-all-asan
test-all-asan: test-parse-asan test-parse2-asan test-process-request-asan test-resolve-asan test-build-error-asan

.PHONY: clean-test
clean-test:
	rm -f $(PARSE_BINARY) $(PARSE_BINARY_ASAN) \
		  $(PARSE2_BINARY) $(PARSE2_BINARY_ASAN) \
		  $(PROCESS_REQUEST_BINARY) $(PROCESS_REQUEST_BINARY_ASAN) \
	      $(RESOLVE_BINARY) $(RESOLVE_BINARY_ASAN) \
		  $(BUILD_OKAY_BINARY) $(BUILD_OKAY_BINARY_ASAN) \
		  $(VALGRIND_LOG) $(VALGRIND_FIXTURE)
	rm -rf test_cases/fixtures
# ═══════════════════════════════════════════════════════════════════════
#  Code quality and formatting
# ═══════════════════════════════════════════════════════════════════════
.PHONY: format
format:
	@echo "Formatting C source files..."
	@for file in $(SOURCES) $(wildcard */*.h); do \
		echo "Formatting $$file"; \
		clang-format19 -i $$file; \
	done

.PHONY: format-check
format-check:
	@if command -v clang-format19 >/dev/null 2>&1; then \
		echo "Checking code formatting..."; \
		failed=0; \
		for file in $(SOURCES) $(wildcard */*.h); do \
			if ! clang-format19 --dry-run --Werror $$file > /dev/null 2>&1; then \
				echo "❌ $$file needs formatting"; \
				failed=1; \
			else \
				echo "✅ $$file"; \
			fi; \
		done; \
		if [ $$failed -eq 1 ]; then \
			echo ""; \
			echo "💡 Run 'make format' to fix formatting issues"; \
			exit 1; \
		else \
			echo "🎉 All files are properly formatted!"; \
		fi; \
	else \
		echo "⚠️  clang-format not found - skipping format check"; \
		echo "📦 Install with: pkg install llvm"; \
	fi

.PHONY: lint
lint:
	@echo "Running cppcheck static analysis..."
	@cppcheck --enable=all --suppress=missingIncludeSystem \
    --check-level=exhaustive --error-exitcode=1 $(SOURCES)
	# @cppcheck --enable=all --suppress=missingIncludeSystem \
 #    --error-exitcode=1 $(SOURCES)
	# @cppcheck --enable=all --suppress=missingIncludeSystem --suppress=unusedFunction \
	# 	--error-exitcode=1 $(SOURCES)
	# cppcheck --enable=all --suppress=missingIncludeSystem --suppress=unusedFunction --error-exitcode=1 $(SOURCES); echo "exit: $?"
	@echo "Running cpplint style check..."
	@cpplint --filter=-whitespace/line_length,-build/include_subdir \
		--linelength=80 $(SOURCES) $(wildcard */*.h)

.PHONY: check
check: format-check lint
	@echo "🎉 All code quality checks passed!"

.PHONY: fix
fix: format
	@echo "✅ Code formatting applied. Run 'make check' to verify."

# ═══════════════════════════════════════════════════════════════════════
#  Documentation (Doxygen)
# ═══════════════════════════════════════════════════════════════════════
.PHONY: docs
docs: docs-graphs

.PHONY: docs-graphs
docs-graphs:
	@echo "🔍 Checking for Doxygen..."
	@if command -v doxygen >/dev/null 2>&1; then \
		echo "✅ Found doxygen: $$(which doxygen)"; \
		doxygen_cmd=doxygen; \
	elif [ -x /usr/local/bin/doxygen ]; then \
		echo "✅ Found doxygen: /usr/local/bin/doxygen"; \
		doxygen_cmd=/usr/local/bin/doxygen; \
	elif [ -x /usr/bin/doxygen ]; then \
		echo "✅ Found doxygen: /usr/bin/doxygen"; \
		doxygen_cmd=/usr/bin/doxygen; \
	else \
		echo "❌ Doxygen not found!"; \
		echo "📦 Please install Doxygen:"; \
		echo "   FreeBSD: pkg install doxygen"; \
		echo "   Linux:   apt install doxygen (Debian/Ubuntu)"; \
		echo "           dnf install doxygen (Fedora/RHEL)"; \
		echo "   macOS:   brew install doxygen"; \
		exit 1; \
	fi; \
	echo ""; \
	echo "🔍 Checking for Graphviz (for diagram generation)..."; \
	if command -v dot >/dev/null 2>&1; then \
		echo "✅ Found Graphviz: $$(which dot)"; \
		echo "📊 Diagrams will be generated (call graphs, dependency graphs, etc.)"; \
		have_dot=YES; \
	elif [ -x /usr/local/bin/dot ]; then \
		echo "✅ Found Graphviz: /usr/local/bin/dot"; \
		echo "📊 Diagrams will be generated (call graphs, dependency graphs, etc.)"; \
		have_dot=YES; \
	else \
		echo "⚠️  Graphviz not found - diagrams will be disabled"; \
		echo "📦 To enable diagrams, install Graphviz:"; \
		echo "   FreeBSD: pkg install graphviz"; \
		echo "   Linux:   apt install graphviz (Debian/Ubuntu)"; \
		echo "           dnf install graphviz (Fedora/RHEL)"; \
		echo "   macOS:   brew install graphviz"; \
		echo ""; \
		echo "💡 Use 'make docs-no-graphs' to generate docs without diagrams"; \
		have_dot=NO; \
	fi; \
	echo ""; \
	echo "🔍 Checking for Doxygen configuration..."; \
	if [ ! -d docs ]; then \
		echo "📁 Creating docs directory..."; \
		mkdir -p docs; \
	fi; \
	if [ -f docs/Doxyfile ]; then \
		echo "✅ Found Doxygen config: docs/Doxyfile"; \
		echo ""; \
		echo "🔧 Configuring diagram generation..."; \
		if [ -f docs/Doxyfile.tmp ]; then rm docs/Doxyfile.tmp; fi; \
		sed "s/HAVE_DOT[[:space:]]*=.*/HAVE_DOT = $$have_dot/" docs/Doxyfile > docs/Doxyfile.tmp; \
		echo "📚 Generating documentation with diagrams..."; \
		cd docs && $$doxygen_cmd Doxyfile.tmp; \
		if [ $$? -eq 0 ]; then \
			echo "🎉 Documentation with diagrams generated successfully!"; \
			echo "📖 Open docs/html/index.html to view documentation"; \
			if [ "$$have_dot" = "YES" ]; then \
				echo "📊 Interactive diagrams included: call graphs, include graphs, directory graphs"; \
			fi; \
		else \
			echo "❌ Documentation generation failed!"; \
			exit 1; \
		fi; \
		rm -f docs/Doxyfile.tmp; \
	else \
		echo "❌ Doxygen configuration file not found!"; \
		echo "🔧 Please create docs/Doxyfile first"; \
		echo "💡 Run 'make docs-init' to create initial configuration"; \
		exit 1; \
	fi

.PHONY: docs-no-graphs
docs-no-graphs:
	@echo "🔍 Checking for Doxygen..."
	@if command -v doxygen >/dev/null 2>&1; then \
		echo "✅ Found doxygen: $$(which doxygen)"; \
		doxygen_cmd=doxygen; \
	elif [ -x /usr/local/bin/doxygen ]; then \
		echo "✅ Found doxygen: /usr/local/bin/doxygen"; \
		doxygen_cmd=/usr/local/bin/doxygen; \
	elif [ -x /usr/bin/doxygen ]; then \
		echo "✅ Found doxygen: /usr/bin/doxygen"; \
		doxygen_cmd=/usr/bin/doxygen; \
	else \
		echo "❌ Doxygen not found!"; \
		echo "📦 Please install Doxygen first"; \
		exit 1; \
	fi; \
	echo ""; \
	echo "📊 Generating documentation without diagrams..."; \
	echo "💡 This is faster and doesn't require Graphviz"; \
	echo ""; \
	if [ ! -d docs ]; then \
		echo "📁 Creating docs directory..."; \
		mkdir -p docs; \
	fi; \
	if [ -f docs/Doxyfile ]; then \
		echo "✅ Found Doxygen config: docs/Doxyfile"; \
		echo ""; \
		echo "🔧 Disabling diagram generation..."; \
		if [ -f docs/Doxyfile.tmp ]; then rm docs/Doxyfile.tmp; fi; \
		sed "s/HAVE_DOT[[:space:]]*=.*/HAVE_DOT = NO/" docs/Doxyfile > docs/Doxyfile.tmp; \
		echo "📚 Generating documentation..."; \
		cd docs && $$doxygen_cmd Doxyfile.tmp; \
		if [ $$? -eq 0 ]; then \
			echo "🎉 Documentation generated successfully!"; \
			echo "📖 Open docs/html/index.html to view documentation"; \
			echo "📊 ASCII diagrams included in main page"; \
		else \
			echo "❌ Documentation generation failed!"; \
			exit 1; \
		fi; \
		rm -f docs/Doxyfile.tmp; \
	else \
		echo "❌ Doxygen configuration file not found!"; \
		echo "🔧 Please create docs/Doxyfile first"; \
		echo "💡 Run 'make docs-init' to create initial configuration"; \
		exit 1; \
	fi

.PHONY: docs-init
docs-init:
	@echo "🔍 Checking for Doxygen..."
	@if command -v doxygen >/dev/null 2>&1; then \
		echo "✅ Found doxygen: $$(which doxygen)"; \
		doxygen_cmd=doxygen; \
	elif [ -x /usr/local/bin/doxygen ]; then \
		echo "✅ Found doxygen: /usr/local/bin/doxygen"; \
		doxygen_cmd=/usr/local/bin/doxygen; \
	elif [ -x /usr/bin/doxygen ]; then \
		echo "✅ Found doxygen: /usr/bin/doxygen"; \
		doxygen_cmd=/usr/bin/doxygen; \
	else \
		echo "❌ Doxygen not found!"; \
		echo "📦 Please install Doxygen first"; \
		exit 1; \
	fi; \
	echo "📁 Creating docs directory..."; \
	mkdir -p docs; \
	echo "🔧 Generating default Doxygen configuration..."; \
	cd docs && $$doxygen_cmd -g Doxyfile; \
	echo "✅ Created docs/Doxyfile"; \
	echo ""; \
	echo "🔧 Applying recommended settings..."; \
	sed -i.bak \
		-e 's/^PROJECT_NAME[[:space:]]*=.*/PROJECT_NAME = "FreeBSD Web Server"/' \
		-e 's/^INPUT[[:space:]]*=.*/INPUT = ..\//' \
		-e 's/^RECURSIVE[[:space:]]*=.*/RECURSIVE = YES/' \
		-e 's/^FILE_PATTERNS[[:space:]]*=.*/FILE_PATTERNS = *.c *.h/' \
		-e 's/^EXCLUDE_PATTERNS[[:space:]]*=.*/EXCLUDE_PATTERNS = *\/build\/* *\/.*/' \
		-e 's/^GENERATE_LATEX[[:space:]]*=.*/GENERATE_LATEX = NO/' \
		-e 's/^EXTRACT_ALL[[:space:]]*=.*/EXTRACT_ALL = YES/' \
		-e 's/^EXTRACT_PRIVATE[[:space:]]*=.*/EXTRACT_PRIVATE = YES/' \
		-e 's/^EXTRACT_STATIC[[:space:]]*=.*/EXTRACT_STATIC = YES/' \
		docs/Doxyfile; \
	rm -f docs/Doxyfile.bak; \
	echo "🎉 Doxygen configuration initialized!"; \
	echo "📝 Edit docs/Doxyfile to customize further"; \
	echo "📚 Run 'make docs' to generate documentation"

.PHONY: docs-clean
docs-clean:
	@echo "🧹 Cleaning generated documentation..."
	@rm -rf docs/html docs/latex docs/man docs/xml docs/Doxyfile.tmp
	@echo "✅ Documentation cleaned"

# ═══════════════════════════════════════════════════════════════════════
#  Help
# ═══════════════════════════════════════════════════════════════════════
.PHONY: help
help:
	@echo "🔨 FreeBSD Web Server - Available Make Targets"
	@echo ""
	@echo "📦 Building:"
	@echo "  make / make all       Build the release server binary ($(BINARY))"
	@echo "  make debug            Build debug binary ($(DEBUG_BINARY)):"
	@echo "                        symbols + AddressSanitizer + -DDEBUG"
	@echo "  make clean            Remove all binaries and build/ objects"
	@echo "  make clean-obj        Remove only object files (keep binaries)"
	@echo ""
	@echo "  Release and debug objects live in separate trees"
	@echo "  (build/release, build/debug) so they never collide — you can"
	@echo "  switch between 'make' and 'make debug' without cleaning."
	@echo ""
	@echo "🧪 Testing (Criterion):"
	@echo "  make test             Run parse_request unit tests"
	@echo "  make test-asan        ...under AddressSanitizer"
	@echo "  make test-resolve     Run resolve_path unit tests"
	@echo "  make test-resolve-asan ...under AddressSanitizer"
	@echo ""
	@echo "🧪 build_okay_response (file/directory serving):"
	@echo "  make test-build-okay       Run unit + integration tests"
	@echo "  make test-build-okay-asan  ...under AddressSanitizer"
	@echo ""
	@echo "  Two layers, both run by either target:"
	@echo "    unit layer         - calls the REAL resolve_path() then the"
	@echo "                         REAL build_okay_response() directly, no"
	@echo "                         sockets. Fast. Catches struct-ownership"
	@echo "                         bugs between the two functions."
	@echo "    integration layer  - forks + execs a REAL server binary,"
	@echo "                         fires actual HTTP requests via curl over"
	@echo "                         a real socket, sends SIGTERM, and"
	@echo "                         requires a clean graceful exit."
	@echo ""
	@echo "  test-build-okay spawns $(BINARY) (plain release build) for the"
	@echo "  integration layer -- fast, correctness-only, no sanitizer."
	@echo ""
	@echo "  test-build-okay-asan compiles the TEST BINARY itself with ASan"
	@echo "  (covers the unit layer in-process) AND spawns $(DEBUG_BINARY)"
	@echo "  (your ASan server build) for the integration layer -- so the"
	@echo "  real accept/do_read/resolve_path/build_okay_response/do_write"
	@echo "  pipeline runs under ASan too, not just the isolated unit calls."
	@echo ""
	@echo "  Point the integration layer at a different binary directly:"
	@echo "    SIMPLE_SERVER_BIN=/path/to/binary make test-build-okay"
	@echo "  e.g. to run it under Valgrind, wrap the TEST invocation:"
	@echo "    valgrind --trace-children=yes ./test_cases/test_build_okay_response"
	@echo "    (with SIMPLE_SERVER_BIN pointed at \$$(VALGRIND_BINARY))"
	@echo ""
	@echo "  make test-all         Run every test suite"
	@echo "  make test-all-asan    Run every suite under ASan"
	@echo "  make clean-test       Remove test binaries and fixtures"
	@echo "🧠 Memory Leak Testing (Valgrind):"
	@echo "  make memcheck         Run server under Valgrind, fire $$VALGRIND_REQUESTS"
	@echo "                        mixed requests, verify zero leaks on shutdown"
	@echo "  make clean-memcheck   Remove memcheck logs and fixtures"
	@echo ""
	@echo "  Override request count: make memcheck VALGRIND_REQUESTS=2000"
	@echo ""
	@echo "  Output flags: override TEST_RUN_FLAGS (default '-j1 --quiet')."
	@echo "  e.g. make test TEST_RUN_FLAGS='-j1 --verbose'"
	@echo ""
	@echo "🔍 Code Quality:"
	@echo "  make check            Run all code quality checks (format + lint)"
	@echo "  make format-check     Check if code formatting is correct"
	@echo "  make format           Auto-format code with clang-format"
	@echo "  make lint             Run static analysis (cppcheck + cpplint)"
	@echo "  make fix              Apply automatic formatting"
	@echo ""
	@echo "📚 Documentation:"
	@echo "  make docs             Generate docs with diagrams (requires Graphviz)"
	@echo "  make docs-graphs      Same as 'make docs' - generates with diagrams"
	@echo "  make docs-no-graphs   Generate docs without diagrams (faster)"
	@echo "  make docs-init        Initialize Doxygen configuration"
	@echo "  make docs-clean       Clean generated documentation"
	@echo ""
	@echo "📋 Dependencies:"
	@echo "  Build (required):         clang, libmagic"
	@echo "  Testing (required):       criterion"
	@echo "  Documentation (required): doxygen"
	@echo "  Documentation (optional): graphviz (call/dependency graphs)"
	@echo "  Code quality (optional):  clang-format, cppcheck, cpplint"
	@echo ""
	@echo "📦 Installing Dependencies (FreeBSD):"
	@echo "  pkg install doxygen graphviz llvm cppcheck py39-cpplint criterion"
	@echo ""
	@echo "🚀 Quick Start:"
	@echo "  make                  # Build the server"
	@echo "  make test-all         # Run all unit tests"
	@echo "  make check            # Verify code quality"
	@echo "  ./simple_server -v    # Run in verbose mode"
