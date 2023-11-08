.DEFAULT_GOAL := all

# place in architecture folder, e.g. build/arm64
BUILD_DIR = build/$(shell uname -m)

CXXFLAGS += -std=c++20 -fstrict-aliasing -ftree-vectorize -flto=auto

# CXXFLAGS += -Wunreachable-code

ifeq ($(shell uname -s),Darwin)
# 	macOS

# fixes gcc on macos
#	LDFLAGS += -Wl,-ld_classic
	ifeq ($(shell uname -m),arm64)
#	 	On Arm, -mcpu combines -march and -mtune
		CXXFLAGS += -mcpu=native
	else
#		On x86, -march implies -mtune
		CXXFLAGS += -march=native
	endif

	CXXFLAGS += -fno-omit-frame-pointer
#	CXXFLAGS += -Rpass=loop-vectorize
#	CXXFLAGS += -Rpass-missed=loop-vectorize
#	CXXFLAGS += -Rpass-analysis=loop-vectorize

	# CXXFLAGS += -fopenmp-simd

	# enable OpenMP for Clang
	# CXXFLAGS += -Xpreprocessor -fopenmp -lomp

	# add -lprofiler for gperftools
	# LDFLAGS += $(LIB)
	# LDFLAGS += -lprofiler

else ifeq ($(USER),localadmin_ccollins)
	# CXX = c++
	LDFLAGS= -lgsl -lgslcblas -lm -I/home/localadmin_ccollins/gsl/include
	INCLUDE = /home/localadmin_ccollins/gsl/include
	LIB = /home/localadmin_ccollins/gsl/lib
	CXXFLAGS += -g -I$(INCLUDE)
	LDFLAGS= -L$(LIB) -lgsl -lgslcblas -lm
	CXXFLAGS += -std=c++17 -Wstrict-aliasing -fstrict-aliasing #-fopenmp=libomp

else
	# sometimes the login nodes have slighty different CPUs
	# to the job nodes. Try to find the lowest common denominator here
	# to enable vector extensions
	# CXXFLAGS += -march=cascadelake
	# CXXFLAGS += -march=skylake-avx512
	# CXXFLAGS += -march=icelake-server

	# to get the current CPU architecture, run this:
	# g++ -march=native -Q --help=target | grep -- '-march=  ' | cut -f3
	ifneq (,$(findstring juwels,$(HOSTNAME)))
		CXXFLAGS += -march=skylake-avx512
	else ifneq (,$(findstring lxbk,$(HOSTNAME)))
		# virgo has some AMD nodes (znver1 arch) and some Intel
		CXXFLAGS += -march=cascadelake
	else ifneq (,$(findstring login-q,$(HOSTNAME)))
		# Cambridge icelake nodes
		CXXFLAGS += -march=icelake-server
	endif

endif

# GSL (GNU Scientific Library)
LDFLAGS += $(shell pkg-config --libs gsl)
CXXFLAGS += $(shell pkg-config --cflags gsl)
# GSL option 1: Use pkg-config or gsl-config to find GSL
#
# GSL option 2: Use default search paths to find GSL
# LDFLAGS += -lgsl -lgslcblas -lm

# Use GSL inline functions
CXXFLAGS += -DHAVE_INLINE -DGSL_C99_INLINE

ifeq ($(TESTMODE),ON)
	CXXFLAGS += -DTESTMODE=true -O3 -DLIBCXX_ENABLE_DEBUG_MODE
	# makes GitHub actions classic test run forever?
	# CXXFLAGS += -D_GLIBCXX_DEBUG=1
	CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer -fno-common
	BUILD_DIR := $(BUILD_DIR)_testmode
else
	# skip array range checking for better performance and use optimizations
	CXXFLAGS += -DTESTMODE=false -DGSL_RANGE_CHECK_OFF -O3
endif

ifeq ($(FASTMATH),ON)
	CXXFLAGS += -ffast-math -fno-finite-math-only
endif

CXXFLAGS += -Werror -Werror=undef -Winline -Wall -Wpedantic -Wredundant-decls -Wundef -Wno-unused-parameter -Wno-unused-function -Wstrict-aliasing -Wno-inline

ifeq ($(MPI),)
	# MPI option not specified. set to true by default
	MPI := ON
endif
ifeq ($(MPI),ON)
	CXX = mpicxx
	CXXFLAGS += -DMPI_ON=true
	BUILD_DIR := $(BUILD_DIR)_mpi
else ifeq ($(MPI),OFF)
else
$(error bad value for MPI option. Should be ON or OFF)
endif

ifeq ($(TESTMODE),ON)
else ifeq ($(TESTMODE),OFF)
else ifeq ($(TESTMODE),)
else
$(error bad value for testmode option. Should be ON or OFF)
endif

ifeq ($(OPENMP),ON)
	CXXFLAGS += -Xpreprocessor -fopenmp
	LDFLAGS += -lomp
	BUILD_DIR := $(BUILD_DIR)_openmp
else ifeq ($(OPENMP),OFF)
else ifeq ($(OPENMP),)
else
$(error bad value for testmode option. Should be ON or OFF)
endif

### use pg when you want to use gprof profiler
#CXXFLAGS = -g -pg -Wall -I$(INCLUDE)

# sn3d.cc and exspec.cc have main() defined
common_files := $(filter-out sn3d.cc exspec.cc, $(wildcard *.cc))

sn3d_files = sn3d.cc $(common_files)
sn3d_objects = $(addprefix $(BUILD_DIR)/,$(sn3d_files:.cc=.o))

exspec_files = exspec.cc $(common_files)
exspec_objects = $(addprefix $(BUILD_DIR)/,$(exspec_files:.cc=.o))

all: sn3d exspec

sn3d: clean version.h $(sn3d_objects)
	$(CXX) $(CXXFLAGS) $(sn3d_objects) $(LDFLAGS) -o sn3d

sn3dwhole: version.h
	$(CXX) $(CXXFLAGS) -g $(sn3d_files) $(LDFLAGS) -o sn3d

$(BUILD_DIR)/%.o: %.cc artisoptions.h Makefile version.h clean
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

exspec: clean version.h $(exspec_objects)
	$(CXX) $(CXXFLAGS) $(exspec_objects) $(LDFLAGS) -o exspec

.PHONY: clean version.h TESTMODE TESTMODEON

version.h: clean
	echo "constexpr const char* GIT_VERSION = \"$(shell git describe --dirty --always --tags)\";" > version.h
	echo "constexpr const char* GIT_HASH = \"$(shell git rev-parse HEAD)\";" >> version.h
# requires git > 2.22
# @echo "constexpr const char* GIT_BRANCH = \"$(shell git branch --show)\";" >> version.h
	echo "constexpr const char* GIT_BRANCH = \"$(shell git symbolic-ref --short HEAD 2>/dev/null || git rev-parse --short HEAD )\";" >> version.h
	echo "constexpr const char* GIT_STATUS = \"$(shell git status --short)\";" >> version.h

clean:
	rm -rf sn3d exspec build *.o *.d
