# Name of application to build
TARGET := findcode

DEBUG ?= 0

### Text variables ###

# These use the fact that += always adds a space to create a variable that is just a space
# Space has a single space, indent has 2
space :=
space +=

indent =
indent += 
indent += 

### Tools ###

# System tools
CD := cd
CP := cp
RM := rm

MKDIR := mkdir
MKDIR_OPTS := -p

RMDIR := rm
RMDIR_OPTS := -rf

PRINT := printf '
ENDCOLOR := \033[0m
WHITE     := \033[0m
ENDWHITE  := $(ENDCOLOR)
GREEN     := \033[0;32m
ENDGREEN  := $(ENDCOLOR)
BLUE      := \033[0;34m
ENDBLUE   := $(ENDCOLOR)
YELLOW    := \033[0;33m
ENDYELLOW := $(ENDCOLOR)
ENDLINE := \n'

RUN := 

# Build tools
CC      := clang
CXX     := clang++
LD      := clang++

### Files and Directories ###

# Function to find files with a given extension recursively in a folder
findfiles = $(shell find $(1) -type f -name "*$(2)")

# Source files
SRC_DIRS     := src
C_SRCS       := $(foreach src_dir,$(SRC_DIRS),$(wildcard $(src_dir)/*.c))
CXX_SRCS     := $(foreach src_dir,$(SRC_DIRS),$(wildcard $(src_dir)/*.cpp)) $(foreach src_dir,$(SRC_DIRS),$(wildcard $(src_dir)/*.cc))

# Root build folder
ifeq ($(DEBUG),0)
BUILD_ROOT     := build/release
else
BUILD_ROOT     := build/debug
endif

# Linked libraries
LIBS_ROOT      := lib
LIBS           :=
LIBS_INC_DIRS  := $(LIBS_ROOT)/fmt/include $(LIBS_ROOT)/rabbitizer/include $(LIBS_ROOT)/rabbitizer/cplusplus/include
LIBS_INC_FLAGS := $(addprefix -I,$(LIBS_INC_DIRS))
LIBS_LD_DIRS   := 
LIBS_LD_FLAGS  := $(addprefix -L,$(LIBS_LD_DIRS)) $(addprefix -l,$(LIBS))
LIBS_SRC_DIRS  := $(LIBS_ROOT)/fmt/src $(wildcard $(LIBS_ROOT)/rabbitizer/src/*) $(wildcard $(LIBS_ROOT)/rabbitizer/cplusplus/src/*)

# Find lib sources
LIBS_C_SRCS    := $(call findfiles,$(LIBS_SRC_DIRS),.c)
LIBS_CPP_SRCS  := $(call findfiles,$(LIBS_SRC_DIRS),.cpp)
LIBS_CC_SRCS   := $(call findfiles,$(LIBS_SRC_DIRS),.cc)
# Don't compile fmt.cc
LIBS_CC_SRCS   := $(filter-out %/fmt.cc,$(LIBS_CC_SRCS))
# Convert lib sources to lib objects
LIBS_C_OBJS    := $(addprefix $(BUILD_ROOT)/,$(LIBS_C_SRCS:.c=.o))
LIBS_CPP_OBJS  := $(addprefix $(BUILD_ROOT)/,$(LIBS_CPP_SRCS:.cpp=.o))
LIBS_CC_OBJS   := $(addprefix $(BUILD_ROOT)/,$(LIBS_CC_SRCS:.cc=.o))
# Strip lib/ prefix from lib objects
LIBS_C_OBJS    := $(LIBS_C_OBJS:$(BUILD_ROOT)/$(LIBS_ROOT)/%=$(BUILD_ROOT)/%)
LIBS_CPP_OBJS  := $(LIBS_CPP_OBJS:$(BUILD_ROOT)/$(LIBS_ROOT)/%=$(BUILD_ROOT)/%)
LIBS_CC_OBJS   := $(LIBS_CC_OBJS:$(BUILD_ROOT)/$(LIBS_ROOT)/%=$(BUILD_ROOT)/%)
LIBS_OBJS      := $(LIBS_C_OBJS) $(LIBS_CPP_OBJS) $(LIBS_CC_OBJS)

# Build files
C_OBJS   := $(addprefix $(BUILD_ROOT)/,$(C_SRCS:.c=.o))
CXX_OBJS := $(addprefix $(BUILD_ROOT)/,$(CXX_SRCS:.cpp=.o))
CXX_OBJS := $(CXX_OBJS:.cc=.o)
OBJS     := $(C_OBJS) $(CXX_OBJS) $(LIBS_CPP_OBJS) $(LIBS_CC_OBJS) $(LIBS_C_OBJS)
D_FILES  := $(C_OBJS:.o=.d) $(CXX_OBJS:.o=.d) $(LIBS_CPP_OBJS:.o=.d) $(LIBS_CC_OBJS:.o=.d)

# Build folders
BUILD_DIRS     := $(sort $(dir $(OBJS)))

APP      := $(BUILD_ROOT)/$(TARGET)

### Flags ###

# Build tool flags

CFLAGS     := -fdata-sections -ffunction-sections
CXXFLAGS   := -std=c++20 -fno-rtti -fdata-sections -ffunction-sections
CPPFLAGS   := -I include $(LIBS_INC_FLAGS) -DAPP_NAME=\"$(TARGET)\"
WARNFLAGS  := -Wall -Wextra -Wpedantic -Wdouble-promotion -Wfloat-conversion
ASFLAGS    := 
LDFLAGS    := -Wl,-dead_strip $(LIBS_LD_FLAGS)

ifneq ($(DEBUG),0)
CPPFLAGS   += -DDEBUG_MODE
OPT_FLAGS  := -O0 -g -ggdb
else
CPPFLAGS   += -DNDEBUG
OPT_FLAGS  := -O3 -flto
LDFLAGS    += -flto
endif

### Rules ###

# Default target, all
all: $(APP)

# Make directories
$(BUILD_ROOT) $(BUILD_DIRS) :
	@$(PRINT)$(GREEN)Creating directory: $(ENDGREEN)$(BLUE)$@$(ENDBLUE)$(ENDLINE)
	@$(MKDIR) $(MKDIR_OPTS) $@

# .cpp -> .o
$(BUILD_ROOT)/%.o : %.cpp | $(BUILD_DIRS)
	@$(PRINT)$(GREEN)Compiling C++ source file: $(ENDGREEN)$(BLUE)$<$(ENDBLUE)$(ENDLINE)
	@$(CXX) $< -o $@ -c -MMD -MF $(@:.o=.d) $(CXXFLAGS) $(CPPFLAGS) $(OPT_FLAGS) $(WARNFLAGS)

# .cpp -> .o (library sources)
$(LIBS_CPP_OBJS): $(BUILD_ROOT)/%.o : $(LIBS_ROOT)/%.cpp | $(BUILD_DIRS)
	@$(PRINT)$(GREEN)Compiling C++ source file: $(ENDGREEN)$(BLUE)$<$(ENDBLUE)$(ENDLINE)
	@$(CXX) $< -o $@ -c -MMD -MF $(@:.o=.d) $(CXXFLAGS) $(CPPFLAGS) $(OPT_FLAGS) $(WARNFLAGS)
	
# .cc -> .o
$(BUILD_ROOT)/%.o : %.cc | $(BUILD_DIRS)
	@$(PRINT)$(GREEN)Compiling C++ source file: $(ENDGREEN)$(BLUE)$<$(ENDBLUE)$(ENDLINE)
	@$(CXX) $< -o $@ -c -MMD -MF $(@:.o=.d) $(CXXFLAGS) $(CPPFLAGS) $(OPT_FLAGS) $(WARNFLAGS)

# .cc -> .o (library sources)
$(LIBS_CC_OBJS): $(BUILD_ROOT)/%.o : $(LIBS_ROOT)/%.cc | $(BUILD_DIRS)
	@$(PRINT)$(GREEN)Compiling C++ source file: $(ENDGREEN)$(BLUE)$<$(ENDBLUE)$(ENDLINE)
	@$(CXX) $< -o $@ -c -MMD -MF $(@:.o=.d) $(CXXFLAGS) $(CPPFLAGS) $(OPT_FLAGS) $(WARNFLAGS)

# .c -> .o
$(BUILD_ROOT)/%.o : %.c | $(BUILD_DIRS)
	@$(PRINT)$(GREEN)Compiling C source file: $(ENDGREEN)$(BLUE)$<$(ENDBLUE)$(ENDLINE)
	@$(CC) $< -o $@ -c -MMD -MF $(@:.o=.d) $(CFLAGS) $(CPPFLAGS) $(OPT_FLAGS) $(WARNFLAGS)

# .c -> .o (library sources)
$(LIBS_C_OBJS): $(BUILD_ROOT)/%.o : $(LIBS_ROOT)/%.c | $(BUILD_DIRS)
	@$(PRINT)$(GREEN)Compiling C source file: $(ENDGREEN)$(BLUE)$<$(ENDBLUE)$(ENDLINE)
	@$(CC) $< -o $@ -c -MMD -MF $(@:.o=.d) $(CFLAGS) $(CPPFLAGS) $(OPT_FLAGS) $(WARNFLAGS)

# .o -> application
$(APP) : $(OBJS)
	@$(PRINT)$(GREEN)Linking application: $(ENDGREEN)$(BLUE)$@$(ENDBLUE)$(ENDLINE)
	@$(LD) -o $@ $^ $(LDFLAGS)
	@$(PRINT)$(WHITE)Application Built!$(ENDWHITE)$(ENDLINE)

clean:
	@$(PRINT)$(YELLOW)Cleaning build$(ENDYELLOW)$(ENDLINE)
	@$(RMDIR) $(RMDIR_OPTS) $(BUILD_ROOT)
	@$(RM) -f $(APP)

.PHONY: all clean load

-include $(D_FILES)

print-% : ; $(info $* is a $(flavor $*) variable set to [$($*)]) @true
