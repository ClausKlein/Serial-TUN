#
# Standard stuff
#
.SUFFIXES:

# Disable the built-in implicit rules.
MAKEFLAGS+= --no-builtin-rules

.PHONY: setup all test lcov install check format clean distclean


# see https://www.kdab.com/clang-tidy-part-1-modernize-source-code-using-c11c14/
# and https://github.com/llvm-mirror/clang-tools-extra/blob/master/clang-tidy/tool/run-clang-tidy.py
#
### checkAllHeader:='include/spdlog/[acdlstv].*'
## checkAllHeader?='include/spdlog/[^f].*'
checkAllHeader?='$(CURDIR)/.*'

# NOTE: to many errors with boost::test
CHECKS:='-cppcoreguidelines-avoid-c-arrays,-modernize-avoid-c-arrays,-modernize-deprecated-headers,-modernize-use-trailing-return-type'
CHECKS?='-*,cppcoreguidelines-*,-cppcoreguidelines-pro-*,-cppcoreguidelines-avoid-c-arrays'
CHECKS?='-*,portability-*,readability-*'
CHECKS?='-*,misc-*,boost-*,cert-*,misc-unused-parameters'


PROJECT_NAME:=$(shell basename $${PWD})
#XXX CXX:=$(shell which clang++)
## CC:=/opt/local/bin/clang
## CXX:=/opt/local/bin/clang++
BUILD_TYPE:=Coverage
BUILD_TYPE?=Debug
BUILD_TYPE?=Release
# GENERATOR:=Xcode
GENERATOR?=Ninja
BUILD_DIR:=../.build-$(PROJECT_NAME)-$(BUILD_TYPE)


all: setup .configure-$(BUILD_TYPE)
	cmake --build $(BUILD_DIR)

test: all
	cd $(BUILD_DIR) && ctest -C $(BUILD_TYPE) --rerun-failed --output-on-failure .
	cd $(BUILD_DIR) && ctest -C $(BUILD_TYPE) .


check: setup .configure-$(BUILD_TYPE) compile_commands.json
	run-clang-tidy.py -header-filter=$(checkAllHeader) -checks=$(CHECKS) *.c *.cpp | tee run-clang-tidy.log 2>&1
	egrep '\b(warning|error):' run-clang-tidy.log | perl -pe 's/(^.*) (warning|error):/\2/' | sort -u

setup: $(BUILD_DIR) .clang-tidy compile_commands.json

.configure-$(BUILD_TYPE): CMakeLists.txt
	cd $(BUILD_DIR) && cmake -G $(GENERATOR) -Wdeprecated -Wdev \
      -DUSE_LCOV=on -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_C_COMPILER=${CC} -DCMAKE_CXX_COMPILER=${CXX} $(CURDIR)
	touch $@

compile_commands.json: .configure-$(BUILD_TYPE)
	ln -sf $(CURDIR)/$(BUILD_DIR)/compile_commands.json .

$(BUILD_DIR): GNUmakefile
	mkdir -p $@


format: .clang-format
	find . -type f -name '*.h' -o -name '*.c' -o -name '*.cpp' | xargs clang-format -style=file -i


lcov: $(BUILD_DIR) .configure-Coverage
	cmake --build $(BUILD_DIR) --target $@

install: $(BUILD_DIR)
	cmake --build $(BUILD_DIR) --target $@

clean: $(BUILD_DIR)
	cmake --build $(BUILD_DIR) --target $@

distclean:
	rm -rf $(BUILD_DIR) .configure-$(BUILD_TYPE) compile_commands.json *~ .*~ tags
	find . -name '*~' -delete


# These rules keep make from trying to use the match-anything rule below
# to rebuild the makefiles--ouch!

## CMakeLists.txt :: ;
GNUmakefile :: ;
.clang-tidy :: ;
.clang-format :: ;

# Anything we don't know how to build will use this rule.  The command is
# a do-nothing command.
% :: ;

